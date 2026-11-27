/* A background window belongs to the already-bound Creo process. Toolkit
   identifies its model and Windows supplies the nonactivating show state. */
typedef struct {
    DWORD pid;
    const wchar_t *name;
    HWND exclude,found;
    int matches;
} LocalBackgroundFrame;

static int local_background_frame_matches(HWND hwnd,DWORD expected_pid,const wchar_t *name){
    DWORD pid=0;
    wchar_t title[512];
    if(!hwnd||!IsWindowVisible(hwnd)||!GetWindowThreadProcessId(hwnd,&pid)||pid!=expected_pid||
       !GetWindowTextW(hwnd,title,_countof(title))||!wcsstr(title,L"Creo Parametric"))return 0;
    const size_t len=wcslen(name);
    return !_wcsnicmp(title,name,len)&&title[len]&&wcschr(L" .(-",title[len])!=NULL;
}

static BOOL CALLBACK local_background_find_frame(HWND hwnd,LPARAM data){
    LocalBackgroundFrame *match=(LocalBackgroundFrame *)data;
    if(hwnd==match->exclude||!local_background_frame_matches(hwnd,match->pid,match->name))return TRUE;
    match->found=hwnd;
    return ++match->matches<2;
}

static HWND local_background_frame(DWORD pid,const wchar_t *name,HWND exclude){
    LocalBackgroundFrame match={.pid=pid,.name=name,.exclude=exclude};
    EnumWindows(local_background_find_frame,(LPARAM)&match);
    return match.matches==1?match.found:NULL;
}

typedef struct {
    DWORD pid;
    HWND foreground;
    wchar_t frame_class[128];
} LocalBackgroundFocus;

static const LocalBackgroundFocus *local_background_focus;

static LRESULT CALLBACK local_background_guard(int code,WPARAM param,LPARAM detail){
    const LocalBackgroundFocus *guard=local_background_focus;
    if(code==HCBT_ACTIVATE&&guard){
        HWND candidate=(HWND)param;
        DWORD pid=0;
        wchar_t window_class[128];
        if(candidate!=guard->foreground&&GetWindowThreadProcessId(candidate,&pid)&&pid==guard->pid&&
           GetClassNameW(candidate,window_class,_countof(window_class))&&
           !wcscmp(window_class,guard->frame_class))return 1;
    }
    return CallNextHookEx(NULL,code,param,detail);
}

static ProError local_control_background_open(const LocalControlContext *current,const wchar_t *payload,PartText *out){
    if(!current->owner||current->window==PRO_VALUE_UNUSED||!current->owner_name[0]){
        part_text_add(out,L"先在该 Creo 会话打开一个模型。Creo 会复用空主窗口，当前无法单独后台打开。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    if(ax_session_activation_waiting()||ax_return_pending.link.root||ax_open_pending.target)
        return PRO_TK_E_IN_USE;
    LocalBackgroundFocus guard={.pid=current->pid,.foreground=GetForegroundWindow()};
    HWND original=local_background_frame(current->pid,current->owner_name,NULL);
    /* A paired ASM and PRT can have the same caption. Disambiguate only with
       the calling Creo thread's active frame, never by enumeration order. */
    if(!original){
        const HWND active=GetAncestor(GetActiveWindow(),GA_ROOTOWNER);
        if(local_background_frame_matches(active,current->pid,current->owner_name)&&!IsIconic(active))
            original=active;
    }
    if(!original||!GetClassNameW(original,guard.frame_class,_countof(guard.frame_class))){
        part_text_add(out,L"无法唯一核对原 Creo 窗口，未打开后台窗口。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    AxSessionSelector target={0};
    ProPath path;
    ProMdl model=NULL,owner=NULL;
    ProError error=local_control_open_exact(current,payload,&target,&model,path,NULL,out);
    if(error)return error;
    if(model==current->owner){
        part_text_add(out,L"%ls.%ls 已在当前 Creo 窗口。当前窗口维持原样。\n",
            target.name,ax_session_model_suffix(target.type));
        return PRO_TK_BAD_CONTEXT;
    }

    int window=PRO_VALUE_UNUSED;
    error=ProMdlWindowGet(model,&window);
    const int create=error==PRO_TK_E_NOT_FOUND;
    if(error&&!create)return error;
    if(GetForegroundWindow()!=guard.foreground)return PRO_TK_BAD_CONTEXT;
    HHOOK hook=SetWindowsHookExW(WH_CBT,local_background_guard,NULL,GetCurrentThreadId());
    if(!hook)return PRO_TK_GENERAL_ERROR;
    local_background_focus=&guard;
    if(create)error=ProObjectwindowMdlnameCreate(target.name,
        target.type==PRO_MDL_ASSEMBLY?PRO_ASSEMBLY:PRO_PART,&window);
    if(!error&&window==current->window)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProWindowMdlGet(window,&owner);
    if(!error&&owner!=model)error=PRO_TK_BAD_CONTEXT;
    if(!error&&create)error=ProMdlDisplay(model);
    int active_window=PRO_VALUE_UNUSED;
    if(!ProWindowCurrentGet(&active_window)&&active_window==window){
        ProMdl active=NULL;
        if(!ProMdlCurrentGet(&active)&&active==current->owner){
            const ProError restored=ProWindowCurrentSet(current->window);
            if(!error)error=restored;
        }
    }
    LocalControlContext readback;
    if(!error)error=local_control_collect(&readback);
    if(!error&&!local_control_same_context(current,&readback))error=PRO_TK_BAD_CONTEXT;

    const HWND frame=error?NULL:local_background_frame(current->pid,target.name,original);
    if(!error&&(!frame||frame==guard.foreground))error=PRO_TK_INCOMPLETE;
    if(!error&&!IsIconic(frame)){
        if(create){
            const HMONITOR monitor=MonitorFromWindow(original,MONITOR_DEFAULTTONEAREST);
            MONITORINFO bounds={.cbSize=sizeof(bounds)};
            if(monitor&&GetMonitorInfoW(monitor,&bounds)){
                const RECT work=bounds.rcWork;
                (void)SetWindowPos(frame,NULL,work.left,work.top,work.right-work.left,work.bottom-work.top,
                    SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);
            }
        }
        ShowWindow(frame,SW_SHOWMINNOACTIVE);
    }
    if(!error&&(!IsWindowVisible(frame)||!IsIconic(frame)||GetForegroundWindow()!=guard.foreground))
        error=PRO_TK_INCOMPLETE;
    if(!error)error=local_control_collect(&readback);
    if(!error&&!local_control_same_context(current,&readback))error=PRO_TK_BAD_CONTEXT;
    local_background_focus=NULL;
    UnhookWindowsHookEx(hook);
    if(error){
        part_text_add(out,L"后台窗口未通过位于独立窗口、已最小化及焦点保持的复核（Creo %d）。"
            L"请以当前会话窗口清单为准。\n",error);
        return error;
    }
    part_text_add(out,L"%ls %ls.%ls，窗口 ID %d，已最小化。原活动模型、窗口及前台焦点仍相同。\n"
        L"windowId=%d\nminimized=1\nactiveWindow=%d\nreused=%d\nsourceFile=%ls\n",
        create?L"已后台打开":L"已复用后台窗口",target.name,ax_session_model_suffix(target.type),window,
        window,current->window,!create,path);
    return PRO_TK_NO_ERROR;
}

static int local_control_background_chat(const wchar_t *text){
    static const wchar_t prefix[]=L"后台打开";
    const size_t length=_countof(prefix)-1;
    if(wcsncmp(text,prefix,length))return 0;
    const wchar_t *path=text+length;
    if(*path&&!iswspace(*path)&&*path!=L'：'&&*path!=L':')return 0;
    if(native_busy||smart_active||ai_process||imitation_pending){
        append(L"请等待当前任务完成。");
        return 1;
    }
    while(iswspace(*path)||*path==L'：'||*path==L':')path++;
    wchar_t result[2200]=L"";
    PartText out={result,_countof(result),0};
    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(!error)error=local_control_background_open(&current,path,&out);
    if(error&&!result[0])part_text_add(&out,L"后台打开未通过会话或窗口核对（Creo %d）。\n",error);
    append_card(L"你",text);
    save_turn(L"用户",text);
    append_card(L"AI",result);
    save_turn(L"助手",result);
    native_prompt_set(L"");
    return 1;
}
