/* Keep native task identities separate from the transcript and active turn.
   The process owns its handles. The E-drive journal is an audit record only. */
typedef struct {
    ProMdl model;
    ProMdlfileType type;
    ProMdlName name;
    ProPath origin;
} ChatTaskModel;

typedef struct {
    ChatTaskModel model;
    wchar_t goal[900],action[300],receipt[1400];
    ProError error;
    int unsafe;
} ChatTaskResult;

typedef struct {
    ChatTaskModel assembly,part;
    int occurrence;
} ChatTaskPair;

typedef struct {
    ProPath directory;
    ULONGLONG used;
    ChatTaskModel project_assembly;
    ChatTaskPair pair;
    ChatTaskResult last,success;
} ChatTaskProject;

static ChatTaskProject chat_task_projects[8];
static ULONGLONG chat_task_serial;

static void chat_task_context_reset(void){
    memset(chat_task_projects,0,sizeof(chat_task_projects));
}

static int chat_task_project_path(ProPath result){
    ProPath path;
    const wchar_t drive=storage_output_drive();
    if(ProDirectoryCurrentGet(path)||(drive&&towupper(path[0])!=drive))return 0;
    HANDLE directory=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(directory==INVALID_HANDLE_VALUE)return 0;
    wchar_t resolved[PRO_PATH_SIZE+4];
    DWORD length=GetFinalPathNameByHandleW(directory,resolved,_countof(resolved),VOLUME_NAME_DOS);
    CloseHandle(directory);
    return length>=7&&length<_countof(resolved)&&!wcsncmp(resolved,L"\\\\?\\",4)&&
        (!drive||towupper(resolved[4])==drive)&&resolved[5]==L':'&&resolved[6]==L'\\'&&!wcscpy_s(result,PRO_PATH_SIZE,resolved+4);
}

static ChatTaskProject *chat_task_current_project(int create){
    ProPath directory;
    if(!chat_task_project_path(directory))return NULL;
    ChatTaskProject *oldest=chat_task_projects;
    for(size_t i=0;i<_countof(chat_task_projects);i++){
        ChatTaskProject *entry=chat_task_projects+i;
        if(!_wcsicmp(entry->directory,directory)){
            entry->used=++chat_task_serial;
            return entry;
        }
        if(entry->used<oldest->used)oldest=entry;
    }
    if(!create)return NULL;
    memset(oldest,0,sizeof(*oldest));
    wcscpy_s(oldest->directory,PRO_PATH_SIZE,directory);
    oldest->used=++chat_task_serial;
    return oldest;
}

static ProError chat_task_model_capture(ProMdl model,ChatTaskModel *result){
    if(!model)return PRO_TK_E_NOT_FOUND;
    ProMdlType type;
    ProError error=ProMdlTypeGet(model,&type);
    if(error)return error;
    if(type!=PRO_MDL_PART&&type!=PRO_MDL_ASSEMBLY)return PRO_TK_INVALID_TYPE;
    ChatTaskModel checked={.model=model,.type=type==PRO_MDL_ASSEMBLY?PRO_MDLFILE_ASSEMBLY:PRO_MDLFILE_PART};
    error=ProMdlMdlnameGet(model,checked.name);
    if(!error)error=ProMdlOriginGet(model,checked.origin);
    if(!error)*result=checked;
    return error;
}

static ProError chat_task_model_verify(const ChatTaskModel *id){
    if(!id->model)return PRO_TK_E_NOT_FOUND;
    ProMdl loaded=NULL;
    ProPath origin;
    ProError error=ProMdlnameInit((wchar_t *)id->name,id->type,&loaded);
    if(!error&&loaded!=id->model)return PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlOriginGet(loaded,origin);
    if(!error&&_wcsicmp(origin,id->origin)){
        ProPath directory,saved;
        const ProMdlType type=id->type==PRO_MDLFILE_ASSEMBLY?PRO_MDL_ASSEMBLY:PRO_MDL_PART;
        if(!chat_task_project_path(directory)||ax_native_source(directory,id->name,type,saved)||_wcsicmp(saved,origin))
            return PRO_TK_BAD_CONTEXT;
    }
    return error;
}

static const wchar_t *chat_task_extension(const ChatTaskModel *model){
    return model->type==PRO_MDLFILE_ASSEMBLY?L".ASM":L".PRT";
}

static void chat_task_journal(const wchar_t *kind,const ChatTaskProject *project,const ChatTaskResult *result,
    const ChatTaskModel *assembly,int occurrence){
    if(!store_ready||!store_session_root[0]||!storage_output_directory(store_session_root))return;
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,MAX_PATH,L"%ls\\任务接续.txt",store_session_root)<0)return;
    HANDLE folder=CreateFileW(store_session_root,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(folder==INVALID_HANDLE_VALUE)return;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE file=INVALID_HANDLE_VALUE;
    if(modeling_rule_file_matches(folder,store_session_root,1,&info))
        file=CreateFileW(path,GENERIC_WRITE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,NULL,OPEN_ALWAYS,
            FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file!=INVALID_HANDLE_VALUE&&modeling_rule_file_matches(file,path,0,&info)){
        SYSTEMTIME time;
        GetLocalTime(&time);
        wchar_t row[4300];
        int length=swprintf_s(row,_countof(row),
            L"\r\n[BWTL 任务接续 v1] %04u-%02u-%02u %02u:%02u:%02u｜%ls\r\n"
            L"项目目录 %ls\r\n已核对模型 %ls%ls\r\n装配 %ls｜组件编号 %d\r\n"
            L"操作 %ls｜Creo %d｜状态存疑 %d\r\n本轮目标 %ls\r\n回执 %ls\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,kind,project->directory,
            result->model.name,chat_task_extension(&result->model),assembly?assembly->name:L"—",occurrence,
            result->action,result->error,result->unsafe,result->goal,result->receipt);
        char bytes[sizeof(row)*2];
        int count=length>0?WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,row,length,bytes,sizeof(bytes),NULL,NULL):0;
        LARGE_INTEGER offset={0};
        DWORD written=0;
        if(!count||!SetFilePointerEx(file,offset,NULL,FILE_END)||
           !WriteFile(file,bytes,(DWORD)count,&written,NULL)||written!=(DWORD)count||!FlushFileBuffers(file))
            trace("BWTL task receipt journal write failed",GetLastError());
    }
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    CloseHandle(folder);
}

static void chat_task_text(wchar_t *target,size_t capacity,const wchar_t *text){
    wcsncpy_s(target,capacity,text,_TRUNCATE);
    const size_t length=wcslen(target);
    if(length&&target[length-1]>=0xd800&&target[length-1]<=0xdbff)target[length-1]=0;
}

static void chat_task_result_make(ChatTaskResult *result,const ChatTaskModel *model,const wchar_t *action,
    ProError error,const wchar_t *receipt,int unsafe){
    result->model=*model;
    chat_task_text(result->goal,_countof(result->goal),smart_goal);
    chat_task_text(result->action,_countof(result->action),action);
    chat_task_text(result->receipt,_countof(result->receipt),receipt);
    result->error=error;
    result->unsafe=unsafe;
}

static void chat_task_record_target_goal(ProMdl model,const wchar_t *action,ProError error,
    const wchar_t *receipt,int unsafe,const wchar_t *goal){
    ChatTaskModel checked={0};
    if(chat_task_model_capture(model,&checked))return;
    ChatTaskProject *project=chat_task_current_project(1);
    if(!project)return;
    chat_task_result_make(&project->last,&checked,action,error,receipt,unsafe);
    if(goal&&goal[0])chat_task_text(project->last.goal,_countof(project->last.goal),goal);
    if(!error&&!unsafe)project->success=project->last;
    chat_task_journal(L"原生操作",project,&project->last,NULL,0);
}

static void chat_task_record_target(ProMdl model,const wchar_t *action,ProError error,const wchar_t *receipt,int unsafe){
    chat_task_record_target_goal(model,action,error,receipt,unsafe,NULL);
}

static ProError chat_task_verify_occurrence(const ChatTaskPair *pair){
    ProAsmcomp component;
    ProFeatStatus state;
    ProMdl actual=NULL;
    ProError error=ax_component_feature(pair->assembly.model,pair->occurrence,&component);
    if(!error)error=ProFeatureStatusGet(&component,&state);
    if(!error&&state!=PRO_FEAT_ACTIVE)return PRO_TK_BAD_CONTEXT;
    if(!error)error=ProAsmcompMdlGet(&component,&actual);
    return !error&&actual!=pair->part.model?PRO_TK_BAD_CONTEXT:error;
}

static void chat_task_record_pair(ProMdl parent,ProMdl part,int occurrence,const wchar_t *receipt){
    ChatTaskPair pair={.occurrence=occurrence};
    if(occurrence<=0||chat_task_model_capture(parent,&pair.assembly)||chat_task_model_capture(part,&pair.part)||
       pair.assembly.type!=PRO_MDLFILE_ASSEMBLY||pair.part.type!=PRO_MDLFILE_PART||
       _wcsicmp(pair.assembly.name,pair.part.name)||chat_task_verify_occurrence(&pair))return;
    ChatTaskProject *project=chat_task_current_project(1);
    if(!project)return;
    project->pair=pair;
    chat_task_result_make(&project->last,&pair.part,L"已核对同号装配和子零件",PRO_TK_NO_ERROR,receipt,0);
    project->success=project->last;
    chat_task_journal(L"同号 ASM/PRT",project,&project->last,&pair.assembly,occurrence);
}

static ProError chat_task_resolve_pair(ProMdl *parent,ProMdl *part,int *occurrence){
    *parent=*part=NULL;
    *occurrence=0;
    ChatTaskProject *project=chat_task_current_project(0);
    if(!project||!project->pair.occurrence)return PRO_TK_E_NOT_FOUND;
    const ChatTaskPair *pair=&project->pair;
    ProError error=chat_task_model_verify(&pair->assembly);
    if(!error)error=chat_task_model_verify(&pair->part);
    if(!error)error=chat_task_verify_occurrence(pair);
    if(!error){
        *parent=pair->assembly.model;
        *part=pair->part.model;
        *occurrence=pair->occurrence;
    }
    return error;
}

static int chat_task_company_name(const wchar_t *name){
    if(wcslen(name)!=14||towupper(name[0])!=L'M'||towupper(name[3])!=L'A'||
       wcsncmp(name+6,L"00000",5)||towupper(name[11])!=L'V'||(name[4]==L'0'&&name[5]==L'0'))return 0;
    const int digit_positions[]={1,2,4,5,12,13};
    for(size_t i=0;i<_countof(digit_positions);i++){
        const wchar_t value=name[digit_positions[i]];
        if(value<L'0'||value>L'9')return 0;
    }
    return 1;
}

static void chat_task_record_company_assembly(ProMdl assembly,const wchar_t *receipt){
    ChatTaskModel verified={0};
    if(chat_task_model_capture(assembly,&verified)||verified.type!=PRO_MDLFILE_ASSEMBLY||
       !chat_task_company_name(verified.name))return;
    ChatTaskProject *project=chat_task_current_project(1);
    if(!project)return;
    project->project_assembly=verified;
    chat_task_result_make(&project->last,&verified,L"创建项目总装",PRO_TK_NO_ERROR,receipt,0);
    project->success=project->last;
    chat_task_journal(L"项目总装图号",project,&project->last,NULL,0);
}

static ProError chat_task_company_assembly(ProMdl *model){
    *model=NULL;
    ChatTaskProject *project=chat_task_current_project(0);
    if(!project)return PRO_TK_E_NOT_FOUND;
    ProError error=chat_task_model_verify(&project->project_assembly);
    if(!error)*model=project->project_assembly.model;
    return error;
}

static ProError chat_task_last_followup(ProMdl *model){
    *model=NULL;
    ChatTaskProject *project=chat_task_current_project(0);
    if(!project)return PRO_TK_E_NOT_FOUND;
    if(project->last.unsafe)return PRO_TK_BAD_CONTEXT;
    ProError error=chat_task_model_verify(&project->last.model);
    if(!error)*model=project->last.model.model;
    return error;
}

static int chat_task_followup_pending(void){
    ChatTaskProject *project=chat_task_current_project(0);
    return project&&project->last.error&&!project->last.unsafe&&!chat_task_model_verify(&project->last.model);
}

static void chat_prompt_task_target(FILE *out){
    ChatTaskProject *project=chat_task_current_project(0);
    if(!project)return;
    fwprintf(out,L"\n<本会话同一项目的原生任务状态，仅供核对>\n");
    const ChatTaskModel *anchor=&project->project_assembly;
    if(!chat_task_model_verify(anchor))fwprintf(out,L"已核对项目总装：%ls.ASM。\n",anchor->name);
    const ChatTaskPair *pair=&project->pair;
    if(pair->occurrence&&!chat_task_model_verify(&pair->assembly)&&!chat_task_model_verify(&pair->part)&&
       !chat_task_verify_occurrence(pair))
        fwprintf(out,L"曾创建并回读同号件：%ls.ASM 与 %ls.PRT，实体组件 ID:%d。\n",
            pair->assembly.name,pair->part.name,pair->occurrence);
    const ChatTaskResult *last=&project->last;
    if(!chat_task_model_verify(&last->model)){
        fwprintf(out,L"最近原生操作目标 %ls%ls。\n操作 %ls，Creo %d。%ls\n"
            L"当时用户目标：%ls\n实际回执：%ls\n",
            last->model.name,chat_task_extension(&last->model),last->action,last->error,
            last->unsafe?L"该轮曾在状态存疑时停止。":L"",last->goal,last->receipt);
        const ChatTaskResult *success=&project->success;
        if((last->error||last->unsafe)&&last->model.model==success->model.model&&!chat_task_model_verify(&success->model))
            fwprintf(out,L"该目标最近核对成功的步骤：%ls。\n实际回执：%.700ls\n",success->action,success->receipt);
    }
    fwprintf(out,L"用户明确提到刚才的对象时才参考以上记录。操作前按当前请求重新核对目标及参照。\n"
        L"</本会话同一项目的原生任务状态>\n");
}
