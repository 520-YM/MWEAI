
typedef struct {
    enum PanelShape shape;
    double width,height,pitch;
    int count;
} MarkupCreation;

static int markup_create_parse(const wchar_t *text,MarkupCreation *creation){
    const wchar_t *const locations[]={L"在标注位置",L"在这里",L"在这",L"这里",L"这儿",L"标注位置"};
    const wchar_t *const actions[]={L"新增",L"新建",L"增加",L"加工",L"画",L"开",L"加",L"打"};
    const wchar_t *const holes[]={L"通孔",L"圆孔",L"孔"};
    const wchar_t *const sizes[]={L"直径",L"孔径"};
    text=markup_skip_space(text);
    (void)markup_take_phrase(&text,locations,_countof(locations));
    if(!markup_take_phrase(&text,actions,_countof(actions)))return 0;
    const wchar_t *numbered=L"一二三四五六七八";
    int count=1;
    if(text[0]&&text[1]==L'个'){
        const wchar_t *number=wcschr(numbered,text[0]);
        if(number)count=(int)(number-numbered)+1;
        else if(text[0]==L'两')count=2;
        else if(text[0]>=L'1'&&text[0]<=L'8')count=text[0]-L'0';
        else return 0;
        text=markup_skip_space(text+2);
    }
    const wchar_t *const slots[]={L"长圆孔",L"长圆槽"};
    const wchar_t *const windows[]={L"矩形开口",L"矩形窗口",L"矩形通口"};
    *creation=(MarkupCreation){.shape=PN_CIRCLE,.count=count};
    if(markup_take_phrase(&text,slots,_countof(slots)))creation->shape=PN_CAPSULE;
    else if(markup_take_phrase(&text,windows,_countof(windows)))creation->shape=PN_RECTANGLE;
    if(creation->shape==PN_CIRCLE){
        (void)markup_take_phrase(&text,holes,_countof(holes));
        (void)markup_take_phrase(&text,sizes,_countof(sizes));
    }
    if(*text==L':'||*text==L'：')text=markup_skip_space(text+1);
    if(!markup_read_size(&text,&creation->width))return 0;
    if(creation->shape!=PN_CIRCLE){
        if(*text!=L'×'&&*text!=L'x'&&*text!=L'X'&&*text!=L'*')return 0;
        text=markup_skip_space(text+1);
        if(!markup_read_size(&text,&creation->height))return 0;
        if(creation->shape==PN_CAPSULE&&creation->width<=creation->height)return 0;
    }else creation->height=creation->width;
    markup_read_mm(&text);
    if(creation->shape==PN_CIRCLE)(void)markup_take_phrase(&text,holes,_countof(holes));
    if(*text==L'，'||*text==L',')text=markup_skip_space(text+1);
    if(!wcsncmp(text,L"孔距",2)){
        text=markup_skip_space(text+2);
        if(count!=2||creation->shape!=PN_CIRCLE||!markup_read_size(&text,&creation->pitch)||
           creation->pitch<=creation->width)return 0;
        markup_read_mm(&text);
    }
    while(iswspace(*text)||*text==L'。'||*text==L'！')text++;
    return !*text;
}

typedef struct {
    ProMdl model;
    ProVector normal,right,orientation_normal;
    int surface;
    double score;
} MarkupPanelDirection;

static ProError markup_panel_orientation(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    MarkupPanelDirection *test=data;
    InspectionPlane plane;
    if(in_plane_read(test->model,surface,&plane)||fabs(ad_dot(plane.normal,test->normal))>1e-6)return 0;
    double score=fabs(ad_dot(plane.normal,test->right));
    if(!test->surface||score>test->score){
        test->surface=plane.id;
        test->score=score;
        memcpy(test->orientation_normal,plane.normal,sizeof(ProVector));
    }
    return 0;
}

/* An exposed straight side is preferable. Curved stock may instead have a
   perpendicular model datum, which must pass the same normal check. */
static ProError markup_panel_datum_item(ProGeomitem *item,ProError status,ProAppData data){
    if(status)return status;
    ProSurface plane;
    return ProGeomitemToSurface(item,&plane)?0:markup_panel_orientation(plane,0,data);
}

static ProError markup_panel_datum(ProFeature *feature,ProError status,ProAppData data){
    if(status)return status;
    ProFeattype type;
    ProFeatStatus state;
    if(ProFeatureTypeGet(feature,&type)||type!=PRO_FEAT_DATUM||
       ProFeatureStatusGet(feature,&state)||state!=PRO_FEAT_ACTIVE)return 0;
    ProError error=ProFeatureGeomitemVisit(feature,PRO_SURFACE,markup_panel_datum_item,NULL,data);
    return error==PRO_TK_E_NOT_FOUND?0:error;
}

static ProError markup_plane_thickness(ProMdl model,int surface,const ProPoint3d point,
    const ProVector inward,double native_per_mm,double *thickness){
    ProRay ray;
    const double lead=0.01*native_per_mm,epsilon=0.0001*native_per_mm;
    for(int i=0;i<3;i++){
        ray.start_point[i]=point[i]-lead*inward[i];
        ray.dir_vector[i]=inward[i];
    }
    QualityRay crossings;
    ProError error=qa_ray(model,&ray,epsilon,&crossings);
    if(error)return error;
    if(crossings.count!=2||crossings.crossing[0].surface_id!=surface||
       fabs(crossings.crossing[0].depth-lead)>epsilon)return PRO_TK_BAD_CONTEXT;
    *thickness=(crossings.crossing[1].depth-crossings.crossing[0].depth)/native_per_mm;
    return *thickness>0&&isfinite(*thickness)?0:PRO_TK_BAD_CONTEXT;
}

static double markup_profile_area(const PanelProfile *p){
    if(p->shape==PN_CIRCLE)return AS_PI*p->height*p->height/4;
    if(p->shape==PN_CAPSULE)return (p->width-p->height)*p->height+AS_PI*p->height*p->height/4;
    return p->width*p->height;
}

static ProError markup_creation_names(ProMdl model,ProName feature,ProName sketch){
    ProModelitem existing;
    for(int i=1;i<=100;i++){
        swprintf_s(feature,PRO_NAME_SIZE,L"BWTL_MARKUP_CUT_%d",i);
        swprintf_s(sketch,PRO_NAME_SIZE,L"BWTL_MARKUP_SK_%d",i);
        if(ProModelitemByNameInit(model,PRO_FEATURE,feature,&existing)==PRO_TK_E_NOT_FOUND&&
           ProModelitemByNameInit(model,PRO_FEATURE,sketch,&existing)==PRO_TK_E_NOT_FOUND)return 0;
    }
    return PRO_TK_E_FOUND;
}

static ProError markup_create_profiles(ProMdl model,const int surface,const ProPoint3d *centers,
    const ProVector right,ProMdl assembly,const MarkupCreation *request,ProName feature,wchar_t *reply,size_t capacity){
    ProMdlsubtype subtype;
    ProSolidBody body;
    ProSurface face;
    InspectionPlane plane;
    PanelFrame panel={.face_id=surface};
    MarkupPanelDirection orient={.model=model};
    FdTxn tx={0};
    PanelProfile profiles[MARKUP_MAX_MARKS]={0};
    FastenerPlan bound={.margin=0.02};
    PartCommand refs={0};
    ProMatrix placement;
    ProName sketch;
    double thickness=0,removed=0;
    int cutting=0;
    const wchar_t *stage=L"核对标注面和可加工实体";
    ProError error=PRO_TK_NO_ERROR;
#define MCUT(step,call) do{stage=step;error=(call);if(error)goto done;}while(0)
    MCUT(stage,ProMdlSubtypeGet(model,&subtype));
    const int sheet=subtype==PROMDLSTYPE_PART_SHEETMETAL;
    MCUT(stage,qa_single_body(model,&body));
    MCUT(stage,ProSurfaceInit((ProSolid)model,surface,&face));
    MCUT(stage,in_plane_read(model,face,&plane));
    memcpy(orient.right,right,sizeof(ProVector));
    memcpy(orient.normal,plane.normal,sizeof(ProVector));
    MCUT(L"读取板面垂直的原生草绘参照",ProSolidBodySurfaceVisit(&body,markup_panel_orientation,&orient));
    if(!orient.surface||orient.score<0.2){
        MCUT(L"从 Creo 查找垂直的原生基准面",
            ProSolidFeatVisit((ProSolid)model,markup_panel_datum,NULL,&orient));
        if(!orient.surface||orient.score<0.2){error=PRO_TK_E_NOT_FOUND;goto done;}
    }
    memcpy(panel.placement.csys.origin,centers[0],sizeof(ProPoint3d));
    memcpy(panel.placement.csys.z_vector,plane.normal,sizeof(ProVector));
    if(ad_dot(orient.orientation_normal,right)<0)
        for(int i=0;i<3;i++)orient.orientation_normal[i]=-orient.orientation_normal[i];
    memcpy(panel.placement.csys.x_vector,orient.orientation_normal,sizeof(ProVector));
    ad_cross(plane.normal,panel.placement.csys.x_vector,panel.placement.csys.y_vector);
    if(!ad_unit(panel.placement.csys.y_vector)){error=PRO_TK_BAD_CONTEXT;goto done;}
    for(int i=0;i<3;i++)panel.inward[i]=-plane.normal[i];
    swprintf_s(refs.arg[3],PART_TOKEN_MAX,L"SURFACE:%d",surface);
    swprintf_s(refs.arg[5],PART_TOKEN_MAX,L"SURFACE:%d",orient.surface);
    MCUT(L"分配新特征名",markup_creation_names(model,feature,sketch));
    MCUT(L"保存修改前的实体和特征状态",sheet?sh_begin(&tx,model,feature,PRO_FEAT_CUT):fd_begin(&tx,model,feature,PRO_FEAT_CUT));
    if(!tx.has_volume||!isfinite(tx.volume)||tx.volume<=0){error=PRO_TK_BAD_CONTEXT;goto done;}
    MCUT(L"实测标注处的板厚",markup_plane_thickness(model,surface,centers[0],panel.inward,tx.scale,&thickness));
    bound.panel.count=request->count;
    for(int i=0;i<request->count;i++){
        double x=0,y=0;
        if(request->pitch)x=(i?0.5:-0.5)*request->pitch;
        else {
            ProVector delta;
            ad_sub(centers[i],centers[0],delta);
            x=ad_dot(delta,panel.placement.csys.x_vector)/tx.scale;
            y=ad_dot(delta,panel.placement.csys.y_vector)/tx.scale;
        }
        profiles[i]=(PanelProfile){.shape=request->shape,.x=x,.y=y,.width=request->width,.height=request->height};
        const double footprint=request->shape==PN_CIRCLE?request->width:hypot(request->width,request->height);
        bound.panel.through[i]=(PanelProfile){.shape=PN_CIRCLE,.x=x,.y=y,.width=footprint,.height=footprint};
        for(int j=0;j<i;j++)
            if(hypot(x-profiles[j].x,y-profiles[j].y)<=footprint+bound.margin){stage=L"检查标注之间的材料";error=PRO_TK_BAD_CONTEXT;goto done;}
        removed+=markup_profile_area(&profiles[i])*thickness;
    }
    MCUT(L"检查孔槽与板边、已有开口的净距",fx_check_boundary(&tx,&refs,&panel,&bound));
    for(int i=0;i<request->count;i++)
        MCUT(L"逐处检查完整板厚",pn_profile_probes(&tx,&panel,&profiles[i],thickness,0));
    cutting=1;
    MCUT(L"创建可编辑的原生草绘",pn_sketch(model,&refs,&panel,sketch,profiles,request->count,sheet,placement));
    MCUT(L"从草绘向板内加工贯穿切口",pn_cut(&tx,&panel,feature,sketch,placement,thickness,sheet));
    for(int i=0;i<request->count;i++)
        MCUT(L"逐处读回贯穿结果",pn_profile_probes(&tx,&panel,&profiles[i],thickness,1));
    if(request->shape==PN_CIRCLE){
        PanelPlan intended={.count=request->count};
        memcpy(intended.through,profiles,request->count*sizeof(profiles[0]));
        ShFourReadback check={&panel,&intended,tx.scale,0};
        ProFeature created;
        MCUT(L"读取切口产生的原生圆柱孔壁",
            ProModelitemByNameInit(model,PRO_FEATURE,feature,(ProModelitem *)&created));
        MCUT(L"核对实际孔径、孔轴与孔中心",ProFeatureGeomitemVisit(&created,PRO_SURFACE,sh_four_cylinder,NULL,&check));
        if(check.matched!=(1u<<request->count)-1){error=PRO_TK_BAD_CONTEXT;goto done;}
    }
    MCUT(L"核对实际去料体积",md_expected_volume(&tx,-removed));
    MCUT(L"核对新特征重生成",part_health(model));
    if(assembly)MCUT(L"重生成原装配",part_regenerate(assembly));
    swprintf_s(reply,capacity,L"已创建 %d 处%ls。特征 %ls，草绘 %ls。Creo 核对板厚 %.8g mm、贯穿和去料体积，未保存。",
        request->count,request->shape==PN_CIRCLE?L"圆通孔":request->shape==PN_CAPSULE?L"长圆通槽":L"矩形贯穿开口",
        feature,sketch,thickness);
done:
    if(error&&cutting){
        wchar_t restoration[300]=L"";
        am_finish(&tx,error,1,restoration,_countof(restoration));
        if(assembly&&part_regenerate(assembly))tx.rollback_ok=0;
        if(!tx.rollback_ok)pn_recovery_uncertain=1;
    }else fd_cleanup(&tx,0);
    if(error)swprintf_s(reply,capacity,L"新增特征卡在「%ls」（Creo %d）。%ls",stage,error,
        cutting?(tx.rollback_ok?L"Creo 已回读确认撤销这次新增。":L"请核对模型，恢复未能完整核实。"):L"模型尚未写入。");
    return error;
#undef MCUT
}

static ProError markup_apply_creation(const MarkupCapture *capture,const MarkupRequest *request,
    const MarkupCreation *creation,wchar_t *reply,size_t capacity){
    if(request->count!=(creation->pitch?1:creation->count)){
        swprintf_s(reply,capacity,L"请在同一板面%ls。",creation->pitch?L"标一个两孔中点":L"为每个孔、槽或开口各标一处中心");
        return PRO_TK_BAD_INPUTS;
    }
    if(pn_recovery_uncertain||am_recovery_uncertain||sh_recovery_uncertain)return PRO_TK_BAD_CONTEXT;
    if(!storage_directory_on_drive(capture->directory,L'E'))return PRO_TK_CANT_WRITE;
    ProMdl model=capture->model,assembly=NULL;
    ProPoint3d centers[MARKUP_MAX_MARKS];
    ProVector right;
    memcpy(right,capture->face_basis[0],sizeof(ProVector));
    int surface=0;
    AxComponent first={0};
    wchar_t authorization[400]=L"";
    for(int i=0;i<request->count;i++){
        int id=0;
        ProSrftype type;
        if(capture->assembly){
            MarkupAssemblyPick pick;
            if(markup_assembly_pick(capture,&request->marks[i],&pick))goto bad_face;
            if(!i){
                first=pick.component;
                ProError allowed=markup_assembly_authorize_child(capture,request,&first,authorization,_countof(authorization));
                if(allowed){wcsncpy_s(reply,capacity,authorization,_TRUNCATE);return allowed;}
                assembly=capture->model;
                model=first.model;
                ProMatrix pose;
                if(ProAsmcomppathTrfGet(&first.path,PRO_B_TRUE,pose))goto bad_face;
                for(int j=0;j<3;j++)right[j]=ad_dot(capture->face_basis[0],pose[j]);
                if(!ad_unit(right))goto bad_face;
            }else if(pick.component.path.table_num!=first.path.table_num||
               memcmp(pick.component.path.comp_id_table,first.path.comp_id_table,first.path.table_num*sizeof(int)))goto bad_face;
            type=pick.type;
            id=pick.surface;
            memcpy(centers[i],pick.local_point,sizeof(ProPoint3d));
        }else{
            MarkupFace hit;
            if(markup_face_hit(capture,&request->marks[i],&hit,centers[i]))goto bad_face;
            type=hit.type;
            id=hit.surface;
        }
        if(type!=PRO_SRF_PLANE||(i&&surface!=id))goto bad_face;
        surface=id;
    }
    ProName feature;
    ProError created=markup_create_profiles(model,surface,centers,right,assembly,creation,feature,reply,capacity);
    if(!created&&authorization[0]&&wcslen(reply)+wcslen(authorization)+24<capacity){
        wcscat_s(reply,capacity,L"\n");
        wcscat_s(reply,capacity,authorization);
        wcscat_s(reply,capacity,L"原装配已重生成。");
    }
    return created;
bad_face:
    wcscpy_s(reply,capacity,L"标注需要落在同一个组件的实体平面内，且每个中心处要有材料。原模型尚未写入。");
    return PRO_TK_BAD_CONTEXT;
}
