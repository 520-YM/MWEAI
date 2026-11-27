#include <ProLayer.h>

typedef struct {
    AxComponent component;
    int surface,feature;
    ProSrftype type;
    double local_point[3],root_point[3];
} MarkupAssemblyPick;

static ProError markup_path_pose(const ProAsmcomppath *path,ProBoolean bottom_up,ProMatrix pose){
    ProAsmcomppath copy=*path;
    return ProAsmcomppathTrfGet(&copy,bottom_up,pose);
}

static int markup_path_visible(const ProAsmcomppath *path){
    ProBoolean visible;
    ProAsmcomppath copy=*path;
    ProError error=ProAsmcompVisibilityGet(&copy,NULL,&visible);
    if(!error&&!visible)return 0;
    ProMdl owner=(ProMdl)path->owner;
    for(int i=0;i<path->table_num;i++){
        ProAsmcomp component;
        ProBoolean hidden=PRO_B_FALSE;
        if(ax_component_feature(owner,path->comp_id_table[i],&component)||
           (!ProModelitemIsHidden((ProModelitem *)&component,&hidden)&&hidden)||
           ProAsmcompMdlGet(&component,&owner))return 0;
    }
    return 1;
}

static int markup_member_matches(const MarkupCapture *capture,ProMdl model){
    for(int i=0;i<capture->member_count;i++){
        const MarkupMemberStamp *member=&capture->members[i];
        if(member->model!=model)continue;
        ProWVerstamp current=NULL;
        if(!member->stamp||ProMdlVerstampGet(model,&current))return 0;
        int same=ProVerstampEqual(member->stamp,current);
        ProVerstampFree(&current);
        return same;
    }
    return 0;
}

/* The entire occurrence chain is bound to the capture. A leaf can change
   without changing the version stamp of the displayed top-level assembly. */
static ProError markup_assembly_component(const MarkupCapture *capture,const MarkupOccurrence *occurrence,AxComponent *out){
    if(!capture->assembly||occurrence->depth<1||occurrence->depth>AX_PATH_DEPTH)return PRO_TK_BAD_INPUTS;
    ProError error=markup_check_capture(capture);
    if(error)return error;
    ProMdl owner=capture->model;
    for(int i=0;i<occurrence->depth;i++){
        ProFeatStatus status;
        ProMdlType type;
        if((error=ProMdlTypeGet(owner,&type)))return error;
        if(type!=PRO_MDL_ASSEMBLY)return PRO_TK_BAD_CONTEXT;
        if((error=ax_component_feature(owner,occurrence->ids[i],&out->feature))||
           (error=ProFeatureStatusGet(&out->feature,&status)))return error;
        if(status!=PRO_FEAT_ACTIVE)return PRO_TK_BAD_CONTEXT;
        if((error=ProAsmcompMdlGet(&out->feature,&owner)))return error;
        if(!markup_member_matches(capture,owner))return PRO_TK_BAD_CONTEXT;
    }
    ProIdTable ids={0};
    memcpy(ids,occurrence->ids,occurrence->depth*sizeof(*ids));
    error=ProAsmcomppathInit((ProSolid)capture->model,ids,occurrence->depth,&out->path);
    if(!error&&!markup_path_visible(&out->path))return PRO_TK_BAD_CONTEXT;
    out->model=owner;
    return error;
}

typedef struct {
    MarkupOccurrence occurrence;
    ProModelitem item;
    double local[3],world[3],depth;
} MarkupAssemblyHit;

/* Get the actual component path and root-space depth of each candidate
   surface. Toolkit returns its points in each member's native coordinates. */
static ProError markup_assembly_hit(ProMdl assembly,ProRay *ray,MarkupAssemblyHit *nearest){
    ProSelection *items=NULL;
    int count=0,found=0,ambiguous=0;
    ProError error=ProSolidRayIntersectionCompute(assembly,0.1,ray,&items,&count);
    if(error)return error;
    for(int i=0;i<count;i++){
        ProAsmcomppath path;
        MarkupAssemblyHit hit={0};
        ProSurface surface;
        ProBoolean datum,hidden;
        ProQuilt quilt;
        ProMatrix pose;
        if(ProSelectionModelitemGet(items[i],&hit.item)||hit.item.type!=PRO_SURFACE||
           ProSelectionAsmcomppathGet(items[i],&path)||path.table_num<1||path.table_num>AX_PATH_DEPTH||
           (ProMdl)path.owner!=assembly||!markup_path_visible(&path)||
           ProGeomitemToSurface(&hit.item,&surface)||
           ProSurfaceIsDatumPlane((ProSolid)hit.item.owner,surface,&datum)||datum||
           (!ProModelitemIsHidden(&hit.item,&hidden)&&hidden)||
           ProSurfaceQuiltGet((ProSolid)hit.item.owner,surface,&quilt)!=PRO_TK_E_NOT_FOUND||
           ProSelectionPoint3dGet(items[i],hit.local)||markup_path_pose(&path,PRO_B_TRUE,pose)||
           ProPntTrfEval(hit.local,pose,hit.world))continue;
        double depth=0;
        for(int j=0;j<3;j++)depth+=(hit.world[j]-ray->start_point[j])*ray->dir_vector[j];
        if(!isfinite(depth)||depth<0)continue;
        hit.depth=depth;
        hit.occurrence.depth=path.table_num;
        memcpy(hit.occurrence.ids,path.comp_id_table,path.table_num*sizeof(int));
        if(!found||depth<nearest->depth-1e-6){
            *nearest=hit;
            found=1;
            ambiguous=0;
        }else if(fabs(depth-nearest->depth)<=1e-6&&
                 (!markup_same_occurrence(&hit.occurrence,&nearest->occurrence)||hit.item.id!=nearest->item.id))ambiguous=1;
    }
    if(!found)return PRO_TK_E_NOT_FOUND;
    return ambiguous?PRO_TK_E_FOUND:PRO_TK_NO_ERROR;
}

static int markup_assembly_visible(const MarkupProjection *map,const ProPoint3d center){
    ProPoint3d p={center[0],center[1],center[2]},root;
    if(ProPntTrfEval(p,(double (*)[4])map->to_root,root))return 0;
    ProRay ray;
    for(int i=0;i<3;i++){
        ray.start_point[i]=root[i]+map->span*map->root_toward[i];
        ray.dir_vector[i]=-map->root_toward[i];
    }
    MarkupAssemblyHit hit;
    ProError error=markup_assembly_hit(map->assembly,&ray,&hit);
    if(error==PRO_TK_E_NOT_FOUND)return 1;
    return !error&&hit.depth>=map->span-1e-5*fmax(1.0,map->span);
}

static ProError markup_assembly_pick(const MarkupCapture *capture,const MarkupMark *mark,MarkupAssemblyPick *pick){
    if(!capture->assembly)return PRO_TK_INVALID_TYPE;
    ProError error=markup_check_capture(capture);
    if(error)return error;
    ProRay ray;
    MarkupAssemblyHit hit;
    if((error=markup_mark_ray(capture,mark,&ray))||
       (error=markup_assembly_hit(capture->model,&ray,&hit))||
       (error=markup_assembly_component(capture,&hit.occurrence,&pick->component)))return error;
    if(hit.item.owner!=pick->component.model)return PRO_TK_BAD_CONTEXT;
    ProSurface surface;
    ProFeature feature;
    if((error=ProGeomitemToSurface(&hit.item,&surface))||
       (error=ProSurfaceTypeGet(surface,&pick->type))||
       (error=ProGeomitemFeatureGet(&hit.item,&feature)))return error;
    pick->surface=hit.item.id;
    pick->feature=feature.id;
    memcpy(pick->local_point,hit.local,sizeof(hit.local));
    memcpy(pick->root_point,hit.world,sizeof(hit.world));
    return PRO_TK_NO_ERROR;
}

static ProError markup_assembly_collect(MarkupCapture *capture,MarkupProjection *root){
    AxScan *scan=NULL;
    ProError error=ax_scan(capture->model,0,&scan);
    if(error)return error;
    if(scan->count){
        capture->members=calloc(scan->count,sizeof(*capture->members));
        if(!capture->members){free(scan);return PRO_TK_OUT_OF_MEMORY;}
    }
    for(int i=0;i<scan->count;i++){
        const AxOccurrence *item=&scan->occurrences[i];
        if(!item->model||item->state!=PRO_FEAT_ACTIVE)continue;
        int j=0;
        while(j<capture->member_count&&capture->members[j].model!=item->model)j++;
        if(j==capture->member_count){
            MarkupMemberStamp *member=&capture->members[capture->member_count++];
            member->model=item->model;
            ProMdlVerstampGet(item->model,&member->stamp);
        }
        if(item->type!=PRO_MDL_PART)continue;
        ProIdTable ids={0};
        ProAsmcomppath path;
        ProMatrix inverse;
        ProBoolean skeleton=PRO_B_FALSE;
        MarkupProjection child=*root;
        memcpy(ids,item->ids,item->depth*sizeof(int));
        if((!ProMdlIsSkeleton(item->model,&skeleton)&&skeleton)||
           ProAsmcomppathInit((ProSolid)capture->model,ids,item->depth,&path)||!markup_path_visible(&path)||
           markup_path_pose(&path,PRO_B_TRUE,child.to_root)||
           markup_path_pose(&path,PRO_B_FALSE,inverse)||
           ProVectorTrfEval(root->toward_viewer,inverse,child.toward_viewer)||
           !ad_unit(child.toward_viewer))continue;
        child.assembly=capture->model;
        child.model=item->model;
        memcpy(child.root_toward,root->toward_viewer,sizeof(child.root_toward));
        child.occurrence.depth=item->depth;
        child.occurrence.mm_per_native=markup_mm_scale(item->model);
        memcpy(child.occurrence.ids,ids,item->depth*sizeof(int));
        /* The screen aperture filters small/far geometry; explicit native
           surface reads still work when no editable silhouettes are visible. */
        error=ProSolidSurfaceVisit((ProSolid)child.model,markup_visit_surface,NULL,&child);
        if(error&&error!=PRO_TK_E_NOT_FOUND){root->image->truncated=1;break;}
        error=ProSolidFeatVisit((ProSolid)child.model,markup_opening_feature,NULL,&child);
        if(error&&error!=PRO_TK_E_NOT_FOUND){root->image->openings_truncated=1;break;}
    }
    if(scan->limited){
        root->image->truncated=1;
        root->image->openings_truncated=1;
    }
    free(scan);
    return PRO_TK_NO_ERROR;
}

typedef struct {
    AxComponent component;
    int surface;
    ProSrftype type;
    int bore;
    double position[3],axis[3],diameter;
} MarkupAssemblySite;

static ProError markup_assembly_site(const MarkupCapture *capture,const MarkupMark *mark,MarkupAssemblySite *site){
    int hole=markup_match(capture->image,mark),opening=markup_match_opening(capture->image,mark);
    ProError error;
    if(hole==-2||opening==-2||(hole>=0&&opening>=0))return PRO_TK_E_FOUND;
    if(hole>=0){
        const MarkupHole *bore=&capture->image->holes[hole];
        ProMatrix pose;
        ProPoint3d center;
        ProVector axis;
        memcpy(center,bore->center,sizeof(center));
        memcpy(axis,bore->axis,sizeof(axis));
        if((error=markup_assembly_component(capture,&bore->occurrence,&site->component))||
           (error=markup_path_pose(&site->component.path,PRO_B_TRUE,pose))||
           (error=ProPntTrfEval(center,pose,site->position))||
           (error=ProVectorTrfEval(axis,pose,site->axis)))return error;
        if(!ad_unit(site->axis)||bore->occurrence.mm_per_native<=0)return PRO_TK_INCOMPLETE;
        site->bore=1;
        site->surface=bore->surface;
        site->type=PRO_SRF_CYL;
        site->diameter=bore->diameter*bore->occurrence.mm_per_native;
        return PRO_TK_NO_ERROR;
    }
    if(opening>=0)return markup_assembly_component(capture,&capture->image->openings[opening].occurrence,&site->component);
    MarkupAssemblyPick hit={0};
    if((error=markup_assembly_pick(capture,mark,&hit)))return error;
    site->component=hit.component;
    site->surface=hit.surface;
    site->type=hit.type;
    memcpy(site->position,hit.root_point,sizeof(hit.root_point));
    return PRO_TK_NO_ERROR;
}

static ProError markup_assembly_highlight(AxComponent *component,ProSelection *selection){
    ProModelitem item;
    ProError error=ProMdlToModelitem(component->model,&item);
    if(!error)error=ProSelectionAlloc(&component->path,&item,selection);
    if(!error)error=ProSelectionHighlight(*selection,PRO_COLOR_HIGHLITE);
    if(error&&*selection)ProSelectionFree(selection);
    return error;
}

static ProError markup_assembly_gap(MarkupAssemblySite *a,MarkupAssemblySite *b,double mm,PartText *out){
    ProSelection pair[2]={0};
    ProBoolean overlap=PRO_B_FALSE;
    Pro3dPnt nearest[2];
    double distance=0;
    ProError error=ax_part_selection(&a->component,&pair[0]);
    if(!error)error=ax_part_selection(&b->component,&pair[1]);
    if(!error)error=ProFitClearanceCompute(pair[0],PRO_FIT_INCLUDE_NONE,pair[1],PRO_FIT_INCLUDE_NONE,
        PRO_FIT_WHOLE_SURFACE,NULL,PRO_B_FALSE,&distance,NULL,&overlap,nearest);
    if(error||(!overlap&&(!isfinite(distance)||distance<0))){
        part_text_add(out,L"\nCreo 未能核实所选组件的最小间隙。");
        if(!error)error=PRO_TK_INCOMPLETE;
    }else if(overlap){
        double volume;
        ProError volume_error=ax_interference_volume(pair[0],pair[1],&volume);
        if(!volume_error)part_text_add(out,L"\n所选组件存在实体干涉，体积 %.8g mm³。",volume*mm*mm*mm);
        else part_text_add(out,L"\n所选组件存在实体干涉，Creo 未能计算干涉体积。");
    }else part_text_add(out,L"\n所选组件最小间隙 %.8g mm，未检测到实体干涉。",distance*mm);
    for(int i=0;i<2;i++)if(pair[i])ProSelectionFree(&pair[i]);
    return error;
}

static ProError markup_assembly_read(const MarkupCapture *capture,const MarkupRequest *request,int mode,wchar_t *reply,size_t capacity){
    if(!reply||!capacity)return PRO_TK_BAD_INPUTS;
    reply[0]=0;
    if(!capture->assembly||!capture->image||request->ticket!=capture->ticket||markup_check_capture(capture)){
        wcscpy_s(reply,capacity,L"截图对应的装配已变化，请重新截图。");
        return PRO_TK_BAD_CONTEXT;
    }
    if(request->count<1||request->count>MARKUP_MAX_MARKS||(mode&&request->count!=2)){
        wcscpy_s(reply,capacity,mode?L"分别标注两个要测量的组件、孔或面。":L"请先在模型区圈选组件或用箭头指向组件。");
        return PRO_TK_BAD_INPUTS;
    }
    MarkupAssemblySite sites[MARKUP_MAX_MARKS]={0};
    ProError first=PRO_TK_NO_ERROR;
    PartText out={reply,capacity,0};
    part_text_add(&out,L"装配 %ls · Creo 原生回读",capture->image->name);
    markup_assembly_clear_highlights();
    for(int i=0;i<request->count;i++){
        MarkupAssemblySite *site=&sites[i];
        wchar_t label[128];
        ProMdlName name;
        ProError error=markup_assembly_site(capture,&request->marks[i],site);
        if(!error)error=ProMdlMdlnameGet(site->component.model,name);
        if(!error&&!ax_path_label(site->component.path.comp_id_table,site->component.path.table_num,label,_countof(label)))
            error=PRO_TK_OUT_OF_RANGE;
        if(error){
            if(!first)first=error;
            part_text_add(&out,L"\n%d 号%ls。",i+1,error==PRO_TK_E_FOUND?L"包含多个候选，请缩小标注":
                error==PRO_TK_BAD_CONTEXT?L"所在组件状态已变化，请重新截图":L"未命中可读组件");
            continue;
        }
        part_text_add(&out,L"\n%d 号 · %ls.PRT · %ls",i+1,name,label);
        if(site->bore)part_text_add(&out,L" · Ø%.8g mm · 孔壁 %d",site->diameter,site->surface);
        else if(site->surface)part_text_add(&out,L" · %ls %d",site->type==PRO_SRF_PLANE?L"平面":L"面",site->surface);
        if(!markup_assembly_highlight(&site->component,&markup_assembly_highlights[i]))part_text_add(&out,L" · 已高亮");
    }
    if(first||!mode)return first;
    double mm=capture->image->mm_per_native;
    if(!isfinite(mm)||mm<=0){
        part_text_add(&out,L"\n无法核对装配单位，未报告尺寸。");
        return PRO_TK_INCOMPLETE;
    }
    const MarkupAssemblySite *a=&sites[0],*b=&sites[1];
    if(a->surface&&b->surface){
        ProVector delta;
        for(int j=0;j<3;j++)delta[j]=b->position[j]-a->position[j];
        const double length=hypot(hypot(delta[0],delta[1]),delta[2]);
        part_text_add(&out,L"\n%ls %.8g mm。",a->bore&&b->bore?L"可见孔口中心距":L"标注落点距离",length*mm);
        if(a->bore&&b->bore){
            const double angle=acos(fmin(1.0,fabs(ad_dot(a->axis,(double *)b->axis))))*(180.0/AS_PI);
            const double along=ad_dot(delta,(double *)a->axis);
            const double lateral=sqrt(fmax(0,length*length-along*along))*mm;
            if(angle<=1e-4)part_text_add(&out,L" 两根孔轴平行，横向偏差 %.8g mm，%ls。",lateral,lateral<=1e-4?L"同轴":L"未对齐");
            else part_text_add(&out,L" 两根孔轴夹角 %.8g°，未对齐。",angle);
        }
    }
    if(!ax_same_occurrence(&sites[0].component,&sites[1].component))
        return markup_assembly_gap(&sites[0],&sites[1],mm,&out);
    return PRO_TK_NO_ERROR;
}
