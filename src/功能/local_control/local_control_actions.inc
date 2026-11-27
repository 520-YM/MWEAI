
/* Native command routing for BWTL's per-process local endpoint. The caller is
   the registered Creo command action. Keep the external binding independent
   of the plug-in chat's pending plan, prompt and task state. */
typedef struct {
    ProMdl model;
    ProMdl owner;
    ProMdlType type;
    ProMdlsubtype subtype;
    int window;
    ProMdlName name;
    ProMdlName owner_name;
    ProPath directory;
    char stamp[240];
    char owner_stamp[240];
    DWORD pid;
    ULONGLONG process_start;
} LocalControlContext;

static LocalControlContext local_control_bound;
static ULONGLONG local_control_generation;
static wchar_t local_control_token[72];
static const wchar_t *local_control_com_startup_state(void);

static ProError local_control_collect(LocalControlContext *current){
    FILETIME creation,exit_time,kernel,user;
    *current=(LocalControlContext){0};
    current->pid=GetCurrentProcessId();
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit_time,&kernel,&user))return PRO_TK_GENERAL_ERROR;
    current->process_start=((ULONGLONG)creation.dwHighDateTime<<32)|creation.dwLowDateTime;
    ProError error=ProDirectoryCurrentGet(current->directory);
    if(error)return error;
    current->window=PRO_VALUE_UNUSED;
    ProError window_error=ProWindowCurrentGet(&current->window);
    error=ProMdlCurrentGet(&current->owner);
    if(error==PRO_TK_E_NOT_FOUND||error==PRO_TK_BAD_CONTEXT||(!error&&!current->owner))return 0;
    if(error)return error;
    if(window_error)return window_error;
    if((error=ProMdlMdlnameGet(current->owner,current->owner_name)))return error;
    ProMdl owner_window=NULL;
    if((error=ProWindowMdlGet(current->window,&owner_window)))return error;
    if(owner_window!=current->owner)return PRO_TK_BAD_CONTEXT;
    ProMdlType owner_type;
    if((error=ProMdlTypeGet(current->owner,&owner_type)))return error;
    current->model=current->owner;
    if(owner_type==PRO_MDL_ASSEMBLY){
        ProMdl active=NULL;
        error=ProMdlActiveGet(&active);
        if(!error&&active)current->model=active;
        else if(error!=PRO_TK_E_NOT_FOUND&&error!=PRO_TK_BAD_CONTEXT&&error!=PRO_TK_NO_ERROR)return error;
    }
    if((error=ProMdlMdlnameGet(current->model,current->name))||
       (error=ProMdlTypeGet(current->model,&current->type)))return error;
    if(current->type!=PRO_MDL_PART&&current->type!=PRO_MDL_ASSEMBLY)return 0;
    if((error=ProMdlSubtypeGet(current->model,&current->subtype))||
       (error=ax_study_verstamp(current->model,current->stamp)))return error;
    if(current->model!=current->owner)return ax_study_verstamp(current->owner,current->owner_stamp);
    return 0;
}

static int local_control_same_context(const LocalControlContext *a,const LocalControlContext *b){
    return a->pid==b->pid&&a->process_start==b->process_start&&a->window==b->window&&
        a->owner==b->owner&&a->model==b->model&&a->type==b->type&&a->subtype==b->subtype&&
        !_wcsicmp(a->name,b->name)&&!_wcsicmp(a->owner_name,b->owner_name)&&
        !_wcsicmp(a->directory,b->directory)&&!strcmp(a->stamp,b->stamp)&&!strcmp(a->owner_stamp,b->owner_stamp);
}

static void local_control_invalidate_context(void){
    local_control_token[0]=0;
}

static void bwtl_live_refresh_model(void){
    if(bwtl_live_file==INVALID_HANDLE_VALUE)return;
    LocalControlContext current;
    if(local_control_collect(&current))return;
    BwtlLiveModel fresh={0};
    wcscpy_s(fresh.name,_countof(fresh.name),current.name);
    wcscpy_s(fresh.directory,_countof(fresh.directory),current.directory);
    MultiByteToWideChar(CP_ACP,0,current.stamp,-1,fresh.stamp,_countof(fresh.stamp));
    const wchar_t *type=!current.model?L"NONE":
        current.type==PRO_MDL_PART?L"PRT":current.type==PRO_MDL_ASSEMBLY?L"ASM":L"OTHER";
    wcscpy_s(fresh.type,_countof(fresh.type),type);
    fresh.window=current.window;
    if(fresh.window==bwtl_live_model.window&&!wcscmp(fresh.name,bwtl_live_model.name)&&
       !wcscmp(fresh.type,bwtl_live_model.type)&&!_wcsicmp(fresh.directory,bwtl_live_model.directory)&&
       !wcscmp(fresh.stamp,bwtl_live_model.stamp))return;
    bwtl_live_model=fresh;
    bwtl_live_emit(L"model",0,0,L"");
}

static int bwtl_live_refresh_command(uiCmdCmdId id,uiCmdValue *value,void *data){
    ax_interference_refresh_view();
    bwtl_live_refresh_model();
    return 0;
}

static ProError bwtl_live_window_change(void){
    if(native_backend_window&&(bwtl_live_file!=INVALID_HANDLE_VALUE||ax_interference_overlay.exists))
        PostMessageW(native_backend_window,BWTL_LIVE_REFRESH,0,0);
    return PRO_TK_NO_ERROR;
}

static ProError bwtl_live_directory_change(ProPath path){
    ax_interference_mark_changed(NULL);
    return bwtl_live_window_change();
}

static ProError bwtl_live_regenerate(ProSolid model,ProFeature *feature,ProError status){
    ax_interference_mark_changed((ProMdl)model);
    if((!native_busy&&!smart_active)||ax_interference_overlay.exists)return bwtl_live_window_change();
    return PRO_TK_NO_ERROR;
}

static unsigned bwtl_live_watches;

static void bwtl_live_watch_start(void){
    if(!ProNotificationSet(PRO_WINDOW_CHANGE_POST,(ProFunction)bwtl_live_window_change))bwtl_live_watches|=1;
    if(!ProNotificationSet(PRO_DIRECTORY_CHANGE_POST,(ProFunction)bwtl_live_directory_change))bwtl_live_watches|=2;
    if(!ProNotificationSet(PRO_SOLID_REGEN_POST,(ProFunction)bwtl_live_regenerate))bwtl_live_watches|=4;
    bwtl_live_window_change();
}

static void bwtl_live_watch_stop(void){
    if(bwtl_live_watches&1)ProNotificationUnset(PRO_WINDOW_CHANGE_POST);
    if(bwtl_live_watches&2)ProNotificationUnset(PRO_DIRECTORY_CHANGE_POST);
    if(bwtl_live_watches&4)ProNotificationUnset(PRO_SOLID_REGEN_POST);
    bwtl_live_watches=0;
    if(native_backend_window)KillTimer(native_backend_window,BWTL_LIVE_TIMER);
    ax_interference_close();
    bwtl_live_close();
}

static ProError local_control_status(const LocalControlContext *current,wchar_t *out,size_t capacity){
    if(wcspbrk(current->name,L"\r\n")||wcspbrk(current->owner_name,L"\r\n")||
       wcspbrk(current->directory,L"\r\n")||strpbrk(current->stamp,"\r\n"))return PRO_TK_BAD_CONTEXT;
    if(!local_control_token[0]||!local_control_same_context(&local_control_bound,current)){
        local_control_bound=*current;
        if(swprintf_s(local_control_token,_countof(local_control_token),L"%lu:%llu:%llu",
           current->pid,current->process_start,++local_control_generation)<0)return PRO_TK_GENERAL_ERROR;
    }
    const wchar_t *type=!current->model?L"NONE":
        current->type==PRO_MDL_PART?L"PRT":current->type==PRO_MDL_ASSEMBLY?L"ASM":L"OTHER";
    const wchar_t *subtype=!current->model?L"NONE":current->type==PRO_MDL_PART?
        (current->subtype==PROMDLSTYPE_PART_SHEETMETAL?L"SHEETMETAL":L"SOLID"):
        current->type==PRO_MDL_ASSEMBLY?L"ASSEMBLY":type;
    if(current->stamp[0])bwtl_save_observe(current->model);
    return swprintf_s(out,capacity,
        L"pid=%lu\nprocessStart=%llu\nwindow=%d\nmodel=%ls\ntype=%ls\nsubtype=%ls\nworkDir=%ls\nstamp=%hs\ncontext=%ls\nwindowModel=%ls\nliveJournal=%ls\ncomStartup=%ls",
        current->pid,current->process_start,current->window,current->name,type,subtype,current->directory,
        current->stamp,local_control_token,current->owner_name,bwtl_live_path,local_control_com_startup_state())<0?
        PRO_TK_OUT_OF_RANGE:PRO_TK_NO_ERROR;
}

static int local_control_read_operation(const PartCommand *command){
    static const wchar_t *const reads[]={L"SKETCH_INFO",L"MACHINING_INFO",L"SMT_INFO",
        L"ORG_HOLES",L"ORG_STATUS",L"ASM_TREE",L"ASM_REFS",L"ASM_PLACEMENT"};
    if(command->verb==PART_READ)return 1;
    if(command->verb!=PART_FOUNDATION)return 0;
    if(!wcscmp(command->arg[0],L"ASM_INTERFERENCE"))return
        (command->count<5&&!wcscmp(command->arg[2],L"REPORT"))||ax_interference_review_command(command->arg[2]);
    if(!wcscmp(command->arg[0],L"REFERENCES"))return command->count==2;
    for(int i=0;i<_countof(reads);i++)if(!wcscmp(command->arg[0],reads[i]))return 1;
    return 0;
}

static int local_control_interference_export(const PartCommand *command){
    return command->verb==PART_FOUNDATION&&command->count>=5&&
        !wcscmp(command->arg[0],L"ASM_INTERFERENCE")&&!wcscmp(command->arg[2],L"REPORT");
}

static ProError local_control_note_new_skeleton(const LocalControlContext *current,const wchar_t *requested){
    ProMdl skeleton=NULL;
    ProMdlName name;
    ProMdlType type;
    BwtlSaveIdentity directory;
    BwtlSaveFile file;
    ProError error=ProAsmSkeletonGet((ProAssembly)current->model,&skeleton);
    if(!error)error=bwtl_save_model_info(skeleton,name,&type);
    if(!error&&(type!=PRO_MDL_PART||_wcsicmp(name,requested)))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=bwtl_save_directory(current->directory,&directory);
    if(!error)error=bwtl_save_latest(current->directory,name,type,&file);
    if(error)return error;
    /* Creo can notify retrieval of a temporary file while a new skeleton is built.
       Register only proven absence. Existing files keep normal conflict checks. */
    return file.present?PRO_TK_E_IN_USE:bwtl_save_note(skeleton,name,type,directory,&file,1);
}

static int local_control_write_operation(const PartCommand *command){
    static const wchar_t *const writes[]={L"SKETCH_CREATE",L"SOLID_EXTRUDE",L"HOLE",
        L"SMT_SKETCH",L"SMT_FIRST_WALL",L"SMT_CUT",L"SMT_FLAT4",L"ASM_EDIT_OFFSET",L"ASM_LAYOUT_SKELETON"};
    if(bwtl_traceback_risk(command))return 0;
    if(local_control_interference_export(command))return 1;
    for(int i=2;i<command->count;i++)if(!wcscmp(command->arg[i],L"SELECTED"))return 0;
    if(command->verb==PART_DIMS||command->verb==PART_REGEN||command->verb==PART_EXTRUDE)return 1;
    if(command->verb==PART_FOUNDATION)
        for(int i=0;i<_countof(writes);i++)if(!wcscmp(command->arg[0],writes[i]))return 1;
    return 0;
}

static ProError local_control_error(ProError error,wchar_t *out,size_t capacity,const wchar_t *message){
    wcsncpy_s(out,capacity,message,_TRUNCATE);
    return error;
}

#include "../assembly_naming/drawing_number_batch.inc"

static ProError local_control_dispatch(const char *action,const wchar_t *expected_context,
    const wchar_t *payload,wchar_t *out,size_t capacity);

static int local_control_interference_chat(const wchar_t *text){
    static const struct {const wchar_t *phrase,*command;} intents[]={
        {L"查看可见组件干涉",L"REPORT VISIBLE"},{L"查看整机干涉",L"REPORT ALL"},
        {L"检查整机干涉",L"REPORT ALL"},{L"查看当前组件干涉",L"REUSE"},
        {L"显示全部干涉红框",L"SHOW ALL"},{L"下一处干涉",L"SHOW NEXT"},
        {L"上一处干涉",L"SHOW PREV"},{L"清除干涉红框",L"CLEAR"}
    };
    const wchar_t *operation=NULL;
    const wchar_t *at=wcsstr(text,L" AT ");
    const size_t request=at?(size_t)(at-text):wcslen(text);
    if(at){
        int path[AX_PATH_DEPTH];
        if(!ax_path_parse(at+4,path))return 0;
    }
    wchar_t row_command[40];
    for(int i=0;i<_countof(intents);i++){
        if(wcslen(intents[i].phrase)!=request||wcsncmp(text,intents[i].phrase,request))continue;
        operation=intents[i].command;
        break;
    }
    int row;
    if(!operation&&request>5&&request<=15&&!wcsncmp(text,L"干涉红框 ",5)){
        wchar_t number[11];
        wcsncpy_s(number,_countof(number),text+5,request-5);
        if(part_integer(number,&row)&&row>0){
            swprintf_s(row_command,_countof(row_command),L"SHOW %d",row);
            operation=row_command;
        }
    }
    if(!operation||(at&&!wcsncmp(operation,L"REPORT",6))||(at&&!wcscmp(operation,L"CLEAR")))return 0;
    if(native_busy||smart_active||ai_process||imitation_pending){
        append(L"请等待当前任务完成。");
        return 1;
    }
    wchar_t result[PART_TEXT_MAX]=L"",protocol[512];
    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(!error&&current.model&&current.model==current.owner){
        swprintf_s(protocol,_countof(protocol),L"CREO_OP ASM_INTERFERENCE %ls %ls%ls",
            current.name,operation,at?at:L"");
        error=local_control_dispatch("READ",L"",protocol,result,_countof(result));
    }else if(!error){
        wcscpy_s(result,_countof(result),L"请先打开要检查的 Creo 总装或组件窗口。");
    }
    if(error&&!result[0])swprintf_s(result,_countof(result),L"干涉检查操作未完成（Creo %d）。",error);
    append_card(L"你",text);
    save_turn(L"用户",text);
    append_card(L"AI",result);
    save_turn(L"助手",result);
    native_prompt_set(L"");
    return 1;
}

static int local_control_context_matches(const LocalControlContext *current,const wchar_t *expected){
    return local_control_token[0]&&!wcscmp(expected,local_control_token)&&
        local_control_same_context(current,&local_control_bound);
}

#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
static const wchar_t local_panel_revision_usage[]=
    L"请明确要改的中间两颗及长度，例如「中间两个改成8长，保存到当前项目」。";

static ProError local_panel_revise(int length,int save,wchar_t *out,size_t capacity){
    ProMdl current=NULL;
    ProMdlType type;
    ProPath directory;
    ProError error=ProMdlCurrentGet(&current);
    if(!error)error=ProMdlTypeGet(current,&type);
    if(!error&&(type!=PRO_MDL_ASSEMBLY||default_part_active(current,-1,NULL)))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProDirectoryCurrentGet(directory);
    if(!error&&!storage_directory_on_drive(directory,L'E'))error=PRO_TK_CANT_WRITE;
    if(error)return local_control_error(error,out,capacity,L"请激活要修改的已核对六孔总装，并使用原来的 E 盘项目目录。");
    PartText receipt={out,capacity,0};
    error=panel_multi_revise(current,length,save,&receipt);
    if(error&&!out[0])swprintf_s(out,capacity,L"中间两个的修改未完成（Creo %d）。请核对实际装配和执行明细。",error);
    return error;
}

static ProError local_grouped_pressfit(const LocalControlContext *current,const GroupPressfitPlan *plan,
    const GroupPressfitRevision *revision,wchar_t *out,size_t capacity){
    if(!current->model||current->type!=PRO_MDL_ASSEMBLY||current->model!=current->owner)
        return local_control_error(PRO_TK_BAD_CONTEXT,out,capacity,L"请激活包含目标安装板的 Creo 装配窗口。");
    if(!storage_directory_on_drive(current->directory,L'E'))
        return local_control_error(PRO_TK_CANT_WRITE,out,capacity,L"请先把 Creo 工作目录设到原来的 E 盘项目目录。");

    PartText receipt={out,capacity,0};
    ProError error;
    if(plan)error=grouped_pressfit_install(current->model,plan,&receipt);
    else if(revision)error=grouped_pressfit_revise(current->model,revision,&receipt);
    else error=grouped_pressfit_list(current->model,&receipt);
    if(plan||revision)local_control_invalidate_context();
    if(error&&!out[0])swprintf_s(out,capacity,L"压铆孔组操作未完成（Creo %d）。请检查当前装配及执行明细。",error);
    return error;
}

static int grouped_pressfit_chat(const wchar_t *text){
    const int listing=grouped_pressfit_list_parse(text);
    GroupPressfitPlan plan;
    const int installing=listing?0:grouped_pressfit_install_parse(text,&plan);
    GroupPressfitRevision revision;
    const int revising=(listing||installing)?0:grouped_pressfit_revision_parse(text,&revision);
    if(!listing&&!installing&&!revising)return 0;
    if(native_busy||smart_active||ai_process||panel_flow_busy()){
        append(L"请等待当前任务结束后再操作压铆孔组。");
        return 1;
    }
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    if(installing<0||revising<0){
        const wchar_t *usage=installing<0?grouped_pressfit_usage:grouped_pressfit_revision_usage;
        append_card(L"AI",usage);
        save_turn(L"助手",usage);
        return 1;
    }

    smart_begin(text);
    smart_local_plan=1;
    wchar_t result[16000]=L"";
    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(error)local_control_error(error,result,_countof(result),L"无法核对当前 Creo 窗口、模型版本或工作目录。");
    else error=local_grouped_pressfit(&current,installing?&plan:NULL,revising?&revision:NULL,result,_countof(result));
    trace("BWTL pressfit hole groups chat receipt",error);
    smart_record_tool(result);
    smart_reply(result);
    return 1;
}

static ProError local_pw_execute(const LocalControlContext *current,const PwCommand *command,wchar_t *result,size_t capacity){
    PartText receipt={result,capacity,0};
    if(command->action==PW_HELP)return pw_execute(current->model,command,&receipt);
    if(!current->model||current->type!=PRO_MDL_ASSEMBLY||current->model!=current->owner)
        return local_control_error(PRO_TK_BAD_CONTEXT,result,capacity,L"请激活包含目标板件的 Creo 装配窗口。");
    if(!storage_directory_on_drive(current->directory,L'E'))
        return local_control_error(PRO_TK_BAD_CONTEXT,result,capacity,L"请把 Creo 工作目录设到 E 盘项目。");
    ProError error=pw_execute(current->model,command,&receipt);
    local_control_invalidate_context();
    if(error&&!result[0])swprintf_s(result,capacity,L"压铆操作未完成（Creo %d）。可发「压铆分组帮助」核对用法。",error);
    return error;
}

static int pw_chat(const wchar_t *text){
    const PwCommand command=pw_command(text);
    if(!command.action)return 0;
    if(native_busy||smart_active||ai_process||panel_flow_busy()){
        append(L"请等待当前任务结束后再操作压铆孔组。");
        return 1;
    }
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    smart_begin(text);
    smart_local_plan=1;
    wchar_t result[16000]=L"";
    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(error)local_control_error(error,result,_countof(result),L"无法核对当前 Creo 装配。");
    else error=local_pw_execute(&current,&command,result,_countof(result));
    trace("BWTL pressfit workspace receipt",error);
    if(!error&&command.action==PW_PDF&&native_attachment_count()==1&&native_window)
        PostMessageW(native_window,CHAT_ATTACHMENTS_SENT,0,0);
    smart_record_tool(result);
    smart_reply(result);
    return 1;
}

/* Ordinary attachment turns stay with chat analysis. The explicitly
   requested PDF hole table comparison must keep the attached file local. */
static int pressfit_workspace_pdf_chat(const wchar_t *text){
    const PwCommand command=pw_command(text);
    return command.action==PW_PDF&&pw_chat(text);
}

static int panel_flow_chat(const wchar_t *text){
    if(pw_chat(text))return 1;
    if(grouped_pressfit_chat(text))return 1;
    int revised_length,save;
    const int revision=panel_flow_revision_parse(text,&revised_length,&save);
    PanelPressfitPlan plan={0};
    const int parsed=revision?0:panel_flow_parse(text,&plan);
    if(!parsed&&!revision)return 0;
    if(native_busy||smart_active||ai_process||panel_flow_busy()){
        append(L"请等待当前任务结束后再开始钣金装配。");
        return 1;
    }
    wchar_t detail[1200]=L"";
    if(parsed>0&&!panel_flow_begin(text,&plan,detail,_countof(detail)))return 1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    if(revision==1){
        smart_begin(text);
        smart_local_plan=1;
        native_prompt_set(L"");
        wchar_t result[11000]=L"";
        (void)local_panel_revise(revised_length,save,result,_countof(result));
        smart_record_tool(result);
        smart_reply(result);
        return 1;
    }
    const wchar_t *reply=detail[0]?detail:revision?local_panel_revision_usage:panel_flow_usage;
    append_card(L"AI",reply);
    save_turn(L"助手",reply);
    native_prompt_set(L"");
    return 1;
}

static ProError local_control_start_workflow(int panel,const wchar_t *expected,const wchar_t *payload,
    wchar_t *out,size_t capacity){
    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(error||!local_control_context_matches(&current,expected))
        return local_control_error(error?error:PRO_TK_BAD_CONTEXT,out,capacity,
            L"当前 Creo 会话状态已变化。请重新读取状态后核对新建目标。");
    if(!storage_directory_on_drive(current.directory,L'E'))
        return local_control_error(PRO_TK_CANT_WRITE,out,capacity,L"请先把 Creo 工作目录设到 E 盘。");
    if(panel){
        PanelPressfitPlan plan={0};
        if(panel_flow_parse(payload,&plan)!=1)
            return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,panel_flow_usage);
        local_control_invalidate_context();
        return panel_flow_begin(payload,&plan,out,capacity);
    }
    local_control_invalidate_context();
    return local_control_pair_begin(payload,out,capacity);
}
#endif

#include "local_control_model.inc"
#include "../reference_design/reference_workflow.inc"

static ProError local_control_dispatch(const char *action,const wchar_t *expected_context,
    const wchar_t *payload,wchar_t *out,size_t capacity){
    if(!out||!capacity)return PRO_TK_BAD_INPUTS;
    out[0]=0;
    if(!action||!expected_context||!payload)return PRO_TK_BAD_INPUTS;
    const int status=!strcmp(action,"STATUS"),read=!strcmp(action,"READ"),
        exec=!strcmp(action,"EXEC"),save=!strcmp(action,"SAVE");
    const int open_model=!strcmp(action,"OPEN_MODEL"),open_background=!strcmp(action,"OPEN_BACKGROUND"),
        switch_model=!strcmp(action,"SWITCH_MODEL"),
        rebuild_model=!strcmp(action,"REBUILD_MODEL"),list_models=!strcmp(action,"LIST_MODELS"),
        handoff=!strcmp(action,"HANDOFF"),reference_design=!strcmp(action,"REFERENCE_DESIGN");
    const int model_action=open_model||open_background||switch_model||rebuild_model||list_models;
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
    const int pressfit=!strcmp(action,"PRESSFIT"),revision=!strcmp(action,"REV_PANEL"),
        group_list=!strcmp(action,"GROUP_HOLES"),group_install=!strcmp(action,"GROUP_FIT"),
        group_revise=!strcmp(action,"REV_GROUP"),workspace_pressfit=!strcmp(action,"FIT_WORK");
#else
    const int pressfit=0,revision=0,group_list=0,group_install=0,group_revise=0,workspace_pressfit=0;
#endif
    const int grouped=group_list||group_install||group_revise||workspace_pressfit;
    if(!(status||read||exec||save||pressfit||revision||grouped||model_action||handoff||reference_design)||(status&&*expected_context)||
       ((status||save||group_list||rebuild_model||list_models)&&*payload)||
       ((exec||pressfit||revision||group_install||group_revise||workspace_pressfit||open_model||open_background||switch_model||handoff||reference_design)&&!*payload))
        return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,L"本机控制指令类型或参数无效。");

    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(error)return local_control_error(error,out,capacity,L"无法核对当前 Creo 窗口、模型版本或工作目录。");
    if(status)return local_control_status(&current,out,capacity);

    if((!read||*expected_context)&&!local_control_context_matches(&current,expected_context))
        return local_control_error(PRO_TK_BAD_CONTEXT,out,capacity,L"当前会话、模型、窗口、版本或工作目录已变化。请重新读取状态并核对目标。");
    if((read||exec)&&!wcsncmp(payload,L"DRAWING_NUMBERS ",16))
        return drawing_number_dispatch(&current,payload+16,exec,out,capacity);
    if((handoff||model_action||reference_design)&&!storage_directory_on_drive(current.directory,L'E'))
        return local_control_error(PRO_TK_CANT_WRITE,out,capacity,L"请先把 Creo 工作目录设到 E 盘。");
    if(reference_design)return reference_dispatch(&current,payload,out,capacity);
    if(open_background){
        PartText result={out,capacity,0};
        error=local_control_background_open(&current,payload,&result);
        if(error&&!out[0])part_text_add(&result,L"后台打开未通过会话或窗口核对（Creo %d）。\n",error);
        return error;
    }
    if(handoff){
        if(bwtl_live_file==INVALID_HANDLE_VALUE)
            return local_control_error(PRO_TK_CANT_WRITE,out,capacity,L"实时任务文件不可用。");
        bwtl_live_refresh_model();
        bwtl_live_begin(1);
        wcsncpy_s(bwtl_live_goal,_countof(bwtl_live_goal),payload,_TRUNCATE);
        bwtl_live_emit(L"request",0,0,payload);
        if(bwtl_live_file==INVALID_HANDLE_VALUE)
            return local_control_error(PRO_TK_CANT_WRITE,out,capacity,L"实时任务写入失败。");
        append_card(L"Codex 接续",payload);
        save_turn(L"Codex 接续",payload);
        swprintf_s(out,capacity,L"已把当前会话的建模任务交接到 BWTL，任务编号 %llu。",bwtl_live_task);
        return PRO_TK_NO_ERROR;
    }
    if(open_model||switch_model||list_models){
        ProError result=local_control_model_action(action,&current,payload,out,capacity);
        if(result&&!out[0])swprintf_s(out,capacity,L"Creo 原生窗口操作返回 %d。",result);
        return result;
    }
    if(!current.model||!current.stamp[0])return local_control_error(PRO_TK_E_NOT_FOUND,out,capacity,L"当前没有已激活的零件或装配。");
    if(!read&&current.model!=current.owner)
        return local_control_error(PRO_TK_BAD_CONTEXT,out,capacity,L"当前目标是装配内激活的子件。请先独立打开子件，再核对该窗口操作。");
    if(!read&&!storage_directory_on_drive(current.directory,L'E'))
        return local_control_error(PRO_TK_CANT_WRITE,out,capacity,L"请先把 Creo 工作目录设到 E 盘。");

    if(rebuild_model){
        bwtl_live_refresh_model();
        bwtl_live_begin(0);
        bwtl_live_emit(L"progress",0,0,L"开始读取源特征并逐步重建。");
        ProError result=local_control_model_action(action,&current,payload,out,capacity);
        if(result&&!out[0])swprintf_s(out,capacity,L"Creo 原生特征重建返回 %d。",result);
        local_control_invalidate_context();
        return result;
    }

    const int summary=read&&!wcscmp(payload,L"SUMMARY");
    const int model=read&&(!*payload||!wcscmp(payload,L"MODEL")||summary);
    PartCommand command;
    if(!model&&!save&&!pressfit&&!revision&&!grouped){
        if(wcsnlen(payload,4096)>=4096||wcsncmp(payload,L"CREO_OP ",8)||wcspbrk(payload,L"\r\n")||
           part_command_parse(payload,&command)!=1)
            return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,L"请提交一条受支持的原生 Creo 操作协议。");
        if(_wcsicmp(command.arg[1],current.name))
            return local_control_error(PRO_TK_BAD_CONTEXT,out,capacity,L"协议中的模型名与当前已绑定的 Creo 模型不同。");
        if((read&&!local_control_read_operation(&command))||(exec&&!local_control_write_operation(&command)))
            return local_control_error(PRO_TK_UNSUPPORTED,out,capacity,L"本机入口尚未开放这项原生操作，模型未修改。");
    }

    if(read)bwtl_save_observe(current.model);
    const int create_skeleton=exec&&command.verb==PART_FOUNDATION&&!wcscmp(command.arg[0],L"ASM_LAYOUT_SKELETON");
    if(exec&&(create_skeleton||local_control_interference_export(&command))){
        if(current.type!=PRO_MDL_ASSEMBLY)
            return local_control_error(PRO_TK_INVALID_TYPE,out,capacity,L"请激活本次操作的目标装配。");
        bwtl_save_observe(current.model);
        error=ax_execute(current.model,&command,out,capacity);
        if(!error&&create_skeleton){
            const ProError disk=local_control_note_new_skeleton(&current,command.arg[2]);
            if(disk){
                PartText receipt={out,capacity,(int)wcslen(out)};
                part_text_add(&receipt,L"骨架留在会话中。磁盘基线暂未核实（Creo %d），保存仍执行冲突检查。\n",disk);
            }
        }
        local_control_invalidate_context();
        if(error&&!out[0])swprintf_s(out,capacity,L"Creo 原生装配操作未完成（Creo %d）。",error);
        if(out[0]){
            append_card(L"Codex 本机回执",out);
            if(store_ready)save_turn(L"Codex 本机回执",out);
        }
        return error;
    }
    if(read&&!model&&command.verb==PART_FOUNDATION&&ax_kind(command.arg[0])==AX_INTERFERENCE){
        LocalControlContext verified;
        error=local_control_collect(&verified);
        if(!error&&(!local_control_same_context(&current,&verified)||current.model!=current.owner))
            error=PRO_TK_BAD_CONTEXT;
        PartText result={out,capacity,0};
        if(!error)error=!wcscmp(command.arg[2],L"REPORT")?
            ax_interference_report(current.model,&command,&result):ax_interference_review(current.model,&command,&result);
        if(error&&!out[0])part_text_add(&result,L"干涉检查或窗口定位未完成（Creo %d）。\n",error);
        return error;
    }
    if(read&&!model&&current.type==PRO_MDL_ASSEMBLY&&command.verb==PART_FOUNDATION&&
       ax_read_diagnostic(ax_kind(command.arg[0]))){
        LocalControlContext verified;
        error=local_control_collect(&verified);
        if(!error&&!local_control_same_context(&current,&verified))error=PRO_TK_BAD_CONTEXT;
        if(!error&&current.model!=current.owner)error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ax_execute(current.model,&command,out,capacity);
        if(error&&!out[0])swprintf_s(out,capacity,L"装配读取未完成（Creo %d）。",error);
        return error;
    }
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
    if(workspace_pressfit){
        const PwCommand command=pw_command(payload);
        if(!command.action)return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,pw_help);
        error=local_pw_execute(&current,&command,out,capacity);
    }else if(grouped){
        GroupPressfitPlan plan;
        GroupPressfitRevision update;
        if(group_install&&grouped_pressfit_install_parse(payload,&plan)!=1)
            return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,grouped_pressfit_usage);
        if(group_revise&&grouped_pressfit_revision_parse(payload,&update)!=1)
            return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,grouped_pressfit_revision_usage);
        error=local_grouped_pressfit(&current,group_install?&plan:NULL,group_revise?&update:NULL,out,capacity);
    }else if(revision){
        int length,write;
        if(panel_flow_revision_parse(payload,&length,&write)!=1)
            return local_control_error(PRO_TK_BAD_INPUTS,out,capacity,local_panel_revision_usage);
        error=local_panel_revise(length,write,out,capacity);
        local_control_invalidate_context();
    }else if(pressfit){
        if(current.type!=PRO_MDL_ASSEMBLY)
            return local_control_error(PRO_TK_INVALID_TYPE,out,capacity,L"请激活需要装配压铆件的 Creo 装配窗口。");
        error=local_control_pressfit(current.model,payload,out,capacity);
        local_control_invalidate_context();
    }else
#endif
    if(save){
        error=bwtl_save_guarded(current.model,out,capacity);
        local_control_invalidate_context();
    }else{
        PartSnapshot *snapshot=calloc(1,sizeof(*snapshot));
        if(!snapshot)return PRO_TK_OUT_OF_MEMORY;
        error=summary?part_snapshot_take_basic(current.model,snapshot):part_snapshot_take(current.model,snapshot);
        if(!error){
            LocalControlContext verified;
            error=local_control_collect(&verified);
            if(!error&&!local_control_same_context(&current,&verified))error=PRO_TK_BAD_CONTEXT;
        }
        if(!error&&model){
            if(summary)part_snapshot_format_basic(snapshot,out,capacity);
            else part_snapshot_format_selected(snapshot,out,capacity,15u);
        }else if(!error){
            error=part_execute(&command,snapshot,out,capacity);
            if(exec)local_control_invalidate_context();
        }
        part_snapshot_clear(snapshot);
        free(snapshot);
    }
    if(error&&!out[0])swprintf_s(out,capacity,L"Creo 原生操作返回 %d。请重新核对当前模型和工作目录。",error);
    if(!read&&!group_list&&out[0]){
        append_card(L"Codex 本机回执",out);
        if(store_ready)save_turn(L"Codex 本机回执",out);
    }
    return error;
}
