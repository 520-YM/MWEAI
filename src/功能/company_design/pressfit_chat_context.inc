typedef struct {
    ProMdl assembly;
    ProMdlName name;
    ProPath directory;
    int window;
    wchar_t selector[MAX_PATH];
    ProMdlName generic,instance;
} PressfitChatContext;
static PressfitChatContext pressfit_last_fit;

static void pressfit_chat_remember(const FastenerPressfitTarget *target,const wchar_t *selector,
        ProMdl source){
    PressfitChatContext saved={.assembly=target->assembly,.window=target->window};
    if(ProMdlMdlnameGet(target->assembly,saved.name)||ProMdlMdlnameGet(source,saved.instance))return;
    wcscpy_s(saved.directory,_countof(saved.directory),target->directory);
    wcscpy_s(saved.selector,_countof(saved.selector),selector);
    ProMdl generic=NULL;
    if(!ProFaminstanceGenericGet(source,PRO_B_TRUE,&generic)&&generic)
        (void)ProMdlMdlnameGet(generic,saved.generic);
    pressfit_last_fit=saved;
}

static int pressfit_chat_contextual(const wchar_t *text,FastenerPressfitRequest *request){
    const wchar_t *p=fastener_bso_action_start(text);
    if(wcspbrk(p,L"?？\r\n"))return 0;
    FastenerPressfitRequest result={.action=PRESSFIT_PLACE,.short_form=1,.reuse_prior=1};
    assembly_take(&p,L"把");
    if(assembly_take(&p,L"换成")||assembly_take(&p,L"改成")){
        const wchar_t *number=fastener_chat_separator(p);
        if(*number<L'1'||*number>L'9')return 0;
        wchar_t *end;
        const long length=wcstol(number,&end,10);
        if(length>999)return 0;
        p=fastener_chat_separator(end);
        if(!_wcsnicmp(p,L"mm",2))p+=2;
        else assembly_take(&p,L"毫米");
        if(!assembly_take(&p,L"长"))return 0;
        assembly_take(&p,L"的");
        result.follow_length=(int)length;
        result.replace=1;
    }else{
        const wchar_t *fragment=p;
        assembly_take(&p,L"这");
        const int count=fastener_bso_count(&p);
        if(count&&(assembly_take(&p,L"个")||assembly_take(&p,L"孔")))result.expected=count;
        else p=fragment;
        assembly_take(&p,L"也");
        if(!fastener_pressfit_take_install(&p)||
           !(assembly_take(&p,L"和刚才一样")||assembly_take(&p,L"一样的")||assembly_take(&p,L"相同的")))return 0;
    }
    if(!fastener_chat_end(p))return 0;
    *request=result;
    return 1;
}

static ProError pressfit_chat_use_prior(FastenerPressfitRequest *request,const FastenerPressfitTarget *target,PartText *out){
    ProMdlName name;
    if(!pressfit_last_fit.selector[0]||pressfit_last_fit.assembly!=target->assembly||
       pressfit_last_fit.window!=target->window||_wcsicmp(pressfit_last_fit.directory,target->directory)||
       ProMdlMdlnameGet(target->assembly,name)||_wcsicmp(name,pressfit_last_fit.name)){
        part_text_add(out,L"当前装配没有可复用的已核实规格。请说出型号，并选一下要装的孔。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    if(!request->follow_length){
        wcscpy_s(request->model,_countof(request->model),pressfit_last_fit.selector);
        return PRO_TK_NO_ERROR;
    }
    const wchar_t *end=wcsrchr(pressfit_last_fit.instance,L'X');
    if(!end)end=wcsrchr(pressfit_last_fit.instance,L'x');
    if(!pressfit_last_fit.generic[0]||!end||!pressfit_bso_size(end+1)||
       swprintf_s(request->model,_countof(request->model),L"%ls:%.*ls%d",pressfit_last_fit.generic,
           (int)(end+1-pressfit_last_fit.instance),pressfit_last_fit.instance,request->follow_length)<0){
        part_text_add(out,L"上一规格没有核实到可换长度的族表。请给出完整的新型号。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}