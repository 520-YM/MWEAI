#include "traceback_risk.inc"
#include "smart_sketch_recovery.inc"
#include "smart_cad_recovery.inc"
#include "smart_recovery_observe.inc"
static ProError imitation_run_chat(ProMdl source,ProPath delivered,int *partial,PartText *out);
static ProError imitation_resize_command(const PartCommand *command,const PartSnapshot *snapshot,
                                         wchar_t *text,size_t capacity);
static ProError imitation_local_command(const PartCommand *command,const PartSnapshot *snapshot,
                                         wchar_t *text,size_t capacity);

static ProError imitation_find_wall(ProFeature *feature,ProError status,ProAppData data){
    if(status)return status;
    ProFeattype type;
    if(ProFeatureTypeGet(feature,&type))return PRO_TK_NO_ERROR;
    if(type==PRO_FEAT_WALL)*(int *)data|=1;
    if(type==PRO_FEAT_DRAFT)*(int *)data|=2;
    if(type==PRO_FEAT_CUT||type==PRO_FEAT_PROTRUSION){
        ProElement tree=NULL,depth;
        int kind;
        if(!ProFeatureElemtreeExtract(feature,NULL,PRO_FEAT_EXTRACT_NO_OPTS,&tree)&&
           !mc_tree_element(tree,(ProElemId[]){PRO_E_STD_EXT_DEPTH,PRO_E_EXT_DEPTH_TO,PRO_E_EXT_DEPTH_TO_TYPE},3,&depth)&&
           !ProElementIntegerGet(depth,NULL,&kind)&&kind==PRO_EXT_DEPTH_TO_NEXT)*(int *)data|=4;
        if(tree)ProFeatureElemtreeFree(feature,tree);
    }
    return PRO_TK_NO_ERROR;
}

static int imitation_warn_wall(ProMdl model){
    int found=0;
    ProMdlType type;
    if(model&&!ProMdlTypeGet(model,&type)&&type==PRO_MDL_PART)
        ProSolidFeatVisit((ProSolid)model,imitation_find_wall,NULL,&found);
    if(found&1){
        const wchar_t *message=L"【回溯风险重点提醒】本次含钣金凸缘。边法兰在不完整附加壁或失败回滚、相邻翻边留缝在直接重定义已有壁时曾触发 Creo 回溯。此次将在新建零件上逐步重建。";
        append_card(L"提示",message);
        save_turn(L"回溯风险",message);
        trace("Traceback risk notice before replay",93);
    }
    if(found&2){
        const wchar_t *message=L"【回溯风险重点提醒】本次包含拔模。旧版读取或映射拔模面参照时曾触发 Creo 回溯。";
        append_card(L"提示",message);
        save_turn(L"回溯风险",message);
        trace("Traceback risk notice before replay",26);
    }
    if(found&4){
        const wchar_t *message=L"【回溯风险重点提醒】本次包含拉伸到下一面。旧版释放射线选取结果时曾触发 Creo 回溯。";
        append_card(L"提示",message);
        save_turn(L"回溯风险",message);
        trace("Traceback risk notice before replay",52);
    }
    return found;
}

static void execute_part_command(void){
    ULONGLONG execution_started=GetTickCount64();
    static wchar_t result[PART_TEXT_MAX];
    PartCommand command=pending_part;
    memset(&pending_part,0,sizeof(pending_part));
    if(command.verb==PART_NONE||!smart_active)return;
    if(!smart_protocol_allowed(&command)){
        smart_reply(!smart_readonly&&!wcscmp(command.arg[0],L"FEATURE_DELETE")?smart_delete_block_message:smart_operation_block());
        return;
    }
    int opening_component=(command.verb==PART_FOUNDATION&&ax_kind(command.arg[0])==AX_OPEN)||
        (command.verb==PART_DISPLAY&&request_snapshot.type==PRO_MDL_ASSEMBLY);
    if(opening_component){
        ProError error=part_execute(&command,&request_snapshot,result,_countof(result));
        if(!error){
            component_open_pending=command;
            component_open_waiting=1;
            error=ProMacroLoad(L"~ Command \x0060" L"BWTLComponentOpenConfirm\x0060;");
            if(!error)return;
            component_open_cancel();
            wcscpy_s(result,_countof(result),L"Creo 已接收打开请求，但无法调度活动窗口核对。请看 Creo 当前窗口。");
        }
        int continuing=!smart_local_plan&&!smart_readonly&&(smart_chain||resident_tool_running());
        smart_step(&command,L"打开装配组件",error,result[0]?result:L"Creo 未能打开该装配组件。",0,!continuing,
                   continuing&&!smart_binding_valid());
        return;
    }
    if(command.verb!=PART_READ&&command.verb!=PART_DISPLAY&&!storage_prepare_output()){smart_reply(storage_output_message);return;}
    if(command.verb==PART_IMITATE){
        PartText receipt={result,_countof(result),0};
        ProPath delivered;
        int partial=0;
        ProMdl model=NULL;
        result[0]=0;
        ProError error=part_check_binding(&command,&request_snapshot,&model);
        if(!error&&request_snapshot.in_assembly)error=PRO_TK_BAD_CONTEXT;
        if(!error){
            imitation_warn_wall(model);
            error=imitation_run_chat(model,delivered,&partial,&receipt);
        }
        if(error&&!result[0])part_text_add(&receipt,L"请先激活要按原特征树重建的零件（Creo %d）。",error);
        smart_step(&command,L"按特征树仿照",error,result,!error||partial,1,0);
        return;
    }
    if(command.verb==PART_IMITATE_SIZE){
        imitation_warn_wall(request_snapshot.model);
        ProError error=imitation_resize_command(&command,&request_snapshot,result,_countof(result));
        smart_step(&command,L"仿照并改尺寸",error,result,!error,1,0);
        return;
    }
    if(command.verb==PART_IMITATE_LOCAL){
        ProError error=imitation_local_command(&command,&request_snapshot,result,_countof(result));
        smart_step(&command,L"局部仿照",error,result,!error,1,0);
        return;
    }
    const wchar_t *verb=command.arg[0];
    int foundation=command.verb==PART_FOUNDATION;
    int special=foundation&&(dv_kind(verb)||ax_kind(verb)||pn_kind(verb)||cm_kind(verb)||fx_kind(verb)||
        mp_kind(verb)||vt_kind(verb)||rv_kind(verb)||in_kind(verb)||org_kind(verb)||fab_kind(verb)||smx_kind(verb)||quick_kind(verb)||!wcscmp(verb,L"STOOL_BACK"));
    int independent=command.verb==PART_COPY||(foundation&&(dv_kind(verb)==DV_IMPORT||quick_kind(verb)||smx_independent(smx_kind(verb))));
    int quality=foundation?ax_kind(verb):AX_NONE;
    int quality_report_flow=quality>=AX_Q_REGEN&&quality<AX_Q_REPORT&&
        (wcsstr(smart_goal,L"检查报告")||wcsstr(smart_goal,L"质量报告")||
         wcsstr(smart_goal,L"检查结果汇总"));
    int assembly_lookup=quality_report_flow||(foundation&&(((smart_intent==SMART_INTENT_CURRENT||(smart_intent==SMART_INTENT_NEW&&smart_progress&&request_snapshot.type==PRO_MDL_ASSEMBLY))&&
        (ax_read_diagnostic(ax_kind(verb))||ax_delete_preview(&command)))||
        (smart_intent==SMART_INTENT_READ&&(ax_motion_inspection_request(smart_goal)||aw_request(smart_goal))&&ax_read_diagnostic(ax_kind(verb)))));
    int final=quality==AX_Q_REPORT||independent||(special&&!smart_chain&&!assembly_lookup)||command.verb==PART_DISPLAY;
    if(resident_tool_running()&&!independent&&command.verb!=PART_DISPLAY&&quality!=AX_Q_REPORT)final=0;
    int write=!smart_read_command(&command);
    if(smart_block_duplicate(&command))return;
    const int four_holes=foundation&&!wcscmp(verb,L"SMT_FLAT4");
    double requested_diameter;
    if(four_holes&&smart_four_task_diameter(&requested_diameter)&&
       fabs(as_arg(&command,2)-requested_diameter)>0.000001){
        swprintf_s(result,_countof(result),L"四孔孔径须使用你指定的 %.8g mm。本次没有改动模型。",requested_diameter);
        smart_step(&command,verb,PRO_TK_BAD_INPUTS,result,0,0,0);
        return;
    }
    if(!independent&&smart_target_model&&request_snapshot.model==smart_target_model&&
       _wcsicmp(command.arg[1],request_snapshot.name)&&smart_binding_valid()){
        swprintf_s(result,_countof(result),L"本次操作尚未执行。当前已核对目标是 %ls.%ls。"
            L"请先读取新目标的参照，再按当前模型继续尚未完成的步骤。",request_snapshot.name,
            request_snapshot.type==PRO_MDL_ASSEMBLY?L"ASM":L"PRT");
        const int recover_target=!smart_local_plan&&!smart_readonly&&smart_intent_writable(smart_intent);
        smart_step(&command,verb,PRO_TK_BAD_CONTEXT,result,0,final&&!recover_target,0);
        return;
    }
    if((command.verb==PART_DIMS||command.verb==PART_PARAM||command.verb==PART_SUPPRESS||command.verb==PART_RESUME)&&!part_selfcheck_passed){
        ProError check=part_selfcheck(result,PART_TEXT_MAX);
        if(check){smart_step(NULL,L"零件工具自检",check,result,0,1,1);return;}
    }

    SmartPartSignature before={0};
    fd_sketch_rolled_back_model=NULL;
    sh_cut_rolled_back_model=NULL;
    sh_cad_rolled_back_model=NULL;
    sd_cad_rolled_back_model=NULL;
    sh_cut_rolled_back_no_material=0;
    double before_thickness=0;
    SmartCadEvidence evidence=smart_cad_capture(&command,&request_snapshot);
    if(evidence.track)before=evidence.signature;
    else if(smart_sketch_can_check(&command,&request_snapshot)||
            smart_smt_cut_capture(&command,&request_snapshot,&before_thickness))
        before=smart_part_signature(request_snapshot.model);
    bwtl_warn_traceback(&command);
    ProError error=part_execute(&command,&request_snapshot,result,PART_TEXT_MAX);
    PartText feedback={result,_countof(result),0};
    int checked_recovery=error&&error!=PRO_TK_USER_ABORT&&!smart_unsafe_receipt(result)&&
        smart_cad_recovered(&command,&request_snapshot,&evidence,error);
    if(checked_recovery){
        PartCommand alternative={0};
        if(smart_cad_cut_alternative(&command,&request_snapshot,error,&evidence,&alternative)&&
           smart_protocol_allowed(&alternative)){
            static PartSnapshot fresh;
            if(!part_snapshot_take_mode(request_snapshot.model,&fresh,0)){
                static wchar_t original[1800],replacement[PART_TEXT_MAX];
                wcsncpy_s(original,_countof(original),result,_TRUNCATE);
                smart_protocol_remember(&command,error);
                smart_stage_failed(SMART_STAGE_WRITE);
                smart_describe_failure(&command,result,smart_last_failure_brief,_countof(smart_last_failure_brief));
                const wchar_t *prior_direction=!wcscmp(command.arg[4],L"POS")?L"POS":L"NEG";
                command=alternative;
                error=part_execute(&command,&fresh,replacement,PART_TEXT_MAX);
                const int restored=error&&error!=PRO_TK_USER_ABORT&&!smart_unsafe_receipt(replacement)&&
                    smart_cad_recovered(&command,&fresh,&evidence,error);
                result[0]=0;
                part_text_add(&feedback,L"第一次 %ls 切向未切掉板料，原件已复核。已用同一闭合草绘和相同尺寸尝试 %ls 切向。\n%ls",
                    prior_direction,command.arg[4],replacement);
                if(error)part_text_add(&feedback,L"\n第一次原生回执：%.1200ls",original);
                checked_recovery=restored;
            }
            part_snapshot_clear(&fresh);
        }
    }
    if(error&&four_holes){
        PartCommand alternative={0};
        double width,height;
        if(smart_four_alternative(&command,&request_snapshot,error,result,&alternative,&width,&height)&&
           smart_protocol_allowed(&alternative)&&!smart_protocol_seen(&alternative)){
            static wchar_t replacement[PART_TEXT_MAX];
            const double original_x=as_arg(&command,3),original_y=as_arg(&command,4);
            smart_protocol_remember(&command,error);
            smart_stage_failed(SMART_STAGE_WRITE);
            command=alternative;
            error=part_execute(&command,&request_snapshot,replacement,_countof(replacement));
            result[0]=0;
            part_text_add(&feedback,L"原孔距 %g × %g mm 未通过板材校验。"
                L"已按实测 %.8g × %.8g mm 板面检查板边、原有开口和完整板厚，"
                L"改用 %.8g × %.8g mm 的居中孔距并保持孔径不变。\n%ls",
                original_x,original_y,width,height,as_arg(&command,3),as_arg(&command,4),replacement);
            trace("BWTL native four-hole pitch replanned",error);
        }
    }
    int readback_failed=0;
    if(!error&&evidence.track){
        static wchar_t verification[1700];
        verification[0]=0;
        PartText check={verification,_countof(verification),0};
        error=smart_cad_verify_result(&command,&request_snapshot,&evidence,&check);
        if(error){
            swprintf_s(result,_countof(result),L"原生回读未能确认本步验收结果。%ls",verification);
            readback_failed=1;
        }else part_text_add(&feedback,L"%ls",verification);
    }
    if(checked_recovery&&error&&!smart_local_plan&&!smart_readonly&&smart_intent_writable(smart_intent)){
        final=0;
        smart_cad_retry_references(&command,&request_snapshot,&evidence,&feedback);
    }
    const int failure_observed=!independent&&!readback_failed&&
        smart_observe_failed_model_action(&command,&request_snapshot,error,checked_recovery,&feedback);
    if(failure_observed)final=0;
    if(evidence.track||failure_observed||(!error&&smart_cad_pending_count&&smart_retryable_model_action(&command)))
        smart_note_cad_pending(&command,request_snapshot.model,error);
    if(smart_cad_pending_count&&!smart_local_plan&&!smart_readonly&&smart_intent_writable(smart_intent)&&
       !independent&&command.verb!=PART_DISPLAY&&quality!=AX_Q_REPORT)final=0;
    trace("PERF native_execute_ms",(int)(GetTickCount64()-execution_started));

    if(error&&foundation&&cm_kind(verb)==BWTL_CM_TRAY&&error==PRO_TK_BAD_INPUTS)final=0;
    int progress=command.verb==PART_COPY&&!error,unsafe=readback_failed;
    if(!independent&&command.verb!=PART_DISPLAY&&request_snapshot.stamp){
        if(error){
            ProMdl target=NULL;
            unsafe=unsafe||(!checked_recovery&&part_check_binding(&command,&request_snapshot,&target)!=PRO_TK_NO_ERROR);
            if(!final&&!readback_failed&&(checked_recovery||smart_sketch_rechecked(&command,&request_snapshot,&before,error)||
                        smart_smt_cut_rechecked(&command,&request_snapshot,&before,before_thickness,error))){
                target=request_snapshot.model;
                unsafe=!smart_binding_set(target,request_snapshot.window_model,request_snapshot.directory);
                if(!unsafe){
                    smart_committed_part=before;
                    if(!checked_recovery)part_text_add(&feedback,L"\n已复核活动零件、工作目录、已有特征和尺寸。保留已完成步骤，从当前零件继续。"
                        L"根据本轮实测数据选择不同参照或受支持的加工方式，继续尚未完成的操作。");
                }
            }else if(!unsafe&&!final&&(!smart_readonly||assembly_lookup)){
                unsafe=!smart_binding_set(target,request_snapshot.window_model,request_snapshot.directory);
            }
        }else{
            ProMdl target=request_snapshot.model;
            ProWVerstamp now=NULL;
            if(write&&command.verb!=PART_SAVE&&command.verb!=PART_STEP){
                if(ProMdlVerstampGet(target,&now))unsafe=1;
                else{progress=!ProVerstampEqual(request_snapshot.stamp,now);ProVerstampFree(&now);}
            }
            if(!unsafe&&!final&&(!smart_readonly||assembly_lookup))
                unsafe=!smart_binding_set(target,request_snapshot.window_model,request_snapshot.directory);
        }
    }
    if(four_holes&&!error&&progress&&!unsafe){
        smart_four_verified_target=request_snapshot.model;
        smart_four_verified_diameter=as_arg(&command,2);
    }
    if(!error&&progress&&!unsafe&&!final&&request_snapshot.type==PRO_MDL_PART&&
       request_snapshot.model==smart_target_model&&request_snapshot.model==request_snapshot.window_model)
        smart_committed_part=smart_part_signature(request_snapshot.model);
    if((quality_report_flow||failure_observed)&&!unsafe&&smart_binding_valid())smart_target_model=request_snapshot.model;
    if(!result[0])swprintf_s(result,_countof(result),L"原生操作 %ls 返回 Creo %d。",verb,error);
    smart_step(&command,verb,error,result,progress,final,unsafe);
}
