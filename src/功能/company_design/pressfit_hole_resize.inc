/* Edit one explicitly picked, independently driven hole before placing the
   requested library instance. Use the existing native sketch/diameter driver. */
static ProError pressfit_chat_resize_hole(FastenerPressfitTarget *target,
        FastenerPressfitRequest *request,double desired,PartText *out){
    AxComponent panel={0};
    int holes[12],face=0,n=0;
    ProError error=fastener_pressfit_read_selection(target->assembly,target->mm,&panel,&face,holes,&n);
    if(error||!face||n!=1||!storage_output_directory(target->directory)){
        part_text_add(out,L"改孔需要在 E 盘项目的装配里明确选一张安装面和一个孔壁。\n");
        return error?error:PRO_TK_BAD_CONTEXT;
    }
    wchar_t hole[32],side[32];
    fastener_bso_surface(holes[0],hole);
    fastener_bso_surface(face,side);
    AxHardwareSite old_site;
    error=ax_hw_site(&panel,hole,side,target->mm,&old_site);
    if(error||!old_site.bore)return error?error:PRO_TK_BAD_CONTEXT;
    const double current=2*old_site.cylinder.radius*target->mm;
    if(fabs(current-desired)<=0.001)return PRO_TK_NO_ERROR;

    AxScan *scan=NULL;
    error=ax_scan(target->assembly,1,&scan);
    if(error)return error;
    const int private_panel=scan->count==1&&!scan->limited&&!scan->unavailable&&
        scan->occurrences[0].model==panel.model;
    free(scan);
    if(!private_panel){
        part_text_add(out,L"板件所在装配还有其他实例或依赖。请先在独立板件上改孔并核对，再继续装配。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    ProGeomitem item;
    ProFeature feature;
    ProFeattype kind;
    ProPatternStatus pattern;
    ProSurface surface;
    ProGeomitemdata *raw=NULL;
    if((error=ProModelitemInit(panel.model,holes[0],PRO_SURFACE,&item))||
       (error=ProGeomitemFeatureGet(&item,&feature))||(error=ProFeatureTypeGet(&feature,&kind))||
       (error=ProFeaturePatternStatusGet(&feature,&pattern))||(error=ProGeomitemToSurface(&item,&surface))||
       (error=ProSurfaceDataGet(surface,&raw)))return error;
    const ProCylinderdata cylinder=raw->data.p_surface_data->srf_shape.cylinder;
    ProGeomitemdataFree(&raw);
    MarkupDriver driver={.diameter=2*cylinder.radius,.owner=feature.id};
    int editable=pattern==PRO_PATTERN_NONE&&
        (kind==PRO_FEAT_HOLE?(!ProFeatureDimensionVisit(&feature,markup_driver,NULL,&driver)&&driver.count==1):
         (kind==PRO_FEAT_CUT||kind==PRO_FEAT_CUT_SMT)&&markup_cut_driver(&feature,&cylinder,&driver));
    if(!editable){
        part_text_add(out,L"选中孔没有独立的驱动孔径，或属于共享尺寸的阵列。本轮仍保留原孔。\n");
        return PRO_TK_UNSUPPORTED;
    }

    double native_mm,old,new_value;
    ProDimensiontype type;
    if((error=qa_mm_per_unit(panel.model,&native_mm))||(error=ProDimensionTypeGet(&driver.dimension,&type))||
       (error=ProDimensionValueGet(&driver.dimension,&old)))return error;
    new_value=desired/(native_mm*(type==PRODIMTYPE_RADIUS?2.0:1.0));
    PartSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    if(!snapshot)return PRO_TK_OUT_OF_MEMORY;
    wchar_t detail[640]=L"";
    error=part_snapshot_take_mode(panel.model,snapshot,0);
    if(!error&&snapshot->unhealthy)error=PRO_TK_BAD_CONTEXT;
    int changed=0;
    if(!error){
        changed=1;
        error=part_write_verified_dimensions(panel.model,snapshot,&driver.dimension,&old,&new_value,1,detail,_countof(detail));
    }
    AxHardwareSite checked;
    if(!error)error=ax_hw_site(&panel,hole,side,target->mm,&checked);
    if(!error&&(!checked.bore||fabs(2*checked.cylinder.radius*target->mm-desired)>0.001||
       ax_hw_separation(checked.contact,old_site.contact,old_site.cylinder.direction,target->mm)>0.001||
       fabs(ad_dot(checked.cylinder.direction,old_site.cylinder.direction))<1-1e-7))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=part_regenerate(target->assembly);
    if(error&&changed){
        wchar_t restore[640]=L"";
        const ProError rolled=part_write_verified_dimensions(panel.model,snapshot,&driver.dimension,
            &new_value,&old,1,restore,_countof(restore));
        if(rolled||part_regenerate(target->assembly)||ax_hw_site(&panel,hole,side,target->mm,&checked)||
           fabs(2*checked.cylinder.radius*target->mm-current)>0.001){
            ax_recovery_uncertain=1;
            part_text_add(out,L"孔径恢复待检查，已暂停后续装配。\n");
        }else part_text_add(out,L"改孔读回失败，已复核原孔径 Ø%.6g mm。\n",current);
    }
    part_snapshot_clear(snapshot);
    free(snapshot);
    if(error)return error;
    if(target->stamp)ProVerstampFree(&target->stamp);
    target->stamp=NULL;
    error=ProMdlVerstampGet(target->assembly,&target->stamp);
    if(error)return error;
    request->expected=1;
    request->face_id=face;
    request->mapped_count=1;
    request->mapped_holes[0]=holes[0];
    if(!ax_path_label(panel.path.comp_id_table,panel.path.table_num,request->panel_path,_countof(request->panel_path)))
        return PRO_TK_BAD_CONTEXT;
    part_text_add(out,L"已把所选独立圆孔从 Ø%.6g 改为 Ø%.6g mm，轴线与再生读回通过。未保存。\n",current,desired);
    return PRO_TK_NO_ERROR;
}