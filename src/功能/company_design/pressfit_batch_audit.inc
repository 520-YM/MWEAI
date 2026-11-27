static ProError fastener_pressfit_batch_preflight(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    const BsoSite sites[12],int count,double mm,PartText *out){
    if(!assembly||!part||!fitting||!sites||count<1||count>12)return PRO_TK_BAD_INPUTS;
    PressfitOccupancy occupied[12]={0};
    ProError error=fastener_pressfit_scan_occupancy(assembly,part,fitting,sites,count,mm,occupied);
    if(error)return error;
    for(int i=0;i<count;i++){
        const BsoSite *site=&sites[i];
        wchar_t panel[AX_PATH_DEPTH*12+4];
        ax_path_label(site->panel.path.comp_id_table,site->panel.path.table_num,panel,_countof(panel));
        double from,to;
        ax_hw_axial_span(&site->geometry,site->geometry.contact,site->geometry.plane.direction,mm,&from,&to);
        part_text_add(out,L"孔 %d %ls SURFACE:%d/安装面:%d，Ø%.6g mm，孔深 %.6g mm，",
            i+1,panel,site->hole_id,site->face_id,2*site->geometry.cylinder.radius*mm,to-from);
        if(occupied[i].count==0)part_text_add(out,L"无原生定位占位。\n");
        else if(occupied[i].count==1)part_text_add(out,L"已有定位实例 ID:%d。\n",occupied[i].id);
        else part_text_add(out,L"检出 %d 个重复定位实例。\n",occupied[i].count);
    }
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_batch_audit(ProMdl assembly,const BsoSite sites[12],
    const PressfitBatchResult *result,double mm,double interference,PartText *out){
    if(!assembly||!sites||!result||result->count<1||result->count>12)return PRO_TK_BAD_INPUTS;
    ProError error=PRO_TK_NO_ERROR;
    int audited=0;
    for(int i=0;i<result->count;i++){
        const PressfitSiteReceipt *row=&result->rows[i];
        if(!row->regenerated||!row->id)continue;
        ProError current=fastener_pressfit_postfit(assembly,&sites[i],row->id,mm,interference,out);
        if(current&&!error)error=current;
        if(!current)audited++;
    }
    part_text_add(out,L"安装板和邻近实体间隙已核对 %d/%d 孔。\n",audited,result->count);
    return error?error:audited==result->count?PRO_TK_NO_ERROR:PRO_TK_INCOMPLETE;
}

static ProError fastener_pressfit_audit_last(ProMdl assembly,PartText *out){
    PressfitCheckpoint state={0};
    ProMdl source=NULL;
    BsoFitting fitting={0};
    BsoSite sites[12]={0};
    PressfitOccupancy current[12]={0};
    PressfitBatchResult result={0};
    ProError error=pressfit_checkpoint_lock(assembly,&state);
    if(!error)error=pressfit_verify_checkpoint_readback(assembly,&state,&source,&fitting,sites,current,out);
    if(!error){
        result.count=state.count;
        for(int i=0;i<state.count;i++)result.rows[i]=current[i].verification;
        error=fastener_pressfit_batch_audit(assembly,sites,&result,state.mm,state.interference,out);

    }else part_text_add(out,L"当前装配没有与当前模型版本一致的压铆实装记录（Creo %d）。\n",error);
    pressfit_checkpoint_close(&state);
    return error;
}
