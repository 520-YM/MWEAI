static int smart_resolve_intent(const wchar_t *answer){
    if(smart_intent!=SMART_INTENT_PENDING)return 1;
    int mode=smart_parse_intent(answer);
    if(mode<=SMART_INTENT_PENDING){
        if(!smart_intent_retry++){
            wcscpy_s(smart_feedback,PART_TEXT_MAX,L"上次未收到唯一的本轮意图标记。请根据最新指令和模型上下文重新判断并返回对应标记。");
            smart_queue_next();
        }else smart_pause(L"无法确认本轮要修改当前模型、另建模型还是只看方案",1);
        return 0;
    }
    if(!smart_intent_retry&&chat_intent_needs_review(mode)){
        smart_intent_retry++;
        wcscpy_s(smart_feedback,PART_TEXT_MAX,L"上一条只给了讨论或读取。请重新核对最新消息是否明确要求执行，并沿用已确定的模型与参数。");
        smart_queue_next();
        return 0;
    }
    if(mode==SMART_INTENT_NEW&&(smart_progress||wcsstr(smart_goal,L"不新建")||wcsstr(smart_goal,L"不要新建")||
       wcsstr(smart_goal,L"不要另建")||wcsstr(smart_goal,L"别另建"))){
        smart_pause(L"本轮要求保留当前任务目标，已拦截另建",1);
        return 0;
    }
    if(mode==SMART_INTENT_CURRENT&&(!request_snapshot.model||!request_snapshot.stamp)){
        smart_pause(L"你要修改现有模型，请先激活对应的 Creo 零件或装配",1);
        return 0;
    }
    smart_intent=mode;
    smart_readonly=!smart_intent_writable(mode);
    if(mode==SMART_INTENT_CURRENT){
        request_requires_current=1;
        smart_target_model=request_snapshot.model;
    }else if(mode==SMART_INTENT_NEW){
        request_requires_current=0;
        smart_target_model=NULL;
    }
    trace("Semantic task intent",mode);
    return 1;
}

static int smart_replan_output(const wchar_t *reason){
    if(smart_readonly&&smart_intent!=SMART_INTENT_PENDING)return 0;
    if(smart_target_model&&smart_steps&&!smart_binding_valid())return 0;
    if(smart_protocol_replan>=2)return 0;
    smart_protocol_replan++;
    wcsncpy_s(smart_feedback,PART_TEXT_MAX,reason,_TRUNCATE);
    smart_queue_next();
    return 1;
}

static void ai_poll(char *dialog,ProUITimerID timer,ProAppData data){
    static wchar_t answer[16000],visible[16000];
    if(!ai_process)return;
    int state=resident_answer(answer,_countof(answer));
    if(!state){
        if(GetTickCount64()-ai_started>1200000){
            ai_cleanup();smart_pause(L"Codex 本轮响应超时，已请求停止",1);return;
        }
        if(resident_take_tool()&&ProMacroLoad(L"~ Command \x0060" L"BWTLResidentTool\x0060;")){
            resident_tool_queued=0;
            resident_tool_inflight=1;
            resident_tool_result(-1,L"Creo 无法调度本条工具调用。");
        }
        if(!native_schedule_poll()){ai_cleanup();smart_reply(L"回复刷新失败。");}
        return;
    }
    if(state<0){smart_pause(L"Codex 连接中断或本轮失败，已完成的 Creo 步骤保留",1);return;}
    smart_answer_text(answer,visible,16000);
    int routing=smart_intent==SMART_INTENT_PENDING;
    if(!smart_resolve_intent(answer))return;
    if(routing&&smart_intent_catalog(smart_intent)){
        wchar_t *report=catalog_message(smart_intent);
        smart_reply(report?report:catalog_read_failed);
        free(report);
        return;
    }
    if(smart_intent==SMART_INTENT_CHAT){
        smart_reply(visible[0]?visible:L"这条消息没有收到完整答复，请再发一次。");
        return;
    }
    if(smart_intent==SMART_INTENT_CLARIFY){
        smart_waiting_for_user=1;
        smart_reply(visible[0]?visible:L"请说明要处理的模型和具体改动。");
        return;
    }
    if(!resident_used_tool){
        PartCommand action={0};int parsed=part_command_parse(answer,&action);
        if(routing&&!(parsed==1&&(action.verb==PART_IMITATE||action.verb==PART_IMITATE_SIZE||action.verb==PART_IMITATE_LOCAL)&&smart_intent==SMART_INTENT_NEW)){
            smart_queue_next();
            return;
        }
        if(parsed<0){pending_plan.ready=0;memset(&pending_part,0,sizeof(pending_part));save_pending_plan();if(!smart_replan_output(L"上一条协议格式有误或含多条命令。重新根据最新结果给出一条合法的下一步。"))smart_pause(L"AI 返回无效或多条操作，本步未执行",1);return;}
        if(parsed==1){
            ProError queued;
            if(!cad_reference_output_allowed(&action)){
                if(!smart_replan_output(L"本轮没有授权保存或导出，请按原需求完成建模或核对现状。"))
                    smart_reply(L"已保留会话中实际完成的步骤。需要保存或导出时由用户指定。");
                return;
            }
            if(!smart_protocol_allowed(&action)){
                const int delete_block=!smart_readonly&&!wcscmp(action.arg[0],L"FEATURE_DELETE");
                const wchar_t *reason=delete_block?smart_delete_block_message:smart_operation_block();
                if(!delete_block&&smart_replan_output(reason))return;
                smart_reply(reason);return;
            }
            pending_plan.ready=0;save_pending_plan();pending_part=action;
            queued=ProMacroLoad(L"~ Command `CodexNativeBuildPending`;");
            if(queued){memset(&pending_part,0,sizeof(pending_part));smart_pause(L"原生操作调度失败，本步未执行",1);}return;
        }
    }
    if(!resident_used_tool){
        int change=model_plan_update(answer,&pending_plan);
        if(change<0){save_pending_plan();smart_pause(L"AI 返回无效的新模型方案，本步未执行",1);return;}
        if(change==1){
            if(quick_intent(smart_goal,quick_topic(smart_goal))){pending_plan.ready=0;save_pending_plan();smart_reply(L"此整件需要对应的快速生成参数。未创建基础替代件。");return;}
            if(!smart_new_plan_allowed()){
                pending_plan.ready=0;save_pending_plan();
                smart_reply(smart_readonly?smart_write_block_message:smart_progress?
                    L"已创建本轮模型，后续操作应在新模型上完成。":
                    L"本轮要处理已指定的现有模型。AI 返回了另建指令，Creo 未执行。");
                return;
            }
            ProError queued;
            queued=ProMacroLoad(L"~ Command `CodexNativeBuildPending`;");if(queued){pending_plan.ready=0;smart_reply(L"无法调度原生建模。");}return;
        }
    }
    if(smart_handle_stale_permission(visible))return;
    int answer_state=smart_answer_state(answer);
    smart_waiting_for_user=!smart_readonly&&(answer_state==1||(!answer_state&&smart_reply_needs_input(visible)));
    if(smart_waiting_for_user&&smart_missing_capability_needs_check(visible)&&
       smart_replan_output(L"当前还没有实际工具回执。请先调用已登记的读取工具，核对当前模型、库目录或适用协议，拿到证据后再说明缺少的条件。")){
        smart_waiting_for_user=0;
        return;
    }
    if(smart_waiting_for_user&&smart_cad_pending_count&&!smart_recovery_reviewed&&
       smart_replan_output(L"先依据当前 Creo 快照、已确认的参照和失败回执核对剩余步骤。"
           L"能核实就直接按另一种有依据的方法处理；只有真实目标或必要参数仍然缺少时才向用户提问。")){
        smart_recovery_reviewed=1;
        smart_waiting_for_user=0;
        return;
    }
    if(smart_four_task_unverified()){
        smart_waiting_for_user=0;
        if(smart_progress){
            smart_pause(L"已保留本次建模。还没有核实所要求的四个居中通孔，为避免重复打孔已暂停",1);
            return;
        }
        if(smart_replan_output(L"还没有确认用户要的四个居中通孔。"
           L"按实测板面和原有开口选孔距，保持用户指定的孔径，调用 SMT_FLAT4 并以 Creo 回读为准。"))return;
        smart_pause(L"还没有取得四个居中通孔的成功回执",1);
        return;
    }
    if(!smart_waiting_for_user&&(smart_failures_without_progress||smart_needs_native_result(0))){
        if(smart_replan_output(L"任务缺少完成回执。先核对现有模型或选中的参照及最近的失败，再从未完成部分继续。确认原操作未写入或已恢复后，换用有实测依据的不同参照或方法。只有缺少当前模型与回执均无法确定的必要数据时才向用户提问。"))return;
        smart_pause(L"未取得目标操作的成功回执，下面保留已完成步骤和原生原因",1);
        return;
    }
    smart_reply(visible[0]?visible:smart_feedback[0]?smart_feedback:L"本轮未收到可显示的结果，请重新发送需求。");
}
