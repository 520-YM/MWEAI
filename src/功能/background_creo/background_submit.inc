
#define BACKGROUND_REQUEST (WM_APP+184)
#define BACKGROUND_TIMER 1847
static wchar_t background_job[MAX_PATH],background_reference[MAX_PATH];
static HANDLE background_process;
static const wchar_t *background_stage(const wchar_t *state){
    if(!wcscmp(state,L"copy"))return L"正在复制磁盘工程";
    if(!wcscmp(state,L"build"))return L"后台 Creo 正在建模和再生";
    if(!wcscmp(state,L"verify"))return L"正在用另一独立 Creo 会话重开验收";
    if(!wcscmp(state,L"done"))return L"49 寸缩窄版已保存并通过重开验收";
    if(!wcscmp(state,L"failed"))return L"后台任务未通过，详见任务目录";
    return L"后台任务正在启动";
}
static int background_read(const wchar_t *leaf,wchar_t *value,size_t capacity){
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,_countof(path),L"%ls\\%ls",background_job,leaf)<0)return 0;
    FILE *file=_wfopen(path,L"rt, ccs=UNICODE");
    if(!file)return 0;
    int ok=fgetws(value,(int)capacity,file)!=NULL;
    fclose(file);
    if(ok)value[wcscspn(value,L"\r\n")]=0;
    return ok;
}
static void background_show(void){
    if(!background_job[0]){append_card(L"后台 Creo",L"此会话还没有提交后台任务。");return;}
    wchar_t state[32]=L"",result[MAX_PATH]=L"",error[768]=L"",view[1700];
    background_read(L"status.txt",state,_countof(state));
    if(!wcscmp(state,L"done")){
        background_read(L"result.txt",result,_countof(result));
        swprintf_s(view,_countof(view),L"%ls。\n%ls\n门板、后箱和底座宽度均为 830 mm。可在「后台 Creo → 打开已验收结果」中独立打开。",background_stage(state),result);
    }else{
        background_read(L"error.txt",error,_countof(error));
        swprintf_s(view,_countof(view),L"%ls。\n任务目录  %ls\n%ls",background_stage(state),background_job,error);
    }
    append_card(L"后台 Creo",view);
}
static int background_launch(int open){
    wchar_t script[MAX_PATH],shell[MAX_PATH],command[1100];
    if(swprintf_s(script,_countof(script),L"%ls\\scripts\\Invoke-MWEAIBackgroundCreo.ps1",runtime_root)<0||
       GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES)return 0;
    UINT length=GetSystemDirectoryW(shell,_countof(shell));
    if(!length||length>=_countof(shell)||wcscat_s(shell,_countof(shell),L"\\WindowsPowerShell\\v1.0\\powershell.exe"))return 0;
    if(swprintf_s(command,_countof(command),L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -File \"%ls\" -JobDirectory \"%ls\"%ls",
        shell,script,background_job,open?L" -OpenResult":L"")<0)return 0;
    STARTUPINFOW startup={.cb=sizeof(startup)};
    PROCESS_INFORMATION child={0};
    if(!CreateProcessW(shell,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,background_job,&startup,&child))return 0;
    CloseHandle(child.hThread);
    if(open)CloseHandle(child.hProcess);
    else background_process=child.hProcess;
    return 1;
}
static void background_poll(void){
    if(!background_process||WaitForSingleObject(background_process,0)!=WAIT_OBJECT_0)return;
    CloseHandle(background_process);
    background_process=NULL;
    KillTimer(native_backend_window,BACKGROUND_TIMER);
    if(GetFileAttributesW(background_job)!=INVALID_FILE_ATTRIBUTES){
        wchar_t state[32]=L"";
        if(!background_read(L"status.txt",state,_countof(state))||
           (wcscmp(state,L"done")&&wcscmp(state,L"failed")))
            append_card(L"后台 Creo",L"启动程序已退出，未收到验收回执。请在任务目录查看日志。");
        background_show();
    }
}
static ProError background_submit(ProMdl model,const wchar_t *reference){
    ProMdlName name;
    ProMdlType type;
    ProPath origin=L"";
    ProError error;
    if(background_process||!model||!reference||!reference[0])return PRO_TK_BAD_CONTEXT;
    if(!storage_output_directory(reference)||!storage_output_directory(L"E:\\project"))return PRO_TK_BAD_CONTEXT;
    error=ProMdlMdlnameGet(model,name);
    if(!error)error=ProMdlTypeGet(model,&type);
    if(error)return error;
    if(type!=PRO_MDL_ASSEMBLY||_wcsicmp(name,L"M26G2000000"))return PRO_TK_BAD_INPUTS;
    AxPackModel entry={.model=model,.type=type};
    wcscpy_s(entry.name,_countof(entry.name),name);
    error=ax_pack_saved_path(&entry,origin);
    if(error)return error;
    if(!storage_output_directory(origin))return PRO_TK_BAD_CONTEXT;
    const ProMdlType model_types[]={PRO_MDL_ASSEMBLY,PRO_MDL_PART};
    for(int k=0;k<_countof(model_types);k++){
        ProMdl *components=NULL;int count=0;
        if(ProSessionMdlList(model_types[k],&components,&count))continue;
        for(int i=0;i<count;i++){
            ProBoolean changed=PRO_B_FALSE;
            ProPath candidate;
            if(!ProMdlOriginGet(components[i],candidate)&&!ProMdlModificationVerify(components[i],&changed)&&changed){
                wchar_t *a=wcsrchr(origin,L'\\'),*b=wcsrchr(candidate,L'\\');
                if(a&&b&&a-origin==b-candidate&&!_wcsnicmp(origin,candidate,(size_t)(a-origin))){
                    error=PRO_TK_BAD_CONTEXT;
                    break;
                }
            }
        }
        if(components)ProArrayFree((ProArray *)&components);
        if(error)return error;
    }

    GUID guid;wchar_t printed[39],id[33];int n=0;
    if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(&guid,printed,_countof(printed)))return PRO_TK_GENERAL_ERROR;
    for(int i=1;printed[i]!=L'}'&&printed[i]&&n<32;i++)if(printed[i]!=L'-')id[n++]=towlower(printed[i]);
    id[n]=0;
    if(n!=32||swprintf_s(background_job,_countof(background_job),L"E:\\project\\MWEAI-49-%ls",id)<0||
       !CreateDirectoryW(background_job,NULL))return PRO_TK_CANT_WRITE;
    wchar_t request[MAX_PATH];
    if(swprintf_s(request,_countof(request),L"%ls\\request.txt",background_job)<0)return PRO_TK_CANT_WRITE;
    FILE *file=_wfopen(request,L"wt, ccs=UNICODE");
    if(!file)return PRO_TK_CANT_WRITE;
    int written=fwprintf(file,L"%ls\n%ls\n",origin,reference);
    if(fclose(file)||written<0||!background_launch(0))return PRO_TK_CANT_WRITE;
    SetTimer(native_backend_window,BACKGROUND_TIMER,750,NULL);
    return PRO_TK_NO_ERROR;
}
static int background_start_command(uiCmdCmdId command,uiCmdValue *value,void *data){
    ProMdl model=NULL;
    ProError error=ProMdlCurrentGet(&model);
    if(!error)error=background_submit(model,background_reference);
    background_reference[0]=0;
    if(error){
        wchar_t message[300];
        swprintf_s(message,_countof(message),L"未启动任务（Creo %d）。请确认当前是 E 盘上已保存的 M26G2000000 立式总装，项目内没有未保存修改，且选中了 49inch 参考零件。",error);
        append_card(L"后台 Creo",message);
    }else background_show();
    return 0;
}
static void background_request(UINT action,wchar_t *reference){
    if(action==1){
        if(background_process||background_reference[0]){
            append_card(L"后台 Creo",L"此会话已有后台任务，完成后可以提交下一项。");
        }else if(reference){
            wcscpy_s(background_reference,_countof(background_reference),reference);
            if(ProMacroLoad(L"~ Command \x0060MWEAIBackgroundStart\x0060;")){
                background_reference[0]=0;
                append_card(L"后台 Creo",L"Creo 没有接受后台提交命令。");
            }
        }
    }else if(action==2)background_show();
    else if(action==3){
        wchar_t state[32]=L"";
        if(!background_job[0]||!background_read(L"status.txt",state,_countof(state))||wcscmp(state,L"done"))
            append_card(L"后台 Creo",L"此会话还没有已验收的后台结果。");
        else if(!background_launch(1))
            append_card(L"后台 Creo",L"无法启动结果查看会话，请从任务目录打开装配。");
    }
    free(reference);
}


