/* Verify the same physical bore and its location independently of the driver. */
static ProError markup_bore_read(ProMdl model,const MarkupHole *hole,double expected_native){
    ProSurface surface;
    ProGeomitem item;
    ProFeature feature;
    ProFeatStatus state;
    ProSrftype type;
    ProError error=ProSurfaceInit((ProSolid)model,hole->surface,&surface);
    if(!error)error=ProSurfaceToGeomitem((ProSolid)model,surface,&item);
    if(!error)error=ProGeomitemFeatureGet(&item,&feature);
    if(!error)error=ProFeatureStatusGet(&feature,&state);
    if(!error)error=ProSurfaceTypeGet(surface,&type);
    if(error)return error;
    if(feature.id!=hole->feature||state!=PRO_FEAT_ACTIVE||type!=PRO_SRF_CYL)return PRO_TK_BAD_CONTEXT;
    ProGeomitemdata *raw=NULL;
    error=ProSurfaceDataGet(surface,&raw);
    if(error)return error;
    const ProSurfacedata *actual=raw->data.p_surface_data;
    const ProCylinderdata *cylinder=&actual->srf_shape.cylinder;
    ProVector offset={0},normal,point;
    ProUvParam uv={(actual->uv_min[0]+actual->uv_max[0])*0.5,(actual->uv_min[1]+actual->uv_max[1])*0.5};
    double delta[3];
    for(int i=0;i<3;i++)delta[i]=hole->center[i]-cylinder->origin[i];
    const double along=ad_dot(delta,cylinder->e3);
    for(int i=0;i<3;i++)offset[i]=delta[i]-along*cylinder->e3[i];
    double lip=fmin(fabs(along-actual->uv_min[1]),fabs(along-actual->uv_max[1]));
    error=ProSurfaceXyzdataEval(surface,uv,point,NULL,NULL,normal);
    if(!error){
        ProVector radial;
        for(int i=0;i<3;i++)radial[i]=point[i]-cylinder->origin[i]-uv[1]*cylinder->e3[i];
        if(!part_close(2*cylinder->radius,expected_native)||fabs(ad_dot(hole->axis,cylinder->e3))<1-1e-7||
           hypot(hypot(offset[0],offset[1]),offset[2])>1e-5||lip>1e-5||ad_dot(radial,normal)>=0||
           !part_close(hole->depth,fabs(actual->uv_max[1]-actual->uv_min[1])))
            error=PRO_TK_BAD_CONTEXT;
    }
    ProGeomitemdataFree(&raw);
    return error;
}

/* Four readable cut lines and five solid ray checks verify the opening in the
   re-generated model. An undersized or covered aperture fails the whole batch. */
static ProError markup_opening_read_geometry(ProMdl model,const MarkupOpening *opening,double x,double y,double (*corners)[3]){
    ProFeature feature;
    ProElement tree=NULL;
    ProSection section=NULL;
    FdSketch linked={0};
    ProFeatStatus state;
    ImitationLocalProfile profile={0};
    int lines[4];
    ProError error=ProFeatureInit((ProSolid)model,opening->feature,&feature);
    if(!error)error=ProFeatureStatusGet(&feature,&state);
    if(!error&&state!=PRO_FEAT_ACTIVE)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProFeatureElemtreeExtract(&feature,NULL,PRO_FEAT_EXTRACT_NO_OPTS,&tree);
    if(!error&&(!markup_opening_section(&feature,tree,&linked,&section)||
       (linked.tree?linked.feature.id:feature.id)!=opening->owner||
       !markup_opening_profile(section,&profile,lines)))error=PRO_TK_BAD_CONTEXT;
    if(!error){
        ProMatrix frame;
        double lo[]={HUGE_VAL,HUGE_VAL},hi[]={-HUGE_VAL,-HUGE_VAL};
        for(int i=0;i<4;i++)for(int a=0;a<2;a++){
            lo[a]=fmin(lo[a],fmin(profile.lines[i].end1[a],profile.lines[i].end2[a]));
            hi[a]=fmax(hi[a],fmax(profile.lines[i].end1[a],profile.lines[i].end2[a]));
        }
        if(!part_close(hi[0]-lo[0],x)||!part_close(hi[1]-lo[1],y)||!part_close(profile.area,x*y))
            error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ProSectionLocationGet(section,frame);
        if(corners)for(int i=0;!error&&i<4;i++){
            ProPoint3d local={(i==1||i==2)?hi[0]:lo[0],i>=2?hi[1]:lo[1],0};
            error=ProPntTrfEval(local,frame,corners[i]);
        }

        ProOutline bounds;
        if(!error)error=ProSolidOutlineGet((ProSolid)model,bounds);
        double span=0;
        if(!error){
            span=4*hypot(hypot(bounds[1][0]-bounds[0][0],bounds[1][1]-bounds[0][1]),bounds[1][2]-bounds[0][2])+1;
            if(!isfinite(span))error=PRO_TK_BAD_CONTEXT;
        }
        const double points[][2]={{.5,.5},{.1,.1},{.1,.9},{.9,.1},{.9,.9}};
        for(int i=0;!error&&i<_countof(points);i++){
            ProPoint3d local={lo[0]+points[i][0]*x,lo[1]+points[i][1]*y,0},world;
            ProRay ray;
            error=ProPntTrfEval(local,frame,world);
            if(error)break;
            for(int j=0;j<3;j++){
                ray.start_point[j]=world[j]+span*frame[2][j];
                ray.dir_vector[j]=-frame[2][j];
            }
            QualityRay hits;
            error=qa_ray(model,&ray,1e-6,&hits);
            if(!error&&hits.count)error=PRO_TK_BAD_CONTEXT;
        }
    }
    fd_sketch_free(&linked);
    if(tree)ProFeatureElemtreeFree(&feature,tree);
    return error;
}

static ProError markup_opening_read(ProMdl model,const MarkupOpening *opening,double x,double y){
    return markup_opening_read_geometry(model,opening,x,y,NULL);
}