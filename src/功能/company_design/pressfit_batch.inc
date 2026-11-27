/* Read what Creo actually assembled at the measured bore before changing another site. */
typedef struct {
    int count,id,depth;
    ProMdl model;
    ProAsmcomp feature;
    int paired;
    PressfitSiteReceipt verification;
} PressfitOccupancy;

static int fastener_pressfit_same_bore(const BsoSite *site,int id,double mm){
    if(id==site->hole_id)return 1;
    AxComponent panel=site->panel;
    AxHardwareSite reference={0};
    wchar_t hole[32],face[32];
    fastener_bso_surface(id,hole);
    fastener_bso_surface(site->face_id,face);
    if(ax_hw_site(&panel,hole,face,mm,&reference)||!reference.bore||ax_hw_world(&panel,&reference))return 0;
    return part_close(reference.cylinder.radius,site->geometry.cylinder.radius)&&
        ad_near(reference.contact,site->geometry.contact,0.001/mm)&&
        fabs(ad_dot(reference.cylinder.direction,site->geometry.cylinder.direction))>=1-1e-7;
}

static int fastener_pressfit_ref_at_site(ProSelection selection,const BsoSite *site,
    const AxOccurrence *component,int bore,double mm){
    ProModelitem item;
    ProAsmcomppath path;
    if(!selection||ProSelectionModelitemGet(selection,&item)||item.type!=PRO_SURFACE||
       item.owner!=site->panel.model||ProSelectionAsmcomppathGet(selection,&path)||
       path.table_num<1||path.table_num>AX_PATH_DEPTH)return 0;
    const ProAsmcomppath *panel=&site->panel.path;
    const int parents=component->depth-1;
    const int full=path.owner==panel->owner&&path.table_num==panel->table_num&&
        !memcmp(path.comp_id_table,panel->comp_id_table,path.table_num*sizeof(int));
    const int relative=parents>0&&parents+path.table_num==panel->table_num&&
        !memcmp(component->ids,panel->comp_id_table,parents*sizeof(int))&&
        !memcmp(path.comp_id_table,panel->comp_id_table+parents,path.table_num*sizeof(int));
    return (full||relative)&&(bore?fastener_pressfit_same_bore(site,item.id,mm):item.id==site->face_id);
}

static int fastener_pressfit_child_ref(ProSelection selection,ProMdl model,int id){
    ProModelitem item;
    return selection&&!ProSelectionModelitemGet(selection,&item)&&item.type==PRO_SURFACE&&
        item.owner==model&&item.id==id;
}

static ProError fastener_pressfit_check_pose(ProMdl part,ProAsmcomp *feature,ProAsmcomppath *path,
    const BsoSite *site,const BsoFitting *fitting,double mm,PressfitSiteReceipt *row){
    ProMatrix pose;
    ProBoolean packaged=PRO_B_TRUE;
    ProFeatStatus active;
    ProMdl readback=NULL;
    ProError error=ProAsmcompMdlGet(feature,&readback);
    if(!error&&readback!=part)return PRO_TK_BAD_CONTEXT;
    if(!error)error=ProFeatureStatusGet(feature,&active);
    if(!error&&active!=PRO_FEAT_ACTIVE)return PRO_TK_INCOMPLETE;
    if(!error)error=ProAsmcompIsPackaged(feature,&packaged);
    if(!error&&packaged)return PRO_TK_INCOMPLETE;
    if(!error)error=ProAsmcomppathTrfGet(path,PRO_B_TRUE,pose);
    if(!error)error=fastener_bso_measure(fitting,site,pose,mm,&row->radial,&row->gap,&row->facing,&row->parallel);
    if(!error&&(row->radial>0.001||fabs(row->gap)>0.001||row->facing>-1+1e-7||row->parallel<1-1e-7))
        error=PRO_TK_INCOMPLETE;
    if(!error){row->id=feature->id;row->constraints=2;row->regenerated=1;}
    return error;
}

static ProError fastener_pressfit_read_rows(const AxOccurrence *component,ProAsmcompconstraint *rows,int n,
    ProMdl part,const BsoFitting *fitting,const BsoSite *sites,int count,double mm,int used[12],int paired[12]){
    int axis[12]={0},seat[12]={0};
    for(int i=0;i<n;i++){
        ProAsmcompConstrType type;
        ProSelection base=NULL,child=NULL;
        ProDatumside side;
        ProError error=ProAsmcompconstraintTypeGet(rows[i],&type);
        if(error)return error;
        if(type!=PRO_ASM_INSERT&&type!=PRO_ASM_ALIGN&&type!=PRO_ASM_MATE)continue;
        error=ProAsmcompconstraintAsmreferenceGet(rows[i],&base,&side);
        if(error==PRO_TK_E_NOT_FOUND||error==PRO_TK_BAD_CONTEXT)continue;
        if(error)return error;
        const int bore=type!=PRO_ASM_MATE;
        int matches[12]={0};
        for(int j=0;j<count;j++){
            matches[j]=fastener_pressfit_ref_at_site(base,&sites[j],component,bore,mm);
            if(bore&&matches[j])used[j]=1;
        }
        if(n!=2)continue;
        error=ProAsmcompconstraintCompreferenceGet(rows[i],&child,&side);
        if(error==PRO_TK_E_NOT_FOUND||error==PRO_TK_BAD_CONTEXT)continue;
        if(error)return error;
        if(!child)continue;
        for(int j=0;j<count;j++){
            if(!matches[j])continue;
            ProModelitem local;
            if(ProSelectionModelitemGet(child,&local)||local.type!=PRO_SURFACE||local.owner!=component->model)
                continue;
            if(component->model==part&&!fastener_pressfit_child_ref(child,part,bore?fitting->shaft:fitting->seat))
                continue;
            AxGeometry geometry;
            if(ax_geometry(&local,&geometry)||geometry.type!=(bore?AX_GEOM_CYLINDER:AX_GEOM_PLANE))continue;
            if(type==PRO_ASM_INSERT)axis[j]++;
            if(type==PRO_ASM_MATE)seat[j]++;
        }
    }
    for(int i=0;i<count;i++)paired[i]=axis[i]==1&&seat[i]==1;
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_scan_occupancy(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    const BsoSite *sites,int count,double mm,PressfitOccupancy result[12]){
    AxScan *scan=NULL;
    ProError error=ax_scan(assembly,0,&scan);
    if(error)return error;
    if(scan->limited||scan->unavailable)error=PRO_TK_INCOMPLETE;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *component=&scan->occurrences[i];
        if(component->state!=PRO_FEAT_ACTIVE||component->type!=PRO_MDL_PART||!component->model)continue;
        int target_panel=0;
        for(int j=0;j<count;j++)if(component->depth==sites[j].panel.path.table_num&&
            !memcmp(component->ids,sites[j].panel.path.comp_id_table,component->depth*sizeof(int)))target_panel=1;
        if(target_panel)continue;
        ProAsmcomppath parent,path;
        ProIdTable ids={0};
        ProSolid owner=NULL;
        ProAsmcomp feature;
        ProAsmcompconstraint *rows=NULL;
        int n=0;
        const int parent_depth=component->depth-1;
        memcpy(ids,component->ids,component->depth*sizeof(int));
        error=ProAsmcomppathInit((ProSolid)assembly,ids,parent_depth,&parent);
        if(!error)error=ProAsmcomppathMdlGet(&parent,&owner);
        if(!error)error=ax_component_feature((ProMdl)owner,component->ids[parent_depth],&feature);
        if(!error)error=ProAsmcomppathInit((ProSolid)assembly,ids,component->depth,&path);
        if(!error)error=ax_read_constraints(&feature,&parent,&rows,&n);
        int used[12]={0},paired[12]={0};
        if(!error)error=fastener_pressfit_read_rows(component,rows,n,part,fitting,sites,count,mm,used,paired);
        if(rows)ProAsmcompconstraintArrayFree(rows);
        BsoFitting measured=*fitting;
        const int known=component->model==part||
            (fastener_bso_model_family(component->name)&&!fastener_bso_fitting(component->model,mm,&measured));
        if(!error&&known){
            ProMatrix pose;
            error=ProAsmcomppathTrfGet(&path,PRO_B_TRUE,pose);
            for(int j=0;!error&&j<count;j++)if(!used[j]){
                double radial,gap,facing,parallel;
                error=fastener_bso_measure(&measured,&sites[j],pose,mm,&radial,&gap,&facing,&parallel);
                if(!error&&radial<=0.05&&fabs(gap)<=0.65&&parallel>=1-1e-6)used[j]=1;
            }
        }
        for(int j=0;!error&&j<count;j++)if(used[j]){
            PressfitOccupancy *occupancy=&result[j];
            occupancy->count++;
            if(occupancy->count>1)continue;
            occupancy->depth=component->depth;
            occupancy->model=component->model;
            occupancy->feature=feature;
            occupancy->id=feature.id;
            occupancy->paired=paired[j];
            if(paired[j]&&component->model==part&&!fastener_pressfit_check_pose(part,&feature,&path,
                &sites[j],fitting,mm,&occupancy->verification))continue;
            occupancy->verification.regenerated=0;
        }
    }
    free(scan);
    return error;
}

/* Retry only with the other native face of the same aperture at the same side. */
static int fastener_pressfit_alternate(const BsoSite *site,const BsoFitting *fitting,double mm,
    double interference,BsoSite *alternate){
    BsoFaces faces={.model=site->panel.model,.mm=mm,.radius=site->geometry.cylinder.radius*mm};
    if(fastener_bso_model_faces(&faces))return 0;
    int found=0;
    for(int i=0;i<faces.hole_count;i++){
        const int id=faces.holes[i];
        if(id==site->hole_id||!fastener_pressfit_same_bore(site,id,mm))continue;
        BsoSite candidate={0};
        const wchar_t *stage;
        AxComponent panel=site->panel;
        if(fastener_pressfit_target(&panel,id,site->face_id,mm,fitting->model,fitting,2*faces.radius,
            interference,&candidate,&stage))continue;
        if(found++)return 0;
        *alternate=candidate;
    }
    return found;
}

static ProError fastener_pressfit_existing_source(const PressfitOccupancy *old,PartText *out){
    ProMdlName name;
    ProError error=ProMdlMdlnameGet(old->model,name);
    if(error)return error;
    for(wchar_t *p=name;*p;p++)
        if(!_wcsnicmp(p,L"_BWTL",5)&&(p[5]==0||p[5]==L'_')){*p=0;break;}
    wchar_t path[MAX_PATH],details[700]=L"";
    PartText check={details,_countof(details),0};
    ProMdl source=NULL;
    error=pressfit_read_exact(name,&source,path,&check);
    if(!error&&source!=old->model)error=pressfit_verify_project(source,old->model,&check);
    if(error)part_text_add(out,L"原实例 ID:%d 的实际几何或材料无法与确切的公司库压铆规格核对，原件保留。\n",old->id);
    return error;
}


static ProError fastener_pressfit_removal_ready(ProMdl assembly,const PressfitOccupancy *old,PartText *out){
    if(old->depth!=1||old->feature.owner!=assembly||!old->paired)return PRO_TK_BAD_CONTEXT;
    ProAsmcomp feature=old->feature;
    ProError error=ax_ordinary_direct(&feature);
    if(!error)error=ax_check_default_set(&feature);
    if(!error)error=ax_dependencies(&feature,out);
    return error;
}

static ProError fastener_pressfit_recheck_added(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    const BsoSite *site,int id,double mm,PressfitSiteReceipt *row){
    ProAsmcomp feature;
    ProAsmcomppath path;
    ProIdTable ids={id};
    ProError error=ax_component_feature(assembly,id,&feature);
    if(!error)error=ProAsmcomppathInit((ProSolid)assembly,ids,1,&path);
    ProAsmcomppath root;
    ProAsmcompconstraint *constraints=NULL;
    int count=0,used[12]={0},paired[12]={0};
    if(!error)error=ax_root_path(assembly,&root);
    if(!error)error=ax_read_constraints(&feature,&root,&constraints,&count);
    AxOccurrence occurrence={.ids={id},.depth=1,.model=part};
    if(!error)error=fastener_pressfit_read_rows(&occurrence,constraints,count,part,fitting,site,1,mm,used,paired);
    if(constraints)ProAsmcompconstraintArrayFree(constraints);
    if(!error&&!paired[0])error=PRO_TK_INCOMPLETE;
    if(!error)error=fastener_pressfit_check_pose(part,&feature,&path,site,fitting,mm,row);
    return error;
}

static void fastener_pressfit_remove_trial(ProMdl assembly,int id,PartText *out){
    if(id<=0)return;
    ProAsmcomp feature;
    if(ax_component_feature(assembly,id,&feature))ax_uncertain(out);
    else ax_discard_occurrence(assembly,&feature,out);
}

static ProError fastener_pressfit_replace_at_site(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    const BsoSite *site,const PressfitOccupancy *old,double mm,double interference,PressfitSiteReceipt *row,
    const wchar_t **stage,PartText *out){
    *stage=L"核对原件定位及下游依赖";
    ProError error=fastener_pressfit_removal_ready(assembly,old,out);
    if(!error)error=fastener_pressfit_existing_source(old,out);
    if(error)return error;
    ProMdl backup=NULL;
    ProPath backup_path;
    ProMdlName backup_name;
    *stage=L"创建 E 盘恢复副本";
    error=ax_delete_backup(assembly,&backup,backup_path,backup_name);
    if(error)goto done;
    part_text_add(out,L"替换前恢复副本 %ls\\%ls.asm。\n",backup_path,backup_name);

    int added=0,delete_started=0;
    FdTxn before={0};
    error=fastener_bso_install_site(assembly,part,site,fitting,mm,&added,stage,out);
    if(!error){
        *stage=L"核对新规格的实际干涉和背面净空";
        error=fastener_pressfit_postfit_except(assembly,site,added,old->id,mm,interference,out);
    }
    if(!error){
        *stage=L"核对替换前实例和依赖";
        error=fastener_pressfit_removal_ready(assembly,old,out);
        if(!error)error=ProSolidFeatVisit((ProSolid)assembly,ax_public_feature,NULL,&before);
    }
    if(!error){
        *stage=L"移除旧实例并读回新实例";
        delete_started=1;
        error=model_remove_feature((ProSolid)assembly,old->id);
        if(!error)error=part_regenerate(assembly);
        if(!error)error=ax_delete_check(assembly,old->id,&before,1);
        if(!error)error=fastener_pressfit_recheck_added(assembly,part,fitting,site,added,mm,row);
    }
    if(error&&added){
        if(delete_started&&ax_delete_check(assembly,old->id,&before,0))ax_uncertain(out);
        else fastener_pressfit_remove_trial(assembly,added,out);
    }
done:
    if(backup)ProMdlErase(backup);
    return error;
}

static ProError fastener_pressfit_place_batch(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    BsoSite sites[12],int count,double mm,double interference_mm,int replace,PressfitCheckpoint *checkpoint,
    PressfitBatchResult *result,PartText *out){
    if(!assembly||!part||!fitting||count<1||count>12||!isfinite(mm)||mm<=0||
       !isfinite(interference_mm)||interference_mm<0||!result)return PRO_TK_BAD_INPUTS;
    memset(result,0,sizeof(*result));
    result->count=count;
    if(checkpoint&&(checkpoint->assembly!=assembly||checkpoint->count!=count))return PRO_TK_BAD_CONTEXT;
    if(ax_recovery_uncertain){
        part_text_add(out,L"前一条装配回退仍待核对，请先核对装配状态。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    ProError error=ax_master(assembly);
    if(!error)error=part_health(assembly);
    if(error)return error;
    PressfitOccupancy occupied[12]={0};
    error=fastener_pressfit_scan_occupancy(assembly,part,fitting,sites,count,mm,occupied);
    if(error){
        part_text_add(out,L"已有压铆件定位读回失败，无法确认孔位是否已用（Creo %d）。\n",error);
        return error;
    }
    if(replace)part_text_add(out,L"【回溯风险重点提醒】连续替换并重定义旧组件曾触发 Creo 回溯。本次先验证新件、净空及恢复副本，再移除无依赖的旧实例。\n");
    for(int i=0;i<count;i++){
        PressfitSiteReceipt *row=&result->rows[i];
        const PressfitOccupancy *existing=&occupied[i];
        const int hole=sites[i].hole_id;
        row->hole=hole;
        row->face=sites[i].face_id;
        ax_path_label(sites[i].panel.path.comp_id_table,sites[i].panel.path.table_num,row->panel,_countof(row->panel));
        row->stage=L"读取已有占位";
        if(existing->count>1)row->disposition=PRESSFIT_DUPLICATE;
        else if(existing->count==1&&existing->verification.regenerated){
            row->id=existing->verification.id;
            row->constraints=existing->verification.constraints;
            row->regenerated=existing->verification.regenerated;
            row->radial=existing->verification.radial;
            row->gap=existing->verification.gap;
            row->facing=existing->verification.facing;
            row->parallel=existing->verification.parallel;
            row->disposition=PRESSFIT_REUSED;
            result->reused++;
        }else if(existing->count&&!replace){
            row->id=existing->id;
            row->disposition=PRESSFIT_WRONG;
        }else if(!existing->count&&replace){
            row->disposition=PRESSFIT_FAILED;
            row->stage=L"找不到可替换的已装实例";
            row->native_error=PRO_TK_E_NOT_FOUND;
        }else{
            BsoSite current={0};
            AxComponent panel=sites[i].panel;
            const double requested=2*sites[i].geometry.cylinder.radius*mm;
            error=fastener_pressfit_target(&panel,hole,sites[i].face_id,mm,part,fitting,requested,interference_mm,
                &current,&row->stage);
            if(!error&&(!ad_near(current.geometry.contact,sites[i].geometry.contact,0.001/mm)||
                ad_dot(current.geometry.plane.direction,sites[i].geometry.plane.direction)<1-1e-7))error=PRO_TK_BAD_CONTEXT;
            if(!error){
                row->stage=L"写入本孔续做记录";
                error=pressfit_checkpoint_mark(checkpoint,i+1,0,i,NULL);
                if(error){
                    part_text_add(out,L"孔 %d 的续做记录写入失败（Creo %d），此孔和后续孔未执行。\n",i+1,error);
                    return error;
                }
            }
            if(!error&&replace){
                error=fastener_pressfit_replace_at_site(assembly,part,fitting,&current,existing,mm,interference_mm,
                    row,&row->stage,out);
                if(!error){row->disposition=PRESSFIT_REPLACED;result->replaced++;}
            }else if(!error){
                int added=0;
                error=fastener_bso_install_site(assembly,part,&current,fitting,mm,&added,&row->stage,out);
                BsoSite alternate={0};
                if(error&&error!=PRO_TK_USER_ABORT&&!ax_recovery_uncertain&&
                   (!wcscmp(row->stage,L"构建原生参照")||!wcscmp(row->stage,L"设置同轴和端面贴合"))&&
                   fastener_pressfit_alternate(&current,fitting,mm,interference_mm,&alternate)){
                    part_text_add(out,L"孔 %d 原生参照失败，改用同一孔口的等效圆柱 SURFACE:%d。\n",i+1,alternate.hole_id);
                    current=alternate;
                    error=fastener_bso_install_site(assembly,part,&current,fitting,mm,&added,&row->stage,out);
                }
                if(!error){
                    row->stage=L"再次读回实际位姿";
                    error=fastener_pressfit_recheck_added(assembly,part,fitting,&current,added,mm,row);
                    if(error)fastener_pressfit_remove_trial(assembly,added,out);
                }
                if(!error){row->disposition=PRESSFIT_ADDED;row->hole=current.hole_id;result->added++;}
            }
            row->native_error=error;
            if(error){row->disposition=PRESSFIT_FAILED;result->failed++;}
        }
        if(row->disposition==PRESSFIT_REUSED||row->disposition==PRESSFIT_ADDED||row->disposition==PRESSFIT_REPLACED){
            const wchar_t *label=row->disposition==PRESSFIT_ADDED?L"已装":
                row->disposition==PRESSFIT_REUSED?L"保留已验":L"已替换";
            part_text_add(out,L"孔 %d %ls SURFACE:%d/面:%d %ls ID:%d，2 条原生约束，轴差 %.6g mm，端差 %.6g mm，朝向和再生通过。\n",
                i+1,row->panel,row->hole,row->face,label,row->id,row->radial,row->gap);
        }else if(row->disposition==PRESSFIT_DUPLICATE){
            part_text_add(out,L"孔 %d SURFACE:%d 有 %d 个重复占位，已保留原状态。\n",i+1,hole,existing->count);
        }else if(row->disposition==PRESSFIT_WRONG){
            part_text_add(out,L"孔 %d SURFACE:%d 已由 ID:%d 占位，规格或原生定位与本次要求不符。\n",i+1,hole,row->id);
        }else{
            part_text_add(out,L"孔 %d SURFACE:%d，「%ls」未完成（Creo %d）。\n",i+1,hole,row->stage,row->native_error);
        }
        if(ax_recovery_uncertain)break;
        error=pressfit_checkpoint_mark(checkpoint,0,0,i,row);
        if(error){
            part_text_add(out,L"已完成的孔位保留，续做记录未能写入（Creo %d），后续孔暂缓。\n",error);
            return error;
        }
        if(row->native_error==PRO_TK_USER_ABORT)break;
    }
    const int done=result->added+result->reused+result->replaced;
    if(result->replaced)part_text_add(out,L"已安全替换 %d 个实例。\n",result->replaced);
    part_text_add(out,L"实际核对 %d/%d 孔，新增 %d，已装复用 %d，待处理 %d。本次装配未自动保存。\n",
        done,count,result->added,result->reused,count-done);
    if(done==count){
        error=pressfit_checkpoint_mark(checkpoint,0,1,0,NULL);
        if(error)return error;
        return PRO_TK_NO_ERROR;
    }
    if(checkpoint&&!ax_recovery_uncertain)
        part_text_add(out,L"当前批次已记录，可在同一装配发送「继续装」。\n");
    return PRO_TK_INCOMPLETE;
}

static ProError fastener_pressfit_place(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    BsoSite sites[12],int count,double mm,double interference_mm,PartText *out){
    PressfitBatchResult result;
    return fastener_pressfit_place_batch(assembly,part,fitting,sites,count,mm,interference_mm,0,NULL,&result,out);
}
