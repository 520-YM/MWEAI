/* BSO geometry uses the current occurrence path and real faces, never saved example IDs. */
typedef struct {
    AxComponent panel;
    AxHardwareSite geometry;
    int hole_id,face_id;
} BsoSite;

typedef struct {
    ProMdl model;
    double mm,radius;
    int holes[128],planes[128],hole_count,plane_count,read_planes,all_holes;
} BsoFaces;

static void fastener_bso_surface(int id,wchar_t token[32]){
    swprintf_s(token,32,L"SURFACE:%d",id);
}

static ProError fastener_bso_face_visit(ProSurface surface,ProError status,ProAppData data){
    BsoFaces *faces=data;
    if(status)return status;
    ProGeomitem item;
    AxGeometry shape;
    ProError error=ProSurfaceToGeomitem((ProSolid)faces->model,surface,&item);
    if(error)return error;
    error=ax_geometry(&item,&shape);
    if(error==PRO_TK_INVALID_TYPE)return PRO_TK_NO_ERROR;
    if(error)return error;
    if(shape.type==AX_GEOM_CYLINDER&&(faces->all_holes||part_close(shape.radius*faces->mm,faces->radius))){
        if(faces->hole_count>=_countof(faces->holes))return PRO_TK_OUT_OF_RANGE;
        faces->holes[faces->hole_count++]=item.id;
    }
    if(faces->read_planes&&shape.type==AX_GEOM_PLANE){
        if(faces->plane_count>=_countof(faces->planes))return PRO_TK_OUT_OF_RANGE;
        faces->planes[faces->plane_count++]=item.id;
    }
    return PRO_TK_NO_ERROR;
}

static ProError fastener_bso_model_faces(BsoFaces *faces){
    ProError error=ProSolidSurfaceVisit((ProSolid)faces->model,fastener_bso_face_visit,NULL,faces);
    return error==PRO_TK_E_NOT_FOUND?PRO_TK_NO_ERROR:error;
}

static ProError fastener_bso_target(AxComponent *panel,int hole_id,int face_id,double mm,BsoSite *site){
    wchar_t hole[32],face[32];
    fastener_bso_surface(hole_id,hole);
    fastener_bso_surface(face_id,face);
    ProError error=ax_hw_site(panel,hole,face,mm,&site->geometry);
    if(!error){
        FdTxn txn={.model=panel->model,.scale=1/mm};
        ShopMouth mouth={0};
        error=mp_mouth(&txn,face,hole,&mouth);
    }
    if(!error)error=ax_hw_world(panel,&site->geometry);
    if(!error){
        double start,end;
        AxHardwareSite *actual=&site->geometry;
        ax_hw_axial_span(actual,actual->contact,actual->plane.direction,mm,&start,&end);
        if(!actual->bore||start>=-0.001||end>0.001)error=PRO_TK_BAD_CONTEXT;
        else if(!part_close(actual->cylinder.radius*2*mm,5.4))error=PRO_TK_BAD_INPUTS;
    }
    if(error)return error;
    site->panel=*panel;
    site->hole_id=hole_id;
    site->face_id=face_id;
    return PRO_TK_NO_ERROR;
}

static ProError fastener_bso_collect(AxComponent *panel,int face,int expected,double mm,
    BsoSite sites[12],int *count,PartText *out){
    BsoFaces candidates={.model=panel->model,.mm=mm,.radius=2.7};
    ProError error=fastener_bso_model_faces(&candidates);
    if(error)return error;
    *count=0;
    for(int i=0;i<candidates.hole_count;i++){
        BsoSite site={0};
        error=fastener_bso_target(panel,candidates.holes[i],face,mm,&site);
        if(error==PRO_TK_BAD_INPUTS||error==PRO_TK_BAD_CONTEXT||error==PRO_TK_INVALID_TYPE)continue;
        if(error)return error;
        int duplicate=0;
        for(int j=0;j<*count;j++)
            if(ad_near(sites[j].geometry.contact,site.geometry.contact,0.001/mm)){duplicate=1;break;}
        if(duplicate)continue;
        if(*count>=12)return PRO_TK_OUT_OF_RANGE;
        sites[(*count)++]=site;
    }
    if(*count==expected&&expected>0)return PRO_TK_NO_ERROR;
    part_text_add(out,L"安装面 SURFACE:%d 实测可定位的 Ø5.4 mm 孔有 %d 个，要求 %d 个。请选定要安装的孔壁。\n",face,*count,expected);
    return PRO_TK_BAD_CONTEXT;
}

/* With just a mounting face, use each matching bore opening on that same face
   only when the requested count matches. With neither side nor face selected,
   a through hole has two possible mounting sides. */
static ProError fastener_bso_resolve_sites(ProMdl assembly,double mm,int expected,
    BsoSite sites[12],int *count,PartText *out){
    ProSelection *selection=NULL;
    int selected=0,face=0,n=0,holes[12];
    AxComponent panel={0};
    wchar_t key[AX_PATH_DEPTH*12+4]=L"";
    *count=0;
    ProError error=ProSelbufferSelectionsGet(&selection);
    if(!error)error=ProArraySizeGet((ProArray)selection,&selected);
    if(!error&&(selected<1||selected>13||expected<0||expected>12))error=PRO_TK_BAD_INPUTS;
    for(int i=0;!error&&i<selected;i++){
        ProModelitem item;
        ProAsmcomppath path;
        wchar_t occurrence[AX_PATH_DEPTH*12+4];
        AxGeometry shape;
        error=ProSelectionModelitemGet(selection[i],&item);
        if(!error)error=ProSelectionAsmcomppathGet(selection[i],&path);
        if(!error&&(item.type!=PRO_SURFACE||path.owner!=(ProSolid)assembly||path.table_num<1||
            path.table_num>AX_PATH_DEPTH||!ax_path_label(path.comp_id_table,path.table_num,occurrence,_countof(occurrence))))
            error=PRO_TK_BAD_CONTEXT;
        if(error)break;
        if(!key[0]){
            wcscpy_s(key,_countof(key),occurrence);
            error=ax_hw_occurrence(assembly,key,mm,&panel);
        }
        if(!error&&(item.owner!=panel.model||wcscmp(key,occurrence)))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ax_geometry(&item,&shape);
        if(error)break;
        if(shape.type==AX_GEOM_PLANE&&!face)face=item.id;
        else if(shape.type==AX_GEOM_CYLINDER&&n<12)holes[n++]=item.id;
        else error=PRO_TK_BAD_INPUTS;
    }
    if(selection)ProSelectionarrayFree(selection);
    if(error||!face||(!n&&!expected)){
        part_text_add(out,L"请先选同一块板的压铆安装面。已说明孔数时可只选安装面，也可 Ctrl 同时选中 1–12 个孔壁。两侧都有孔口时需要明确安装方向。\n");
        return error?error:PRO_TK_BAD_INPUTS;
    }
    if(!n)return fastener_bso_collect(&panel,face,expected,mm,sites,count,out);
    if(expected&&n!=expected){
        part_text_add(out,L"实际选中 %d 个孔壁，要求 %d 个，未开始装配。\n",n,expected);
        return PRO_TK_BAD_INPUTS;
    }
    for(int i=0;i<n;i++){
        error=fastener_bso_target(&panel,holes[i],face,mm,&sites[i]);
        if(error){
            part_text_add(out,L"孔位 %d（SURFACE:%d）请核对 Ø5.4 mm 圆孔、实际孔口及安装侧（Creo %d）。\n",i+1,holes[i],error);
            return error;
        }
        for(int j=0;j<i;j++)if(ad_near(sites[j].geometry.contact,sites[i].geometry.contact,0.001/mm)){
            part_text_add(out,L"两个选面落在同一孔口，请每个孔只选一面。\n");
            return PRO_TK_E_FOUND;
        }
    }
    *count=n;
    return PRO_TK_NO_ERROR;
}

typedef struct {
    AxHardwareSite geometry;
    int shaft,seat;
    ProMdl model;
} BsoFitting;

/* The accepted BSO family has a Ø5 mm x 0.3 mm locating shoulder. Read
   the pair by geometry on the actual model to tolerate renumbered faces. */
static ProError fastener_bso_fitting(ProMdl model,double mm,BsoFitting *fitting){
    double units;
    ProError error=qa_mm_per_unit(model,&units);
    if(error||!part_close(units,mm))return error?error:PRO_TK_BAD_CONTEXT;
    BsoFaces faces={.model=model,.mm=units,.radius=2.5,.read_planes=1};
    if((error=fastener_bso_model_faces(&faces)))return error;
    AxComponent part={.model=model};
    int matches=0;
    for(int i=0;i<faces.hole_count;i++){
        wchar_t shaft[32];
        fastener_bso_surface(faces.holes[i],shaft);
        for(int j=0;j<faces.plane_count;j++){
            AxHardwareSite actual={0};
            wchar_t seat[32];
            fastener_bso_surface(faces.planes[j],seat);
            error=ax_hw_site(&part,shaft,seat,units,&actual);
            if(error||actual.bore)continue;
            double start,end;
            ax_hw_axial_span(&actual,actual.contact,actual.plane.direction,units,&start,&end);
            if(!part_close(start,0)||!part_close(end,0.3)||!part_close(actual.contact[2]*units,0.5))continue;
            if(matches){
                if(fitting->seat==faces.planes[j]&&ad_near(fitting->geometry.contact,actual.contact,0.001/units)&&
                    fabs(ad_dot(fitting->geometry.cylinder.direction,actual.cylinder.direction))>1-1e-7)continue;
                return PRO_TK_E_FOUND;
            }
            matches=1;
            fitting->geometry=actual;
            fitting->shaft=faces.holes[i];
            fitting->model=model;
            fitting->seat=faces.planes[j];
        }
    }
    return matches==1?PRO_TK_NO_ERROR:PRO_TK_E_NOT_FOUND;
}

static ProError fastener_bso_measure(const BsoFitting *fitting,const BsoSite *target,ProMatrix pose,double mm,
    double *radial,double *gap,double *facing,double *parallel){
    ProVector center,axis,normal,delta;
    ProError error=ProPntTrfEval((double *)fitting->geometry.contact,pose,center);
    if(!error)error=ProVectorTrfEval((double *)fitting->geometry.cylinder.direction,pose,axis);
    if(!error)error=ProVectorTrfEval((double *)fitting->geometry.plane.direction,pose,normal);
    if(error||!ad_unit(axis)||!ad_unit(normal))return error?error:PRO_TK_BAD_CONTEXT;
    const AxHardwareSite *seat=&target->geometry;
    ad_sub(center,seat->contact,delta);
    *radial=ax_hw_separation(center,seat->contact,seat->cylinder.direction,mm);
    *gap=ad_dot(delta,seat->plane.direction)*mm;
    *facing=ad_dot(normal,seat->plane.direction);
    *parallel=fabs(ad_dot(axis,seat->cylinder.direction));
    return PRO_TK_NO_ERROR;
}

static int fastener_bso_model_family(const wchar_t *name){
    return !_wcsnicmp(name,L"BSO_3_5M3X",10)||!_wcsnicmp(name,L"BSO-3_5M3X",10)||!_wcsnicmp(name,L"BSOS_3_5M3X",11);
}

typedef struct {int count,verified,id;} BsoOccupancy;

static ProError fastener_bso_occupancy(ProMdl assembly,ProMdl part,const BsoFitting *fitting,
    const BsoSite *sites,int count,double mm,BsoOccupancy occupied[12]){
    AxScan *scan=NULL;
    ProError error=ax_scan(assembly,1,&scan);
    if(error)return error;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *entry=&scan->occurrences[i];
        if(entry->type!=PRO_MDL_PART||(entry->model!=part&&!fastener_bso_model_family(entry->name)))continue;
        if(!entry->model){error=PRO_TK_INCOMPLETE;break;}
        AxComponent existing={0};
        wchar_t key[AX_PATH_DEPTH*12+4];
        if(!ax_path_label(entry->ids,entry->depth,key,_countof(key))){error=PRO_TK_BAD_CONTEXT;break;}
        error=ax_hw_occurrence(assembly,key,mm,&existing);
        ProMatrix pose;
        if(!error)error=ProAsmcomppathTrfGet(&existing.path,PRO_B_TRUE,pose);
        BsoFitting other=*fitting;
        if(!error&&entry->model!=part)error=fastener_bso_fitting(entry->model,mm,&other);
        if(error)break;
        for(int j=0;!error&&j<count;j++){
            double radial,gap,facing,parallel;
            error=fastener_bso_measure(&other,&sites[j],pose,mm,&radial,&gap,&facing,&parallel);
            if(error||radial>0.05||fabs(gap)>0.65||parallel<1-1e-6)continue;
            occupied[j].count++;
            occupied[j].id=existing.feature.id;
            if(entry->model==part&&radial<=0.001&&fabs(gap)<=0.001&&facing<-1+1e-7){
                ProBoolean packaged=PRO_B_TRUE;
                error=ProAsmcompIsPackaged(&existing.feature,&packaged);
                if(!error&&!packaged)occupied[j].verified++;
            }
        }
    }
    free(scan);
    return error;
}

static ProError fastener_bso_install_site(ProMdl assembly,ProMdl part,const BsoSite *site,
    const BsoFitting *fitting,double mm,int *id,const wchar_t **stage,PartText *out){
    AxComponent fitting_part={.model=part},panel=site->panel;
    ProAsmcomp occurrence={0};
    ProAsmcomppath root;
    ProAsmcompconstraint *rows=NULL;
    wchar_t bore[32],shaft[32],panel_face[32],fitting_face[32];
    AxGeometry base_geometry,part_geometry;
    ProMatrix seed,actual;
    ProError error;
    *id=0;
    *stage=L"构建原生参照";
    fastener_bso_surface(site->hole_id,bore);
    fastener_bso_surface(site->face_id,panel_face);
    fastener_bso_surface(fitting->shaft,shaft);
    fastener_bso_surface(fitting->seat,fitting_face);
    error=ax_root_path(assembly,&root);
    if(!error)error=ProArrayAlloc(0,sizeof(*rows),2,(ProArray *)&rows);
    if(!error)error=ax_add_relation(&rows,&panel,&fitting_part,bore,shaft,PRO_ASM_INSERT,0,&base_geometry,&part_geometry);
    if(!error)error=ax_add_relation(&rows,&panel,&fitting_part,panel_face,fitting_face,PRO_ASM_MATE,0,&base_geometry,&part_geometry);
    if(error)goto done;
    ax_hw_seat_pose(&fitting->geometry,&site->geometry,seed);
    *stage=L"装入";
    error=ProAsmcompAssemble((ProAssembly)assembly,(ProSolid)part,seed,&occurrence);
    if(error)goto done;
    *stage=L"设置同轴和端面贴合";
    error=ProAsmcompConstraintsSet(&root,&occurrence,rows);
    if(error)goto done;
    *stage=L"再生";
    if((error=part_regenerate(assembly))||(error=part_health(assembly)))goto done;
    *stage=L"读回型号、定位及朝向";
    ProMdl actual_part=NULL;
    ProBoolean packaged=PRO_B_TRUE;
    if((error=ProAsmcompMdlGet(&occurrence,&actual_part))||(error=ProAsmcompIsPackaged(&occurrence,&packaged))||
        (error=ax_compare_constraints(&occurrence,&root,rows,2,mm))||
        (error=ax_actual_pose(assembly,&occurrence,actual)))goto done;
    double radial,gap,facing,parallel;
    error=fastener_bso_measure(fitting,site,actual,mm,&radial,&gap,&facing,&parallel);
    if(!error&&(actual_part!=part||packaged||radial>0.001||fabs(gap)>0.001||facing>-1+1e-7||parallel<1-1e-7))
        error=PRO_TK_INCOMPLETE;
    if(!error)*id=occurrence.id;
done:
    if(error&&occurrence.id>0)ax_discard_occurrence(assembly,&occurrence,out);
    if(rows)ProAsmcompconstraintArrayFree(rows);
    return error;
}

static ProError fastener_bso_place(ProMdl assembly,ProMdl part,BsoSite sites[12],int count,double mm,PartText *out){
    if(!assembly||!part||count<1||count>12||mm<=0)return PRO_TK_BAD_INPUTS;
    if(ax_recovery_uncertain){
        part_text_add(out,L"前一条装配回退仍待核对，请先核对装配状态。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    ProError error=ax_master(assembly);
    if(!error)error=part_health(assembly);
    if(error){
        part_text_add(out,L"当前装配的主表示或再生状态未通过核对（Creo %d）。\n",error);
        return error;
    }
    BsoFitting fitting={0};
    error=fastener_bso_fitting(part,mm,&fitting);
    if(error){
        part_text_add(out,L"库件的压铆肩与接触端面未能按实际几何唯一确认（Creo %d）。\n",error);
        return error;
    }
    BsoOccupancy occupied[12]={0};
    error=fastener_bso_occupancy(assembly,part,&fitting,sites,count,mm,occupied);
    if(error){
        part_text_add(out,L"装配中的压铆件状态无法完整读回（Creo %d），无法确认是否会重复安装。\n",error);
        return error;
    }
    int added=0,reused=0;
    for(int i=0;i<count;i++){
        const int hole=sites[i].hole_id;
        if(occupied[i].count){
            if(occupied[i].count==1&&occupied[i].verified){
                part_text_add(out,L"孔 %d SURFACE:%d 保留已验 ID:%d。\n",i+1,hole,occupied[i].id);
                reused++;
            }else part_text_add(out,L"孔 %d SURFACE:%d 已有 %d 个疑似占位实例，保留并请核对规格或定位。\n",i+1,hole,occupied[i].count);
            continue;
        }
        BsoSite current={0};
        AxComponent panel=sites[i].panel;
        error=fastener_bso_target(&panel,hole,sites[i].face_id,mm,&current);
        const wchar_t *stage=L"复核当前孔轴和安装面";
        int id=0;
        if(!error&&(!ad_near(current.geometry.contact,sites[i].geometry.contact,0.001/mm)||
            ad_dot(current.geometry.plane.direction,sites[i].geometry.plane.direction)<1-1e-7))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=fastener_bso_install_site(assembly,part,&current,&fitting,mm,&id,&stage,out);
        if(error){
            part_text_add(out,L"孔 %d SURFACE:%d，%ls失败（Creo %d）。\n",i+1,hole,stage,error);
            if(ax_recovery_uncertain){
                part_text_add(out,L"后续 %d 孔待核对。\n",count-i-1);
                break;
            }
        }else{
            part_text_add(out,L"孔 %d SURFACE:%d 已装 ID:%d，同轴、零端面偏移、朝向及再生通过。\n",i+1,hole,id);
            added++;
        }
    }
    part_text_add(out,L"实际核对 %d/%d 孔，新增 %d，已装复用 %d，待处理 %d。本次装配未自动保存。\n",
        added+reused,count,added,reused,count-added-reused);
    return added+reused==count?PRO_TK_NO_ERROR:PRO_TK_INCOMPLETE;
}
