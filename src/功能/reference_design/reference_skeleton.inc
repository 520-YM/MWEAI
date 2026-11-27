static ProError reference_skeleton_plane(ProMdl skeleton,const wchar_t *name,int axis,double *position){
    ProFeature feature;
    ProFeattype type;
    ProGeomitem plane;
    ProVector point,normal;
    ProError error=ux_named_feature(skeleton,name,&feature);
    if(!error)error=ProFeatureTypeGet(&feature,&type);
    if(!error&&type!=PRO_FEAT_DATUM)error=PRO_TK_INVALID_TYPE;
    if(!error)error=fd_feature_geom(&feature,PRO_SURFACE,&plane);
    if(!error)error=fd_plane_data(&plane,point,normal);
    if(error)return error;
    if(!isfinite(point[axis])||fabs(fabs(normal[axis])-1)>1e-7)return PRO_TK_BAD_CONTEXT;
    *position=point[axis];
    return PRO_TK_NO_ERROR;
}

static ProError reference_skeleton_size(const LocalControlContext *context,ReferenceDesign *plan,PartText *out){
    if(!plan->skeleton[0]&&!plan->current_skeleton)return PRO_TK_NO_ERROR;
    const wchar_t *symbols[]={L"BW_WIDTH",L"BW_DEPTH",L"BW_HEIGHT"};
    ProMdl skeleton=NULL;
    ProMdlName name=L"";
    ProBoolean verified=PRO_B_FALSE;
    double mm=0,span=0;
    ProError error;
    if(plan->current_skeleton){
        error=context->model&&context->model==context->owner&&context->type==PRO_MDL_ASSEMBLY?
            ProAsmSkeletonGet((ProAssembly)context->model,&skeleton):PRO_TK_BAD_CONTEXT;
        if(error){
            part_text_add(out,L"当前窗口需打开目标装配，且 Creo 必须能读取该装配的骨架（Creo %d）。\n",error);
            return error;
        }
    }else error=reference_load(context->directory,plan->skeleton,PRO_MDL_PART,&skeleton,out);
    if(!error)error=ProMdlIsSkeleton(skeleton,&verified);
    if(!error&&!verified)error=PRO_TK_INVALID_TYPE;
    if(!error)error=ProMdlMdlnameGet(skeleton,name);
    if(!error)error=qa_mm_per_unit(skeleton,&mm);
    if(error)return error;

    if(plan->skeleton_planes[0][0]){
        double positions[2];
        for(int i=0;i<2;i++){
            error=reference_skeleton_plane(skeleton,plan->skeleton_planes[i],plan->axis,&positions[i]);
            if(error){
                part_text_add(out,L"骨架 %ls 的 %ls 必须为法向平行 %lc 轴的原生基准面（Creo %d）。\n",
                    name,plan->skeleton_planes[i],L'X'+plan->axis,error);
                return error;
            }
        }
        span=fabs(positions[1]-positions[0])*mm;
        part_text_add(out,L"骨架 %ls 基准面 %ls 至 %ls 的 %lc 向间距 %.8g mm。\n",
            name,plan->skeleton_planes[0],plan->skeleton_planes[1],L'X'+plan->axis,span);
    }else if(plan->current_skeleton){
        UxDimension dim;
        error=ux_dimension(skeleton,symbols[plan->axis],&dim);
        if(error){
            part_text_add(out,L"骨架 %ls 未读到唯一尺寸 %ls（Creo %d），可明确指定两道基准面。\n",
                name,symbols[plan->axis],error);
            return error;
        }
        span=dim.value*mm;
        part_text_add(out,L"骨架 %ls 的尺寸 %ls 实读 %.8g mm。\n",name,symbols[plan->axis],span);
    }else{
        double extents[3];
        error=imitation_extents(skeleton,mm,extents);
        if(error)return error;
        span=extents[plan->axis];
        part_text_add(out,L"骨架 %ls 的 %lc 外廓 %.8g mm。\n",name,L'X'+plan->axis,span);
    }
    plan->size=span-plan->clearance;
    if(!isfinite(plan->size)||plan->size<0.1||plan->size>10000)return PRO_TK_BAD_INPUTS;
    part_text_add(out,L"扣除总间隙 %.8g mm，新件 %lc 向目标 %.8g mm。\n",
        plan->clearance,L'X'+plan->axis,plan->size);
    return PRO_TK_NO_ERROR;
}
