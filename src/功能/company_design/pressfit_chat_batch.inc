#define PRESSFIT_CHAT_GROUPS 6

static int pressfit_chat_single(const wchar_t *text,FastenerPressfitRequest *request){
    const wchar_t *p=model_request_action_start(text);
    if((assembly_take(&p,L"识别压铆孔型")||assembly_take(&p,L"检查压铆孔型"))&&fastener_chat_end(p)){
        *request=(FastenerPressfitRequest){.action=PRESSFIT_PLACE,.check_openings=1};
        return 1;
    }
    p=model_request_action_start(text);
    if((assembly_take(&p,L"核对压铆干涉")||assembly_take(&p,L"核对上次压铆干涉"))&&fastener_chat_end(p)){
        *request=(FastenerPressfitRequest){.action=PRESSFIT_PLACE,.check_previous=1};
        return 1;
    }
    p=model_request_action_start(text);
    if((assembly_take(&p,L"继续装压铆件")||assembly_take(&p,L"继续装紧固件")||
        assembly_take(&p,L"继续压铆装配")||assembly_take(&p,L"继续装"))&&fastener_chat_end(p)){
        *request=(FastenerPressfitRequest){.action=PRESSFIT_PLACE,.resume=1,.short_form=1};
        return 1;
    }
    p=model_request_action_start(text);
    if((assembly_take(&p,L"学习本次压铆")||assembly_take(&p,L"保存本次压铆方法"))&&fastener_chat_end(p)){
        *request=(FastenerPressfitRequest){.action=PRESSFIT_PLACE,.learn=1};
        return 1;
    }
    p=model_request_action_start(text);
    if(assembly_take(&p,L"按已学压铆方法")&&fastener_pressfit_parse(p,request)&&request->action==PRESSFIT_PLACE){
        request->reuse_method=1;
        return 1;
    }
    return fastener_pressfit_short_nut(text,request,1)||fastener_pressfit_parse(text,request)||
        pressfit_chat_contextual(text,request);
}

static int pressfit_chat_groups(const wchar_t *text,FastenerPressfitRequest group[PRESSFIT_CHAT_GROUPS],
        wchar_t clause[PRESSFIT_CHAT_GROUPS][640]){
    const wchar_t *p=text;
    int count=0;
    wchar_t separators[]=L";；，,";
    while(*p&&count<PRESSFIT_CHAT_GROUPS){
        const wchar_t *end=p+wcscspn(p,separators);
        const size_t length=end-p;
        if(!length||length>=_countof(clause[0])||wcsncpy_s(clause[count],_countof(clause[0]),p,length)||
           !fastener_pressfit_parse(clause[count],group+count)||group[count].action!=PRESSFIT_PLACE||
           group[count].reuse_prior||group[count].recommend||group[count].example_path[0]||
           (!group[count].panel_path[0]&&!group[count].side_zone))return 0;
        count++;
        if(!*end)return count>1?count:0;
        p=end+1;
    }
    return 0;
}

static struct {
    int waiting;
    PressfitParentSelection selection;
    FastenerPressfitRequest request;
    wchar_t text[6001];
} pressfit_parent_pending;

static void pressfit_parent_cancel(void){
    if(!pressfit_parent_pending.waiting)return;
    fastener_pressfit_parent_clear(&pressfit_parent_pending.selection);
    ax_session_activation_clear(&ax_session_pending);
    pressfit_parent_pending.waiting=0;
}

static int pressfit_parent_confirm(uiCmdCmdId id,uiCmdValue *value,void *data){
    if(!pressfit_parent_pending.waiting)return 0;
    if(!smart_active){pressfit_parent_cancel();return 0;}
    static wchar_t report[15000];
    report[0]=0;
    PartText out={report,_countof(report),0};
    ProMdl active=NULL;
    ProPath directory;
    ProError error=ax_session_activate_readback(&active,directory,&out);
    if(!error)error=fastener_pressfit_apply_parent_selection(&pressfit_parent_pending.selection,
        &pressfit_parent_pending.request,&out);
    if(!error){
        part_text_add(&out,L"\n已把选中的孔和安装面映射到原装配实例。开始按当前 Creo 模型核对。\n");
        fastener_pressfit_execute(pressfit_parent_pending.request,pressfit_parent_pending.text,&out);
    }else if(!report[0])part_text_add(&out,L"原装配窗口或选孔无法核对（Creo %d）。",error);
    pressfit_parent_cancel();
    smart_record_tool(report);
    smart_reply(report);
    return 0;
}

/* Returns 1 when scheduled, -1 on a handled error, and 0 for the normal ASM route. */
static int pressfit_parent_begin(const FastenerPressfitRequest *request,const wchar_t *text,PartText *out){
    if(request->action!=PRESSFIT_PLACE||request->resume||request->learn||request->panel_path[0]||
       (!request->model[0]&&!request->nut_style&&!request->recommend))return 0;
    ProMdl model=NULL;
    ProMdlType type;
    if(ProMdlCurrentGet(&model)||ProMdlTypeGet(model,&type)||type!=PRO_MDL_PART)return 0;
    ProError error=fastener_pressfit_capture_parent_selection(NULL,&pressfit_parent_pending.selection,out);
    if(!error){
        AxSessionSelector selected={.type=PRO_MDL_ASSEMBLY,.has_window_id=1,
            .window_id=pressfit_parent_pending.selection.link.root_window};
        error=ax_session_activate(&selected,out);
    }
    if(!error){
        pressfit_parent_pending.request=*request;
        wcscpy_s(pressfit_parent_pending.text,_countof(pressfit_parent_pending.text),text);
        pressfit_parent_pending.waiting=1;
        error=ProMacroLoad(L"~ Command \x0060" L"BWTLPressfitParentConfirm\x0060;");
        if(!error){native_set_busy(1);return 1;}
    }
    pressfit_parent_cancel();
    fastener_pressfit_parent_clear(&pressfit_parent_pending.selection);
    if(!out->text[0])part_text_add(out,L"原装配窗口的选孔核对未完成（Creo %d）。",error);
    return -1;
}

static int fastener_pressfit_chat(const wchar_t *text,int place_only){
    FastenerPressfitRequest request[PRESSFIT_CHAT_GROUPS];
    wchar_t clauses[PRESSFIT_CHAT_GROUPS][640];
    int groups=1;
    if(!pressfit_chat_single(text,request)){
        if(!place_only||!(groups=pressfit_chat_groups(text,request,clauses)))return 0;
    }
    if(place_only!=(request[0].action==PRESSFIT_PLACE))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    fastener_pressfit_highlight_clear();
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");

    static wchar_t report[24000];
    report[0]=0;
    PartText out={report,_countof(report),0};
    const int parent=groups==1?pressfit_parent_begin(request,text,&out):0;
    if(parent>0)return 1;
    for(int i=0;!parent&&i<groups;i++){
        if(groups>1)part_text_add(&out,L"%ls第 %d 组 %ls\n",i?L"\n\n":L"",i+1,clauses[i]);
        ProError error=fastener_pressfit_execute(request[i],groups==1?text:clauses[i],&out);
        if(error){
            if(i+1<groups)part_text_add(&out,L"\n其余 %d 组待执行，已核实的结果保留。",groups-i-1);
            break;
        }
    }
    smart_record_tool(report);
    smart_reply(report);
    return 1;
}