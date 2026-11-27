/* Local navigation and the matching narrowly scoped resident tool actions. */
static int assembly_take(const wchar_t **input,const wchar_t *word);

enum { SESSION_WINDOW_LIST=1, SESSION_WINDOW_ACTIVATE, SESSION_WINDOW_TREE };

static int session_window_wants_tree;
static const wchar_t session_window_command[]=L"~ Command \x0060" L"BWTLSessionWindowConfirm\x0060;";

static int session_window_tree_phrase(const wchar_t *text){
    return !wcscmp(text,L"读取装配树")||!wcscmp(text,L"读取当前装配树")||
        !wcscmp(text,L"查看装配树")||!wcscmp(text,L"查看当前装配树");
}

static int session_window_selector(const wchar_t *text,size_t length,ProMdlType implied,AxSessionSelector *out){
    *out=(AxSessionSelector){.type=implied};
    while(length&&iswspace(*text)){text++;length--;}
    while(length&&(iswspace(text[length-1])||wcschr(L"。！!",text[length-1])))length--;
    if(length>1&&((text[0]==L'「'&&text[length-1]==L'」')||
       (text[0]==L'“'&&text[length-1]==L'”')||(text[0]==L'"'&&text[length-1]==L'"'))){
        text++;length-=2;
    }
    if(length>=2&&!wcsncmp(text,L"窗口",2)){
        text+=2;length-=2;
        while(length&&iswspace(*text)){text++;length--;}
        if(length>=2&&!_wcsnicmp(text,L"ID",2)){text+=2;length-=2;}
        while(length&&(iswspace(*text)||wcschr(L":：#",*text))){text++;length--;}
        if(!length||length>9)return 0;
        for(size_t i=0;i<length;i++){
            if(text[i]<L'0'||text[i]>L'9')return 0;
            out->window_id=out->window_id*10+(int)(text[i]-L'0');
        }
        out->has_window_id=1;
        return 1;
    }
    if(length>4){
        ProMdlType actual=PRO_MDL_UNUSED;
        const wchar_t *extension=text+length-4;
        if(!_wcsnicmp(extension,L".ASM",4))actual=PRO_MDL_ASSEMBLY;
        else if(!_wcsnicmp(extension,L".PRT",4))actual=PRO_MDL_PART;
        if(actual!=PRO_MDL_UNUSED){
            if(implied!=PRO_MDL_UNUSED&&implied!=actual)return 0;
            out->type=actual;
            length-=4;
        }
    }
    return length&&length<_countof(out->name)&&
        !wcsncpy_s(out->name,_countof(out->name),text,length)&&part_name(out->name);
}

static int session_window_request_mode(const wchar_t *text,AxSessionSelector *selector,int *tree,int authorize){
    static const wchar_t *const lists[]={L"读取会话模型与窗口清单",L"读取会话模型和窗口清单",L"查看会话模型与窗口清单",
        L"查看Creo会话模型",L"查看会话模型",L"读取会话模型",L"列出会话模型",L"列出已打开的模型",L"查看已打开的模型",
        L"现在打开了哪些模型",L"读取模型与窗口清单",L"查看模型窗口清单",L"模型与窗口清单"};
    static const struct {const wchar_t *prefix;ProMdlType type;int needs_suffix;} verbs[]={
        {L"激活指定模型窗口",PRO_MDL_UNUSED,0},{L"激活模型窗口",PRO_MDL_UNUSED,0},
        {L"打开已加载模型",PRO_MDL_UNUSED,0},{L"打开已加载的模型",PRO_MDL_UNUSED,0},
        {L"切换到模型窗口",PRO_MDL_UNUSED,0},{L"激活",PRO_MDL_UNUSED,1},
        {L"回到装配窗口",PRO_MDL_ASSEMBLY,0},{L"回到总装配",PRO_MDL_ASSEMBLY,0},
        {L"回到总装",PRO_MDL_ASSEMBLY,0},{L"回总装",PRO_MDL_ASSEMBLY,0},{L"回到装配体",PRO_MDL_ASSEMBLY,0},
        {L"回到装配",PRO_MDL_ASSEMBLY,0},{L"返回装配窗口",PRO_MDL_ASSEMBLY,0},
        {L"返回装配体",PRO_MDL_ASSEMBLY,0},{L"返回装配",PRO_MDL_ASSEMBLY,0},
        {L"切回总装",PRO_MDL_ASSEMBLY,0},{L"切回装配窗口",PRO_MDL_ASSEMBLY,0},
        {L"切回装配体",PRO_MDL_ASSEMBLY,0},{L"切回装配",PRO_MDL_ASSEMBLY,0},
        {L"切换到装配体",PRO_MDL_ASSEMBLY,0},{L"切换到装配",PRO_MDL_ASSEMBLY,0},
        {L"切回到",PRO_MDL_UNUSED,1},{L"切换到",PRO_MDL_UNUSED,1},
        {L"切回",PRO_MDL_UNUSED,1},{L"回到",PRO_MDL_UNUSED,1},
        {L"返回到",PRO_MDL_UNUSED,1},{L"返回",PRO_MDL_UNUSED,1}
    };
    const wchar_t *p=model_request_action_start(text);
    if(*p==L'先')p=model_request_action_start(p+1);
    wchar_t clean[6001];
    if(wcsncpy_s(clean,_countof(clean),p,_TRUNCATE))return 0;
    size_t length=wcslen(clean);
    while(length&&(iswspace(clean[length-1])||wcschr(L"。！!",clean[length-1])))clean[--length]=0;
    for(int i=0;i<_countof(lists);i++)if(!wcscmp(clean,lists[i]))return SESSION_WINDOW_LIST;
    if(session_window_tree_phrase(clean))return SESSION_WINDOW_TREE;
    if(wcspbrk(clean,L"?？\r\n"))return 0;
    *tree=0;
    for(int i=0;i<_countof(verbs);i++){
        const wchar_t *tail=clean;
        if(!assembly_take(&tail,verbs[i].prefix))continue;
        while(*tail&&(iswspace(*tail)||wcschr(L"：:",*tail)))tail++;
        if(!*tail)return 0; /* Keep existing unnamed parent navigation. */
        if(!wcsncmp(tail,L"原装配",3)||!wcsncmp(tail,L"父装配",3))return 0;
        const wchar_t *follow=wcsstr(tail,L"然后");
        if(!follow)follow=wcsstr(tail,L"接着");
        size_t n=follow?(size_t)(follow-tail):wcslen(tail);
        while(n&&(iswspace(tail[n-1])||wcschr(L"，,。；;",tail[n-1])))n--;
        if(n&&tail[n-1]==L'吧')n--;
        if(follow&&!authorize&&!session_window_tree_phrase(model_request_action_start(follow+2)))return 0;
        if(!session_window_selector(tail,n,verbs[i].type,selector))return i<5?-SESSION_WINDOW_ACTIVATE:0;
        if(verbs[i].needs_suffix&&selector->type==PRO_MDL_UNUSED&&!selector->has_window_id)return 0;
        *tree=follow!=NULL;
        return SESSION_WINDOW_ACTIVATE;
    }
    return 0;
}

static int session_window_request(const wchar_t *text,AxSessionSelector *selector,int *tree){
    return session_window_request_mode(text,selector,tree,0);
}

static void session_window_prompt(FILE *prompt){
    fwprintf(prompt,L"\n会话导航：用户要求读取当前 Creo 内的零件和装配窗口时，用 CREO_SESSION_MODELS，回执包含型号、.PRT/.ASM、窗口 ID 和活动状态。"
        L"用户明确指定会话内型号并要求切换时，用 CREO_ACTIVATE_WINDOW 实际型号.ASM 或 实际型号.PRT。"
        L"已加载但未显示的对象可在独立窗口打开；源窗口和未保存模型始终保留。等原生切换和任务绑定回执，再按新快照操作。"
        L"移回本轮此前打开子件的父装配时仍用 CREO_RETURN_PARENT。读取活动装配树可用 CREO_ASM_TREE。\n");
}

static ProError session_window_tree(PartText *out){
    ProMdl active=NULL,owner=NULL;
    ProMdlType type;
    ProMdlName name;
    int window;
    ProError error=ProMdlCurrentGet(&active);
    if(!error)error=ProMdlTypeGet(active,&type);
    if(!error&&type!=PRO_MDL_ASSEMBLY)error=PRO_TK_INVALID_TYPE;
    if(!error)error=ProWindowCurrentGet(&window);
    if(!error)error=ProWindowMdlGet(window,&owner);
    if(!error&&owner!=active)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlMdlnameGet(active,name);
    if(error){part_text_add(out,L"当前活动窗口没有可读取的 Creo 装配树。\n");return error;}
    return ax_tree(active,name,out);
}

static ProError session_window_report(int action,wchar_t *receipt,size_t capacity){
    PartText out={receipt,capacity,0};
    if(action==SESSION_WINDOW_TREE)return session_window_tree(&out);
    wchar_t *list=NULL;
    ProError error=ax_session_inventory(&list);
    if(error){part_text_add(&out,L"读取 Creo 会话模型与窗口清单失败（Creo %d）。",error);return error;}
    if(wcslen(list)<capacity)wcscpy_s(receipt,capacity,list);
    else{
        wchar_t path[MAX_PATH];
        FILE *file=NULL;
        if(storage_output_directory(store_session_root)&&
           swprintf_s(path,_countof(path),L"%ls\\Creo会话模型与窗口-%llu.txt",store_session_root,GetTickCount64())>0)
            file=_wfopen(path,L"wx, ccs=UTF-8");
        if(file){
            int written=fwprintf(file,L"%ls",list)>=0;
            if(fclose(file)||!written)error=PRO_TK_CANT_WRITE;
        }else error=PRO_TK_CANT_WRITE;
        if(error)part_text_add(&out,L"模型清单长度超出聊天框，完整清单暂未写入数据盘（Creo %d）。",error);
        else part_text_add(&out,L"完整清单已写入 %ls\n\n%.*ls\n",path,4000,list);
    }
    free(list);
    return error;
}

static void session_window_cancel(void){
    session_window_wants_tree=0;
    ax_session_activation_clear(&ax_session_pending);
}

static int session_window_confirm(uiCmdCmdId id,uiCmdValue *value,void *data){
    if(!ax_session_activation_waiting())return 0;
    if(!smart_active){session_window_cancel();return 0;}
    static wchar_t receipt[PART_TEXT_MAX];
    receipt[0]=0;
    PartText out={receipt,_countof(receipt),0};
    const int continuing=resident_tool_running();
    ProMdl active=NULL;
    ProPath directory;
    ProError error=ax_session_activate_readback(&active,directory,&out);
    if(!error&&!smart_navigation_binding(active,directory)){
        error=PRO_TK_BAD_CONTEXT;
        smart_target_model=NULL;
        part_text_add(&out,L"\n活动窗口已切换。Creo 未确认目标的新快照，本轮暂停后续操作。");
    }
    if(!error){
        smart_parent_link=(AxReturnLink){0};
        part_text_add(&out,L" 已刷新操作目标，保留原窗口和未保存的改动。\n");
        trace("Verified exact Creo model window and rebound task",0);
        if(session_window_wants_tree){part_text_add(&out,L"\n");error=session_window_tree(&out);}
    }
    session_window_wants_tree=0;
    if(continuing)smart_step(NULL,L"激活会话模型窗口",error,receipt,0,0,error&&!smart_binding_valid());
    else{smart_record_tool(receipt);smart_reply(receipt);}
    return 0;
}

static ProError session_window_start(AxSessionSelector *selector,int tree,PartText *out){
    ProError error=ax_session_activate(selector,out);
    if(!error){
        session_window_wants_tree=tree;
        error=ProMacroLoad((wchar_t *)session_window_command);
        if(error){
            session_window_cancel();
            part_text_add(out,L"Creo 已收到窗口切换请求，活动窗口的回读未能调度（Creo %d）。",error);
        }
    }
    if(error&&!out->text[0])part_text_add(out,L"Creo 未能核对目标模型或窗口（Creo %d）。",error);
    return error;
}

static int session_window_chat(const wchar_t *text){
    AxSessionSelector selector;
    int tree=0;
    int action=session_window_request(text,&selector,&tree);
    if(!action)return 0;
    if(native_busy||smart_active||ai_process){append(L"请等当前操作结束后再读取或切换模型窗口。");return 1;}
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    static wchar_t receipt[PART_TEXT_MAX];
    receipt[0]=0;
    if(action==SESSION_WINDOW_ACTIVATE){
        PartText out={receipt,_countof(receipt),0};
        if(!session_window_start(&selector,tree,&out))return 1;
    }else if(action>0)session_window_report(action,receipt,_countof(receipt));
    else wcscpy_s(receipt,_countof(receipt),L"请指定会话里已加载的名称及 .ASM／.PRT，或窗口 ID，例如「激活模型窗口 FRAME.ASM」。");
    smart_record_tool(receipt);
    smart_reply(receipt);
    return 1;
}

/* A resident plan may change windows only to the target explicitly named by
   the current user. The existing parent-link protocol covers task-created children. */
static int session_window_user_authorized(const AxSessionSelector *wanted){
    AxSessionSelector request;
    int tree;
    if(session_window_request_mode(smart_latest_request,&request,&tree,1)!=SESSION_WINDOW_ACTIVATE)return 0;
    if(request.has_window_id)return wanted->has_window_id&&wanted->window_id==request.window_id;
    return !wanted->has_window_id&&!_wcsicmp(wanted->name,request.name)&&wanted->type==request.type;
}

static int session_window_resident(const wchar_t *operation){
    int action=!wcscmp(operation,L"CREO_SESSION_MODELS")?SESSION_WINDOW_LIST:
        !wcscmp(operation,L"CREO_ASM_TREE")?SESSION_WINDOW_TREE:0;
    static wchar_t report[PART_TEXT_MAX];
    report[0]=0;
    if(action){
        ProError error=session_window_report(action,report,_countof(report));
        resident_tool_result(error,report);
        return 1;
    }
    const wchar_t *target=operation;
    if(!assembly_take(&target,L"CREO_ACTIVATE_WINDOW "))return 0;
    AxSessionSelector selector;
    if(smart_readonly||smart_intent==SMART_INTENT_PENDING||smart_intent==SMART_INTENT_CHAT||smart_intent==SMART_INTENT_CLARIFY||
       !session_window_selector(target,wcslen(target),PRO_MDL_UNUSED,&selector)||
       !session_window_user_authorized(&selector)){
        resident_tool_result(PRO_TK_BAD_CONTEXT,L"请在本轮明确指定要激活的会话模型或窗口 ID。");
        return 1;
    }
    if(smart_steps&&smart_target_model&&!smart_binding_valid()){
        resident_tool_result(PRO_TK_BAD_CONTEXT,L"切换前无法核对本轮已绑定的源模型，已保留已完成的步骤。");
        return 1;
    }
    PartText out={report,_countof(report),0};
    ProError error=session_window_start(&selector,0,&out);
    if(error)resident_tool_result(error,report);
    return 1;
}
