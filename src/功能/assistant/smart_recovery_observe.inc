/* Re-read native state before replanning a failed in-memory operation.
   File output, replacement, deletion and historically unstable paths keep
   their operation-specific recovery rules. */
static int smart_retryable_model_action(const PartCommand *cmd){
    if(cmd->verb==PART_DIMS||cmd->verb==PART_PARAM||cmd->verb==PART_REGEN||
       cmd->verb==PART_EXTRUDE)return 1;
    if(cmd->verb!=PART_FOUNDATION||bwtl_traceback_risk(cmd))return 0;
    const int assembly=ax_kind(cmd->arg[0]);
    if(assembly){
        switch(assembly){
        case AX_INSERT:case AX_INSERT_ASM:case AX_CSYS:case AX_MATE:case AX_PARALLEL:case AX_OFFSET:
        case AX_COAX:case AX_THREE:case AW_ALIGN:case AW_ALIGN_BATCH:
            return 1;
        default:return 0;
        }
    }
    if(smart_cad_track(cmd))return 1;
    static const wchar_t *const operations[]={
        L"EDGE_ROUND",L"EDGE_CHAMFER",L"HOLE_CREATE",L"SMT_FLAT4",L"CASE_TRAY",L"STOOL_BACK"
    };
    for(size_t i=0;i<_countof(operations);i++)
        if(!wcscmp(cmd->arg[0],operations[i]))return 1;
    return 0;
}

static int smart_observe_failed_model_action(const PartCommand *cmd,const PartSnapshot *expected,
    ProError error,int verified_rollback,PartText *out){
    if(!error||error==PRO_TK_USER_ABORT||smart_readonly||smart_local_plan||
       !smart_intent_writable(smart_intent)||!expected->model||!expected->stamp||
       (smart_target_model&&smart_target_model!=expected->model)||
       !smart_retryable_model_action(cmd)||smart_unsafe_receipt(out->text))return 0;
    ProMdl current=expected->model;
    if(!verified_rollback&&part_check_binding(cmd,expected,&current))return 0;
    if(part_health(current))return 0;
    static PartSnapshot checked;
    int observed=0;
    if(!part_snapshot_take_mode(current,&checked,0)&&checked.model==expected->model&&
       checked.window_model==expected->window_model&&checked.type==expected->type&&
       checked.subtype==expected->subtype&&!_wcsicmp(checked.name,expected->name)&&
       !_wcsicmp(checked.directory,expected->directory)){
        part_text_add(out,L"\n已重新读取 Creo 的实际状态：%ls.%ls，工作目录 %ls。"
            L"%ls，已完成的步骤仍在当前模型内。",
            checked.name,checked.type==PRO_MDL_ASSEMBLY?L"ASM":L"PRT",checked.directory,
            verified_rollback?L"原生回退及原有特征已核对":L"模型与父装配（如适用）的版本与执行前一致");
        if(checked.has_outline){
            const double mm=checked.mm_per_native;
            part_text_add(out,L"实测外形 %.7g × %.7g × %.7g mm。",
                (checked.outline[1][0]-checked.outline[0][0])*mm,
                (checked.outline[1][1]-checked.outline[0][1])*mm,
                (checked.outline[1][2]-checked.outline[0][2])*mm);
        }
        part_text_add(out,L"先核对与失败相关的参照，使用不同的有效方法。保留用户已确认的尺寸和约束，只处理未完成的操作。");
        observed=1;
    }
    part_snapshot_clear(&checked);
    return observed;
}


/* Never reinterpret a fixed spacing. The narrow native planner requires
   explicit delegation and a diameter that agrees with the user's request. */
static int smart_four_pitch_delegated(const wchar_t *goal,double *diameter){
    static const wchar_t *const choices[]={L"自选",L"自行决定",L"自行选择",L"自行确定",
        L"自己决定",L"自己选",L"你决定",L"由你定",L"由你决定",L"你定",L"自定"};
    static const wchar_t *const fixed[]={L"不得",L"不能",L"不许",L"不可",L"不要",
        L"不能改",L"固定",L"不变",L"必须",L"别改",L"保持",L"原孔距"};
    static const wchar_t *const preferred[]={L"优先",L"首选",L"先试",L"先按"};
    if(!wcsstr(goal,L"居中")||(!wcsstr(goal,L"四个")&&!wcsstr(goal,L"4个")&&
       !wcsstr(goal,L"四孔")&&!wcsstr(goal,L"4孔"))||
       (!wcsstr(goal,L"通孔")&&!wcsstr(goal,L"贯穿")))return 0;
    int delegated=0,explicit_diameter=0;
    while(*goal){
        const size_t length=wcscspn(goal,L"，,。；;\r\n！!？?");
        if(length>=512)return 0;
        wchar_t clause[512];
        wmemcpy(clause,goal,length);
        clause[length]=0;
        goal+=length;
        if(*goal)goal++;
        const int pitch=wcsstr(clause,L"孔距")||wcsstr(clause,L"间距");
        const int first_choice=pitch&&smart_has_text(clause,preferred,_countof(preferred));
        if(pitch){
            if(smart_has_text(clause,fixed,_countof(fixed)))return 0;
            if(smart_has_text(clause,choices,_countof(choices)))delegated=1;
        }
        const wchar_t *diameter_field=wcsstr(clause,L"直径");
        if(!diameter_field)diameter_field=wcsstr(clause,L"孔径");
        if(diameter_field)diameter_field+=2;
        else{
            diameter_field=wcspbrk(clause,L"ØΦφ⌀");
            if(diameter_field)diameter_field++;
        }
        if(diameter_field)while(iswspace(*diameter_field))diameter_field++;
        for(const wchar_t *p=clause;*p;p++){
            if(p==diameter_field&&*p>=L'0'&&*p<=L'9'){
                wchar_t *end=NULL;
                const double value=wcstod(p,&end);
                const wchar_t *unit=end;
                while(iswspace(*unit))unit++;
                if(wcsncmp(unit,L"毫米",2)&&(_wcsnicmp(unit,L"mm",2)||iswalpha(unit[2])))return 0;
                if(!isfinite(value)||value<0.1||value>1000000||explicit_diameter)return 0;
                *diameter=value;
                explicit_diameter=1;
                p=end-1;
            }else if(*p>=L'0'&&*p<=L'9'){
                if(!first_choice&&(*p!=L'4'||(p[1]!=L'个'&&p[1]!=L'孔')))return 0;
            }
        }
        if(!first_choice&&(pitch||wcsstr(clause,L"横向")||wcsstr(clause,L"纵向"))&&
           wcspbrk(clause,L"零〇一二两三四五六七八九十百千万１２３４５６７８９０"))return 0;
    }
    return delegated&&explicit_diameter;
}

static int smart_four_alternative(const PartCommand *command,const PartSnapshot *expected,
    ProError error,const wchar_t *receipt,PartCommand *alternative,double *width,double *height){
    if(command->verb!=PART_FOUNDATION||wcscmp(command->arg[0],L"SMT_FLAT4")||
       smart_readonly||smart_local_plan||!smart_intent_writable(smart_intent)||
       expected->type!=PRO_MDL_PART||expected->subtype!=PROMDLSTYPE_PART_SHEETMETAL||
       expected->window_model!=expected->model||!expected->stamp||sh_recovery_uncertain||
       pn_recovery_uncertain||smart_unsafe_receipt(receipt))return 0;
    const int can_replan=(error==PRO_TK_BAD_CONTEXT&&wcsstr(receipt,L"还没有写入模型。")&&
        (wcsstr(receipt,L"检查四孔与板边、现有开口之间的净距")||
         wcsstr(receipt,L"确认每个孔位都有完整板厚的材料")))||
        (error==PRO_TK_BAD_INPUTS&&wcsstr(receipt,L"横纵孔距都须大于孔径"));
    if(!can_replan)return 0;
    const double diameter=as_arg(command,2);
    double requested_diameter;
    if(!isfinite(diameter)||!smart_four_pitch_delegated(smart_goal,&requested_diameter)||
       fabs(diameter-requested_diameter)>0.000001)return 0;
    ProMdl current=NULL;
    if(part_check_binding(command,expected,&current)||part_health(current))return 0;
    double pitch_x,pitch_y;
    if(sh_four_choose_pitch(current,diameter,&pitch_x,&pitch_y,width,height)||
       (fabs(pitch_x-as_arg(command,3))<0.000001&&fabs(pitch_y-as_arg(command,4))<0.000001))return 0;
    *alternative=*command;
    swprintf_s(alternative->arg[3],PART_TOKEN_MAX,L"%.12g",pitch_x);
    swprintf_s(alternative->arg[4],PART_TOKEN_MAX,L"%.12g",pitch_y);
    return 1;
}

static int smart_four_task_diameter(double *diameter){
    return !smart_readonly&&!smart_local_plan&&smart_intent==SMART_INTENT_CURRENT&&
        request_snapshot.model&&request_snapshot.model==smart_target_model&&
        request_snapshot.subtype==PROMDLSTYPE_PART_SHEETMETAL&&smart_four_pitch_delegated(smart_goal,diameter);
}

static int smart_four_task_unverified(void){
    double diameter;
    return smart_four_task_diameter(&diameter)&&
        (smart_four_verified_target!=smart_target_model||fabs(smart_four_verified_diameter-diameter)>0.000001);
}
