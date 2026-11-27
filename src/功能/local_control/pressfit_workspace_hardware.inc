/* Recommendations use actual Creo geometry and the revisioned company library.
   These paths check geometry only and never add occurrences. */
typedef struct {
    const PressfitAcceptance *row;
    double nominal;
    int rank;
} WorkspacePressfitOption;

static int workspace_pressfit_instance_length(const wchar_t *instance){
    const wchar_t *digits=wcsrchr(instance,L'X');
    if(!digits)digits=wcsrchr(instance,L'x');
    return digits?pressfit_bso_size(digits+1):0;
}

static ProError workspace_pressfit_check_candidate(const GroupPressfitGeometry *geometry,const GroupPressfitHole *hole,
    ProMdl model,const PressfitAcceptance *accepted,PartText *out){
    BsoFitting fitting={0};
    const wchar_t *stage=L"读取定位径与承压面";
    ProError error=fastener_pressfit_fitting_for_hole(model,geometry->mm,accepted?accepted->shaft:0,
        accepted?accepted->seat:0,hole->diameter,&fitting,out);
    AxComponent panel=geometry->panel;
    BsoSite site={0};
    if(!error)error=fastener_pressfit_target(&panel,hole->hole_id,hole->face_id,geometry->mm,
        model,&fitting,hole->diameter,0,&site,&stage);
    if(error)part_text_add(out,L"「%ls」未通过（Creo %d）。\n",stage,error);
    return error;
}

static int workspace_pressfit_options(const wchar_t *library,const wchar_t *thread,const wchar_t *kind,
    const PressfitSelector *requested,double hole,double sheet,WorkspacePressfitOption options[12]){
    int count=0,verified=0;
    wchar_t source[MAX_PATH];
    const wchar_t *last=NULL;
    for(size_t k=0;k+1<_countof(pressfit_acceptance_rows);k++){
        const PressfitAcceptance *row=&pressfit_acceptance_rows[k];
        if(!pressfit_kind_matches(row->kind,kind)||!pressfit_thread_matches(row->instance,thread))continue;
        if(requested&&requested->generic[0]){
            ProName family;
            if(!pressfit_acceptance_family(row,family)||_wcsicmp(family,requested->generic)||
               pressfit_match_name(row->instance,requested->instance,requested->instance_key))continue;
        }
        const int same_trial=pressfit_acceptance_equal(hole,row->trial_hole_mm);
        const int same_nominal=pressfit_acceptance_equal(hole,row->library_hole_mm);
        if(!same_trial&&!same_nominal)continue;
        if(!last||_wcsicmp(last,row->source)){
            verified=pressfit_acceptance_current_source(row,library,source);
            last=row->source;
        }
        if(!verified)continue;
        const double nominal=pressfit_acceptance_nominal(row,source);
        const int known_hole=pressfit_acceptance_equal(hole,nominal);
        if(!known_hole&&!same_trial)continue;
        const int length=workspace_pressfit_instance_length(row->instance);
        const int desired=requested?(requested->bso_length?requested->bso_length:
            workspace_pressfit_instance_length(requested->instance)):0;
        const int score=(known_hole?0:10000)+(pressfit_acceptance_equal(sheet,row->trial_thickness_mm)?0:1000)+
            (length&&desired?abs(length-desired):0);
        int at=count;
        while(at&&score<options[at-1].rank)at--;
        if(at>=12)continue;
        if(count<12)count++;
        for(int j=count-1;j>at;j--)options[j]=options[j-1];
        options[at]=(WorkspacePressfitOption){row,nominal,score};
    }
    return count;
}

static ProError workspace_pressfit_recommend(ProMdl assembly,int panel,int hole_number,
    const wchar_t *thread,const wchar_t *kind,const wchar_t *requested,PartText *out){
    ProError error=default_part_active(assembly,-1,NULL);
    if(error)return error;
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    error=group_pressfit_geometry(assembly,panel,geometry,out);
    if(error)goto done;
    if(hole_number<1||hole_number>geometry->count){
        part_text_add(out,L"孔位 %d 超出当前板的圆形通孔范围（1–%d）。\n",hole_number,geometry->count);
        error=PRO_TK_BAD_INPUTS;
        goto done;
    }
    const GroupPressfitHole *hole=&geometry->holes[hole_number-1];
    const double sheet=pressfit_chat_panel_thickness(geometry->panel.model);
    part_text_add(out,L"组件 ID:%d 第 %d 号孔，实测 Ø%.6g mm，通孔深 %.6g mm，",
        geometry->panel_id,hole_number,hole->diameter,hole->depth);
    if(sheet>0)part_text_add(out,L"钣金板厚 %.6g mm。\n",sheet);
    else{
        part_text_add(out,L"无法读取原生板厚。\n");error=PRO_TK_BAD_CONTEXT;goto done;
    }

    PressfitSelector choice={0};
    const int exact=requested&&*requested;
    if(exact){
        wchar_t report[3700]=L"",source[MAX_PATH];
        PartText detail={report,_countof(report),0};
        ProMdl model=NULL;
        if(!pressfit_selector(requested,&choice)){error=PRO_TK_BAD_INPUTS;goto done;}
        error=pressfit_read_exact(requested,&model,source,&detail);
        const PressfitAcceptance *accepted=error?NULL:pressfit_acceptance_lookup(model,source);
        if(!error)error=workspace_pressfit_check_candidate(geometry,hole,model,accepted,&detail);
        if(!error){
            part_text_add(out,L"指定型号可在此孔进行 Creo 定位。%ls",report);
            pressfit_acceptance_describe(model,source,hole->diameter,sheet,out);
            goto done;
        }
        part_text_add(out,L"指定型号 %ls 当前不可用（Creo %d）。%ls",requested,error,report);
        if(error==PRO_TK_CANT_OPEN||error==PRO_TK_E_FOUND)goto done;
        if(!choice.generic[0]&&(!kind||!*kind)&&(!thread||!*thread)){
            part_text_add(out,L"请指定原件族表，或给出同类紧固件类别和螺纹，便于核对替代件。\n");
            goto done;
        }
    }

    wchar_t library[MAX_PATH];
    if(!company_fastener_resolve_report(COMPANY_OPEN_FASTENER_FIRST+4,library,out)){
        error=PRO_TK_CANT_OPEN;goto done;
    }
    WorkspacePressfitOption options[12]={0};
    const int count=workspace_pressfit_options(library,thread,kind,exact?&choice:NULL,hole->diameter,sheet,options);
    int shown=0;
    for(int k=0;k<count&&shown<5;k++){
        const PressfitAcceptance *accepted=options[k].row;
        wchar_t selector[MAX_PATH],source[MAX_PATH],report[2600]=L"";
        if(!pressfit_acceptance_selector(accepted,selector))continue;
        PartText detail={report,_countof(report),0};
        ProMdl model=NULL;
        if(pressfit_read_exact(selector,&model,source,&detail)||
           workspace_pressfit_check_candidate(geometry,hole,model,accepted,&detail))continue;
        part_text_add(out,L"%ls%ls · %ls · 通过此孔的 Creo 尺寸与承压面检查",
            shown?L"":L"可选的精确规格\n",selector,pressfit_kind_name(accepted->kind));
        if(options[k].nominal>0.0)part_text_add(out,L"，目录底孔 Ø%.6g",options[k].nominal);
        else part_text_add(out,L"，正式底孔待核实");
        part_text_add(out,L"，试板 %.6g mm%ls。\n",accepted->trial_thickness_mm,
            pressfit_acceptance_equal(sheet,accepted->trial_thickness_mm)?L"（同板厚）":L"（板厚不同）");
        shown++;
    }
    if(shown){
        part_text_add(out,L"以上为 Creo 装配候选。请按公司工艺核对底孔、板厚范围和工具净空后，明确点选规格再装。\n");
        error=PRO_TK_NO_ERROR;
    }else{
        part_text_add(out,L"现行库里暂无同时通过实际孔径和承压面检查的可核实型号。\n");
        error=PRO_TK_E_NOT_FOUND;
    }
done:
    part_text_add(out,L"本次仅查询，模型未写入。\n");
    free(geometry);
    return error;
}

/* The sequence accepts only named PCB and fastener plans with exact local
   model names and native references. Group placeholders are resolved from
   verified group records after the press-fit stage completes. */
static int workspace_pressfit_group_token(const wchar_t *token,wchar_t name[24],int *position){
    if(wcsncmp(token,L"GROUP:",6))return 0;
    const wchar_t *p=token+6;
    if(!grouped_pressfit_name(&p,name)||*p!=L':'||!ax_int_range(p+1,1,GROUP_PRESSFIT_MAX_HOLES))
        return 0;
    *position=(int)wcstol(p+1,NULL,10);
    return 1;
}

static int workspace_pressfit_stack_args(const PartCommand *cmd){
    if(cmd->count!=17||wcscmp(cmd->arg[0],L"GROUP_SCREW_STACK")||
       !dv_safe_leaf(cmd->arg[9])||!dv_safe_leaf(cmd->arg[12])||
       !qa_number(cmd->arg[16],0.001,1000))return 0;
    int ids[AX_PATH_DEPTH],position;
    wchar_t name[24];
    if(wcscmp(cmd->arg[2],L"%PCB%")&&!ax_path_parse(cmd->arg[2],ids))return 0;
    if(!ax_path_parse(cmd->arg[6],ids)&&!workspace_pressfit_group_token(cmd->arg[6],name,&position))return 0;
    for(int i=3;i<=15;i++)if(i!=6&&i!=9&&i!=12&&
        (wcsncmp(cmd->arg[i],L"SURFACE:",8)||!ax_ref_token(cmd->arg[i])))return 0;
    return 1;
}

static int workspace_pressfit_stack_parse_op(ProMdl assembly,const wchar_t *text,int electronics,PartCommand *out){
    const size_t count=wcsnlen(text,2600);
    ProMdlName actual;
    if(!count||count>=2600||wcspbrk(text,L"\r\n")||!part_tokens(text,count,out)||
       out->count<3||ProMdlMdlnameGet(assembly,actual)||
       (_wcsicmp(out->arg[1],L"A")&&_wcsicmp(out->arg[1],actual)))return 0;
    wcscpy_s(out->arg[1],PART_TOKEN_MAX,actual);
    if(!electronics&&!wcscmp(out->arg[0],L"GROUP_SCREW_STACK"))
        return workspace_pressfit_stack_args(out);
    const int kind=ax_kind(out->arg[0]);
    if(electronics?kind!=AX_PCB_MOUNT:kind!=AX_FASTENER_STACK)return 0;
    if(out->count!=(electronics?15:16))return 0;
    PartCommand checked=*out;
    if(electronics){
        const int indices[]={2,5};
        for(int k=0;k<_countof(indices);k++){
            wchar_t name[24];
            int index;
            if(workspace_pressfit_group_token(checked.arg[indices[k]],name,&index))
                wcscpy_s(checked.arg[indices[k]],PART_TOKEN_MAX,L"ID:1");
        }
    }else if(!wcscmp(checked.arg[2],L"%PCB%"))wcscpy_s(checked.arg[2],PART_TOKEN_MAX,L"ID:1");
    return assembly_plan_valid(checked.count,checked.arg)==1;
}

static ProError workspace_pressfit_resolve_group(ProMdl assembly,int panel,wchar_t token[PART_TOKEN_MAX],PartText *out){
    wchar_t name[24];
    int location;
    if(!workspace_pressfit_group_token(token,name,&location))return PRO_TK_NO_ERROR;
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=grouped_pressfit_open(assembly,panel,&record);
    if(!error)error=grouped_pressfit_bind(assembly,&record,geometry,out);
    const int index=error?-1:grouped_pressfit_find(&record,name);
    if(!error&&(index<0||record.groups[index].pending[0]||location>record.groups[index].count))
        error=PRO_TK_E_NOT_FOUND;
    GroupedPressfitView view={0};
    if(!error)error=grouped_pressfit_read_view(assembly,geometry,&record.groups[index],1,&view,out);
    if(!error&&swprintf_s(token,PART_TOKEN_MAX,L"ID:%d",view.ids[location-1])<0)error=PRO_TK_OUT_OF_RANGE;
    if(error)part_text_add(out,L"%ls 第 %d 个位置没有可核对的已安装实例（Creo %d）。\n",name,location,error);
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}

static ProError workspace_pressfit_fixed(ProMdl assembly,ProAsmcomp *feature){
    ProAsmcomppath root;
    ProAsmcompconstraint *rows=NULL;
    int count=0;
    ProAsmcompConstrType type;
    ProBoolean packed=PRO_B_TRUE,under=PRO_B_TRUE;
    ProError error=ax_root_path(assembly,&root);
    if(!error)error=ax_read_constraints(feature,&root,&rows,&count);
    if(!error&&count!=1)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProAsmcompconstraintTypeGet(rows[0],&type);
    if(!error)error=ProAsmcompIsPackaged(feature,&packed);
    if(!error)error=ProAsmcompIsUnderconstrained(feature,&under);
    if(!error&&(type!=PRO_ASM_FIX||packed||under))error=PRO_TK_BAD_CONTEXT;
    if(rows)ProAsmcompconstraintArrayFree(rows);
    return error;
}

static ProError workspace_pressfit_existing(ProMdl assembly,AxBatchItem *planned,int count,double mm,int ids[2]){
    AxScan *scan=NULL;
    ProError error=ax_scan(assembly,0,&scan);
    if(!error&&(scan->limited||scan->unavailable))error=PRO_TK_INCOMPLETE;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *found=&scan->occurrences[i];
        if(found->depth!=1||found->state!=PRO_FEAT_ACTIVE||found->type!=PRO_MDL_PART)continue;
        for(int j=0;!error&&j<count;j++){
            if(found->model!=planned[j].model)continue;
            ProAsmcomp child;
            ProMatrix pose;
            error=ax_component_feature(assembly,found->ids[0],&child);
            if(!error)error=ax_actual_pose(assembly,&child,pose);
            if(error||!ax_same_pose(planned[j].pose,pose,mm))continue;
            if(ids[j])error=PRO_TK_E_FOUND;
            if(!error)error=workspace_pressfit_fixed(assembly,&child);
            if(!error)ids[j]=child.id;
        }
    }
    free(scan);
    return error;
}

/* Derive the same expected PCB transform from the two specified supports.
   This readback verifies the instance and makes resubmission safe. */
static ProError workspace_pressfit_pcb(ProMdl assembly,const PartCommand *cmd,double mm,int *id,PartText *out){
    AxComponent support[2]={0},board={0};
    AxHardwareSite target[2]={0},source[2]={0};
    ProError error=PRO_TK_NO_ERROR;
    for(int i=0;!error&&i<2;i++){
        const int pos=i?5:2;
        error=ax_hw_occurrence(assembly,cmd->arg[pos],mm,&support[i]);
        if(!error)error=ax_hw_site(&support[i],cmd->arg[pos+2],cmd->arg[pos+1],mm,&target[i]);
        if(!error)error=ax_hw_world(&support[i],&target[i]);
    }
    int loaded=0;
    if(!error)error=ax_batch_model(cmd->arg[8],mm,&board.model,&loaded);
    for(int i=0;!error&&i<2;i++){
        error=ax_hw_site(&board,cmd->arg[10+i],cmd->arg[9],mm,&source[i]);
        if(!error&&(!source[i].bore||(!target[i].bore&&
           source[i].cylinder.radius+0.0005/mm<target[i].cylinder.radius)))error=PRO_TK_BAD_CONTEXT;
    }
    if(error)goto done;
    AxDeviceFrame a,b;
    double target_pitch,source_pitch;
    error=ax_hole_pair_frame(target,&a,&target_pitch);
    if(!error)error=ax_hole_pair_frame(source,&b,&source_pitch);
    if(!error&&fabs(target_pitch-source_pitch)*mm>0.01)error=PRO_TK_BAD_CONTEXT;
    if(error)goto done;
    for(int i=0;i<3;i++){a.axis[1][i]=-a.axis[1][i];a.axis[2][i]=-a.axis[2][i];}
    AxBatchItem item={.model=board.model};
    ax_device_align(&b,&a,item.pose);
    error=ax_pcb_height(assembly,cmd,&item,mm,out);
    int current[2]={0};
    if(!error)error=workspace_pressfit_existing(assembly,&item,1,mm,current);
    if(error)goto done;
    AxDevicePlane mounting={0};
    memcpy(mounting.point,target[0].contact,sizeof(ProVector));
    memcpy(mounting.normal,target[0].plane.direction,sizeof(ProVector));
    AxComponent *second=ax_same_occurrence(support,support+1)?NULL:support+1;
    if(!current[0]){
        error=ax_device_create(assembly,&item,1,mm,support,&mounting,second,&mounting,out);
        if(!error)*id=item.feature.id;
    }else{
        double interference;
        item.feature.id=current[0];
        error=ax_device_fit(assembly,&item,support,&mounting,mm,&interference);
        if(!error&&interference>0.01)error=PRO_TK_BAD_CONTEXT;
        if(!error&&second){
            error=ax_device_fit(assembly,&item,second,&mounting,mm,&interference);
            if(!error&&interference>0.01)error=PRO_TK_BAD_CONTEXT;
        }
        if(!error)*id=current[0];
    }
    if(!error)part_text_add(out,L"PCB 实例 ID:%d，两支撑位置与 PCB 实测孔距 %.6g mm 相符，间距和接合干涉检查通过。\n",
        *id,target_pitch*mm);
done:
    if(error&&loaded&&!ax_recovery_uncertain&&!(*id))ProMdlErase(board.model);
    return error;
}

static ProError workspace_pressfit_pair_rings(const AxHardwareSite *front,const AxHardwareSite *under,
    const AxHardwareSite *upper,const AxHardwareSite *shaft,AxComponent *pcb,AxComponent *washer,
    AxComponent *screw,const PartCommand *cmd,double mm){
    int pcb_face,washer_under,washer_top,screw_face;
    if(!local_control_pressfit_surface(cmd->arg[4],&pcb_face)||
       !local_control_pressfit_surface(cmd->arg[14],&washer_under)||
       !local_control_pressfit_surface(cmd->arg[15],&washer_top)||
       !local_control_pressfit_surface(cmd->arg[11],&screw_face))return PRO_TK_BAD_INPUTS;
    const double pad_radius=fmax(front->cylinder.radius,under->cylinder.radius)*mm+0.02;
    const double head_radius=fmax(upper->cylinder.radius,shaft->cylinder.radius)*mm+0.02;
    AxHardwareSite local={0};
    ProError error=ax_hw_site(pcb,cmd->arg[3],cmd->arg[4],mm,&local);
    if(!error)error=fastener_pressfit_ring(pcb->model,pcb_face,&local,pad_radius,mm);
    if(!error)error=fastener_pressfit_ring(washer->model,washer_under,under,pad_radius,mm);
    if(!error)error=fastener_pressfit_ring(washer->model,washer_top,upper,head_radius,mm);
    if(!error)error=fastener_pressfit_ring(screw->model,screw_face,shaft,head_radius,mm);
    return error;
}

#include "pressfit_direct_thread.inc"

static ProError workspace_pressfit_screw_pair(ProMdl assembly,const PartCommand *cmd,double mm,PartText *out){
    AxComponent pcb={0},receiver={0},screw={0},washer={0};
    AxHardwareSite front={0},back={0},entry={0},shaft={0},under={0},upper={0};
    AxBatchItem washer_instance={0},screw_instance={0};
    int loaded_screw=0,loaded_washer=0,new_screw=0,new_washer=0;
    double thickness=0;
    ProError error=ax_recovery_uncertain?PRO_TK_BAD_CONTEXT:part_health(assembly);
    if(!error)error=ax_hw_occurrence(assembly,cmd->arg[2],mm,&pcb);
    if(!error)error=ax_hw_slab(&pcb,cmd->arg[3],cmd->arg[4],cmd->arg[5],mm,&front,&back,&thickness);
    if(!error)error=ax_hw_occurrence(assembly,cmd->arg[6],mm,&receiver);
    if(!error&&ax_same_occurrence(&pcb,&receiver))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_hw_site(&receiver,cmd->arg[7],cmd->arg[8],mm,&entry);
    if(!error)error=ax_hw_world(&receiver,&entry);
    if(!error&&(!entry.bore||!ad_near(entry.contact,back.contact,0.001/mm)||
       ad_dot(entry.plane.direction,front.plane.direction)<1-1e-7))error=PRO_TK_BAD_CONTEXT;
    if(error){
        part_text_add(out,L"PCB 孔、背面和柱孔入口未能核对到同轴贴合（Creo %d）。\n",error);
        goto done;
    }

    error=ax_batch_model(cmd->arg[9],mm,&screw.model,&loaded_screw);
    if(!error)error=ax_batch_model(cmd->arg[12],mm,&washer.model,&loaded_washer);
    if(!error&&(screw.model==washer.model||screw.model==receiver.model||screw.model==pcb.model||
       washer.model==receiver.model||washer.model==pcb.model))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_hw_site(&screw,cmd->arg[10],cmd->arg[11],mm,&shaft);
    if(!error)error=ax_hw_site(&washer,cmd->arg[13],cmd->arg[14],mm,&under);
    if(!error)error=ax_hw_site(&washer,cmd->arg[13],cmd->arg[15],mm,&upper);
    if(error){
        part_text_add(out,L"不能核对螺钉圆杆、垫圈孔或承压面（Creo %d）。\n",error);
        goto done;
    }
    ProVector offset,inward;
    ad_sub(under.contact,upper.contact,offset);
    const double washer_thickness=ad_dot(offset,under.plane.direction)*mm;
    for(int i=0;i<3;i++)inward[i]=-front.plane.direction[i];
    double shaft_start,shaft_end,entry_start,entry_end;
    ax_hw_axial_span(&shaft,shaft.contact,shaft.plane.direction,mm,&shaft_start,&shaft_end);
    ax_hw_axial_span(&entry,entry.contact,inward,mm,&entry_start,&entry_end);
    if(!under.bore||!upper.bore||shaft.bore||washer_thickness<=0.001||
       ad_dot(under.plane.direction,upper.plane.direction)>-1+1e-7||
       fabs(shaft_start)>0.001||fabs(entry_start)>0.001||shaft_end<=0.001||entry_end<=0.001){
        part_text_add(out,L"垫圈方向、螺钉承压面或柱孔有效直段无法确认。\n");
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }

    DirectThread inner={0},outer={0};
    error=direct_thread_read(receiver.model,cmd->arg[7],1,mm,entry_end,&inner);
    if(!error)error=direct_thread_read(screw.model,cmd->arg[10],0,mm,shaft_end,&outer);
    if(error){
        part_text_add(out,L"%ls缺少与所选圆柱面绑定的可核对 ISO 螺纹规格和有效长度（Creo %d）。\n",
            inner.end>0?L"螺钉":L"柱孔",error);
        goto done;
    }
    if(fabs(inner.diameter-outer.diameter)>0.0001||fabs(inner.pitch-outer.pitch)>0.0001||inner.hand!=outer.hand){
        part_text_add(out,L"螺纹不匹配。柱孔 M%.6g×%.6g %ls，螺钉 M%.6g×%.6g %ls，本次未装入螺钉或垫圈。\n",
            inner.diameter,inner.pitch,inner.hand>0?L"右旋":L"左旋",
            outer.diameter,outer.pitch,outer.hand>0?L"右旋":L"左旋");
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }

    const double rod_radius=shaft.cylinder.radius*mm,bore_radius=entry.cylinder.radius*mm;
    if(2*rod_radius>outer.diameter+0.002||2*bore_radius>inner.diameter+0.002||
       2*rod_radius+0.002<2*bore_radius||rod_radius>
       fmin(front.cylinder.radius,under.cylinder.radius)*mm+0.001){
        part_text_add(out,L"螺钉杆径 Ø%.6g mm 与螺纹标称、柱孔或 PCB 和垫圈的实测通孔不符。\n",2*rod_radius);
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
    const double stack=thickness+washer_thickness;
    const double insertion=shaft_end-stack;
    const double minimum=as_arg(cmd,16);
    const double engagement=fmin(outer.end-stack,inner.end)-fmax(0,fmax(inner.start,outer.start-stack));
    if(insertion>=entry_end-0.001||engagement+0.001<minimum||
       (rod_radius>bore_radius+0.001&&engagement+0.002<insertion)){
        part_text_add(out,L"几何旋入 %.6g mm，有效螺纹啮合 %.6g mm，柱孔直段 %.6g mm，最低要求 %.6g mm。请核对螺钉长度、孔底余量或未攻牙段。\n",
            insertion,fmax(0,engagement),entry_end,minimum);
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
    error=workspace_pressfit_pair_rings(&front,&under,&upper,&shaft,&pcb,&washer,&screw,cmd,mm);
    if(error){
        part_text_add(out,L"板面、垫圈或螺钉头的实际承压环无法核实（Creo %d）。\n",error);
        goto done;
    }

    const DirectThreadPlacement washer_place={&pcb,&pcb,cmd->arg[3],cmd->arg[4],cmd->arg[13],cmd->arg[14],&under,&front};
    DirectThreadPosition washer_check={0},screw_check={0};
    error=direct_thread_occurrence(assembly,washer.model,&washer_place,mm,&washer_instance,&new_washer,&washer_check,out);
    if(error)goto done;
    wchar_t id[32];
    swprintf_s(id,_countof(id),L"ID:%d",washer_instance.feature.id);
    error=ax_hw_occurrence(assembly,id,mm,&washer);
    if(!error)error=ax_hw_site(&washer,cmd->arg[13],cmd->arg[15],mm,&upper);
    if(!error)error=ax_hw_world(&washer,&upper);
    if(error)goto done;
    const DirectThreadPlacement screw_place={&receiver,&washer,cmd->arg[7],cmd->arg[15],cmd->arg[10],cmd->arg[11],&shaft,&upper};
    error=direct_thread_occurrence(assembly,screw.model,&screw_place,mm,&screw_instance,&new_screw,&screw_check,out);
    if(error)goto done;

    AxHardwareSite actual_shaft=shaft;
    swprintf_s(id,_countof(id),L"ID:%d",screw_instance.feature.id);
    error=ax_hw_occurrence(assembly,id,mm,&screw);
    if(!error)error=ax_hw_world(&screw,&actual_shaft);
    if(error)goto done;
    double actual_start,actual_insertion;
    ax_hw_axial_span(&actual_shaft,entry.contact,inward,mm,&actual_start,&actual_insertion);
    ad_sub(actual_shaft.contact,entry.contact,offset);
    const double actual_stack=-ad_dot(offset,inward)*mm;
    const double actual_engagement=fmin(outer.end-actual_stack,inner.end)-fmax(0,fmax(inner.start,outer.start-actual_stack));
    const double radial=ax_hw_separation(actual_shaft.contact,entry.contact,inward,mm);
    if(fabs(actual_start+actual_stack)>0.001||radial>0.001||
       fabs(actual_insertion-insertion)>0.001||actual_engagement+0.001<minimum||
       actual_insertion>=entry_end-0.001){
        part_text_add(out,L"装后实际位置未通过孔轴或有效旋入深度复核。\n");
        error=PRO_TK_INCOMPLETE;
        goto done;
    }

    AxDevicePlane separator={0};
    memcpy(separator.point,front.contact,sizeof(ProVector));
    memcpy(separator.normal,front.plane.direction,sizeof(ProVector));
    double pcb_overlap=0,washer_overlap=0,receiver_overlap=0,washer_pcb_overlap=0,washer_receiver_overlap=0;
    error=ax_device_fit(assembly,&washer_instance,&pcb,&separator,mm,&washer_pcb_overlap);
    if(!error)error=ax_device_fit(assembly,&washer_instance,&receiver,&separator,mm,&washer_receiver_overlap);
    if(!error)error=ax_device_fit(assembly,&screw_instance,&pcb,&separator,mm,&pcb_overlap);
    if(!error)error=ax_device_fit(assembly,&screw_instance,&washer,&separator,mm,&washer_overlap);
    if(!error)error=ax_device_fit(assembly,&screw_instance,&receiver,&separator,mm,&receiver_overlap);
    const double thread_volume=AS_PI*fmax(0,rod_radius*rod_radius-bore_radius*bore_radius)*actual_engagement;
    if(!error&&(pcb_overlap>0.01||washer_overlap>0.01||washer_pcb_overlap>0.01||washer_receiver_overlap>0.01||
        !isfinite(receiver_overlap)||fabs(receiver_overlap-thread_volume)>fmax(0.01,0.002*thread_volume)))error=PRO_TK_BAD_CONTEXT;
    if(error){
        part_text_add(out,L"插入后的实体检查未通过（Creo %d）。螺钉与 PCB %.6g、螺钉与垫圈 %.6g、螺钉与柱孔 %.6g mm³，预计螺纹包络 %.6g mm³，垫圈与 PCB %.6g、垫圈与柱 %.6g mm³。\n",
            error,pcb_overlap,washer_overlap,receiver_overlap,thread_volume,washer_pcb_overlap,washer_receiver_overlap);
        goto done;
    }
    part_text_add(out,L"螺纹 ISO M%.6g×%.6g %ls核对通过。柱孔依据%ls，螺钉依据对应圆柱面的规格记录。\n",
        inner.diameter,inner.pitch,inner.hand>0?L"右旋":L"左旋",inner.native_hole?L"Creo 原生攻牙特征":L"已绑定的库件规格记录");
    part_text_add(out,L"垫圈 ID:%d、螺钉 ID:%d 均已读回原生同轴和端面贴合约束。螺钉轴偏差 %.6g mm，承压面间隙 %.6g mm。\n",
        washer_instance.feature.id,screw_instance.feature.id,radial,fabs(screw_check.seat));
    part_text_add(out,L"几何旋入 %.6g mm，有效螺纹啮合 %.6g mm，最低要求 %.6g mm，距柱孔直段末端 %.6g mm。\n",
        actual_insertion,actual_engagement,minimum,entry_end-actual_insertion);
    if(thread_volume>0.01)
        part_text_add(out,L"Creo 的螺纹简化实体重叠 %.6g mm³，与核对的啮合包络相符。PCB 和垫圈无异常重叠。\n",receiver_overlap);
done:
    if(error){
        if(new_screw)ax_discard_occurrence(assembly,&screw_instance.feature,out);
        if(new_washer&&!ax_recovery_uncertain)ax_discard_occurrence(assembly,&washer_instance.feature,out);
        if(!ax_recovery_uncertain){
            if(loaded_screw&&screw.model)ProMdlErase(screw.model);
            if(loaded_washer&&washer.model)ProMdlErase(washer.model);
        }
    }
    return error;
}

static ProError workspace_pressfit_direct(ProMdl assembly,const wchar_t *text,int panel,PartText *out){
    const wchar_t suffix[]=L"，保存到当前项目";
    size_t length=wcsnlen(text,2600);
    if(!length||length>=2600)return PRO_TK_BAD_INPUTS;
    wchar_t input[2600];
    wcscpy_s(input,_countof(input),text);
    while(length&&(iswspace(input[length-1])||wcschr(L"。！!",input[length-1])))input[--length]=0;
    const size_t tail=_countof(suffix)-1;
    const int save=length>tail&&!wcscmp(input+length-tail,suffix);
    if(save)input[length-tail]=0;
    PartCommand cmd={0};
    if(!workspace_pressfit_stack_parse_op(assembly,input,0,&cmd)||
       wcscmp(cmd.arg[0],L"GROUP_SCREW_STACK")||!wcscmp(cmd.arg[2],L"%PCB%")){
        part_text_add(out,L"请提供 GROUP_SCREW_STACK、已定位 PCB 的实例 ID、柱孔、螺钉、垫圈与最低啮合深度。\n");
        return PRO_TK_BAD_INPUTS;
    }
    double mm;
    ProError error=default_part_active(assembly,-1,NULL);
    if(!error)error=ax_master(assembly);
    if(!error)error=qa_mm_per_unit(assembly,&mm);
    if(!error)error=workspace_pressfit_resolve_group(assembly,panel,cmd.arg[6],out);
    if(!error)error=workspace_pressfit_screw_pair(assembly,&cmd,mm,out);
    if(!error&&save)error=grouped_pressfit_save(assembly,1,out);
    else if(!error)part_text_add(out,L"装配改动保留在当前 Creo 会话。\n");
    return error;
}

static ProError workspace_pressfit_stack(ProMdl assembly,const GroupPressfitPlan *plan,
    const wchar_t *electronics,const wchar_t *fasteners,PartText *out){
    PartCommand pcb={0},hardware={0};
    ProError error=default_part_active(assembly,-1,NULL);
    if(!error&&(!plan||!plan->group_count||plan->preview||
       !workspace_pressfit_stack_parse_op(assembly,electronics,1,&pcb)||
       !workspace_pressfit_stack_parse_op(assembly,fasteners,0,&hardware))){
        part_text_add(out,L"请提供分组孔位、PCB 的两柱定位指令和螺钉垫圈的实际参照。\n");
        error=PRO_TK_BAD_INPUTS;
    }
    double mm;
    if(!error)error=ax_master(assembly);
    if(!error)error=qa_mm_per_unit(assembly,&mm);
    if(error)return error;
    GroupPressfitPlan initial=*plan;
    initial.save=0;
    error=grouped_pressfit_install(assembly,&initial,out);
    if(error)return error;

    const int references[]={2,5};
    for(int i=0;!error&&i<_countof(references);i++)
        error=workspace_pressfit_resolve_group(assembly,plan->panel,pcb.arg[references[i]],out);
    int pcb_id=0;
    if(!error)error=workspace_pressfit_pcb(assembly,&pcb,mm,&pcb_id,out);
    if(error){
        part_text_add(out,L"分组压铆件已保留。PCB 定位未通过，请核对孔距、指定柱面和净高（Creo %d）。\n",error);
        return error;
    }
    if(!wcscmp(hardware.arg[2],L"%PCB%"))
        swprintf_s(hardware.arg[2],PART_TOKEN_MAX,L"ID:%d",pcb_id);
    if(!wcscmp(hardware.arg[0],L"GROUP_SCREW_STACK")){
        error=workspace_pressfit_resolve_group(assembly,plan->panel,hardware.arg[6],out);
        if(!error)error=workspace_pressfit_screw_pair(assembly,&hardware,mm,out);
    }else{
        wchar_t reply[3800]=L"";
        error=ax_execute(assembly,&hardware,reply,_countof(reply));
        if(reply[0])part_text_add(out,L"%ls",reply);
    }
    if(error){
        part_text_add(out,L"分组压铆件和 PCB 已保留，最后的紧固件定位未完成（Creo %d）。\n",error);
        return error;
    }
    return grouped_pressfit_save(assembly,plan->save,out);
}
