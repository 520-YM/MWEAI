/* Native ISO tapped-hole readback and exact surface-bound library metadata.
   Distances are millimetres from the selected entrance or screw bearing plane. */
typedef struct {
    double diameter,pitch,start,end;
    int hand,feature,surface,native_hole;
} DirectThread;

static int direct_thread_decimal(const wchar_t **text,double *value){
    const wchar_t *p=*text,*start=p;
    while(*p>=L'0'&&*p<=L'9')p++;
    if(*p==L'.'){
        p++;
        while(*p>=L'0'&&*p<=L'9')p++;
    }
    wchar_t field[32];
    if(p==start||wcsncpy_s(field,_countof(field),start,p-start)||!part_number(field,value)||
       !isfinite(*value)||*value<=0)return 0;
    *text=p;
    return 1;
}

static int direct_thread_size(const wchar_t *text,int declaration,DirectThread *thread){
    if(declaration){
        if(_wcsnicmp(text,L"ISO ",4))return 0;
        text+=4;
    }
    if(towupper(*text)!=L'M')return 0;
    text++;
    if(!direct_thread_decimal(&text,&thread->diameter)||!wcschr(L"xX×",*text)||!*text)return 0;
    text++;
    if(!direct_thread_decimal(&text,&thread->pitch)||thread->pitch>=thread->diameter)return 0;
    if(!declaration){thread->hand=1;return !*text;}
    if(!_wcsicmp(text,L" RH"))thread->hand=1;
    else if(!_wcsicmp(text,L" LH"))thread->hand=-1;
    else return 0;
    return 1;
}

static ProError direct_thread_parameter(ProFeature *feature,const wchar_t *name,ProParamvalueType type,ProParamvalue *value){
    ProParameter parameter;
    ProError error=ProParameterInit((ProModelitem *)feature,(wchar_t *)name,&parameter);
    if(!error)error=ProParameterValueWithUnitsGet(&parameter,value,NULL);
    return !error&&value->type!=type?PRO_TK_BAD_CONTEXT:error;
}

static ProError direct_thread_declared(ProFeature *feature,int face,DirectThread *thread){
    ProParamvalue value={0};
    ProError error=direct_thread_parameter(feature,L"BWTL_THREAD_SURFACE_ID",PRO_PARAM_INTEGER,&value);
    if(error||value.value.i_val!=face)return error?error:PRO_TK_BAD_CONTEXT;
    error=direct_thread_parameter(feature,L"BWTL_THREAD_SPEC",PRO_PARAM_STRING,&value);
    if(error||!direct_thread_size(value.value.s_val,1,thread))return error?error:PRO_TK_BAD_CONTEXT;
    error=direct_thread_parameter(feature,L"BWTL_THREAD_FROM_MM",PRO_PARAM_DOUBLE,&value);
    if(error)return error;
    thread->start=value.value.d_val;
    error=direct_thread_parameter(feature,L"BWTL_THREAD_LENGTH_MM",PRO_PARAM_DOUBLE,&value);
    if(!error)thread->end=thread->start+value.value.d_val;
    return error;
}

static ProError direct_thread_hole(ProMdl model,ProFeature *feature,double mm,double physical_end,DirectThread *thread){
    const ProElemId added[]={PRO_E_HLE_COM,PRO_E_HLE_ADD_THREAD};
    const ProElemId mode[]={PRO_E_HLE_COM,PRO_E_HLE_THRD_DEPTH};
    const ProElemId length[]={PRO_E_HLE_COM,PRO_E_HLE_THRDDEPTH};
    ProElement tree=NULL,field=NULL;
    wchar_t *series=NULL,*size=NULL;
    int is_thread=0,depth_mode=0;
    ProError error=ProFeatureElemtreeExtract(feature,NULL,PRO_FEAT_EXTRACT_NO_OPTS,&tree);
    if(!error)error=mc_tree_element(tree,added,2,&field);
    if(!error)error=ProElementIntegerGet(field,NULL,&is_thread);
    if(!error&&is_thread!=PRO_HLE_ADD_THREAD)error=PRO_TK_UNSUPPORTED;
    if(!error)error=ProElementHoleThreadSeriesGet(tree,model,&series);
    if(!error)error=ProElementHoleScrewSizeGet(tree,model,&size);
    if(!error&&(!series||_wcsicmp(series,L"ISO")||!size||!direct_thread_size(size,0,thread)))
        error=PRO_TK_UNSUPPORTED;
    if(!error)error=mc_tree_element(tree,mode,2,&field);
    if(!error)error=ProElementIntegerGet(field,NULL,&depth_mode);
    if(!error){
        if(depth_mode==PRO_HLE_THRU_THREAD)thread->end=physical_end;
        else if(depth_mode==PRO_HLE_VARIABLE_THREAD){
            error=mc_tree_element(tree,length,2,&field);
            if(!error)error=ProElementDoubleGet(field,NULL,&thread->end);
            if(!error)thread->end*=mm;
        }else error=PRO_TK_UNSUPPORTED;
    }
    if(!error)thread->native_hole=1;
    if(series)ProWstringFree(series);
    if(size)ProWstringFree(size);
    if(tree)ProFeatureElemtreeFree(feature,tree);
    return error;
}

static ProError direct_thread_read(ProMdl model,const wchar_t *surface,int receiver,double mm,double bound,DirectThread *thread){
    ProModelitem item;
    ProFeature feature;
    ProFeattype type;
    if(!local_control_pressfit_surface(surface,&thread->surface))return PRO_TK_BAD_INPUTS;
    ProError error=ProModelitemInit(model,thread->surface,PRO_SURFACE,&item);
    if(!error)error=ProGeomitemFeatureGet(&item,&feature);
    if(!error)error=ProFeatureTypeGet(&feature,&type);
    if(error)return error;
    thread->feature=feature.id;
    if(receiver&&type==PRO_FEAT_HOLE)error=direct_thread_hole(model,&feature,mm,bound,thread);
    else error=direct_thread_declared(&feature,thread->surface,thread);
    if(!error&&(!isfinite(thread->start)||!isfinite(thread->end)||thread->start<0||
       thread->end<=thread->start||thread->end>bound+0.002))error=PRO_TK_BAD_CONTEXT;
    return error;
}

typedef struct {
    AxComponent *axis_owner,*seat_owner;
    const wchar_t *axis_ref,*seat_ref,*shaft_ref,*bearing_ref;
    const AxHardwareSite *source,*target;
} DirectThreadPlacement;

typedef struct {
    double radial,seat,parallel,facing;
} DirectThreadPosition;

static ProError direct_thread_measure(ProMatrix pose,const DirectThreadPlacement *placement,double mm,DirectThreadPosition *read){
    ProVector contact,axis,normal,offset;
    const AxHardwareSite *source=placement->source,*target=placement->target;
    ProError error=ProPntTrfEval((double *)source->contact,pose,contact);
    if(!error)error=ProVectorTrfEval((double *)source->cylinder.direction,pose,axis);
    if(!error)error=ProVectorTrfEval((double *)source->plane.direction,pose,normal);
    if(error)return error;
    if(!ad_unit(axis)||!ad_unit(normal))return PRO_TK_BAD_CONTEXT;
    ad_sub(contact,target->contact,offset);
    read->radial=ax_hw_separation(contact,target->contact,target->plane.direction,mm);
    read->seat=ad_dot(offset,target->plane.direction)*mm;
    read->parallel=fabs(ad_dot(axis,target->plane.direction));
    read->facing=ad_dot(normal,target->plane.direction);
    return PRO_TK_NO_ERROR;
}

static ProError direct_thread_occurrence(ProMdl assembly,ProMdl model,const DirectThreadPlacement *placement,
    double mm,AxBatchItem *item,int *created,DirectThreadPosition *read,PartText *out){
    ProAsmcompconstraint *rows=NULL;
    ProAsmcomppath root;
    AxComponent child={.model=model};
    AxGeometry base_geometry,part_geometry;
    *created=0;
    item->model=model;
    ProError error=ax_root_path(assembly,&root);
    if(!error)error=ProArrayAlloc(0,sizeof(*rows),2,(ProArray *)&rows);
    if(!error)error=ax_add_relation(&rows,placement->axis_owner,&child,placement->axis_ref,
        placement->shaft_ref,PRO_ASM_INSERT,0,&base_geometry,&part_geometry);
    if(!error)error=ax_add_relation(&rows,placement->seat_owner,&child,placement->seat_ref,
        placement->bearing_ref,PRO_ASM_MATE,0,&base_geometry,&part_geometry);

    AxScan *scan=NULL;
    if(!error)error=ax_scan(assembly,0,&scan);
    if(!error&&(scan->limited||scan->unavailable))error=PRO_TK_INCOMPLETE;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *found=&scan->occurrences[i];
        if(found->depth!=1||found->state!=PRO_FEAT_ACTIVE||found->model!=model)continue;
        ProAsmcomp feature;
        ProMatrix pose;
        DirectThreadPosition existing={0};
        error=ax_component_feature(assembly,found->ids[0],&feature);
        if(!error)error=ax_actual_pose(assembly,&feature,pose);
        if(!error)error=direct_thread_measure(pose,placement,mm,&existing);
        if(!error&&existing.radial<=0.002&&fabs(existing.seat)<=0.002){
            if(item->feature.id>0){error=PRO_TK_E_FOUND;break;}
            item->feature=feature;
        }
    }
    free(scan);
    if(!error&&item->feature.id<=0){
        ProMatrix seed;
        ax_hw_seat_pose(placement->source,placement->target,seed);
        error=ProAsmcompAssemble((ProAssembly)assembly,(ProSolid)model,seed,&item->feature);
        *created=item->feature.id>0;
        if(!error){
            error=ProAsmcompConstraintsSet(&root,&item->feature,rows);
        }
        if(!error)error=part_regenerate(assembly);
    }
    ProBoolean packaged=PRO_B_TRUE;
    ProMdl actual=NULL;
    if(!error)error=part_health(assembly);
    if(!error)error=ProAsmcompMdlGet(&item->feature,&actual);
    if(!error)error=ProAsmcompIsPackaged(&item->feature,&packaged);
    if(!error)error=ax_compare_constraints(&item->feature,&root,rows,2,mm);
    if(!error)error=ax_actual_pose(assembly,&item->feature,item->pose);
    if(!error)error=direct_thread_measure(item->pose,placement,mm,read);
    if(!error&&(actual!=model||packaged||read->radial>0.001||fabs(read->seat)>0.001||
       read->parallel<1-1e-7||read->facing>-1+1e-7))error=PRO_TK_INCOMPLETE;
    if(error)part_text_add(out,L"原生同轴或承压面贴合未验收（Creo %d）。\n",error);
    if(rows)ProAsmcompconstraintArrayFree(rows);
    return error;
}