/* Draw a flat sheet from measured contours. Resizing a rectangular blank moves
   each enclosed cutout as one contour so its original width and radius stay fixed. */
typedef struct {
    ImitationFlatPlan *flat;
    int resize_axis;
    double shift;
} ReferenceFlat;

static double reference_shift(double value,double centre,double shift){
    return value>centre+1e-7?shift:value<centre-1e-7?-shift:0;
}

typedef struct {
    ProVector low,high;
    double movement;
    int present;
} ReferenceContour;

static void reference_contour_add(ReferenceContour *contour,const ImitationInferredEdge *edge,int axis){
    const int u=(axis+1)%3,v=(axis+2)%3;
    double start=0,sweep=2*AS_PI;
    if(edge->type==PRO_ENT_ARC){
        start=atan2(edge->a[v]-edge->center[v],edge->a[u]-edge->center[u]);
        const double end=atan2(edge->b[v]-edge->center[v],edge->b[u]-edge->center[u]);
        const double mid=atan2(edge->middle[v]-edge->center[v],edge->middle[u]-edge->center[u]);
        sweep=fmod(end-start+2*AS_PI,2*AS_PI);
        if(fmod(mid-start+2*AS_PI,2*AS_PI)>sweep+1e-9){start=end;sweep=2*AS_PI-sweep;}
    }
    for(int i=0;i<3;i++){
        double low=fmin(edge->a[i],edge->b[i]);
        double high=fmax(edge->a[i],edge->b[i]);
        if(edge->type!=PRO_ENT_LINE&&i!=axis){
            const double peak=i==u?0:AS_PI/2;
            if(fmod(peak+AS_PI-start+2*AS_PI,2*AS_PI)<=sweep+1e-9)
                low=fmin(low,edge->center[i]-edge->radius);
            if(fmod(peak-start+2*AS_PI,2*AS_PI)<=sweep+1e-9)
                high=fmax(high,edge->center[i]+edge->radius);
        }
        contour->low[i]=contour->present?fmin(contour->low[i],low):low;
        contour->high[i]=contour->present?fmax(contour->high[i],high):high;
    }
    contour->present=1;
}

static ProError reference_flat_resize(ReferenceFlat *draft){
    ImitationFlatPlan *flat=draft->flat;
    const int k=draft->resize_axis;
    if(k<0||fabs(draft->shift)<1e-10)return PRO_TK_NO_ERROR;
    if(k==flat->axis)return PRO_TK_UNSUPPORTED;
    const int u=(flat->axis+1)%3,v=(flat->axis+2)%3;
    const int other=k==u?v:u;
    const double middle=(flat->bounds[1][k]+flat->bounds[0][k])*0.5;
    const double low=flat->bounds[0][k]-draft->shift,high=flat->bounds[1][k]+draft->shift;
    if(flat->loops<1||flat->loops>IMITATION_FLAT_MAX||high-low<0.1)return PRO_TK_BAD_INPUTS;
    ReferenceContour holes[IMITATION_FLAT_MAX]={0};
    int straight=0;
    for(int i=0;i<flat->count;i++){
        const ImitationInferredEdge *edge=&flat->edge[i];
        if(!edge->external){
            if(edge->contour<0||edge->contour>=flat->loops)return PRO_TK_BAD_CONTEXT;
            reference_contour_add(&holes[edge->contour],edge,flat->axis);
            continue;
        }
        if(edge->type!=PRO_ENT_LINE)return PRO_TK_UNSUPPORTED;
        straight++;
        const int direction=fabs(edge->b[u]-edge->a[u])>0.001?u:v;
        const int fixed=direction==u?v:u;
        const double length=fabs(edge->b[direction]-edge->a[direction]);
        if(fabs(edge->b[fixed]-edge->a[fixed])>0.001||
           fabs(length-flat->bounds[1][direction]+flat->bounds[0][direction])>0.001||
           (fabs(edge->a[fixed]-flat->bounds[0][fixed])>0.001&&
            fabs(edge->a[fixed]-flat->bounds[1][fixed])>0.001))return PRO_TK_UNSUPPORTED;
    }
    if(straight!=4)return PRO_TK_UNSUPPORTED;
    for(int i=0;i<flat->loops;i++){
        ReferenceContour *hole=&holes[i];
        if(!hole->present)continue;
        hole->movement=reference_shift((hole->low[k]+hole->high[k])*0.5,middle,draft->shift);
        hole->low[k]+=hole->movement;
        hole->high[k]+=hole->movement;
        if(hole->low[k]<=low+0.001||hole->high[k]>=high-0.001)return PRO_TK_BAD_INPUTS;
        for(int j=0;j<i;j++){
            const ReferenceContour *prior=&holes[j];
            if(!prior->present||fabs(prior->movement-hole->movement)<1e-7)continue;
            if(hole->low[other]<=prior->high[other]+0.001&&prior->low[other]<=hole->high[other]+0.001&&
               hole->low[k]<=prior->high[k]+0.001&&prior->low[k]<=hole->high[k]+0.001)return PRO_TK_BAD_INPUTS;
        }
    }
    for(int i=0;i<flat->count;i++){
        ImitationInferredEdge *edge=&flat->edge[i];
        if(edge->external){
            edge->a[k]+=reference_shift(edge->a[k],middle,draft->shift);
            edge->b[k]+=reference_shift(edge->b[k],middle,draft->shift);
        }else{
            const double translation=holes[edge->contour].movement;
            edge->a[k]+=translation;
            edge->b[k]+=translation;
            edge->center[k]+=translation;
            edge->middle[k]+=translation;
        }
    }
    flat->bounds[0][k]=low;
    flat->bounds[1][k]=high;
    return PRO_TK_NO_ERROR;
}

static ProError reference_profile(ProSection section,const ProMatrix frame,double scale,void *context){
    const ImitationFlatPlan *plan=context;
    int dimensions[2]={0};
    for(int i=0;i<plan->count;i++){
        if(plan->edge[i].external&&plan->edge[i].type==PRO_ENT_ARC)
            return imitation_draw_flat_plan(section,frame,scale,context);
    }
    for(int i=0;i<plan->count;i++){
        const ImitationInferredEdge *edge=&plan->edge[i];
        const AsPoint a=imitation_inferred_point(edge->a,frame,scale);
        const AsPoint b=imitation_inferred_point(edge->b,frame,scale);
        int id;
        ProError error;
        if(edge->type==PRO_ENT_LINE){
            error=as_add_line(section,a,b,&id);
            const double dx=fabs(b.x-a.x),dy=fabs(b.y-a.y);
            const int axis=dy<1e-7&&dx>1e-7?0:dx<1e-7&&dy>1e-7?1:-1;
            if(!error&&edge->external&&axis>=0&&!dimensions[axis]){
                dimensions[axis]=1;
                const double length=axis?dy:dx;
                error=model_section_dimension(section,id,PRO_TK_DIM_LINE,length,
                    (a.x+b.x)/2+axis*5*scale,(a.y+b.y)/2+(1-axis)*5*scale);
            }
        }else if(edge->type==PRO_ENT_CIRCLE){
            const AsPoint centre=imitation_inferred_point(edge->center,frame,scale);
            const double radius=edge->radius*scale;
            error=model_circle(section,centre.x,centre.y,radius,&id);
            if(!error)error=model_section_dimension(section,id,PRO_TK_DIM_DIA,
                radius*2,centre.x+radius*3,centre.y+radius*3);
        }else error=imitation_inferred_arc(section,edge,frame,scale,&id);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

static int reference_contour_edge_matches(const ImitationInferredEdge *expected,const ImitationInferredEdge *actual){
    const double tol=IMITATION_SIZE_TOL;
    if(expected->type!=actual->type||expected->external!=actual->external)return 0;
    if(expected->type!=PRO_ENT_LINE&&(!ad_near(expected->center,actual->center,tol)||
       fabs(expected->radius-actual->radius)>tol))return 0;
    if(expected->type==PRO_ENT_CIRCLE)return 1;
    if(expected->type==PRO_ENT_ARC&&!ad_near(expected->middle,actual->middle,tol))return 0;
    return (ad_near(expected->a,actual->a,tol)&&ad_near(expected->b,actual->b,tol))||
           (ad_near(expected->a,actual->b,tol)&&ad_near(expected->b,actual->a,tol));
}

static ProError reference_verify_contours(ProMdl part,const ImitationFlatPlan *expected,PartText *out){
    ImitationShapeMetrics shape;
    ImitationFlatPlan *actual=calloc(1,sizeof(*actual));
    if(!actual)return PRO_TK_OUT_OF_MEMORY;
    ProError error=imitation_shape_read(part,&shape);
    if(!error)error=imitation_flat_identify(part,&shape,actual);
    if(!error&&(actual->count!=expected->count||actual->loops!=expected->loops))error=PRO_TK_INCOMPLETE;
    int matched[IMITATION_FLAT_MAX]={0};
    for(int i=0;!error&&i<expected->count;i++){
        int j=0;
        while(j<actual->count&&(matched[j]||!reference_contour_edge_matches(&expected->edge[i],&actual->edge[j])))j++;
        if(j==actual->count)error=PRO_TK_INCOMPLETE;
        else matched[j]=1;
    }
    if(!error)part_text_add(out,L"已回读外轮廓及 %d 个完整开口，位置、直边、圆径及圆弧与新规格一致。\n",
        expected->loops-1);
    free(actual);
    return error;
}
static ProError reference_draw_sheet(ProMdl model,ImitationFlatPlan *plan,PartText *out){
    const int axis=plan->axis;
    const wchar_t *plane=imitation_template_plane(model,axis);
    const wchar_t *orient=imitation_template_plane(model,(axis+1)%3);
    if(!plane||!orient)return PRO_TK_E_NOT_FOUND;
    ProError error=PRO_TK_NO_ERROR;
    ProMdlName name;
    if((error=ProMdlMdlnameGet(model,name)))return error;
    ProVector point,normal;
    double mm;
    if((error=qa_mm_per_unit(model,&mm))||(error=imitation_plane(model,plane,point,normal)))return error;
    const double offset=(plan->bounds[1][axis]-point[axis]*mm)/normal[axis];
    if(fabs(offset)>1e-7){
        error=imitation_order_plane(model,plane,offset);
        plane=L"PLANE:WALL_PLANE";
    }
    ProMatrix frame;
    if(!error)error=smx_custom_sketch(model,L"REFERENCE_PROFILE",plane,orient,
                                     reference_profile,plan,frame);
    if(!error)error=imitation_operation(model,out,0,
        L"CREO_OP SMT_FIRST_WALL %ls FIRST_WALL SKETCH:REFERENCE_PROFILE %.12g %ls",
        name,plan->thickness,frame[2][axis]>0?L"NEG":L"POS");
    return error;
}

/* A redraw can have a different volume. Check the assigned materials rather
   than requiring the original absolute mass of an intentionally resized part. */
static ProError reference_material_labels(ProMdl source,ProMdl target,PartText *out){
    ProMaterial old_part={0},old_body={0},new_part={0},new_body={0};
    ProError error=imitation_material_read(source,&old_part,&old_body);
    if(!error)error=imitation_material_read(target,&new_part,&new_body);
    if(error)return error;
    if(_wcsicmp(old_part.matl_name,new_part.matl_name)||
       _wcsicmp(old_body.matl_name,new_body.matl_name)){
        part_text_add(out,L"新旧零件或实体的材质分配不一致。\n");
        return PRO_TK_INCOMPLETE;
    }
    return PRO_TK_NO_ERROR;
}
static ProError reference_dxf(ProMdl part,const wchar_t *folder,PartText *out){
    ImitationFlatFace face={.model=part};
    ProError error=qa_mm_per_unit(part,&face.mm);
    if(!error)error=ProSolidSurfaceVisit((ProSolid)part,imitation_flat_face,NULL,&face);
    if(!error&&(!face.id||face.tied))error=PRO_TK_E_AMBIGUOUS;
    if(error)return error;
    FabDxf *flat=calloc(1,sizeof(*flat));
    if(!flat)return PRO_TK_OUT_OF_MEMORY;
    wchar_t selected[40];
    ProPath path;
    swprintf_s(selected,_countof(selected),L"SURFACE:%d",face.id);
    error=fab_flat_read(part,folder,L"reference_flat_check",selected,flat,path);
    if(!error&&!flat->outline)error=PRO_TK_INCOMPLETE;
    if(!error)part_text_add(out,L"已核对钣金展开：外轮廓图元 %d、孔槽图元 %d、折弯线 %d。\n",
        flat->outline,flat->holes,flat->bends);
    free(flat);
    return error;
}

static ProError reference_resize_replay(ProMdl part,int axis,double wanted,PartText *out){
    PartSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    if(!snapshot)return PRO_TK_OUT_OF_MEMORY;
    double goal[3],before[3];
    int driver[3]={-1,-1,-1};
    ProError error=qa_mm_per_unit(part,&snapshot->mm_per_native);
    if(!error)error=imitation_extents(part,snapshot->mm_per_native,before);
    if(!error&&fabs(before[axis]-wanted)>IMITATION_SIZE_TOL){
        memcpy(goal,before,sizeof(goal));
        goal[axis]=wanted;
        error=ProSolidDimensionVisit((ProSolid)part,PRO_B_FALSE,part_dimension_visit,NULL,snapshot);
        if(!error&&snapshot->truncated)error=PRO_TK_OUT_OF_RANGE;
        if(!error)error=imitation_size_drivers(part,snapshot,before,goal,driver);
        if(!error&&driver[axis]>=0){
            const PartDimension *d=&snapshot->dimension[driver[axis]];
            ProDimension dimension;
            error=ProModelitemInit(part,d->handle.id,PRO_DIMENSION,&dimension);
            const int initialized=!error;
            if(!error)error=ProDimensionValueSet(&dimension,d->value+(wanted-before[axis])/snapshot->mm_per_native);
            if(!error)error=part_regenerate(part);
            if(error&&initialized){
                const ProError restore=ProDimensionValueSet(&dimension,d->value);
                if(!restore)(void)part_regenerate(part);
            }
        }else if(!error)error=PRO_TK_INCOMPLETE;
    }
    if(!error)part_text_add(out,L"%lc 目标外廓 %.8g mm，调整的是独立新件。\n",L'X'+axis,wanted);
    part_snapshot_clear(snapshot);
    free(snapshot);
    return error;
}
