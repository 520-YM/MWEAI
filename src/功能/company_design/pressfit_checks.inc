/* Audit the exact placed occurrence against the intended panel and other solids. */
static ProError fastener_pressfit_pair_fit(ProSelection first,ProSelection second,double mm,
        double *gap,double *overlap){
    double distance=0,volume=0;
    Pro3dPnt nearest[2];
    ProBoolean interfering=PRO_B_FALSE;
    ProError error=ProFitClearanceCompute(first,PRO_FIT_INCLUDE_NONE,second,PRO_FIT_INCLUDE_NONE,
        PRO_FIT_WHOLE_SURFACE,NULL,PRO_B_FALSE,&distance,NULL,&interfering,nearest);
    *gap=0;
    *overlap=0;
    if(!error&&!interfering){
        if(!isfinite(distance)||distance<0)return PRO_TK_GENERAL_ERROR;
        *gap=distance*mm;
        return PRO_TK_NO_ERROR;
    }
    const ProError volume_error=ax_interference_volume(first,second,&volume);
    if(volume_error)return volume_error;
    if(error&&!volume)return error;
    *overlap=volume*mm*mm*mm;
    return PRO_TK_NO_ERROR;
}

static double fastener_pressfit_outline_gap(ProOutline first,ProOutline second){
    double distance2=0;
    for(int axis=0;axis<3;axis++){
        const double offset=fmax(0,fmax(first[0][axis]-second[1][axis],second[0][axis]-first[1][axis]));
        distance2+=offset*offset;
    }
    return sqrt(distance2);
}

static ProError fastener_pressfit_postfit_except(ProMdl assembly,const BsoSite *site,int installed_id,
        int old_id,double mm,double allowed_interference,PartText *out){
    wchar_t installed_key[24];
    swprintf_s(installed_key,_countof(installed_key),L"ID:%d",installed_id);
    AxComponent installed={0},panel=site->panel;
    ProSelection moving=NULL,mount=NULL;
    AxScan *scan=NULL;
    double seat_gap=0,seat_overlap=0;
    const AxDeviceFrame world={.axis={{1,0,0},{0,1,0},{0,0,1}}};
    ProOutline moving_box={0};
    ProError error=ax_component_resolve(assembly,installed_key,&installed);
    if(!error)error=ax_part_selection(&installed,&moving);
    if(!error)error=ax_part_selection(&panel,&mount);
    if(!error)error=fastener_pressfit_pair_fit(moving,mount,mm,&seat_gap,&seat_overlap);
    if(error){
        part_text_add(out,L"实例 %d 与安装板的实体干涉未能核对（Creo %d）。\n",installed_id,error);
        goto done;
    }
    if(seat_overlap>0.001){
        double start,end;
        ax_hw_axial_span(&site->geometry,site->geometry.contact,site->geometry.plane.direction,mm,&start,&end);
        const double hole_radius=site->geometry.cylinder.radius*mm;
        const double maximum_radius=hole_radius+allowed_interference*0.5;
        const double maximum_volume=AS_PI*(maximum_radius*maximum_radius-hole_radius*hole_radius)*(end-start);
        if(!isfinite(maximum_volume)||seat_overlap>maximum_volume+0.001){
            part_text_add(out,L"实例 %d 与安装板干涉 %.8g mm³，超过按孔深和最大过盈算出的包络 %.8g mm³。\n",
                installed_id,seat_overlap,maximum_volume);
            error=PRO_TK_BAD_CONTEXT;
            goto done;
        }
        part_text_add(out,L"实例 %d 与安装板重叠 %.8g mm³，最大授权过盈包络 %.8g mm³。\n",
            installed_id,seat_overlap,maximum_volume);
    }
    if((error=ax_scan(assembly,1,&scan)))goto done;
    if(scan->limited||scan->unavailable){
        part_text_add(out,L"活动装配树存在未读全的实例；无法确认周边实体干涉。\n");
        error=PRO_TK_INCOMPLETE;
        goto done;
    }
    const int has_moving_box=!ax_device_world_bounds(&installed,&world,moving_box);
    int compared=0;
    double nearest=HUGE_VAL;
    wchar_t nearest_key[AX_PATH_DEPTH*12+4]=L"";
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *item=&scan->occurrences[i];
        if(item->state!=PRO_FEAT_ACTIVE||item->type!=PRO_MDL_PART||!item->model)continue;
        if(item->depth==1&&(item->ids[0]==installed_id||item->ids[0]==old_id))continue;
        if(ax_field_path_matches(&panel.path,item))continue;
        ProBoolean skeleton=PRO_B_FALSE;
        error=ProMdlIsSkeleton(item->model,&skeleton);
        if(error)break;
        if(skeleton)continue;
        double measured=0,overlap=0;
        wchar_t key[AX_PATH_DEPTH*12+4];
        AxComponent other={0};
        if(!ax_path_label(item->ids,item->depth,key,_countof(key))){error=PRO_TK_BAD_CONTEXT;break;}
        error=ax_component_resolve(assembly,key,&other);
        if(error)break;
        if(has_moving_box&&isfinite(nearest)){
            ProOutline bounds;
            if(!ax_device_world_bounds(&other,&world,bounds)&&
               fastener_pressfit_outline_gap(moving_box,bounds)*mm>fmax(nearest,0.001)+0.001){
                compared++;
                continue;
            }
        }
        ProSelection selected=NULL;
        error=ax_part_selection(&other,&selected);
        if(!error)error=fastener_pressfit_pair_fit(moving,selected,mm,&measured,&overlap);
        if(selected)ProSelectionFree(&selected);
        if(error){
            part_text_add(out,L"实例 %d 与 %ls（%ls）的实体间隙未能核对（Creo %d）。\n",
                installed_id,key,item->name,error);
            break;
        }
        compared++;
        if(overlap>0.001){
            part_text_add(out,L"实例 %d 与周边 %ls（%ls）干涉 %.8g mm³。\n",installed_id,key,item->name,overlap);
            error=PRO_TK_BAD_CONTEXT;
        }else if(measured<nearest){
            nearest=measured;
            wcscpy_s(nearest_key,_countof(nearest_key),key);
        }
    }
    if(!error&&compared)
        part_text_add(out,L"实例 %d 已核对 %d 个周边零件，最小实体间隙 %.8g mm（实例 %ls）。\n",
            installed_id,compared,nearest,nearest_key);
    else if(!error)
        part_text_add(out,L"实例 %d 安装板实体核对通过，当前装配无其他活动实体零件。\n",installed_id);
done:
    if(moving)ProSelectionFree(&moving);
    if(mount)ProSelectionFree(&mount);
    free(scan);
    return error;
}

static ProError fastener_pressfit_postfit(ProMdl assembly,const BsoSite *site,int installed_id,
        double mm,double allowed_interference,PartText *out){
    return fastener_pressfit_postfit_except(assembly,site,installed_id,0,mm,allowed_interference,out);
}