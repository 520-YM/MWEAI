/* Explicit references to the preceding task use its verified Creo identity. */
static int chat_followup_info(const wchar_t *text){
    static const wchar_t *const questions[]={
        L"刚才建的图号是多少",L"刚才建的图号是什么",L"刚才创建的图号是什么",
        L"刚才创建的图号是多少",L"刚才那个图号是多少",L"刚才那个图号是什么",
        L"刚刚建的图号是什么",L"我刚才建的图号是多少",L"我刚才建的图号是什么",
        L"上一个零件的图号是什么",L"刚才那对零件和装配的图号是什么"
    };
    wchar_t compact[96];
    size_t used=0;
    const wchar_t *original=text;
    text=model_request_action_start(text);
    for(;*text;text++){
        if(iswspace(*text)||wcschr(L"。！？?!，,",*text))continue;
        if(used+1==_countof(compact))return 0;
        compact[used++]=*text;
    }
    compact[used]=0;
    int matched=0;
    for(size_t i=0;i<_countof(questions);i++)
        if(!wcscmp(compact,questions[i])){matched=1;break;}
    if(!matched)return 0;
    if(smart_active||ai_process||native_busy){append(L"请等当前 Creo 操作完成，我会核对实际创建的图号。");return 1;}
    smart_begin(original);
    append_card(L"你",original);
    save_turn(L"用户",original);
    native_prompt_set(L"");
    wchar_t answer[640]=L"";
    ProMdl last=NULL,parent=NULL,part=NULL;
    int occurrence=0;
    ProMdlName name,sheet;
    ProMdlType type;
    ProError error=chat_task_last_followup(&last);
    if(!error&&!chat_task_resolve_pair(&parent,&part,&occurrence)&&(last==parent||last==part)&&
       !ProMdlMdlnameGet(parent,name)&&!ProMdlMdlnameGet(part,sheet)){
        swprintf_s(answer,_countof(answer),L"刚才核对的装配是 %ls.ASM，钣金件是 %ls.PRT（组件 ID:%d）。",name,sheet,occurrence);
    }else if(!error&&!ProMdlMdlnameGet(last,name)&&!ProMdlTypeGet(last,&type)){
        swprintf_s(answer,_countof(answer),L"刚才实际核对的模型是 %ls.%ls。",name,type==PRO_MDL_ASSEMBLY?L"ASM":L"PRT");
    }else wcscpy_s(answer,_countof(answer),L"当前项目没有可核对的最近建模结果。若模型仍打开，可以按当前窗口读实际图号。");
    smart_reply(answer);
    return 1;
}

static int chat_followup_execute(const wchar_t *text){
    static const wchar_t *const objects[]={
        L"刚才那个",L"刚刚那个",L"刚才那块",L"刚才那对",L"刚才创建的",
        L"刚才建的",L"上一个零件",L"上一个装配",L"上次那个",L"继续刚才",L"继续上次任务"
    };
    static const wchar_t *const actions[]={
        L"继续",L"接着",L"修改",L"改成",L"改为",L"改到",L"加孔",L"打孔",L"开孔",L"倒角",
        L"加厚",L"装入",L"装配到",L"保存",L"重生成",L"改名",L"重命名",L"再画完",L"做完"
    };
    static const wchar_t *const incompatible[]={
        L"刚才那个方案",L"刚才那个做法",L"刚才那个方法",L"刚才那个尺寸",
        L"当前装配",L"当前零件",L"当前模型",L"另建",L"新建",L"另一个",
        L".prt",L".PRT",L".asm",L".ASM",L"不要改",L"别改",L"先不改"
    };
    if(wcspbrk(text,L"?？「」“”\"")||smart_readonly_override(text)||model_request_explicit_new(text)||
       smart_has_text(text,incompatible,_countof(incompatible)))return 0;
    return smart_has_text(text,objects,_countof(objects))&&smart_has_text(text,actions,_countof(actions));
}

static ProError chat_followup_activate(const wchar_t *text,ProMdl *model){
    ProError error=chat_task_last_followup(model);
    if(error)return error;
    /* A direct new operation may continue from a successful previous task. A
       bare resume needs verified unfinished work. */
    if(!wcscmp(text,L"继续上次任务")&&!chat_task_followup_pending())return PRO_TK_NOT_EXIST;
    ProMdl actual=NULL;
    if(!ProMdlCurrentGet(&actual)&&actual==*model)return PRO_TK_NO_ERROR;
    ProMdlType type;
    ProMdlName name;
    int window,previous=-1;
    if((error=ProMdlTypeGet(*model,&type))||(error=ProMdlMdlnameGet(*model,name)))return error;
    if(type!=PRO_MDL_ASSEMBLY&&type!=PRO_MDL_PART)return PRO_TK_INVALID_TYPE;
    ProWindowCurrentGet(&previous);
    error=native_activate_model_window(*model,name,type==PRO_MDL_ASSEMBLY?PRO_ASSEMBLY:PRO_PART,&window);
    if(!error&&(ProMdlCurrentGet(&actual)||actual!=*model))error=PRO_TK_BAD_CONTEXT;
    if(error&&previous>=0){
        ProWindowCurrentSet(previous);
        ProWindowActivate(previous);
    }
    return error;
}
