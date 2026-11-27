/* A project stem comes from the live total assembly selected by the user.
   The drawing standard reserves annual serial allocation to Engineering. */
static int company_pair_digits(const wchar_t *value,int count){
    for(int i=0;i<count;i++)if(value[i]<L'0'||value[i]>L'9')return 0;
    return 1;
}

static int company_pair_revision(const wchar_t *value){
    return wcslen(value)==3&&(value[0]==L'V'||value[0]==L'v')&&company_pair_digits(value+1,2);
}

static int company_pair_total(const wchar_t *name){
    return wcslen(name)==14&&towupper(name[0])==L'M'&&towupper(name[3])==L'A'&&
        company_pair_digits(name+1,2)&&company_pair_digits(name+4,2)&&
        !wcsncmp(name+6,L"00000",5)&&company_pair_revision(name+11);
}

static int company_pair_serial(const wchar_t *name,const wchar_t *prefix){
    if(wcslen(name)<14||_wcsnicmp(name,prefix,8)||!company_pair_digits(name+8,3)||
       towupper(name[11])!=L'V'||!company_pair_digits(name+12,2))return 0;
    if(name[14]&&((name[14]!=L'.')||
       (_wcsnicmp(name+15,L"asm",3)&&_wcsnicmp(name+15,L"prt",3)&&_wcsnicmp(name+15,L"drw",3))||
       (name[18]&&name[18]!=L'.')))return 0;
    return 100*(name[8]-L'0')+10*(name[9]-L'0')+name[10]-L'0';
}

static ProError company_pair_active_total(ProMdl *total){
    ProMdl model=NULL;
    ProMdlType type;
    ProMdlName name;
    ProPath directory,origin,latest;
    if(ProMdlCurrentGet(&model)||ProMdlTypeGet(model,&type)||type!=PRO_MDL_ASSEMBLY||
       ProMdlMdlnameGet(model,name)||!company_pair_total(name))return PRO_TK_NOT_EXIST;
    ProError error=ProDirectoryCurrentGet(directory);
    if(!error)error=ProMdlOriginGet(model,origin);
    if(error)return error;
    int occupied=0;
    error=model_assembly_disk(directory,name,latest,&occupied);
    if(error)return error;
    const int saved=latest[0]&&origin[0]&&assembly_rename_same_file(origin,latest);
    const int staged=!occupied&&model_assembly_creation_index(model,name,directory,origin)>=0;
    if(!saved&&!staged)return PRO_TK_NOT_EXIST;
    ChatTaskModel verified;
    error=chat_task_model_capture(model,&verified);
    if(error)return error;
    ChatTaskProject *project=chat_task_current_project(1);
    if(!project)return PRO_TK_BAD_CONTEXT;
    project->project_assembly=verified;
    *total=model;
    return PRO_TK_NO_ERROR;
}

static ProError company_pair_project(ProMdlName prefix,PartText *out){
    ProMdl total=NULL;
    ProError error=chat_task_company_assembly(&total);
    if(error==PRO_TK_E_NOT_FOUND)error=company_pair_active_total(&total);
    if(error)return error;
    ProMdlName actual=L"";
    error=ProMdlMdlnameGet(total,actual);
    if(error)return error;
    if(!company_pair_total(actual)){
        if(out)part_text_add(out,L"已读取总装 %ls，无法从它核对公司图号中的年份和工程流水。\n",actual);
        return PRO_TK_BAD_CONTEXT;
    }
    return swprintf_s(prefix,PRO_NAME_SIZE,L"%.6ls81",actual)<0?PRO_TK_BAD_INPUTS:PRO_TK_NO_ERROR;
}

static ProError company_pair_scan_names(const wchar_t *directory,const wchar_t *prefix,unsigned char used[1000]){
    ProPath mask;
    if(swprintf_s(mask,PRO_PATH_SIZE,L"%ls\\%ls*",directory,prefix)<0)return PRO_TK_BAD_INPUTS;
    WIN32_FIND_DATAW item;
    HANDLE find=FindFirstFileW(mask,&item);
    if(find!=INVALID_HANDLE_VALUE){
        do{
            const int serial=company_pair_serial(item.cFileName,prefix);
            if(serial)used[serial]=1;
        }while(FindNextFileW(find,&item));
        DWORD done=GetLastError();
        FindClose(find);
        if(done!=ERROR_NO_MORE_FILES)return PRO_TK_CANT_OPEN;
    }else if(GetLastError()!=ERROR_FILE_NOT_FOUND)return PRO_TK_CANT_OPEN;
    const ProMdlType types[]={PRO_MDL_PART,PRO_MDL_ASSEMBLY,PRO_MDL_DRAWING};
    for(int kind=0;kind<_countof(types);kind++){
        ProMdl *models=NULL;
        int count=0;
        ProError error=ProSessionMdlList(types[kind],&models,&count);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
        for(int i=0;!error&&i<count;i++){
            ProMdlName name;
            error=ProMdlMdlnameGet(models[i],name);
            if(!error){
                const int serial=company_pair_serial(name,prefix);
                if(serial)used[serial]=1;
            }
        }
        if(models)ProArrayFree((ProArray *)&models);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

/* Windows closes the E-drive reservation if this Creo process exits. Keep
   the sequence reserved while the new unsaved model is still in this session. */
typedef struct {HANDLE file;ProMdlName name;ProPath directory;} CompanyPairReservation;
static CompanyPairReservation company_pair_reserved[999];
static int company_pair_reservations;

static HANDLE company_pair_claim(const wchar_t *directory,const wchar_t *name,DWORD *reason){
    ProPath path;
    if(swprintf_s(path,PRO_PATH_SIZE,L"%ls\\.bwtl-pair-%.11ls.lock",directory,name)<0){
        *reason=ERROR_BAD_PATHNAME;
        return INVALID_HANDLE_VALUE;
    }
    HANDLE lock=CreateFileW(path,GENERIC_WRITE|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,CREATE_NEW,
        FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    *reason=lock==INVALID_HANDLE_VALUE?GetLastError():0;
    return lock;
}

static ProError company_pair_remember_claim(HANDLE lock,const wchar_t *directory,const wchar_t *name){
    if(company_pair_reservations>=_countof(company_pair_reserved))return PRO_TK_OUT_OF_RANGE;
    CompanyPairReservation *kept=&company_pair_reserved[company_pair_reservations++];
    kept->file=lock;
    wcscpy_s(kept->name,PRO_NAME_SIZE,name);
    wcscpy_s(kept->directory,PRO_PATH_SIZE,directory);
    return PRO_TK_NO_ERROR;
}

static ProError company_pair_next(const wchar_t *directory,const wchar_t *prefix,const wchar_t *revision,
    ProMdlName name,HANDLE *claim,PartText *out){
    name[0]=0;
    *claim=INVALID_HANDLE_VALUE;
    if(!company_pair_revision(revision)||!storage_output_directory(directory))return PRO_TK_BAD_CONTEXT;
    const DWORD attrs=GetFileAttributesW(directory);
    if(attrs==INVALID_FILE_ATTRIBUTES||!(attrs&FILE_ATTRIBUTE_DIRECTORY)||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))
        return PRO_TK_CANT_OPEN;
    unsigned char used[1000]={0};
    ProError error=company_pair_scan_names(directory,prefix,used);
    if(error){
        if(out)part_text_add(out,L"没能读完整的项目目录或 Creo 会话图号，请先核对项目目录的访问。\n");
        return error;
    }
    for(int serial=1;serial<=999;serial++){
        if(used[serial])continue;
        if(swprintf_s(name,PRO_NAME_SIZE,L"%ls%03d%ls",prefix,serial,revision)<0)return PRO_TK_INVALID_NAME;
        DWORD reason=0;
        *claim=company_pair_claim(directory,name,&reason);
        if(*claim==INVALID_HANDLE_VALUE){
            if(reason==ERROR_ALREADY_EXISTS||reason==ERROR_FILE_EXISTS||reason==ERROR_SHARING_VIOLATION)continue;
            if(out)part_text_add(out,L"无法在当前项目预留结构序号（Windows %lu）。\n",reason);
            return PRO_TK_CANT_WRITE;
        }
        memset(used,0,sizeof(used));
        error=company_pair_scan_names(directory,prefix,used);
        if(!error&&!used[serial])return PRO_TK_NO_ERROR;
        CloseHandle(*claim);
        *claim=INVALID_HANDLE_VALUE;
        if(error)return error;
    }
    if(out)part_text_add(out,L"这个项目的 81 类结构号 001–999 已被占用或预留。\n");
    return PRO_TK_E_FOUND;
}

static ProError company_pair_number_for_creation(const ModelingPartIntent *request,const wchar_t *revision,
    ProMdlName name,PartText *out){
    if(!request||request->scope!=MODELING_PART_NEW||request->name[0]||request->kind==1)
        return PRO_TK_NOT_EXIST;
    ProMdlName prefix;
    ProError error=company_pair_project(prefix,out);
    if(error)return error;
    ProPath directory;
    error=ProDirectoryCurrentGet(directory);
    if(error)return error;
    HANDLE claim=INVALID_HANDLE_VALUE;
    error=company_pair_next(directory,prefix,revision,name,&claim,out);
    if(!error){
        error=company_pair_remember_claim(claim,directory,name);
        if(error)CloseHandle(claim);
    }
    if(!error&&out)part_text_add(out,L"沿用已核对的项目流水 %.2ls，查重后分配 81 类结构序号 %.3ls。\n",prefix+4,name+8);
    return error;
}

static ProError company_pair_dependency(ProMdl owner,ProMdlType owner_type,
    ProMdl parent,ProMdl part,const wchar_t *name,int *found){
    *found=0;
    if(owner_type==PRO_MDL_DRAWING){
        ProSolid *models=NULL;
        int count=0;
        ProError error=ProDrawingSolidsCollect((ProDrawing)owner,&models);
        if(!error)error=ProArraySizeGet((ProArray)models,&count);
        for(int i=0;!error&&i<count;i++)
            if((ProMdl)models[i]==parent||(ProMdl)models[i]==part){*found=1;break;}
        if(models)ProArrayFree((ProArray *)&models);
        return error==PRO_TK_E_NOT_FOUND?PRO_TK_NO_ERROR:error;
    }
    ProMdlnameShortdata *names=NULL;
    ProMdlfileType *types=NULL;
    int count=0;
    ProError error=ProMdlDependenciesDataList(owner,&names,&types,&count);
    for(int i=0;!error&&i<count;i++)
        if((types[i]==PRO_MDLFILE_PART||types[i]==PRO_MDLFILE_ASSEMBLY)&&!_wcsicmp(names[i].name,name)){
            *found=1;
            break;
        }
    if(names)ProArrayFree((ProArray *)&names);
    if(types)ProArrayFree((ProArray *)&types);
    return error==PRO_TK_E_NOT_FOUND?PRO_TK_NO_ERROR:error;
}

static ProError company_pair_external_refs(ProMdl parent,ProMdl part,const wchar_t *name,PartText *out){
    const ProMdlType types[]={PRO_MDL_ASSEMBLY,PRO_MDL_DRAWING,PRO_MDL_PART};
    for(int kind=0;kind<_countof(types);kind++){
        ProMdl *models=NULL;
        int count=0;
        ProError error=ProSessionMdlList(types[kind],&models,&count);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
        for(int i=0;!error&&i<count;i++){
            if(models[i]==parent||models[i]==part)continue;
            int found=0;
            error=company_pair_dependency(models[i],types[kind],parent,part,name,&found);
            if(!error&&found){
                ProMdlName dependent=L"";
                ProMdlMdlnameGet(models[i],dependent);
                part_text_add(out,L"Creo 会话里的 %ls 引用了这对模型，需要先核对它的引用关系。\n",dependent);
                error=PRO_TK_E_IN_USE;
            }
        }
        if(models)ProArrayFree((ProArray *)&models);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

static int company_pair_model_name(ProMdl model,ProMdlType type,const wchar_t *expected){
    ProMdlName actual;
    ProMdl found=NULL;
    return !ProMdlMdlnameGet(model,actual)&&!_wcsicmp(actual,expected)&&
        !ProMdlnameInit(actual,type,&found)&&found==model;
}

static ProError company_pair_occurrence(ProMdl parent,ProMdl part,int id,ProMatrix pose){
    ProAsmcomp component;
    ProMdl found=NULL;
    ProError error=ax_component_feature(parent,id,&component);
    if(!error)error=ProAsmcompMdlGet(&component,&found);
    if(!error&&found!=part)error=PRO_TK_BAD_CONTEXT;
    return error?error:ax_actual_pose(parent,&component,pose);
}

static ProError company_pair_unsaved(ProMdl model,const wchar_t *directory,const wchar_t *name,
    const wchar_t *extension){
    int occupied=0;
    ProPath origin;
    ProError error=model_named_disk(directory,name,extension,NULL,&occupied);
    if(!error&&occupied)error=PRO_TK_E_IN_USE;
    if(!error)error=ProMdlOriginGet(model,origin);
    if(error||!origin[0])return error;
    if(!ai_dir[0])return PRO_TK_BAD_CONTEXT;
    size_t prefix=wcslen(ai_dir);
    const wchar_t *file=wcsrchr(origin,L'\\');
    if(!file)file=wcsrchr(origin,L'/');
    return !_wcsnicmp(origin,ai_dir,prefix)&&(origin[prefix]==L'\\'||origin[prefix]==L'/')&&
        file&&file>origin+prefix?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}

static int company_pair_restore_name(ProMdl model,ProMdlType type,ProMdlName original,ProMdlName attempted){
    if(company_pair_model_name(model,type,original))return 1;
    if(!company_pair_model_name(model,type,attempted))return 0;
    return !ProMdlnameRename(model,original)&&company_pair_model_name(model,type,original);
}

static ProError company_pair_rename(ProMdl parent,ProMdl part,int occurrence,ProMdlName old,
    ProMdlName target,const wchar_t *directory,PartText *out){
    if(!company_pair_model_name(parent,PRO_MDL_ASSEMBLY,old)||!company_pair_model_name(part,PRO_MDL_PART,old)||
       ax_recovery_uncertain)return PRO_TK_BAD_CONTEXT;
    if(!_wcsicmp(old,target))return PRO_TK_NO_ERROR;
    ProMdlsubtype subtype;
    ProError error=ProMdlSubtypeGet(part,&subtype);
    if(!error&&subtype!=PROMDLSTYPE_PART_SHEETMETAL)error=PRO_TK_INVALID_TYPE;
    if(!error)error=company_pair_unsaved(parent,directory,old,L"asm");
    if(!error)error=company_pair_unsaved(part,directory,old,L"prt");
    if(error==PRO_TK_E_IN_USE)
        part_text_add(out,L"这对模型已经写入项目文件。需先核对磁盘上的上级装配和工程图引用，再处理已保存图号。\n");
    if(!error)error=company_pair_external_refs(parent,part,old,out);
    if(!error)error=model_named_available(target,directory,PRO_MDL_ASSEMBLY);
    if(!error)error=model_named_available(target,directory,PRO_MDL_PART);
    if(error)return error;
    ProMatrix before,after;
    double scale;
    error=company_pair_occurrence(parent,part,occurrence,before);
    if(!error)error=qa_mm_per_unit(parent,&scale);
    if(error)return error;

    error=ProMdlnameRename(part,target);
    if(!error&&!company_pair_model_name(part,PRO_MDL_PART,target))error=PRO_TK_GENERAL_ERROR;
    if(!error)error=ProMdlnameRename(parent,target);
    if(!error&&(!company_pair_model_name(parent,PRO_MDL_ASSEMBLY,target)||
        !company_pair_model_name(part,PRO_MDL_PART,target)))error=PRO_TK_GENERAL_ERROR;
    if(!error)error=company_pair_occurrence(parent,part,occurrence,after);
    if(!error&&!ax_same_pose(before,after,scale))error=PRO_TK_BAD_CONTEXT;
    if(error){
        const int parent_ok=company_pair_restore_name(parent,PRO_MDL_ASSEMBLY,old,target);
        const int child_ok=company_pair_restore_name(part,PRO_MDL_PART,old,target);
        if(!parent_ok||!child_ok||company_pair_occurrence(parent,part,occurrence,after)||
           !ax_same_pose(before,after,scale)){
            ax_recovery_uncertain=1;
            part_text_add(out,L"改名回退尚未核实，需先核对 Creo 模型树中两份模型的名称与组件关系。\n");
        }else part_text_add(out,L"这次改名没有完成，已回读两份原名称和原装配位置。\n");
        return error;
    }
    model_assembly_creation_record(parent,target);
    assembly_rename_refresh(parent);
    assembly_rename_refresh(part);
    int parent_window,part_window;
    wchar_t occurrence_path[32];
    if(!ProMdlWindowGet(parent,&parent_window)&&!ProMdlWindowGet(part,&part_window)&&
       swprintf_s(occurrence_path,_countof(occurrence_path),L"ID:%d",occurrence)>=0)
        ax_return_note_open(parent,parent_window,part,part_window,occurrence_path,directory);
    part_text_add(out,L"已将 %ls 的装配和钣金件统一改为 %ls。已在 Creo 回读 %ls.ASM、%ls.PRT 和原组件 ID:%d。未自动保存到项目目录。\n",
        old,target,target,target,occurrence);
    return PRO_TK_NO_ERROR;
}

static int company_pair_recent_numbering(void){
    const wchar_t *last=conversation;
    for(const wchar_t *p=conversation;(p=wcsstr(p,L"\n用户："))!=NULL;p++)last=p;
    return wcsstr(last,L"图号")||wcsstr(last,L"结构序号");
}

static int company_pair_number_chat(const wchar_t *text){
    const wchar_t *p=model_request_action_start(text);
    ProMdlName requested=L"";
    const wchar_t *const correct[]={
        L"把刚才那对图号改成正确的",L"把刚才那对图号改正确",L"把刚才那个图号改成正确的",
        L"把刚才那个图号改正确",L"把刚才的图号改正确",L"按公司规则改刚才那对图号"
    };
    int identified=modeling_part_take_any(&p,correct,_countof(correct));
    if(!identified&&company_pair_recent_numbering()){
        const wchar_t *const from_history[]={
            L"那就帮我改成正确的",L"把刚才那个改成正确的",L"那就改正确",L"改成正确的",L"改正确"
        };
        identified=modeling_part_take_any(&p,from_history,_countof(from_history));
    }
    if(!identified){
        const wchar_t *const rename[]={
            L"把刚才那个图号改成",L"把刚才那对图号改成",L"把刚才的图号改成",L"把刚才那对改成"
        };
        if(!modeling_part_take_any(&p,rename,_countof(rename)))return 0;
        p=modeling_part_skip(p);
        if(!modeling_part_name(&p,requested))return 0;
    }
    if(*modeling_part_skip(p))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    wchar_t reply[2600]=L"";
    PartText out={reply,_countof(reply),0};
    ProMdl parent=NULL,part=NULL;
    ProMdlName old=L"",prefix=L"",next=L"";
    ProPath directory;
    int occurrence=0,record_pair=0;
    HANDLE claim=INVALID_HANDLE_VALUE;
    ProError error=chat_task_resolve_pair(&parent,&part,&occurrence);
    if(error){
        part_text_add(&out,L"当前项目没有能和 Creo 原生组件关系核对上的上次同号模型。\n");
        goto done;
    }
    if((error=ProMdlMdlnameGet(parent,old))||(error=ProDirectoryCurrentGet(directory)))goto done;
    error=company_pair_project(prefix,&out);
    if(error){
        if(error==PRO_TK_NOT_EXIST)part_text_add(&out,L"还缺当前项目已确认的总装图号，无法确定年度流水。\n");
        goto done;
    }
    if(requested[0]){
        if(wcslen(requested)!=14||!company_pair_serial(requested,prefix)){
            error=PRO_TK_INVALID_NAME;
            part_text_add(&out,L"当前项目的零件图号以 %ls 开头，再接三位结构序号及 V00 这样的版本。\n",prefix);
            goto done;
        }
        if(!_wcsicmp(requested,old)){
            part_text_add(&out,L"已从 Creo 核对，刚才的装配和钣金件已经使用 %ls。\n",old);
            goto done;
        }
        wcscpy_s(next,PRO_NAME_SIZE,requested);
        int owns_claim=0;
        for(int i=0;i<company_pair_reservations;i++){
            const CompanyPairReservation *held=&company_pair_reserved[i];
            if(!_wcsicmp(held->directory,directory)&&!_wcsnicmp(held->name,next,11)&&!_wcsnicmp(old,next,11)){
                owns_claim=1;
                break;
            }
        }
        if(!owns_claim){
            DWORD reason=0;
            claim=company_pair_claim(directory,next,&reason);
            if(claim==INVALID_HANDLE_VALUE){
                error=PRO_TK_E_IN_USE;
                part_text_add(&out,L"这个结构序号无可用预留位置（Windows %lu）。\n",reason);
                goto done;
            }
        }
        if(_wcsnicmp(old,next,11)){
            unsigned char used[1000]={0};
            error=company_pair_scan_names(directory,prefix,used);
            if(!error&&used[company_pair_serial(next,prefix)])error=PRO_TK_E_FOUND;
            if(error){
                part_text_add(&out,L"希望使用的结构序号已被占用，或未能完成项目查重。\n");
                goto done;
            }
        }
    }else{
        if(wcslen(old)==14&&company_pair_serial(old,prefix)){
            part_text_add(&out,L"已从 Creo 核对，刚才的装配和钣金件已经使用正确格式的 %ls。\n",old);
            goto done;
        }
        size_t length=wcslen(old);
        if(length<3||!company_pair_revision(old+length-3)){
            error=PRO_TK_BAD_INPUTS;
            part_text_add(&out,L"已定位刚才的模型 %ls，需要核对它应沿用的版本。\n",old);
            goto done;
        }
        error=company_pair_next(directory,prefix,old+length-3,next,&claim,&out);
        if(error)goto done;
    }
    if(company_pair_reservations>=_countof(company_pair_reserved)){
        error=PRO_TK_OUT_OF_RANGE;
        goto done;
    }
    error=company_pair_rename(parent,part,occurrence,old,next,directory,&out);
    if(claim!=INVALID_HANDLE_VALUE&&(!error||ax_recovery_uncertain)){
        if(!company_pair_remember_claim(claim,directory,next))claim=INVALID_HANDLE_VALUE;
    }
    if(!error)record_pair=1;
done:
    if(claim!=INVALID_HANDLE_VALUE)CloseHandle(claim);
    if(error&&!reply[0])part_text_add(&out,L"已核对当前项目，配套图号改名未完成（Creo %d）。\n",error);
    if(record_pair)chat_task_record_pair(parent,part,occurrence,reply);
    smart_record_tool(reply);
    smart_reply(reply);
    return 1;
}

