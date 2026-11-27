/* Resolve only registered press-fit parts. The directory's Creo index is a
   search aid. A selected family row is always checked against the opened PRT. */
typedef struct {
    ProName instance;
    ProName generic;
    wchar_t path[MAX_PATH];
    int bso_length;
} PressfitSource;

typedef struct {
    ProName instance;
    ProName generic;
    ProName instance_key;
    ProName generic_key;
    unsigned long revision;
    int bso_length;
} PressfitSelector;

typedef struct {
    PressfitSource choices[6];
    int score;
    unsigned matches;
} PressfitMatch;

static void pressfit_acceptance_find_source(const CompanyFastenerCatalog *catalog,
    const PressfitSelector *selector,PressfitMatch *match);
static void pressfit_preserve_registered_ambiguity(const CompanyFastenerCatalog *catalog,
    const PressfitSelector *selector,PressfitMatch *match);

static int pressfit_name_key(const wchar_t *name,ProName key){
    size_t used=0;
    for(const wchar_t *p=name;*p;p++){
        wchar_t c=towupper(*p);
        if(c==L'×')c=L'X';
        if(c==L'.'||c==L'_'||c==L'-'){
            if(p==name||!iswdigit(p[-1])||!iswdigit(p[1]))continue;
            c=L'.';
        }
        if(iswspace(c))continue;
        if(!wcschr(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.",c)||used+1>=PRO_NAME_SIZE)return 0;
        key[used++]=c;
    }
    key[used]=0;
    return used>0;
}

static int pressfit_bso_size(const wchar_t *digits){
    if(!iswdigit(*digits))return 0;
    wchar_t *end;
    const long value=wcstol(digits,&end,10);
    return !*end&&value>0&&value<1000?(int)value:0;
}

static int pressfit_bso_shorthand(PressfitSelector *selector){
    /* Short designations bind to the company catalog families. Native
       instance validation still checks that the requested row exists. */
    static const struct {
        const wchar_t *compact,*family,*instance_prefix,*first_instance;
        int first_length;
    } families[]={
        {L"BSOM3X",L"BSO_3_5M3XX",L"BSO_3_5M3X",L"BSO-3_5M3X5",5},
        {L"BSO3.5M3X",L"BSO_3_5M3XX",L"BSO_3_5M3X",L"BSO-3_5M3X5",5},
        {L"BSO3.5M4X",L"BSO_3_5M4XX",L"BSO_3_5M4X",L"BSO-3_5M4X5",5},
        {L"BSOM2.5X",L"BSO_M2_5XX",L"BSO_M2_5X",L"BSO-M2_5X3",3},
        {L"BSOM4X",L"BSO_M4XX",L"BSO_M4X",L"BSO-M4X5",5},
        {L"BSOM5X",L"BSO_M5XX",L"BSO-M5X",NULL,0},
        {L"BSOSM3X",L"BSOS_3_5M3XX",L"BSOS_3_5M3X",L"BSOS-3_5M3X5",5},
        {L"BSOS3.5M3X",L"BSOS_3_5M3XX",L"BSOS_3_5M3X",L"BSOS-3_5M3X5",5}
    };
    for(size_t i=0;i<_countof(families);i++){
        const size_t prefix=wcslen(families[i].compact);
        if(wcsncmp(selector->instance_key,families[i].compact,prefix))continue;
        const int length=pressfit_bso_size(selector->instance_key+prefix);
        if(!length)return 0;
        selector->bso_length=length;
        wcscpy_s(selector->generic,_countof(selector->generic),families[i].family);
        pressfit_name_key(selector->generic,selector->generic_key);
        if(length==families[i].first_length)wcscpy_s(selector->instance,_countof(selector->instance),families[i].first_instance);
        else swprintf_s(selector->instance,_countof(selector->instance),L"%ls%d",families[i].instance_prefix,length);
        return pressfit_name_key(selector->instance,selector->instance_key);
    }
    return 0;
}

static int pressfit_selector(const wchar_t *requested,PressfitSelector *selector){
    wchar_t name[MAX_PATH];
    if(!requested||wcscpy_s(name,_countof(name),requested))return 0;
    company_trim_path(name);
    const wchar_t *instance=name;
    wchar_t *separator=wcschr(name,L':');
    if(separator){
        *separator=0;
        instance=separator+1;
        if(wcscpy_s(selector->generic,_countof(selector->generic),name)||
           !pressfit_name_key(name,selector->generic_key))return 0;
    }
    size_t model_chars=0;
    unsigned long revision=0;
    if(company_fastener_model_name(instance,&model_chars,&revision)){
        selector->revision=revision;
        const wchar_t *extension=instance+model_chars-4;
        if(_wcsnicmp(extension,L".prt",4))return 0;
        *(wchar_t *)extension=0;
    }
    if(!pressfit_name_key(instance,selector->instance_key))return 0;
    /* BSO_M3X5 and BSO-M3X5 are explicit series names. Plain BSO M3×5
       selects the established 3.5-series company alias. */
    const int exact_m3=!wcsncmp(selector->instance_key,L"BSOM3X",6)&&wcspbrk(instance,L"_-");
    if(!separator&&!model_chars&&!exact_m3&&pressfit_bso_shorthand(selector))return 1;
    return !wcscpy_s(selector->instance,_countof(selector->instance),instance);
}

static int pressfit_match_name(const wchar_t *candidate,const wchar_t *requested,const wchar_t *key){
    ProName candidate_key;
    if(!_wcsicmp(candidate,requested))return 2;
    return pressfit_name_key(candidate,candidate_key)&&!wcscmp(candidate_key,key);
}

static int pressfit_model_name(const CompanyFastenerItem *item,ProName name){
    const wchar_t *leaf=item->path+item->name_offset;
    return item->model_chars>4&&!_wcsnicmp(leaf+item->model_chars-4,L".prt",4)&&
        !wcsncpy_s(name,PRO_NAME_SIZE,leaf,item->model_chars-4);
}

static void pressfit_add_match(PressfitMatch *match,int score,const wchar_t *instance,
    const wchar_t *generic,const CompanyFastenerItem *item){
    if(!score||score<match->score)return;
    if(score>match->score){
        match->score=score;
        match->matches=0;
    }
    for(unsigned i=0;i<match->matches&&i<_countof(match->choices);i++){
        const PressfitSource *other=&match->choices[i];
        if(!_wcsicmp(instance,other->instance)&&!_wcsicmp(generic,other->generic)&&
           !_wcsicmp(item->path,other->path))return;
    }
    if(match->matches<_countof(match->choices)){
        PressfitSource *selection=&match->choices[match->matches];
        wcscpy_s(selection->instance,_countof(selection->instance),instance);
        wcscpy_s(selection->generic,_countof(selection->generic),generic);
        wcscpy_s(selection->path,_countof(selection->path),item->path);
    }
    match->matches++;
}
static int pressfit_search_index(const wchar_t *file,const wchar_t *directory,
    const CompanyFastenerCatalog *catalog,const PressfitSelector *selector,
    PressfitMatch *match){
    wchar_t resolved[MAX_PATH];
    HANDLE index=company_open_path(file,0,resolved);
    if(index==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER size;
    if(!GetFileSizeEx(index,&size)||size.QuadPart<=0||size.QuadPart>2*1024*1024){
        CloseHandle(index);
        return 0;
    }
    char *contents=malloc((size_t)size.QuadPart+1);
    DWORD read=0;
    int valid=contents&&ReadFile(index,contents,(DWORD)size.QuadPart,&read,NULL)&&
        read==(DWORD)size.QuadPart;
    CloseHandle(index);
    if(!valid){free(contents);return 0;}
    contents[read]=0;
    const int utf8=read>=3&&(unsigned char)contents[0]==0xef&&
        (unsigned char)contents[1]==0xbb&&(unsigned char)contents[2]==0xbf;
    const UINT code_page=utf8?CP_UTF8:CP_ACP;
    char *line_context=NULL;
    char *line=strtok_s(contents+(utf8?3:0),"\r\n",&line_context);
    if(!line||strncmp(line,"MB_INST_IDX",11)){free(contents);return 0;}
    while((line=strtok_s(NULL,"\r\n",&line_context))!=NULL){
        char *field_context=NULL;
        const char *kind=strtok_s(line," \t",&field_context);
        const char *raw_instance=strtok_s(NULL," \t",&field_context);
        const char *raw_generic=strtok_s(NULL," \t",&field_context);
        ProName instance,generic;
        if(!kind||strcmp(kind,"2")||!raw_instance||!raw_generic||
           !MultiByteToWideChar(code_page,MB_ERR_INVALID_CHARS,raw_instance,-1,instance,_countof(instance))||
           !MultiByteToWideChar(code_page,MB_ERR_INVALID_CHARS,raw_generic,-1,generic,_countof(generic)))continue;
        int score=pressfit_match_name(instance,selector->instance,selector->instance_key);
        if(!score||(selector->generic[0]&&!pressfit_match_name(generic,selector->generic,selector->generic_key)))continue;
        size_t offset=wcslen(directory)+1;
        for(size_t i=0;i<catalog->count;i++){
            const CompanyFastenerItem *item=&catalog->items[i];
            ProName name;
            if(i&&!company_fastener_catalog_key(item,&catalog->items[i-1]))continue;
            if(item->name_offset==offset&&!_wcsnicmp(item->path,directory,offset-1)&&
               pressfit_model_name(item,name)&&!_wcsicmp(name,generic))
                pressfit_add_match(match,score,instance,generic,item);
        }
    }
    free(contents);
    return 1;
}

static int pressfit_search_directory(const wchar_t *directory,const CompanyFastenerCatalog *catalog,
    const PressfitSelector *selector,PressfitMatch *match){
    wchar_t search[MAX_PATH];
    if(swprintf_s(search,_countof(search),L"%ls\\*.idx",directory)<0)return 0;
    WIN32_FIND_DATAW file;
    HANDLE list=FindFirstFileW(search,&file);
    if(list==INVALID_HANDLE_VALUE)return 0;
    int readable=0;
    do{
        if(!(file.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
           swprintf_s(search,_countof(search),L"%ls\\%ls",directory,file.cFileName)>=0)
            readable+=pressfit_search_index(search,directory,catalog,selector,match);
    }while(FindNextFileW(list,&file));
    FindClose(list);
    return readable;
}

static ProError pressfit_find_source(const wchar_t *requested,const wchar_t *root,
    PressfitSource *source,PartText *out){
    PressfitSelector selector={0};
    if(!pressfit_selector(requested,&selector)){
        part_text_add(out,L"请输入注册压铆件的完整型号。族表同名规格可用“族表名:实例名”区分。\n");
        return PRO_TK_BAD_INPUTS;
    }
    CompanyFastenerCatalog catalog={0};
    if(!company_fastener_catalog_scan(&catalog,root,4,0)){
        free(catalog.items);
        part_text_add(out,L"压铆件库有目录读取失败，未按不完整结果选件。\n");
        return PRO_TK_CANT_OPEN;
    }
    if(catalog.count)qsort(catalog.items,catalog.count,sizeof(*catalog.items),company_fastener_catalog_order);
    PressfitMatch match={0};
    for(size_t i=0;i<catalog.count;i++){
        const CompanyFastenerItem *item=&catalog.items[i];
        ProName model;
        if(selector.generic[0]||!pressfit_model_name(item,model)||
           (selector.revision?item->revision!=selector.revision:
              (i&&!company_fastener_catalog_key(item,&catalog.items[i-1]))))continue;
        int score=pressfit_match_name(model,selector.instance,selector.instance_key);
        pressfit_add_match(&match,score,model,L"",item);
    }
    int readable_indexes=0;
    if(!selector.revision)for(size_t i=0;i<catalog.count;i++){
        const CompanyFastenerItem *item=&catalog.items[i];
        size_t previous=0;
        while(previous<i){
            const CompanyFastenerItem *other=&catalog.items[previous];
            if(other->name_offset==item->name_offset&&
               !_wcsnicmp(other->path,item->path,item->name_offset-1))break;
            previous++;
        }
        wchar_t directory[MAX_PATH];
        if(previous==i&&!wcsncpy_s(directory,_countof(directory),item->path,item->name_offset-1))
            readable_indexes+=pressfit_search_directory(directory,&catalog,&selector,&match);
    }
    if(!selector.revision){
        pressfit_acceptance_find_source(&catalog,&selector,&match);
        pressfit_preserve_registered_ambiguity(&catalog,&selector,&match);
    }
    if(!match.matches&&selector.generic[0]&&!selector.revision){
        for(size_t i=0;i<catalog.count;i++){
            const CompanyFastenerItem *item=&catalog.items[i];
            ProName model;
            if((i&&!company_fastener_catalog_key(item,&catalog.items[i-1]))||
               !pressfit_model_name(item,model))continue;
            int score=pressfit_match_name(model,selector.generic,selector.generic_key);
            pressfit_add_match(&match,score,selector.instance,model,item);
        }
    }
    free(catalog.items);
    if(match.matches>1){
        part_text_add(out,L"型号有多个注册来源，请明确选择族表或文件：\n");
        for(unsigned i=0;i<match.matches&&i<_countof(match.choices);i++){
            const PressfitSource *item=&match.choices[i];
            part_text_add(out,L"%ls%ls%ls｜%ls\n",item->generic,item->generic[0]?L":":L"",
                item->instance,item->path);
        }
        return PRO_TK_E_FOUND;
    }
    if(!match.matches){
        if(selector.revision)part_text_add(out,L"压铆件目录可读，未找到指定版本 %ls。\n",requested);
        else if(readable_indexes)part_text_add(out,L"压铆件目录和 Creo 索引可读，未找到型号 %ls 的精确登记。\n",requested);
        else part_text_add(out,L"压铆件目录可读，Creo 族表索引缺失或无法读取，现有验收也不能核实 %ls。\n",requested);
        part_text_add(out,L"可用“族表名:实例名”直接核对原生族表，或指定库中独立零件的完整 PRT 名。\n");
        return PRO_TK_E_NOT_FOUND;
    }
    *source=match.choices[0];
    source->bso_length=selector.bso_length;
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_family_has_row(ProFaminstance *instance,ProError status,ProAppData data){
    int *has_row=data;
    if(!status)*has_row=1;
    return status?status:PRO_TK_USER_ABORT;
}

typedef struct {
    const wchar_t *requested;
    ProName key,actual;
    int score,matches;
} PressfitFamilyRow;

static ProError pressfit_native_row_match(ProFaminstance *instance,ProError status,ProAppData data){
    PressfitFamilyRow *match=data;
    if(status)return status;
    const int score=pressfit_match_name(instance->name,match->requested,match->key);
    if(!score||score<match->score)return PRO_TK_NO_ERROR;
    if(score>match->score){
        match->score=score;
        match->matches=0;
        wcscpy_s(match->actual,_countof(match->actual),instance->name);
    }
    if(!match->matches||_wcsicmp(match->actual,instance->name))match->matches++;
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_native_instance(ProMdl generic,PressfitSource *source,ProMdl *model,PartText *out){
    ProFamtable table;
    ProError error=ProFamtableInit(generic,&table);
    if(error)return error;
    if(!source->generic[0]){
        int has_row=0;
        error=ProFamtableInstanceVisit(&table,pressfit_family_has_row,NULL,&has_row);
        if(has_row){
            part_text_add(out,L"%ls 是族表母件。请选择具体规格，或用“族表名:实例名”指定。\n",source->instance);
            return PRO_TK_BAD_CONTEXT;
        }
        if(error&&error!=PRO_TK_E_NOT_FOUND)return error;
        *model=generic;
        return PRO_TK_NO_ERROR;
    }
    ProName name;
    wcscpy_s(name,_countof(name),source->instance);
    ProFaminstance instance;
    int locked;
    error=ProFaminstanceInit(name,&table,&instance);
    if(!error)error=ProFaminstanceCheck(&instance,&locked);
    if(error==PRO_TK_E_NOT_FOUND){
        PressfitFamilyRow match={source->instance};
        if(pressfit_name_key(source->instance,match.key)){
            const ProError rows=ProFamtableInstanceVisit(&table,pressfit_native_row_match,NULL,&match);
            if(rows&&rows!=PRO_TK_E_NOT_FOUND)error=rows;
            else if(match.matches>1){
                part_text_add(out,L"原生族表 %ls 有多个名称近似的规格，请使用表中的完整行名。\n",source->generic);
                return PRO_TK_E_FOUND;
            }else if(match.matches==1){
                wcscpy_s(name,_countof(name),match.actual);
                error=ProFaminstanceInit(name,&table,&instance);
                if(!error)error=ProFaminstanceCheck(&instance,&locked);
            }
        }
    }
    if(error){
        part_text_add(out,L"原生族表 %ls 未核实精确规格 %ls，Creo %d。\n",source->generic,name,error);
        return error;
    }

    ProMdl loaded=NULL,owner=NULL;
    error=ProMdlnameInit(name,PRO_MDL_PART,&loaded);
    if(error==PRO_TK_E_NOT_FOUND)error=ProFaminstanceMdlGet(&instance,&loaded);
    int was_loaded=!error;
    if(error==PRO_TK_E_NOT_FOUND)error=ProFaminstanceRetrieve(&instance,&loaded);
    if(error||!loaded)return error?error:PRO_TK_BAD_CONTEXT;

    ProMdlName actual;
    error=ProFaminstanceGenericGet(loaded,PRO_B_TRUE,&owner);
    if(!error)error=ProMdlMdlnameGet(loaded,actual);
    if(!error&&(owner!=generic||_wcsicmp(actual,name)))error=PRO_TK_BAD_CONTEXT;
    ProBoolean modified=PRO_B_TRUE;
    if(was_loaded&&(error||ProMdlModificationVerify(loaded,&modified)||modified))
        return fastener_bso_report_conflict(loaded,L"族表实例核对",name,source->path,out);
    if(!error){
        wcscpy_s(source->instance,_countof(source->instance),name);
        *model=loaded;
    }
    return error;
}

static ProError pressfit_read_exact(const wchar_t *selector,ProMdl *part,
    wchar_t path[MAX_PATH],PartText *out){
    PressfitSource source={0};
    wchar_t root[MAX_PATH],resolved[MAX_PATH];
    *part=NULL;
    path[0]=0;
    if(!company_fastener_resolve_report(COMPANY_OPEN_FASTENER_FIRST+4,root,out))return PRO_TK_CANT_OPEN;
    ProError error=pressfit_find_source(selector,root,&source,out);
    if(error)return error;

    HANDLE reference=company_open_path(source.path,0,resolved);
    if(reference==INVALID_HANDLE_VALUE)return PRO_TK_CANT_OPEN;
    NativeFile file={0};
    HANDLE lock=company_is_under(resolved,root)?company_library_source(resolved,&file,out):INVALID_HANDLE_VALUE;
    if(lock==INVALID_HANDLE_VALUE){CloseHandle(reference);return PRO_TK_CANT_OPEN;}

    ProMdl generic=NULL,model=NULL;
    int opened=0;
    error=attachment_open_native(&file,&generic,&opened,out);
    if(!error)error=pressfit_native_instance(generic,&source,&model,out);
    if(!error)error=part_health(model);
    int bso=source.bso_length;
    if(!bso&&(!_wcsnicmp(source.generic,L"BSO_",4)||!_wcsnicmp(source.generic,L"BSOS_",5))){
        const wchar_t *length=wcsrchr(source.instance,L'X');
        if(!length)length=wcsrchr(source.instance,L'x');
        if(length)bso=pressfit_bso_size(length+1);
    }
    if(!error&&bso)error=fastener_bso_measure_length(model,bso,out);
    if(!error){
        *part=model;
        wcscpy_s(path,MAX_PATH,resolved);
        if(source.generic[0])part_text_add(out,L"压铆件库已核实 %ls，所属族表 %ls。\n",source.instance,source.generic);
        else part_text_add(out,L"压铆件库已核实独立零件 %ls。\n",source.instance);
        if(bso)part_text_add(out,L"沿零件轴线实测长度 %d mm。\n",bso);
    }else{
        if(opened&&generic&&!model)ProMdlErase(generic);
        part_text_add(out,L"库件 %ls 的原生读取或规格核对未完成（Creo %d）。\n",source.instance,error);
    }
    CloseHandle(lock);
    CloseHandle(reference);
    return error;
}


static ProError pressfit_verify_project(ProMdl source,ProMdl project,PartText *out){
    ProMaterial original={0},staged={0};
    ProError error=ProMaterialCurrentGet((ProSolid)source,&original);
    if(!error)error=ProMaterialCurrentGet((ProSolid)project,&staged);
    if(!error&&_wcsicmp(original.matl_name,staged.matl_name)){
        part_text_add(out,L"项目件材料读回与库件不一致。\n");
        error=PRO_TK_BAD_CONTEXT;
    }
    return error?error:fastener_bso_verify_copy(source,project,out);
}

static ProError pressfit_project_stem(ProMdl source,const wchar_t *library_path,ProName stem){
    ProMdlName name;
    ProError error=ProMdlMdlnameGet(source,name);
    if(error)return error;
    unsigned long hash=2166136261UL;
    for(const wchar_t *c=library_path;*c;c++)hash=(hash^(unsigned long)towupper(*c))*16777619UL;
    for(const wchar_t *c=name;*c;c++)hash=(hash^(unsigned long)towupper(*c))*16777619UL;

    if(swprintf_s(stem,PRO_NAME_SIZE,L"%.14ls_BWTL_%08lx",name,hash)<0||!dv_safe_leaf(stem))
        return PRO_TK_INVALID_NAME;
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_library_project(ProMdl source,const wchar_t *library_path,ProMdl *copy,
    wchar_t saved[MAX_PATH],PartText *out){
    ProName stem;
    ProError error=pressfit_project_stem(source,library_path,stem);
    return error?error:fastener_copy_project_named(source,stem,copy,saved,pressfit_verify_project,out);
}

static FastenerSavedView *pressfit_project_placement_begin(ProMdl project,const wchar_t *path){
    FastenerSavedView *known=fastener_saved_model(project);
    return fastener_project_bytes_match(known,path)?known:NULL;
}

static void pressfit_project_placement_end(ProMdl source,ProMdl project,const wchar_t *path,
    FastenerSavedView *before){
    if(!before||before->model!=project||!fastener_project_bytes_match(before,path))return;
    ProMdlName name;
    ProMdl same=NULL;
    ProWVerstamp stamp=NULL;
    if(ProMdlMdlnameGet(project,name)||ProMdlnameInit(name,PRO_MDL_PART,&same)||same!=project||
       ProMdlVerstampGet(project,&stamp)||!stamp)return;
    if(!ProVerstampEqual(before->stamp,stamp)){
        wchar_t summary[1200]=L"";
        PartText verification={summary,_countof(summary),0};
        if(!pressfit_verify_project(source,project,&verification)){
            ProVerstampFree(&before->stamp);
            before->stamp=stamp;
            before->verified_placement_stamp=1;
            return;
        }
    }
    ProVerstampFree(&stamp);
}
