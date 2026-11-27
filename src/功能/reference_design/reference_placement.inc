/* Replay the named coordinate-system interface of one real, direct source
   occurrence. This helper does not activate, save, or modify either source. */
static ProError reference_source_placement(ProMdl assembly,int id,AxComponent *component,
        AxStudyRule *rule,wchar_t base[PART_TOKEN_MAX],wchar_t own[PART_TOKEN_MAX]){
    wchar_t key[32];
    ProAsmcompconstraint *rows=NULL;
    ProAsmcomppath root;
    double mm=0;
    ProBoolean packaged=PRO_B_TRUE,under=PRO_B_TRUE;
    if(swprintf_s(key,_countof(key),L"ID:%d",id)<0)return PRO_TK_BAD_INPUTS;
    ProError error=ax_component_resolve(assembly,key,component);
    if(!error)error=ax_ordinary_direct(&component->feature);
    if(!error)error=ProAsmcompIsPackaged(&component->feature,&packaged);
    if(!error)error=ProAsmcompIsUnderconstrained(&component->feature,&under);
    if(!error&&(packaged||under))error=PRO_TK_INCOMPLETE;
    if(!error)error=qa_mm_per_unit(assembly,&mm);
    if(!error)error=ax_root_path(assembly,&root);
    if(!error)error=ax_read_constraints(&component->feature,&root,&rows,&rule->reported);
    AxStudyRow *row=&rule->rows[0];
    if(!error&&(rule->reported!=1||
       !ax_study_row(rows[0],assembly,component->model,mm,row)||
       row->type!=PRO_ASM_CSYS||row->flags||row->base.type!=PRO_CSYS||row->part.type!=PRO_CSYS||
       wcscmp(row->base.path,L"ROOT")||wcscmp(row->part.path,L"ROOT")||
       !row->base.name[0]||!row->part.name[0]))error=PRO_TK_UNSUPPORTED;
    if(rows)ProAsmcompconstraintArrayFree(rows);
    if(error)return error;
    if(swprintf_s(base,PART_TOKEN_MAX,L"CSYS:%ls",row->base.name)<0||
       swprintf_s(own,PART_TOKEN_MAX,L"CSYS:%ls",row->part.name)<0)return PRO_TK_BAD_INPUTS;
    if(!aw_aligned(assembly,component,base,own,mm))return PRO_TK_INCOMPLETE;
    error=ax_actual_pose(assembly,&component->feature,rule->pose);
    if(error)return error;
    for(int i=0;i<3;i++)rule->pose[3][i]*=mm;
    rule->type=PRO_MDL_PART;
    rule->state=PRO_FEAT_ACTIVE;
    rule->count=1;
    return PRO_TK_NO_ERROR;
}

static ProError reference_verify_placement(ProMdl assembly,ProMdl part,int id,
    const wchar_t *base,const wchar_t *own,double mm,AxStudyReceipt *proof){
    wchar_t key[32];
    AxComponent installed={0};
    if(swprintf_s(key,_countof(key),L"ID:%d",id)<0)return PRO_TK_BAD_INPUTS;
    ProError error=ax_component_resolve(assembly,key,&installed);
    if(!error&&installed.model!=part)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_study_inspect(assembly,id,proof);
    if(!error&&(!aw_aligned(assembly,&installed,base,own,mm)||proof->native_rows!=1))
        error=PRO_TK_INCOMPLETE;
    return error;
}

static ProError reference_verify_insertion(ProMdl assembly,const AwAssemblyState *before,
    int id,double mm){
    AwAssemblyState after={0};
    ProError error=aw_state_capture(assembly,&after);
    if(error)return error;
    int found=0,matched=0;
    if(after.tree->count!=before->tree->count+1)error=PRO_TK_INCOMPLETE;
    for(int i=0;!error&&i<after.tree->count;i++){
        AxOccurrence *current=&after.tree->occurrences[i];
        if(current->depth==1&&current->ids[0]==id){found++;continue;}
        if(matched>=before->tree->count){error=PRO_TK_INCOMPLETE;break;}
        AxOccurrence *original=&before->tree->occurrences[matched];
        if(!ax_layout_same_path(original,current)||original->model!=current->model||
           original->state!=current->state||_wcsicmp(original->name,current->name)||
           before->pose_valid[matched]!=after.pose_valid[i]||
           (after.pose_valid[i]&&!ax_same_pose(before->poses[matched],after.poses[i],mm)))
            error=PRO_TK_INCOMPLETE;
        matched++;
    }
    if(!error&&(found!=1||matched!=before->tree->count))error=PRO_TK_INCOMPLETE;
    aw_state_free(&after);
    return error;
}

static ProError reference_verify_distinct_assemblies(ProMdl source,ProMdl target){
    ProMdlName target_name;
    ProError error=ProMdlMdlnameGet(target,target_name);
    if(error)return error;
    AxScan *tree=NULL;
    error=ax_scan(source,0,&tree);
    if(error)return error;
    if(tree->limited||tree->unavailable)error=PRO_TK_INCOMPLETE;
    for(int i=0;!error&&i<tree->count;i++){
        AxOccurrence *entry=&tree->occurrences[i];
        if(entry->model==target||(entry->type==PRO_MDL_ASSEMBLY&&!_wcsicmp(entry->name,target_name)))
            error=PRO_TK_BAD_CONTEXT;
    }
    free(tree);
    return error;
}

static ProError reference_place_component(ProMdl target,ProMdl source_assembly,int source_component_id,
    ProMdl new_part,int *new_component_id,PartText *out){
    if(!out||!new_component_id)return PRO_TK_BAD_INPUTS;
    *new_component_id=0;
    if(!target||!source_assembly||!new_part||target==source_assembly||source_component_id<=0)
        return PRO_TK_BAD_INPUTS;
    if(ax_recovery_uncertain){
        part_text_add(out,L"当前会话的装配恢复还未核实，暂不能装配参考件。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    ProMdlType target_type,source_type,part_type;
    ProPath directory;
    ProError error=ProDirectoryCurrentGet(directory);
    if(!error&&!storage_output_directory(directory))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlTypeGet(target,&target_type);
    if(!error)error=ProMdlTypeGet(source_assembly,&source_type);
    if(!error)error=ProMdlTypeGet(new_part,&part_type);
    if(error)return error;
    if(target_type!=PRO_MDL_ASSEMBLY||source_type!=PRO_MDL_ASSEMBLY||part_type!=PRO_MDL_PART)
        return PRO_TK_INVALID_TYPE;
    error=reference_verify_distinct_assemblies(source_assembly,target);
    if(error){
        part_text_add(out,L"无法核对目标装配独立于来源总成，未修改装配。\n");
        return error;
    }
    AxStudyRule rule={0};
    AxComponent component={0};
    wchar_t base[PART_TOKEN_MAX],own[PART_TOKEN_MAX];
    error=reference_source_placement(source_assembly,source_component_id,&component,&rule,base,own);
    if(error){
        part_text_add(out,L"来源装配的 ID:%d 未通过原生坐标系复核。当前支持直接零件的一条命名坐标系重合约束。\n",
            source_component_id);
        return error;
    }
    if(component.model==new_part){
        part_text_add(out,L"装配目标件仍指向来源零件。请先独立重建出新零件。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    double mm=0,part_mm=0;
    char source_stamp[240],source_after[240],part_stamp[240],part_after[240];
    ProMdlName source_name,new_name;
    error=ax_study_verstamp(source_assembly,source_stamp);
    if(!error)error=ax_study_verstamp(component.model,part_stamp);
    if(!error)error=ProMdlMdlnameGet(source_assembly,source_name);
    if(!error)error=ProMdlMdlnameGet(new_part,new_name);
    if(!error)error=qa_mm_per_unit(target,&mm);
    if(!error)error=qa_mm_per_unit(new_part,&part_mm);
    if(!error&&!part_close(mm,part_mm))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_master(target);
    if(!error)error=part_health(target);
    if(!error)error=part_health(new_part);
    if(error)return error;

    AwAssemblyState before={0};
    error=aw_state_capture(target,&before);
    if(error)return error;
    int placed=0,existing=0,existing_count=0;
    for(int i=0;i<before.tree->count;i++){
        const AxOccurrence *entry=&before.tree->occurrences[i];
        if(entry->model!=new_part&&(entry->type!=PRO_MDL_PART||_wcsicmp(entry->name,new_name)))continue;
        if(entry->model==new_part&&entry->depth==1&&entry->state==PRO_FEAT_ACTIVE)existing=entry->ids[0];
        existing_count++;
    }
    AxStudyReceipt actual={0};
    if(existing_count){
        if(existing_count!=1||!existing||
           reference_verify_placement(target,new_part,existing,base,own,mm,&actual)){
            part_text_add(out,L"目标装配已有同一零件的实例，无法核对为一个采用相同坐标系的直接实例。\n");
            error=PRO_TK_E_FOUND;
        }else{
            *new_component_id=existing;
            part_text_add(out,L"新零件已按来源的 %ls ↔ %ls 定位在 ID:%d，沿用现有实例。\n",base,own,existing);
        }
        aw_state_free(&before);
        return error;
    }

    wchar_t detail[1200]=L"";
    PartText placement={detail,_countof(detail),0};
    error=ax_study_place(target,new_part,&rule,NULL,&placed,&placement);
    if(error&&detail[0])part_text_add(out,L"%ls",detail);
    if(!error)error=reference_verify_placement(target,new_part,placed,base,own,mm,&actual);
    if(!error)error=reference_verify_insertion(target,&before,placed,mm);
    if(!error)error=ax_study_verstamp(source_assembly,source_after);
    if(!error)error=ax_study_verstamp(component.model,part_after);
    if(!error&&(strcmp(source_stamp,source_after)||strcmp(part_stamp,part_after)))
        error=PRO_TK_BAD_CONTEXT;
    if(error){
        ProAsmcomp occurrence;
        int added=placed;
        for(int i=0;added&&i<before.tree->count;i++){
            AxOccurrence *entry=&before.tree->occurrences[i];
            if(entry->depth==1&&entry->ids[0]==added)added=0;
        }
        if(added&&!ax_component_feature(target,added,&occurrence))ax_discard_occurrence(target,&occurrence,out);
        if(aw_state_verify(target,&before,0,mm,out)||part_health(target)){
            ax_uncertain(out);
            error=PRO_TK_INCOMPLETE;
        }else part_text_add(out,L"本次定位未通过（Creo %d）。已核对原有实例和位姿，未保留本次新增件。\n",error);
    }else{
        *new_component_id=placed;
        part_text_add(out,L"已按来源 %ls.ASM 的 ID:%d，以 %ls ↔ %ls 定位新零件。"
            L"新实例 ID:%d 的位置为 %.6g / %.6g / %.6g mm。原生约束、坐标系三轴及原点重合、"
            L"其他组件位姿均已回读。未保存。\n",source_name,source_component_id,base,own,placed,
            actual.pose[3][0]*mm,actual.pose[3][1]*mm,actual.pose[3][2]*mm);
    }
    aw_state_free(&before);
    return error;
}
