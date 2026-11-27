/* Short commands use verified company nut families and re-read the active assembly geometry. */
static int fastener_pressfit_take_install(const wchar_t **p){
    return assembly_take(p,L"装配")||assembly_take(p,L"安装")||assembly_take(p,L"装上")||assembly_take(p,L"装");
}

static int fastener_pressfit_take_thread(const wchar_t **p,wchar_t thread[12]){
    const wchar_t *start=fastener_chat_separator(*p);
    if(towupper(*start)!=L'M'||start[1]<L'1'||start[1]>L'9')return 0;
    wchar_t *end;
    const long number=wcstol(start+1,&end,10);
    if(number<1||number>64)return 0;
    int half=(*end==L'.'||*end==L'_')&&end[1]==L'5';
    if(half)end+=2;
    if(*end>=L'0'&&*end<=L'9')return 0;
    if(swprintf_s(thread,12,half?L"M%ld_5":L"M%ld",number)<0)return 0;
    *p=end;
    return 1;
}

static int fastener_pressfit_short_nut(const wchar_t *text,FastenerPressfitRequest *result,int allow_prior);

static int fastener_pressfit_previous_nut(FastenerPressfitRequest *prior){
    const wchar_t *line=NULL;
    for(const wchar_t *p=conversation;(p=wcsstr(p,L"\n用户："))!=NULL;){p+=4;line=p;}
    if(!line)return 0;
    const size_t n=wcscspn(line,L"\r\n");
    wchar_t request[640];
    return n&&n<_countof(request)&&!wcsncpy_s(request,_countof(request),line,n)&&
        fastener_pressfit_short_nut(request,prior,0);
}

static int fastener_pressfit_short_nut(const wchar_t *text,FastenerPressfitRequest *result,int allow_prior){
    const wchar_t *p=fastener_chat_separator(text);
    if(wcspbrk(p,L"?？\r\n"))return 0;
    const int follow=assembly_take(&p,L"那就")||assembly_take(&p,L"那么就")||assembly_take(&p,L"改成");
    if(!assembly_take(&p,L"选好孔了")&&!assembly_take(&p,L"选好孔"))assembly_take(&p,L"孔选好了");
    p=model_request_action_start(fastener_chat_separator(p));
    assembly_take(&p,L"给");
    assembly_take(&p,L"把");
    assembly_take(&p,L"在");
    const int targets=fastener_bso_hole_count(&p);
    p=fastener_chat_separator(p);
    assembly_take(&p,L"各");
    assembly_take(&p,L"都");
    p=fastener_chat_separator(p);
    if(!fastener_pressfit_take_install(&p))return 0;
    const wchar_t *quantity=p;
    int amount=fastener_bso_count(&quantity);
    if(amount&&(assembly_take(&quantity,L"颗")||assembly_take(&quantity,L"个")||assembly_take(&quantity,L"件")))
        p=quantity;
    else amount=0;
    if(targets&&amount>1)return 0;
    p=fastener_chat_separator(p);
    int style=0;
    if(assembly_take(&p,L"不锈钢"))style=2;
    else if(assembly_take(&p,L"碳钢"))style=1;
    const int scope_first=assembly_take(&p,L"压铆螺母");
    FastenerPressfitRequest request={.action=PRESSFIT_PLACE,.expected=targets?targets:amount};
    if(!fastener_pressfit_take_thread(&p,request.thread))return 0;
    p=fastener_chat_separator(p);
    if(!style&&assembly_take(&p,L"不锈钢"))style=2;
    if(!style&&assembly_take(&p,L"碳钢"))style=1;
    const int scoped=scope_first||assembly_take(&p,L"压铆螺母");
    if(!scoped){
        FastenerPressfitRequest prior={0};
        if(!allow_prior||!follow||!fastener_pressfit_previous_nut(&prior))return 0;
        if(!request.expected)request.expected=prior.expected;
        if(!style)style=prior.nut_style;
        request.choose_side=prior.choose_side;
        request.follow_nut=1;
    }
    request.nut_style=style?style:1;
    p=fastener_chat_separator(p);
    assembly_take(&p,L"的");
    assembly_take(&p,L"吧");
    p=fastener_chat_separator(p);
    if(assembly_take(&p,L"型号和方向你定")||assembly_take(&p,L"型号和安装方向你定")||
       assembly_take(&p,L"型号、方向你定")||assembly_take(&p,L"方向你定")||
       assembly_take(&p,L"方向你决定"))request.choose_side=1;
    else assembly_take(&p,L"型号你定");
    p=fastener_chat_separator(p);
    if(scoped){
        assembly_take(&p,L"到");
        if(!assembly_take(&p,L"选中的孔位")&&!assembly_take(&p,L"选中的孔"))
            assembly_take(&p,L"我选的孔");
    }else assembly_take(&p,L"你只要给我装上就行");
    if(!fastener_chat_end(p))return 0;
    *result=request;
    return 1;
}

static ProError fastener_pressfit_short_model(FastenerPressfitRequest *request,PartText *out){
    const wchar_t *family=request->nut_style==2?L"SS_M2_10_XX":L"S_M2_10_XX";
    const wchar_t *material=request->nut_style==2?L"SS":L"S";
    wchar_t prefix[22];
    swprintf_s(prefix,_countof(prefix),L"%ls-%ls-",material,request->thread);
    size_t length=wcslen(prefix);
    const PressfitAcceptance *chosen=NULL;
    for(size_t i=0;i+1<_countof(pressfit_acceptance_rows);i++){
        const PressfitAcceptance *row=pressfit_acceptance_rows+i;
        if(_wcsnicmp(row->source,family,wcslen(family))||_wcsnicmp(row->instance,prefix,length)||
           row->library_hole_mm<=0.0||row->instance[length]<L'0'||row->instance[length]>L'9'||
           row->instance[length+1])continue;
        if(chosen&&fabs(row->library_hole_mm-chosen->library_hole_mm)>0.001){
            part_text_add(out,L"%ls 通用压铆螺母有不同底孔，需指定具体型号。\n",request->thread);
            return PRO_TK_E_FOUND;
        }
        if(!chosen||row->instance[length]<chosen->instance[length])chosen=row;
    }
    if(!chosen){
        part_text_add(out,L"公司库没有已验收的 %ls %ls 通用压铆螺母。\n",request->thread,material);
        return PRO_TK_E_NOT_FOUND;
    }
    if(swprintf_s(request->model,_countof(request->model),L"%ls:%ls",family,chosen->instance)<0)return PRO_TK_BAD_INPUTS;
    request->hole=chosen->library_hole_mm;
    return PRO_TK_NO_ERROR;
}

#include "pressfit_targets.inc"
