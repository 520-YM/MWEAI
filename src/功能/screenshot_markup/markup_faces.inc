/* Solve the inverse of the captured, orthographic screen projection at the
   saved view plane. The screenshot is merely the pick location. Creo does
   the actual depth-ordered surface hit test. */
static int markup_face_projection(MarkupCapture *capture,MarkupProjection *map,
    const ProPoint3d anchor,const RECT *view,double sx,double sy){
    Pro2dPnt base,points[2];
    ProPoint3d origin={anchor[0],anchor[1],anchor[2]};
    if(!markup_project(map,origin,base))return 0;
    for(int a=0;a<2;a++){
        ProPoint3d sample;
        for(int i=0;i<3;i++){
            capture->face_basis[a][i]=map->view[i][a];
            sample[i]=anchor[i]+map->span*map->view[i][a];
        }
        if(!markup_project(map,sample,points[a]))return 0;
        capture->face_pixels[a][0]=(points[a][0]-base[0])*sx/map->span;
        capture->face_pixels[a][1]=(points[a][1]-base[1])*sy/map->span;
    }
    capture->face_anchor[0]=view->left+base[0]*sx;
    capture->face_anchor[1]=view->top+base[1]*sy;
    memcpy(capture->face_origin,anchor,sizeof(capture->face_origin));
    memcpy(capture->face_toward,map->toward_viewer,sizeof(capture->face_toward));
    capture->face_span=map->span;
    capture->face_view=*view;
    return 1;
}

typedef struct {
    int surface,feature;
    ProSrftype type;
    ProName name;
} MarkupFace;

static ProError markup_mark_ray(const MarkupCapture *capture,const MarkupMark *mark,ProRay *ray){
    if(!capture->face_span||!mark->valid)return PRO_TK_BAD_CONTEXT;
    double px=mark->tool==MARKUP_ARROW?mark->x2:(mark->x1+mark->x2)*0.5;
    double py=mark->tool==MARKUP_ARROW?mark->y2:(mark->y1+mark->y2)*0.5;
    if(px<capture->face_view.left||px>=capture->face_view.right||
       py<capture->face_view.top||py>=capture->face_view.bottom)return PRO_TK_E_NOT_FOUND;
    const double (*map)[2]=capture->face_pixels;
    const double x=px-capture->face_anchor[0],y=py-capture->face_anchor[1];
    const double det=map[0][0]*map[1][1]-map[0][1]*map[1][0];
    if(fabs(det)<1e-9)return PRO_TK_BAD_CONTEXT;
    const double u=(x*map[1][1]-y*map[1][0])/det,v=(y*map[0][0]-x*map[0][1])/det;

    for(int i=0;i<3;i++){
        ray->start_point[i]=capture->face_origin[i]+u*capture->face_basis[0][i]+v*capture->face_basis[1][i]+
            capture->face_span*capture->face_toward[i];
        ray->dir_vector[i]=-capture->face_toward[i];
    }
    return PRO_TK_NO_ERROR;
}

static ProError markup_face_hit(const MarkupCapture *capture,const MarkupMark *mark,MarkupFace *target,ProPoint3d point){
    ProRay ray;
    ProError projected=markup_mark_ray(capture,mark,&ray);
    if(projected)return projected;
    QualityRay result;
    ProError error=qa_ray(capture->model,&ray,1e-6,&result);
    if(error||!result.count)return error?error:PRO_TK_E_NOT_FOUND;
    target->surface=result.crossing[0].surface_id;
    if(point)for(int i=0;i<3;i++)point[i]=ray.start_point[i]+result.crossing[0].depth*ray.dir_vector[i];
    ProSurface surface;
    ProGeomitem item;
    ProFeature owner;
    error=ProSurfaceInit((ProSolid)capture->model,target->surface,&surface);
    if(!error)error=ProSurfaceTypeGet(surface,&target->type);
    if(!error)error=ProSurfaceToGeomitem((ProSolid)capture->model,surface,&item);
    if(!error)error=ProGeomitemFeatureGet(&item,&owner);
    if(error)return error;
    target->feature=owner.id;
    if(ProModelitemNameGet((ProModelitem *)&owner,target->name))target->name[0]=0;
    return PRO_TK_NO_ERROR;
}

static ProError markup_face_at(const MarkupCapture *capture,const MarkupMark *mark,MarkupFace *target){
    return markup_face_hit(capture,mark,target,NULL);
}