/* The role of each hole is recorded only after verified creation. A later
   instruction must resolve both groups again on the active native assembly. */
typedef struct {
    PressfitCheckpoint scope;
    PanelPressfitPlan plan;
    int panel,ids[6];
} PanelMultiContext;

typedef struct {
    int face,bores[6],ids[6];
    double mm;
    ProMatrix poses[6];
    ProMdl models[6];
} PanelMultiReadback;

static ProError panel_multi_scope(ProMdl assembly,PanelMultiContext *state){
    ProError error=default_part_active(assembly,-1,NULL);
    if(!error)error=pressfit_record_scope(assembly,&state->scope);
    if(error)return error;
    if(!storage_directory_on_drive(state->scope.workdir,L'E'))return PRO_TK_BAD_CONTEXT;
    wchar_t *suffix=wcsrchr(state->scope.file,L'.');
    if(!suffix||wcscpy_s(suffix,_countof(state->scope.file)-(suffix-state->scope.file),L".panel-six"))
        return PRO_TK_OUT_OF_RANGE;
    wchar_t guard[MAX_PATH];
    if(swprintf_s(guard,_countof(guard),L"%ls\\.%ls.bwtl-panel-six.lock",state->scope.workdir,state->scope.target)<0)
        return PRO_TK_OUT_OF_RANGE;
    state->scope.lock=CreateFileW(guard,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    return state->scope.lock==INVALID_HANDLE_VALUE?PRO_TK_E_IN_USE:PRO_TK_NO_ERROR;
}

static int panel_multi_write(const PanelMultiContext *state){
    wchar_t temporary[MAX_PATH];
    FILE *file=ax_study_atomic(state->scope.file,temporary);
    if(!file)return 0;
    const PanelPressfitPlan *p=&state->plan;
    fwprintf(file,L"BWTL-PANEL-SIX-1\n%ls\n%ls\n%ls\n%ls\n%d %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
        state->scope.workdir,state->scope.target,p->spec,p->center_spec,state->panel,p->length,p->width,p->thickness,
        p->hole,p->corner_x,p->corner_y,p->center_spacing);
    fwprintf(file,L"%d %d %d %d %d %d\n",state->ids[0],state->ids[1],state->ids[2],state->ids[3],state->ids[4],state->ids[5]);
    return ax_study_commit(file,temporary,state->scope.file);
}

static int panel_multi_read(PanelMultiContext *state){
    FILE *file=ax_learning_open(state->scope.file);
    if(!file)return 0;
    wchar_t line[MAX_PATH+3];
    PanelPressfitPlan *p=&state->plan;
    int used=0;
    int ok=pressfit_record_line(file,line,_countof(line))&&!wcscmp(line,L"BWTL-PANEL-SIX-1")&&
        pressfit_record_line(file,line,_countof(line))&&!_wcsicmp(line,state->scope.workdir)&&
        pressfit_record_line(file,line,_countof(line))&&!_wcsicmp(line,state->scope.target)&&
        pressfit_record_line(file,line,_countof(line))&&!wcscpy_s(p->spec,_countof(p->spec),line)&&
        pressfit_record_line(file,line,_countof(line))&&!wcscpy_s(p->center_spec,_countof(p->center_spec),line)&&
        pressfit_record_line(file,line,_countof(line))&&
        swscanf_s(line,L"%d %lf %lf %lf %lf %lf %lf %lf%n",&state->panel,&p->length,&p->width,&p->thickness,
            &p->hole,&p->corner_x,&p->corner_y,&p->center_spacing,&used)==8&&!line[used]&&
        state->panel>0&&isfinite(p->length)&&p->length>0&&isfinite(p->width)&&p->width>0&&
        isfinite(p->thickness)&&p->thickness>0&&isfinite(p->hole)&&p->hole>0&&
        isfinite(p->corner_x)&&p->corner_x>0&&isfinite(p->corner_y)&&p->corner_y>0&&
        isfinite(p->center_spacing)&&p->center_spacing>0&&pressfit_record_line(file,line,_countof(line))&&
        swscanf_s(line,L"%d %d %d %d %d %d%n",&state->ids[0],&state->ids[1],&state->ids[2],&state->ids[3],
            &state->ids[4],&state->ids[5],&used)==6&&!line[used]&&!fgetws(line,_countof(line),file)&&!ferror(file);
    fclose(file);
    if(!ok)return 0;
    for(int i=0;i<6;i++){
        if(state->ids[i]<1||state->ids[i]==state->panel)return 0;
        for(int j=0;j<i;j++)if(state->ids[i]==state->ids[j])return 0;
    }
    p->six=1;
    return !wcscpy_s(p->drawing,_countof(p->drawing),state->scope.target);
}

static ProError panel_multi_count(ProMdl assembly){
    AxScan *scan=NULL;
    ProError error=ax_scan(assembly,0,&scan);
    if(!error&&(scan->limited||scan->unavailable||scan->suppressed||scan->count!=7||scan->parts!=7))
        error=PRO_TK_BAD_CONTEXT;
    if(!error)for(int i=0;i<scan->count;i++)
        if(scan->occurrences[i].depth!=1||scan->occurrences[i].type!=PRO_MDL_PART||
           scan->occurrences[i].state!=PRO_FEAT_ACTIVE)error=PRO_TK_BAD_CONTEXT;
    free(scan);
    return error;
}

static ProError panel_multi_read_group(ProMdl assembly,const PanelMultiContext *state,const BsoSite *sites,
    int offset,int count,PanelMultiReadback *view,PartText *out){
    wchar_t details[1200]=L"",library[MAX_PATH]=L"";
    PartText report={details,_countof(details),0};
    ProMdl source=NULL;
    const wchar_t *selector=offset?state->plan.center_spec:state->plan.spec;
    ProError error=pressfit_read_exact(selector,&source,library,&report);
    ProMdlName source_name;
    if(!error)error=ProMdlMdlnameGet(source,source_name);
    if(error)return error;
    const size_t stem=wcsnlen(source_name,14);
    BsoFitting fitting={0};
    const PressfitAcceptance *catalog=pressfit_acceptance_lookup(source,library);
    error=fastener_pressfit_fitting_for_hole(source,view->mm,catalog?catalog->shaft:0,catalog?catalog->seat:0,
        state->plan.hole,&fitting,&report);
    PressfitOccupancy occupied[12]={0};
    if(!error)error=fastener_pressfit_scan_occupancy(assembly,source,&fitting,sites+offset,count,view->mm,occupied);
    for(int i=0;!error&&i<count;i++){
        const int target=offset+i;
        const PressfitOccupancy *row=&occupied[i];
        ProMdlName installed;
        if(row->count!=1||!row->paired||row->depth!=1||row->id<1||(error=ProMdlMdlnameGet(row->model,installed))||
           _wcsnicmp(source_name,installed,stem)||_wcsnicmp(installed+stem,L"_BWTL_",6)){
            error=PRO_TK_BAD_CONTEXT;
            break;
        }
        const int verified_same=i&&row->model==occupied[i-1].model;
        if(!verified_same)error=pressfit_verify_project(source,row->model,&report);
        BsoFitting loaded={0};
        if(!error)error=fastener_pressfit_fitting(row->model,view->mm,fitting.shaft,fitting.seat,&loaded,&report);
        PressfitSiteReceipt readback={0};
        if(!error)error=fastener_pressfit_recheck_added(assembly,row->model,&loaded,&sites[target],row->id,view->mm,&readback);
        ProAsmcomp child=row->feature;
        if(!error)error=ax_actual_pose(assembly,&child,view->poses[target]);
        if(!error){
            view->models[target]=row->model;
            view->ids[target]=row->id;
        }
    }
    if(error&&out)part_text_add(out,L"%ls孔组的实物规格、定位或占位复核未通过（Creo %d）。\n",
        offset?L"中间":L"四角",error);
    return error;
}

static ProError panel_multi_audit(ProMdl assembly,ProMdl expected_panel,const PanelMultiContext *state,
    PanelMultiReadback *view,PartText *out){
    ProError error=panel_multi_count(assembly);
    wchar_t key[32];
    swprintf_s(key,_countof(key),L"ID:%d",state->panel);
    if(!error)error=qa_mm_per_unit(assembly,&view->mm);
    AxComponent panel={0};
    if(!error)error=ax_hw_occurrence(assembly,key,view->mm,&panel);
    ProMdlName board;
    ProMdlsubtype subtype;
    if(!error)error=ProMdlMdlnameGet(panel.model,board);
    if(!error)error=ProMdlSubtypeGet(panel.model,&subtype);
    if(!error&&(_wcsicmp(board,state->scope.target)||subtype!=PROMDLSTYPE_PART_SHEETMETAL||
       (expected_panel&&expected_panel!=panel.model)))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=panel_six_sites(panel.model,&state->plan,view->bores,NULL);
    if(!error)error=panel_six_install_face(assembly,key,view->bores,6,&view->face);
    BsoSite sites[6]={0};
    for(int i=0;!error&&i<6;i++)error=fastener_bso_target(&panel,view->bores[i],view->face,view->mm,&sites[i]);
    if(!error)error=panel_multi_read_group(assembly,state,sites,0,4,view,out);
    if(!error)error=panel_multi_read_group(assembly,state,sites,4,2,view,out);
    if(!error)for(int i=0;i<6;i++)for(int j=0;j<i;j++)if(view->ids[i]==view->ids[j])error=PRO_TK_BAD_CONTEXT;
    return error;
}

static ProError panel_multi_remember(ProMdl root,ProMdl sheet,int child_id,const PanelPressfitPlan *plan,PartText *out){
    PanelMultiContext current={0};
    ProError error=panel_multi_scope(root,&current);
    if(!error){
        current.plan=*plan;
        current.panel=child_id;
        if(_wcsicmp(plan->drawing,current.scope.target))error=PRO_TK_BAD_CONTEXT;
    }
    PanelMultiReadback measured={0};
    if(!error)error=panel_multi_audit(root,sheet,&current,&measured,out);
    if(!error){
        memcpy(current.ids,measured.ids,sizeof(current.ids));
        if(!panel_multi_write(&current))error=PRO_TK_CANT_WRITE;
    }
    if(error)part_text_add(out,L"六孔分组的续做关系登记未完成（Creo %d）。已生成的模型保留。\n",error);
    else part_text_add(out,L"已记住当前板件的四角和中间两组，合计六个压铆孔位。\n");
    pressfit_checkpoint_close(&current.scope);
    return error;
}

static int panel_multi_length_selector(const wchar_t *previous,int target,wchar_t next[64],int *current_length){
    PressfitSelector selector={0};
    if(target<1||target>=1000||!pressfit_selector(previous,&selector)||!selector.generic[0])return 0;
    const wchar_t *last=wcsrchr(selector.instance,L'X');
    if(!last)last=wcsrchr(selector.instance,L'x');
    *current_length=last?pressfit_bso_size(last+1):0;
    if(!*current_length)return 0;
    return swprintf_s(next,64,L"%ls:%.*ls%d",selector.generic,(int)(last-selector.instance+1),
        selector.instance,target)>=0;
}

static ProError panel_multi_verify_revised(PanelMultiReadback *before,PanelMultiReadback *after,PartText *out){
    for(int i=0;i<6;i++){
        if(before->bores[i]!=after->bores[i]||before->face!=after->face)return PRO_TK_BAD_CONTEXT;
        if(i<4&&(before->ids[i]!=after->ids[i]||before->models[i]!=after->models[i]||
           !ax_same_pose(before->poses[i],after->poses[i],before->mm))){
            part_text_add(out,L"四角 ID 或安装位置发生变化，已暂缓保存。\n");
            return PRO_TK_BAD_CONTEXT;
        }
    }
    return PRO_TK_NO_ERROR;
}

static ProError panel_multi_revise(ProMdl assembly,int target_length,int save,PartText *out){
    PanelMultiContext current={0};
    ProError error=panel_multi_scope(assembly,&current);
    if(!error&&!panel_multi_read(&current))error=PRO_TK_E_NOT_FOUND;
    if(error){
        part_text_add(out,L"没有找到与当前装配及工作目录对应的六孔分组记录（Creo %d）。",error);
        goto done;
    }
    wchar_t next[64];
    PanelMultiReadback before={0};
    error=panel_multi_audit(assembly,NULL,&current,&before,out);
    if(!error&&memcmp(before.ids,current.ids,sizeof(current.ids)))error=PRO_TK_BAD_CONTEXT;
    if(error){
        part_text_add(out,L"当前压铆实例或板件参照与记录不符，尚未执行中间两处更改。\n");
        goto done;
    }
    int former_length;
    if(!panel_multi_length_selector(current.plan.center_spec,target_length,next,&former_length)){
        error=PRO_TK_BAD_INPUTS;
        part_text_add(out,L"无法从已核对的中间实例确定同系列的新长度，本次保留原件。\n");
        goto done;
    }

    if(target_length==former_length){
        part_text_add(out,L"已读回中间两个规格均为 %d mm，六个现有实例保持。\n",target_length);
        goto save_panel;
    }

    FastenerPressfitRequest request={.action=PRESSFIT_PLACE,.replace=1,.short_form=1,.check_clearance=1,
        .expected=2,.mapped_count=2,.face_id=before.face};
    wcscpy_s(request.model,_countof(request.model),next);
    swprintf_s(request.panel_path,_countof(request.panel_path),L"ID:%d",current.panel);
    request.mapped_holes[0]=before.bores[4];
    request.mapped_holes[1]=before.bores[5];
    error=local_control_pressfit_apply(assembly,&request,L"仅替换当前六孔板件的中间两处",out);
    if(error)goto done;
    wcscpy_s(current.plan.center_spec,_countof(current.plan.center_spec),next);
    PanelMultiReadback after={0};
    error=panel_multi_audit(assembly,NULL,&current,&after,out);
    if(!error)error=panel_multi_verify_revised(&before,&after,out);
    if(error)goto done;

    memcpy(current.ids,after.ids,sizeof(current.ids));
    if(!panel_multi_write(&current)){
        error=PRO_TK_CANT_WRITE;
        part_text_add(out,L"两处中间实例已通过读回，更新分组记录失败，暂缓保存。\n");
        goto done;
    }
    part_text_add(out,L"已核对中间两个规格为 %d mm。四角 ID:%d、ID:%d、ID:%d、ID:%d 及各自安装位置保持。\n",
        target_length,after.ids[0],after.ids[1],after.ids[2],after.ids[3]);
save_panel:
    if(save){
        wchar_t receipt[2048]=L"";
        error=bwtl_save_guarded(assembly,receipt,_countof(receipt));
        if(receipt[0])part_text_add(out,L"%ls\n",receipt);
    }else part_text_add(out,L"更新保留在当前 Creo 会话。");
done:
    pressfit_checkpoint_close(&current.scope);
    return error;
}
