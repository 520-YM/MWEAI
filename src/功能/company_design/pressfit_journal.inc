/* Resume records bind a press-fit batch to one Creo assembly and exact library bytes. */
typedef enum {
    PRESSFIT_PENDING, PRESSFIT_ADDED, PRESSFIT_REUSED, PRESSFIT_WRONG,
    PRESSFIT_DUPLICATE, PRESSFIT_FAILED, PRESSFIT_REPLACED
} PressfitDisposition;

typedef struct {
    int id,hole,face,constraints,regenerated,native_error;
    PressfitDisposition disposition;
    double radial,gap,facing,parallel;
    wchar_t panel[AX_PATH_DEPTH*12+4];
    const wchar_t *stage;
} PressfitSiteReceipt;

typedef struct {
    int count,added,reused,replaced,failed;
    PressfitSiteReceipt rows[12];
} PressfitBatchResult;

typedef struct {
    int hole,face,id;
    wchar_t panel[AX_PATH_DEPTH*12+4];
    double diameter,contact[3],normal[3];
    PressfitDisposition disposition;
} PressfitCheckpointSite;

typedef struct {
    HANDLE lock;
    ProMdl assembly;
    ProMdlName target;
    wchar_t workdir[MAX_PATH],file[MAX_PATH],selector[PRO_NAME_SIZE*2],source_hash[65];
    char initial[240],last[240];
    int shaft,seat,replace,count,complete,pending;
    double mm,interference;
    PressfitCheckpointSite sites[12];
} PressfitCheckpoint;

static int pressfit_record_path(wchar_t file[MAX_PATH],const wchar_t *directory,
    const wchar_t *name,const wchar_t *ending){
    wchar_t base[MAX_PATH];
    return storage_method_directory(base,directory,1)&&
        swprintf_s(file,MAX_PATH,L"%ls\\%ls%ls",base,name,ending)>=0;
}

static ProError pressfit_record_scope(ProMdl assembly,PressfitCheckpoint *state){
    if(!assembly)return PRO_TK_BAD_INPUTS;
    ProMdlType type;
    ProError error=ProMdlTypeGet(assembly,&type);
    if(error||type!=PRO_MDL_ASSEMBLY)return error?error:PRO_TK_BAD_CONTEXT;
    ProPath directory;
    if((error=ProDirectoryCurrentGet(directory))||(error=ProMdlMdlnameGet(assembly,state->target)))return error;
    company_trim_path(directory);
    if(!ax_study_key(state->target)||!storage_output_directory(directory)||
       wcslen(directory)>=_countof(state->workdir))return PRO_TK_BAD_CONTEXT;
    HANDLE handle=CreateFileW(directory,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(handle==INVALID_HANDLE_VALUE)return PRO_TK_CANT_OPEN;
    wchar_t resolved[PRO_PATH_SIZE];
    const DWORD n=GetFinalPathNameByHandleW(handle,resolved,_countof(resolved),VOLUME_NAME_DOS);
    CloseHandle(handle);
    if(n<7||n>=_countof(resolved)||wcsncmp(resolved,L"\\\\?\\",4)||resolved[5]!=L':'||
       n-4>=_countof(state->workdir))return PRO_TK_BAD_CONTEXT;
    wcscpy_s(state->workdir,_countof(state->workdir),resolved+4);
    unsigned long long key=14695981039346656037ULL;
    for(const wchar_t *p=state->workdir;*p;p++){key^=towlower(*p);key*=1099511628211ULL;}
    wchar_t name[128];
    if(swprintf_s(name,_countof(name),L"%ls-%016llx",state->target,key)<0||
       !pressfit_record_path(state->file,L"压铆任务",name,L".txt"))return PRO_TK_CANT_WRITE;
    state->assembly=assembly;
    return PRO_TK_NO_ERROR;
}

static void pressfit_checkpoint_close(PressfitCheckpoint *state){
    if(state->lock&&state->lock!=INVALID_HANDLE_VALUE)CloseHandle(state->lock);
    state->lock=NULL;
}

static ProError pressfit_checkpoint_lock(ProMdl assembly,PressfitCheckpoint *state){
    memset(state,0,sizeof(*state));
    ProError error=pressfit_record_scope(assembly,state);
    if(error)return error;
    wchar_t guard[MAX_PATH];
    if(swprintf_s(guard,_countof(guard),L"%ls\\.%ls.bwtl-resume.lock",state->workdir,state->target)<0)
        return PRO_TK_OUT_OF_RANGE;
    state->lock=CreateFileW(guard,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,OPEN_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,NULL);

    return state->lock==INVALID_HANDLE_VALUE?PRO_TK_E_IN_USE:PRO_TK_NO_ERROR;
}

static int pressfit_checkpoint_write(const PressfitCheckpoint *state){
    if(!state->lock||state->lock==INVALID_HANDLE_VALUE)return 0;
    wchar_t temp[MAX_PATH];
    FILE *file=ax_study_atomic(state->file,temp);
    if(!file)return 0;
    fwprintf(file,L"BWTL-PRESSFIT-1\n%ls\n%ls\n%ls\n%ls\n%hs %hs %d %d %d %d %d %d %.17g %.17g\n",
        state->workdir,state->target,state->selector,state->source_hash,state->initial,state->last,
        state->shaft,state->seat,state->replace,state->count,state->complete,state->pending,state->mm,state->interference);
    for(int i=0;i<state->count;i++){
        const PressfitCheckpointSite *row=&state->sites[i];
        fwprintf(file,L"%ls %d %d %d %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
            row->panel,row->hole,row->face,row->id,row->disposition,row->diameter,
            row->contact[0],row->contact[1],row->contact[2],row->normal[0],row->normal[1],row->normal[2]);
    }
    return ax_study_commit(file,temp,state->file);
}

static int pressfit_record_line(FILE *file,wchar_t *value,size_t capacity){
    if(!fgetws(value,(int)capacity,file))return 0;
    const size_t size=wcscspn(value,L"\r\n");
    if(!value[size]||!size)return 0;
    value[size]=0;
    return 1;
}

static int pressfit_checkpoint_read(PressfitCheckpoint *state){
    FILE *file=ax_learning_open(state->file);
    if(!file)return 0;
    wchar_t line[1024],workdir[MAX_PATH+3],target[PRO_NAME_SIZE+3],selector[PRO_NAME_SIZE*2+3];
    wchar_t source_hash[68],initial[240],last[240];
    int fields=0,parsed=0;
    int good=pressfit_record_line(file,line,_countof(line))&&!wcscmp(line,L"BWTL-PRESSFIT-1")&&
        pressfit_record_line(file,workdir,_countof(workdir))&&!_wcsicmp(workdir,state->workdir)&&
        pressfit_record_line(file,target,_countof(target))&&!_wcsicmp(target,state->target)&&
        pressfit_record_line(file,selector,_countof(selector))&&wcslen(selector)<_countof(state->selector)&&
        pressfit_record_line(file,source_hash,_countof(source_hash))&&wcslen(source_hash)==64&&
        wcsspn(source_hash,L"0123456789abcdefABCDEF")==64&&
        pressfit_record_line(file,line,_countof(line));
    if(good)fields=swscanf_s(line,L"%239ls %239ls %d %d %d %d %d %d %lf %lf",initial,(unsigned)_countof(initial),
        last,(unsigned)_countof(last),&state->shaft,&state->seat,&state->replace,&state->count,
        &state->complete,&state->pending,&state->mm,&state->interference);
    good=good&&fields==10&&state->shaft>0&&state->seat>0&&(state->replace==0||state->replace==1)&&
        state->count>0&&state->count<=12&&(state->complete==0||state->complete==1)&&
        state->pending>=0&&state->pending<=state->count&&state->mm>0&&isfinite(state->mm)&&
        state->interference>=0&&isfinite(state->interference)&&
        WideCharToMultiByte(CP_UTF8,0,initial,-1,state->initial,_countof(state->initial),NULL,NULL)&&
        WideCharToMultiByte(CP_UTF8,0,last,-1,state->last,_countof(state->last),NULL,NULL);
    for(int i=0;good&&i<state->count;i++){
        PressfitCheckpointSite *row=&state->sites[i];
        wchar_t key[AX_PATH_DEPTH*12+4];
        ProIdTable ids={0};
        int status=0;
        if(!pressfit_record_line(file,line,_countof(line))){good=0;break;}
        fields=swscanf_s(line,L"%203ls %d %d %d %d %lf %lf %lf %lf %lf %lf %lf",
            key,(unsigned)_countof(key),&row->hole,&row->face,&row->id,&status,&row->diameter,
            &row->contact[0],&row->contact[1],&row->contact[2],&row->normal[0],&row->normal[1],&row->normal[2]);
        good=fields==12&&ax_path_parse(key,ids)>0&&row->hole>0&&row->face>0&&row->id>=0&&
            status>=PRESSFIT_PENDING&&status<=PRESSFIT_REPLACED&&isfinite(row->diameter)&&row->diameter>0;
        for(int j=0;good&&j<3;j++)good=isfinite(row->contact[j])&&isfinite(row->normal[j]);
        if(good){
            row->disposition=(PressfitDisposition)status;
            wcscpy_s(row->panel,_countof(row->panel),key);
            parsed++;
        }
    }
    good=good&&parsed==state->count&&!fgetws(line,_countof(line),file)&&!ferror(file);
    fclose(file);
    if(good){
        wcscpy_s(state->selector,_countof(state->selector),selector);
        wcscpy_s(state->source_hash,_countof(state->source_hash),source_hash);
    }
    return good;
}

static int pressfit_checkpoint_archive(const PressfitCheckpoint *state){
    DWORD attributes=GetFileAttributesW(state->file);
    if(attributes==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
    if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return 0;
    PressfitCheckpoint previous=*state;
    if(pressfit_checkpoint_read(&previous)&&previous.complete)return 1;
    wchar_t saved[MAX_PATH];
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return swprintf_s(saved,_countof(saved),L"%ls.%08lx%08lx.%lx.previous",state->file,
        now.dwHighDateTime,now.dwLowDateTime,GetCurrentProcessId())>=0&&CopyFileW(state->file,saved,TRUE);
}


static ProError pressfit_checkpoint_start(ProMdl assembly,const wchar_t *selector,const wchar_t *library,
    const BsoFitting *fitting,const BsoSite *sites,int count,double mm,double interference,int replace,
    PressfitCheckpoint *state,PartText *out){
    if(!selector||!*selector||wcspbrk(selector,L"\r\n")||wcslen(selector)>=_countof(state->selector)||
       !library||!fitting||!sites||count<1||count>12||!isfinite(mm)||mm<=0||
       !isfinite(interference)||interference<0)return PRO_TK_BAD_INPUTS;
    ProError error=pressfit_checkpoint_lock(assembly,state);
    if(error)return error;
    wchar_t digest[65];
    unsigned long long bytes;
    error=ax_study_verstamp(assembly,state->initial);
    if(!error&&!dv_sha256(library,digest,&bytes))error=PRO_TK_CANT_OPEN;
    if(error)goto done;
    strcpy_s(state->last,_countof(state->last),state->initial);
    wcscpy_s(state->selector,_countof(state->selector),selector);
    wcscpy_s(state->source_hash,_countof(state->source_hash),digest);
    state->count=count;
    state->shaft=fitting->shaft;
    state->seat=fitting->seat;
    state->mm=mm;
    state->interference=interference;
    state->replace=replace!=0;
    for(int i=0;i<count;i++){
        PressfitCheckpointSite *row=&state->sites[i];
        const BsoSite *site=&sites[i];
        if(!ax_path_label(site->panel.path.comp_id_table,site->panel.path.table_num,row->panel,_countof(row->panel))){
            error=PRO_TK_BAD_CONTEXT;
            goto done;
        }
        row->hole=site->hole_id;
        row->face=site->face_id;
        row->diameter=2*site->geometry.cylinder.radius*mm;
        memcpy(row->contact,site->geometry.contact,sizeof(row->contact));
        memcpy(row->normal,site->geometry.plane.direction,sizeof(row->normal));
    }
    if(!pressfit_checkpoint_archive(state)||!pressfit_checkpoint_write(state))error=PRO_TK_CANT_WRITE;
done:
    if(error){
        part_text_add(out,L"压铆任务未能登记到本机资料目录（Creo %d），尚未开始装配。\n",error);
        pressfit_checkpoint_close(state);
    }
    return error;
}

static ProError pressfit_checkpoint_load(ProMdl assembly,PressfitCheckpoint *state,PartText *out){
    ProError error=pressfit_checkpoint_lock(assembly,state);
    if(error)return error;
    char stamp[240];
    if(!pressfit_checkpoint_read(state)||state->complete)error=PRO_TK_E_NOT_FOUND;
    if(!error)error=ax_study_verstamp(assembly,stamp);
    if(!error&&strcmp(state->last,stamp)&&strcmp(state->initial,stamp))error=PRO_TK_BAD_CONTEXT;
    if(error){
        part_text_add(out,error==PRO_TK_E_NOT_FOUND?L"当前装配没有可继续的压铆批次。\n":
            L"当前装配版本已变，待续装孔位无法绑定到现有模型。请重新指定孔位。\n");
        pressfit_checkpoint_close(state);
    }
    return error;
}

static ProError pressfit_checkpoint_sites(ProMdl assembly,const wchar_t *library,const BsoFitting *fitting,
    PressfitCheckpoint *state,BsoSite sites[12],PartText *out){
    wchar_t hash[65];
    unsigned long long bytes;
    if(state->assembly!=assembly||fitting->shaft!=state->shaft||fitting->seat!=state->seat||
       !dv_sha256(library,hash,&bytes)||_wcsicmp(hash,state->source_hash)){
        part_text_add(out,L"待续装的库件版本或原生定位参照已经改变。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    for(int i=0;i<state->count;i++){
        const PressfitCheckpointSite *row=&state->sites[i];
        const wchar_t *phase=L"读取记录中的板件实例";
        AxComponent panel={0};
        ProError error=ax_hw_occurrence(assembly,row->panel,state->mm,&panel);
        if(!error)error=fastener_pressfit_target(&panel,row->hole,row->face,state->mm,fitting->model,
            fitting,row->diameter,state->interference,&sites[i],&phase);
        if(!error&&(!ad_near(sites[i].geometry.contact,row->contact,0.001/state->mm)||
            ad_dot(sites[i].geometry.plane.direction,row->normal)<1-1e-7))error=PRO_TK_BAD_CONTEXT;
        if(error){
            part_text_add(out,L"恢复孔 %d 时无法核对%ls（Creo %d）。请重新指定当前孔位。\n",i+1,phase,error);
            return error;
        }
    }
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_checkpoint_mark(PressfitCheckpoint *state,int pending,int complete,
    int index,const PressfitSiteReceipt *row){
    if(!state)return PRO_TK_NO_ERROR;
    ProError error=ax_study_verstamp(state->assembly,state->last);
    if(error)return error;
    state->pending=pending;
    state->complete=complete;
    if(row){
        state->sites[index].id=row->id;
        state->sites[index].disposition=row->disposition;
    }
    return pressfit_checkpoint_write(state)?PRO_TK_NO_ERROR:PRO_TK_CANT_WRITE;
}
