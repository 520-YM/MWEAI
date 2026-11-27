/* Inspect the local cut topology, radial bearing area and tooling envelope. */
enum {PF_OPENING_UNKNOWN,PF_OPENING_THROUGH,PF_OPENING_SLOT,PF_OPENING_STEPPED,PF_OPENING_BLIND};

typedef struct {
    ProMdl model;
    const AxHardwareSite *local;
    double mm,span;
    int front,hole,far_face;
} PressfitBoreKind;

static ProError fastener_pressfit_kind_plane(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    PressfitBoreKind *check=data;
    ProSrftype type;
    int id;
    ProError error=ProSurfaceTypeGet(surface,&type);
    if(error||type!=PRO_SRF_PLANE)return error;
    if((error=ProSurfaceIdGet(surface,&id)))return error;
    if(id==check->front||check->far_face)return PRO_TK_NO_ERROR;
    ProModelitem item;
    AxGeometry geometry;
    if(ProModelitemInit(check->model,id,PRO_SURFACE,&item)||ax_geometry(&item,&geometry)||geometry.type!=AX_GEOM_PLANE)
        return PRO_TK_NO_ERROR;
    if(ad_dot(geometry.direction,check->local->plane.direction)>-1+1e-7)return PRO_TK_NO_ERROR;
    ProVector offset;
    ad_sub(geometry.point,check->local->contact,offset);
    const double depth=-ad_dot(offset,check->local->plane.direction)*check->mm;
    if(fabs(depth-check->span)<=0.002){
        ShopMouth reverse={0};
        wchar_t opposite[32],hole[32];
        FdTxn tx={.model=check->model,.scale=1/check->mm};
        fastener_bso_surface(id,opposite);
        fastener_bso_surface(check->hole,hole);
        if(!mp_mouth(&tx,opposite,hole,&reverse))check->far_face=id;
    }
    return PRO_TK_NO_ERROR;
}

typedef struct {int cylinder,lines,arcs,match,mixed;} PressfitOpeningContour;

static ProError fastener_pressfit_kind_edge(ProEdge edge,ProError status,ProAppData data){
    if(status)return status;
    PressfitOpeningContour *kind=data;
    ProEnttype type;
    ProError error=ProEdgeTypeGet(edge,&type);
    if(error)return error;
    if(type==PRO_ENT_LINE)kind->lines++;
    else if(type==PRO_ENT_ARC||type==PRO_ENT_CIRCLE)kind->arcs++;
    else kind->mixed++;
    ProSurface adjacent[2];
    int id;
    if((error=ProEdgeNeighborsGet(edge,NULL,NULL,&adjacent[0],&adjacent[1])))return error;
    for(int i=0;i<2;i++)if(!ProSurfaceIdGet(adjacent[i],&id)&&id==kind->cylinder)kind->match=1;
    return PRO_TK_NO_ERROR;
}

typedef struct {ProSurface plane;int cylinder,slot;} PressfitSlotSearch;

static ProError fastener_pressfit_kind_contour(ProContour contour,ProError status,ProAppData data){
    if(status)return status;
    PressfitSlotSearch *search=data;
    PressfitOpeningContour kind={.cylinder=search->cylinder};
    const ProError error=ProContourEdgeVisit(search->plane,contour,fastener_pressfit_kind_edge,NULL,&kind);
    if(!error&&kind.match&&kind.lines>=2&&kind.arcs>=2&&!kind.mixed)search->slot=1;
    return error;
}

/* A compound hole can have a split circular rim that fails the strict
   assembly-mouth test. Prove an actual diameter step from shared B-rep
   edges and a concentric inward cylindrical continuation. */
typedef struct {
    ProMdl model;
    const AxHardwareSite *site;
    ProSurface face;
    double mm;
    int bore,face_id,transition;
} PressfitStepRing;

static ProError fastener_pressfit_step_ring_edge(ProEdge edge,ProError status,ProAppData data){
    if(status)return status;
    PressfitStepRing *ring=data;
    ProEnttype kind;
    ProError error=ProEdgeTypeGet(edge,&kind);
    if(error||(kind!=PRO_ENT_ARC&&kind!=PRO_ENT_CIRCLE))return error;
    ProSurface faces[2];
    int ids[2];
    if((error=ProEdgeNeighborsGet(edge,NULL,NULL,&faces[0],&faces[1]))||
       (error=ProSurfaceIdGet(faces[0],&ids[0]))||(error=ProSurfaceIdGet(faces[1],&ids[1])))return error;
    const int index=ids[0]==ring->face_id?1:ids[1]==ring->face_id?0:-1;
    if(index<0||ids[index]==ring->bore)return PRO_TK_NO_ERROR;
    ProSrftype type;
    if((error=ProSurfaceTypeGet(faces[index],&type)))return error;
    if(type!=PRO_SRF_CYL)return PRO_TK_NO_ERROR;
    AxComponent panel={.model=ring->model};
    AxGeometry other;
    ProVector end[2];
    int bore=0;
    wchar_t token[32];
    fastener_bso_surface(ids[index],token);
    if(!ax_hw_cylinder(&panel,token,&other,end,&bore)&&bore&&
       fabs(ad_dot(other.direction,ring->site->cylinder.direction))>1-1e-7&&
       fabs(other.radius-ring->site->cylinder.radius)*ring->mm>0.002&&
       ax_hw_separation(other.point,ring->site->cylinder.point,ring->site->cylinder.direction,ring->mm)<0.001)
        ring->transition=1;
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_step_ring_contour(ProContour contour,ProError status,ProAppData data){
    PressfitStepRing *ring=data;
    return status?status:ProContourEdgeVisit(ring->face,contour,fastener_pressfit_step_ring_edge,NULL,ring);
}

typedef struct {
    ProMdl model;
    AxHardwareSite site;
    ProSurface cylinder;
    double mm,start,end;
    int bore,face,has_mouth,has_step;
} PressfitStepSearch;

typedef struct {
    PressfitStepSearch *search;
    ProSurface cone;
    int cone_id,plane_id;
} PressfitConeRim;

static ProError fastener_pressfit_cone_rim_edge(ProEdge edge,ProError status,ProAppData data){
    if(status)return status;
    PressfitConeRim *rim=data;
    ProEnttype kind;
    ProError error=ProEdgeTypeGet(edge,&kind);
    if(error||(kind!=PRO_ENT_ARC&&kind!=PRO_ENT_CIRCLE))return error;
    ProSurface faces[2];
    int ids[2];
    if((error=ProEdgeNeighborsGet(edge,NULL,NULL,&faces[0],&faces[1]))||
       (error=ProSurfaceIdGet(faces[0],&ids[0]))||(error=ProSurfaceIdGet(faces[1],&ids[1])))return error;
    const int other=ids[0]==rim->cone_id?1:ids[1]==rim->cone_id?0:-1;
    if(other<0)return PRO_TK_NO_ERROR;
    ProSrftype type;
    if((error=ProSurfaceTypeGet(faces[other],&type)))return error;
    if(type!=PRO_SRF_PLANE)return PRO_TK_NO_ERROR;
    PressfitStepSearch *s=rim->search;
    ProGeomitem face;
    AxGeometry geometry;
    if((error=ProSurfaceToGeomitem((ProSolid)s->model,faces[other],&face))||(error=ax_geometry(&face,&geometry)))return error;
    if(geometry.type!=AX_GEOM_PLANE||fabs(ad_dot(geometry.direction,s->site.cylinder.direction))<1-1e-7)return PRO_TK_NO_ERROR;
    double radius=0;
    for(int i=0;i<3;i++){
        ProVector point;
        if((error=ProEdgeXyzdataEval(edge,(i+0.23)/3,point,NULL,NULL,NULL)))return error;
        const double observed=ax_hw_separation(point,s->site.contact,s->site.plane.direction,s->mm);
        if((i&&fabs(radius-observed)>0.001)||observed<=s->site.cylinder.radius*s->mm+0.002)return PRO_TK_NO_ERROR;
        radius=observed;
    }
    rim->plane_id=ids[other];
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_cone_rim_contour(ProContour contour,ProError status,ProAppData data){
    PressfitConeRim *rim=data;
    return status?status:ProContourEdgeVisit(rim->cone,contour,fastener_pressfit_cone_rim_edge,NULL,rim);
}

static ProError fastener_pressfit_cone_transition(PressfitStepSearch *search,ProSurface cone,int cone_id){
    ProGeomitemdata *raw=NULL;
    ProError error=ProSurfaceDataGet(cone,&raw);
    if(error)return error;
    const ProConedata *shape=&raw->data.p_surface_data->srf_shape.cone;
    const int coaxial=fabs(ad_dot(shape->e3,search->site.cylinder.direction))>1-1e-7&&
        ax_hw_separation(shape->origin,search->site.cylinder.point,search->site.cylinder.direction,search->mm)<0.001;
    ProGeomitemdataFree(&raw);
    if(!coaxial)return PRO_TK_NO_ERROR;
    PressfitConeRim rim={.search=search,.cone=cone,.cone_id=cone_id};
    error=ProSurfaceContourVisit(cone,fastener_pressfit_cone_rim_contour,NULL,&rim);
    if(!error&&rim.plane_id){
        search->has_step=1;
        if(rim.plane_id==search->face)search->has_mouth=1;
    }
    return error==PRO_TK_E_NOT_FOUND?PRO_TK_NO_ERROR:error;
}


static ProError fastener_pressfit_step_wall_edge(ProEdge edge,ProError status,ProAppData data){
    if(status)return status;
    PressfitStepSearch *search=data;
    ProEnttype kind;
    ProError error=ProEdgeTypeGet(edge,&kind);
    if(error||(kind!=PRO_ENT_ARC&&kind!=PRO_ENT_CIRCLE))return error;
    ProSurface faces[2];
    int ids[2];
    if((error=ProEdgeNeighborsGet(edge,NULL,NULL,&faces[0],&faces[1]))||
       (error=ProSurfaceIdGet(faces[0],&ids[0]))||(error=ProSurfaceIdGet(faces[1],&ids[1])))return error;
    const int opposite=ids[0]==search->bore?1:ids[1]==search->bore?0:-1;
    if(opposite<0)return PRO_TK_NO_ERROR;
    double depth=0;
    for(int i=0;i<3;i++){
        ProVector point,offset;
        error=ProEdgeXyzdataEval(edge,(i+0.23)/3,point,NULL,NULL,NULL);
        if(error)return error;
        ad_sub(point,search->site.contact,offset);
        const double axial=ad_dot(offset,search->site.plane.direction)*search->mm;
        if(i&&fabs(axial-depth)>0.001)return PRO_TK_NO_ERROR;
        depth=axial;
        const double radial=ax_hw_separation(point,search->site.contact,search->site.plane.direction,search->mm);
        if(fabs(radial-search->site.cylinder.radius*search->mm)>0.001)return PRO_TK_NO_ERROR;
    }
    if(fabs(depth)<=0.001&&ids[opposite]==search->face){
        search->has_mouth=1;
        return PRO_TK_NO_ERROR;
    }
    if(fabs(depth-search->start)>0.001&&fabs(depth-search->end)>0.001)return PRO_TK_NO_ERROR;
    ProSrftype type;
    if((error=ProSurfaceTypeGet(faces[opposite],&type)))return error;
    if(type==PRO_SRF_CONE)return fastener_pressfit_cone_transition(search,faces[opposite],ids[opposite]);
    if(type!=PRO_SRF_PLANE||ids[opposite]==search->face)return PRO_TK_NO_ERROR;
    ProGeomitem item;
    AxGeometry plane;
    error=ProSurfaceToGeomitem((ProSolid)search->model,faces[opposite],&item);
    if(!error)error=ax_geometry(&item,&plane);
    if(error||plane.type!=AX_GEOM_PLANE||
       fabs(ad_dot(plane.direction,search->site.cylinder.direction))<1-1e-7)return error;
    PressfitStepRing ring={.model=search->model,.site=&search->site,.face=faces[opposite],.bore=search->bore,.face_id=ids[opposite],.mm=search->mm};
    error=ProSurfaceContourVisit(ring.face,fastener_pressfit_step_ring_contour,NULL,&ring);
    if(!error&&ring.transition)search->has_step=1;
    return error==PRO_TK_E_NOT_FOUND?PRO_TK_NO_ERROR:error;
}

static ProError fastener_pressfit_step_wall_contour(ProContour contour,ProError status,ProAppData data){
    PressfitStepSearch *search=data;
    return status?status:ProContourEdgeVisit(search->cylinder,contour,fastener_pressfit_step_wall_edge,NULL,search);
}

static int fastener_pressfit_actual_step(AxComponent *panel,int face_id,int hole_id,double mm){
    PressfitStepSearch search={.model=panel->model,.mm=mm,.face=face_id,.bore=hole_id};
    wchar_t face[32],hole[32];
    fastener_bso_surface(face_id,face);
    fastener_bso_surface(hole_id,hole);
    ProSelection selected=NULL;
    ProError error=ax_hw_cylinder(panel,hole,&search.site.cylinder,search.site.end,&search.site.bore);
    if(!error)error=ax_ref_resolve(panel,face,0,&selected,&search.site.plane);
    if(selected)ProSelectionFree(&selected);
    if(error||!search.site.bore||search.site.plane.type!=AX_GEOM_PLANE||
       ProSurfaceInit((ProSolid)panel->model,hole_id,&search.cylinder))return 0;
    const double aligned=ad_dot(search.site.cylinder.direction,search.site.plane.direction);
    if(fabs(aligned)<1-1e-7)return 0;
    ProVector relative;
    ad_sub(search.site.plane.point,search.site.cylinder.point,relative);
    const double distance=ad_dot(relative,search.site.plane.direction)/aligned;
    cm_point(search.site.cylinder.point,search.site.cylinder.direction,distance,search.site.contact);
    ax_hw_axial_span(&search.site,search.site.contact,search.site.plane.direction,mm,&search.start,&search.end);
    return !ProSurfaceContourVisit(search.cylinder,fastener_pressfit_step_wall_contour,NULL,&search)&&
        search.has_mouth&&search.has_step;
}


static int fastener_pressfit_classify(AxComponent *panel,int face_id,int hole_id,double mm,ShopMouth *mouth){
    wchar_t face[32],hole[32];
    fastener_bso_surface(face_id,face);
    fastener_bso_surface(hole_id,hole);
    FdTxn tx={.model=panel->model,.scale=1/mm};
    if(mp_mouth(&tx,face,hole,mouth)){
        if(fastener_pressfit_actual_step(panel,face_id,hole_id,mm))return PF_OPENING_STEPPED;
        PressfitSlotSearch search={.cylinder=hole_id};
        if(!ProSurfaceInit((ProSolid)panel->model,face_id,&search.plane)&&
           !ProSurfaceContourVisit(search.plane,fastener_pressfit_kind_contour,NULL,&search)&&search.slot)
            return PF_OPENING_SLOT;
        return PF_OPENING_UNKNOWN;
    }
    AxHardwareSite local={0};
    if(ax_hw_site(panel,hole,face,mm,&local))return PF_OPENING_UNKNOWN;
    double start,end;
    ax_hw_axial_span(&local,local.contact,local.plane.direction,mm,&start,&end);
    if(start>=-0.001||fabs(end)>0.001)return PF_OPENING_UNKNOWN;
    PressfitBoreKind check={.model=panel->model,.local=&local,.mm=mm,.span=-start,.front=face_id,.hole=hole_id};
    if(ProSolidSurfaceVisit((ProSolid)panel->model,fastener_pressfit_kind_plane,NULL,&check))
        return PF_OPENING_UNKNOWN;
    if(check.far_face)return PF_OPENING_THROUGH;
    if(fastener_pressfit_actual_step(panel,face_id,hole_id,mm))return PF_OPENING_STEPPED;

    /* The center ray separates a real bottom from a diameter transition.
       The stock probes use only the nearby plate rather than global extents. */
    ProRay ray;
    const double offset=0.05/mm;
    for(int i=0;i<3;i++){
        ray.start_point[i]=local.contact[i]+offset*local.plane.direction[i];
        ray.dir_vector[i]=-local.plane.direction[i];
    }
    QualityRay axis={0};
    if(qa_ray(panel->model,&ray,0.0001/mm,&axis))return PF_OPENING_UNKNOWN;
    double thickness=0;
    for(int i=0;i<6;i++){
        const double angle=(i+0.21)*AS_PI/3;
        const double radius=mouth->radius+0.1;
        AsPoint p=as_point(radius*cos(angle),radius*sin(angle));
        double actual=0;
        if(!mp_read_stock(&tx,&mouth->frame,p,0,&actual,1)&&actual>check.span+0.002&&
           (!thickness||fabs(thickness-actual)<0.002))thickness=actual;
    }
    if(!thickness)return PF_OPENING_UNKNOWN;
    if(!axis.count)return PF_OPENING_UNKNOWN;
    if(axis.crossing[0].entering&&axis.crossing[0].depth*mm>=check.span+0.047&&
       axis.crossing[0].depth*mm<thickness+0.049)return PF_OPENING_BLIND;
    return PF_OPENING_UNKNOWN;
}

static const wchar_t *fastener_pressfit_opening_label(int kind){
    switch(kind){
    case PF_OPENING_THROUGH:return L"圆形通孔";
    case PF_OPENING_SLOT:return L"含直边和圆弧的槽形孔";
    case PF_OPENING_STEPPED:return L"台阶或沉头开口";
    case PF_OPENING_BLIND:return L"盲孔";
    default:return L"开口类型待核对";
    }
}

static ProError fastener_pressfit_site_preflight(const BsoSite *site,double mm,
        double edge_min,double bend_min,const double tooling[4],PartText *out){
    AxComponent panel=site->panel;
    ShopMouth mouth={0};
    const int kind=fastener_pressfit_classify(&panel,site->face_id,site->hole_id,mm,&mouth);
    if(kind!=PF_OPENING_THROUGH){
        part_text_add(out,L"孔壁 SURFACE:%d 为%ls。压铆定位需要已核实的圆形通孔。\n",
            site->hole_id,fastener_pressfit_opening_label(kind));
        return PRO_TK_BAD_CONTEXT;
    }
    InspectionSeat contour={&mouth,1/mm,INFINITY};
    ProError error=ProSurfaceContourVisit(mouth.ring.face,in_seat_contour,NULL,&contour);
    double bend=0;
    int bend_edge=0;
    const ProError bend_result=fastener_pressfit_bend_distance(site,mm,&bend,&bend_edge);
    const double edge_gap=contour.nearest-mouth.radius;
    const int has_edge=!error&&isfinite(edge_gap);
    part_text_add(out,L"孔壁 SURFACE:%d 圆形通孔 Ø%.6g mm，安装面承压环已核对。",site->hole_id,2*mouth.radius);
    if(has_edge)part_text_add(out,L" 孔缘到本面其它边界的最小净距 %.6g mm。",edge_gap);
    if(!bend_result)part_text_add(out,L" 孔缘到相邻折弯 EDGE:%d 的净距 %.6g mm。",bend_edge,bend-mouth.radius);
    part_text_add(out,L"\n");
    if(edge_min>0&&(!has_edge||!qa_at_least(edge_gap,edge_min))){
        part_text_add(out,L"未满足已给定的边界净距 %.6g mm。\n",edge_min);
        return PRO_TK_BAD_CONTEXT;
    }
    if(bend_min>0&&(bend_result||!qa_at_least(bend-mouth.radius,bend_min))){
        part_text_add(out,L"未能核对达到已给定的折弯净距 %.6g mm。\n",bend_min);
        return PRO_TK_BAD_CONTEXT;
    }
    if(tooling){
        double thickness;
        error=fab_thickness(panel.model,&mm,&thickness);
        SmyAccessResult front={0},back={0};
        if(!error)error=smy_access_sweep(panel.model,&mouth.frame,mm,0,0,tooling[0],tooling[0],
            0.002,tooling[1],1,&front);
        if(!error)error=smy_access_sweep(panel.model,&mouth.frame,mm,0,0,tooling[2],tooling[2],
            -thickness-tooling[3],-thickness-0.002,1,&back);
        if(error)return error;
        smy_access_report(L"板件上模进给",&front,out);
        smy_access_report(L"板件下模及托座",&back,out);
        if(front.blocked||back.blocked||front.unknown||back.unknown)return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}