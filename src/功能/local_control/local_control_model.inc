/* Model actions for BWTL's bound, per-process local command endpoint. */
static int local_control_model_selector(const wchar_t *leaf,int revision_allowed,AxSessionSelector *selector){
    const wchar_t *suffix=wcschr(leaf,L'.');
    if(!suffix||suffix==leaf||wcspbrk(leaf,L"\\/:*?\"<>|\r\n\t"))return 0;
    if(!_wcsnicmp(suffix,L".prt",4))selector->type=PRO_MDL_PART;
    else if(!_wcsnicmp(suffix,L".asm",4))selector->type=PRO_MDL_ASSEMBLY;
    else return 0;
    if(suffix[4]){
        if(!revision_allowed||suffix[4]!=L'.'||!suffix[5])return 0;
        int revision=0;
        const wchar_t *digits=suffix+5;
        for(const wchar_t *p=digits;*p;p++)if(*p<L'0'||*p>L'9')return 0;
        if(!part_integer(digits,&revision)||revision<1)return 0;
    }
    const size_t length=(size_t)(suffix-leaf);
    return length<_countof(selector->name)&&!wcsncpy_s(selector->name,_countof(selector->name),leaf,length);
}

static ProError local_control_open_path(const wchar_t *directory,const wchar_t *request,
                                       ProPath resolved,AxSessionSelector *selector){
    wchar_t root[MAX_PATH],input[MAX_PATH],full[MAX_PATH];
    if(!or_real_path(directory,1,root)||_wcsnicmp(root,L"E:\\",3))return PRO_TK_BAD_CONTEXT;
    if(!*request||wcspbrk(request,L"*?\"<>|\r\n\t"))return PRO_TK_BAD_INPUTS;
    if(!_wcsnicmp(request,L"E:\\",3)){
        if(wcschr(request+3,L':')||wcslen(request)>=_countof(input))return PRO_TK_BAD_INPUTS;
        wcscpy_s(input,_countof(input),request);
    }else{
        if(wcspbrk(request,L"\\/:")||swprintf_s(input,_countof(input),L"%ls\\%ls",root,request)<0)
            return PRO_TK_BAD_INPUTS;
    }
    const DWORD length=GetFullPathNameW(input,_countof(full),full,NULL);
    if(!length||length>=_countof(full)||!or_below(full,root))return PRO_TK_NO_PERMISSION;
    const wchar_t *leaf=wcsrchr(full,L'\\');
    if(!leaf||!local_control_model_selector(leaf+1,1,selector))return PRO_TK_BAD_INPUTS;

    const DWORD attributes=GetFileAttributesW(full);
    if(attributes==INVALID_FILE_ATTRIBUTES){
        const DWORD reason=GetLastError();
        return reason==ERROR_FILE_NOT_FOUND||reason==ERROR_PATH_NOT_FOUND?PRO_TK_E_NOT_FOUND:PRO_TK_CANT_OPEN;
    }
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return PRO_TK_CANT_OPEN;
    wchar_t canonical[MAX_PATH];
    if(!or_real_path(full,0,canonical)||!or_below(canonical,root)||
       _wcsicmp(full,canonical)||!dv_file_nonempty(canonical))return PRO_TK_NO_PERMISSION;
    return wcscpy_s(resolved,PRO_PATH_SIZE,canonical)?PRO_TK_OUT_OF_RANGE:PRO_TK_NO_ERROR;
}

static ProError local_control_open_exact(const LocalControlContext *current,const wchar_t *payload,
    AxSessionSelector *selector,ProMdl *model,ProPath path,int *was_loaded,PartText *out){
    ProError error=local_control_open_path(current->directory,payload,path,selector);
    if(error){
        part_text_add(out,L"未打开模型。请指定当前 E 盘工作目录下已存在的 .prt/.asm 文件，带上磁盘版本后缀（如果有）。Creo %d。\n",error);
        return error;
    }

    const ProMdlfileType file_type=selector->type==PRO_MDL_ASSEMBLY?PRO_MDLFILE_ASSEMBLY:PRO_MDLFILE_PART;
    *model=NULL;
    error=ProMdlnameInit(selector->name,file_type,model);
    if(was_loaded)*was_loaded=!error&&*model;
    if(error==PRO_TK_E_NOT_FOUND)error=ProMdlFiletypeLoad(path,file_type,PRO_B_FALSE,model);
    if(error||!*model){
        part_text_add(out,L"Creo 未能加载指定的磁盘模型 %ls（Creo %d）。\n",path,error);
        return error?error:PRO_TK_E_NOT_FOUND;
    }
    /* Loading a name already in session must not replace or select another revision. */
    ProPath origin;
    wchar_t existing[MAX_PATH];
    if(ProMdlOriginGet(*model,origin)||!or_real_path(origin,0,existing)||_wcsicmp(path,existing)){
        part_text_add(out,L"Creo 会话中已有同名、同类型模型，来源与指定路径不一致，未切换窗口。"
                          L"要继续使用已加载模型，可指定其名称和类型切换。\n");
        return PRO_TK_E_IN_USE;
    }
    LocalControlContext rechecked;
    error=local_control_collect(&rechecked);
    if(!error&&!local_control_same_context(current,&rechecked))error=PRO_TK_BAD_CONTEXT;
    if(error){
        part_text_add(out,L"加载后原活动窗口、模型版本或工作目录已变化，未切换窗口。\n");
        return error;
    }
    return PRO_TK_NO_ERROR;
}

static ProError local_control_model_open(const LocalControlContext *current,const wchar_t *payload,PartText *out){
    AxSessionSelector selector={0};
    ProMdl model=NULL;
    ProPath path;
    int was_loaded=0;
    ProError error=local_control_open_exact(current,payload,&selector,&model,path,&was_loaded,out);
    if(!error)error=ax_session_activate(&selector,out);
    if(!error)part_text_add(out,was_loaded?L"复用会话中已加载的模型，来源：%ls。\n":L"从磁盘加载：%ls。\n",path);
    return error;
}

#include "local_control_background.inc"

static ProError local_control_model_switch(const wchar_t *payload,PartText *out){
    AxSessionSelector selector={.type=PRO_MDL_UNUSED};
    if(!wcsncmp(payload,L"WINDOW ",7)){
        const wchar_t *value=payload+7;
        for(const wchar_t *p=value;*p;p++)if(*p<L'0'||*p>L'9')return PRO_TK_BAD_INPUTS;
        if(!*value||!part_integer(value,&selector.window_id)||selector.window_id<0)return PRO_TK_BAD_INPUTS;
        selector.has_window_id=1;
    }else if(!local_control_model_selector(payload,0,&selector)){
        part_text_add(out,L"请指定 NAME.PRT、NAME.ASM 或 WINDOW 1023 格式的会话窗口 ID。\n");
        return PRO_TK_BAD_INPUTS;
    }
    return ax_session_activate(&selector,out);
}

static ProError local_control_model_rebuild(const LocalControlContext *current,PartText *out){
    if(current->type!=PRO_MDL_PART||current->model!=current->owner)
        return local_control_error(PRO_TK_INVALID_TYPE,out->text,out->size,L"请先独立打开要按特征树重建的实体或钣金零件。");
    ImitationSourceInfo info={0};
    ProError error=imitation_read_source(current->model,&info);
    if(error){
        part_text_add(out,L"源零件的特征或尺寸预检未完成（Creo %d）。\n",error);
        return error;
    }
    if(info.imports)
        return local_control_error(PRO_TK_UNSUPPORTED,out->text,out->size,L"源件含导入特征，无法按原生历史逐步重建。");
    if(!storage_prepare_output())
        return local_control_error(PRO_TK_CANT_WRITE,out->text,out->size,storage_output_message);

    imitation_warn_wall(current->model);
    ProPath delivered;
    ImitationOutcome outcome={0};
    error=imitation_run_mode_impl(current->model,delivered,1,0,&outcome,out);
    if(!error&&delivered[0]){
        imitation_record(delivered,&imitation_last,current->model,&outcome,out);
    }else if(!error){
        part_text_add(out,L"未取得已通过验证的新零件文件路径。\n");
        error=PRO_TK_INCOMPLETE;
    }
    if(error&&outcome.stage)part_text_add(out,L"停止阶段：%ls。\n",outcome.stage);
    return error;
}

static ProError local_control_model_action(const char *action,const LocalControlContext *current,
                                         const wchar_t *payload,wchar_t *out,size_t capacity){
    PartText result={out,capacity,0};
    if(!strcmp(action,"OPEN_MODEL"))return local_control_model_open(current,payload,&result);
    if(!strcmp(action,"SWITCH_MODEL"))return local_control_model_switch(payload,&result);
    if(!strcmp(action,"REBUILD_MODEL"))return local_control_model_rebuild(current,&result);
    if(!strcmp(action,"LIST_MODELS")){
        wchar_t *inventory=NULL;
        const ProError error=ax_session_inventory(&inventory);
        if(error)return error;
        const errno_t copied=wcsncpy_s(out,capacity,inventory,_TRUNCATE);
        free(inventory);
        return copied?PRO_TK_OUT_OF_RANGE:PRO_TK_NO_ERROR;
    }
    return PRO_TK_BAD_INPUTS;
}

/* ProWindowActivate takes effect after the initiating command returns to Creo. */
static ProError local_control_model_activation_reply(wchar_t *out,size_t capacity){
    ProMdl activated=NULL;
    ProPath directory;
    PartText result={out,capacity,0};
    return ax_session_activate_readback(&activated,directory,&result);
}
