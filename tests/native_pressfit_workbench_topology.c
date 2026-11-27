#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

typedef struct {double radius,x,y;int id;} PressfitTopologySearch;

static ProError pressfit_topology_cylinder(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    PressfitTopologySearch *query=data;
    ProSrftype kind;
    ProError error=ProSurfaceTypeGet(surface,&kind);
    if(error||kind!=PRO_SRF_CYL)return error;
    ProGeomitemdata *raw=NULL;
    error=ProSurfaceDataGet(surface,&raw);
    if(!error){
        const ProCylinderdata *shape=&raw->data.p_surface_data->srf_shape.cylinder;
        if(fabs(shape->radius-query->radius)<0.001&&fabs(shape->origin[0]-query->x)<0.001&&
           fabs(shape->origin[1]-query->y)<0.001)error=ProSurfaceIdGet(surface,&query->id);
    }
    if(raw)ProGeomitemdataFree(&raw);
    return error;
}

static int pressfit_topology_get(ProMdl model,double radius,double x,double y){
    PressfitTopologySearch search={.radius=radius,.x=x,.y=y};
    if(ProSolidSurfaceVisit((ProSolid)model,pressfit_topology_cylinder,NULL,&search))return 0;
    return search.id;
}

static int pressfit_topology_report(FILE *log,AxComponent *part,int face_id,int hole_id){
    ShopMouth mouth={0};
    const int result=fastener_pressfit_classify(part,face_id,hole_id,1,&mouth);
    wchar_t face[32],hole[32];
    fastener_bso_surface(face_id,face);
    fastener_bso_surface(hole_id,hole);
    FdTxn tx={.model=part->model,.scale=1};
    const ProError mouth_error=mp_mouth(&tx,face,hole,&mouth);
    AxHardwareSite local={0};
    const ProError site_error=ax_hw_site(part,hole,face,1,&local);
    double begin=0,end=0;
    if(!site_error)ax_hw_axial_span(&local,local.contact,local.plane.direction,1,&begin,&end);
    fwprintf(log,L"PROBE face=%d bore=%d kind=%d mouth=%d site=%d span=%g:%g normal=[%g,%g,%g] ring=%d\n",
        face_id,hole_id,result,mouth_error,site_error,begin,end,local.plane.direction[0],local.plane.direction[1],local.plane.direction[2],mouth.ring.count);
    if(!site_error){
        ProRay ray;
        for(int i=0;i<3;i++){
            ray.start_point[i]=local.contact[i]+0.05*local.plane.direction[i];
            ray.dir_vector[i]=-local.plane.direction[i];
        }
        QualityRay axis={0};
        const ProError axis_error=qa_ray(part->model,&ray,0.0001,&axis);
        fwprintf(log,L"AXIS err=%d N=%d\n",axis_error,axis.count);
        for(int i=0;i<axis.count;i++)
            fwprintf(log,L"    hit depth %g id %d enter %d\n",axis.crossing[i].depth,axis.crossing[i].surface_id,axis.crossing[i].entering);
    }
    if(!mouth_error)for(int i=0;i<6;i++){
        const double angle=(i+0.21)*AS_PI/3;
        const double radius=mouth.radius+0.1;
        AsPoint p=as_point(radius*cos(angle),radius*sin(angle));
        double back=0;
        const ProError stock=mp_read_stock(&tx,&mouth.frame,p,0,&back,1);
        fwprintf(log,L"STOCK near %g,%g err=%d back=%g\n",p.x,p.y,stock,back);
    }
    return result;
}

static int topology_fixture(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    FILE *log=_wfopen(L"pressfit-topology-native.log",L"wt, ccs=UTF-8");
    if(!log){ProEngineerEnd();return 0;}
    ProMdl model=NULL;
    ProMdlName name;
    Fixture stock={0};
    wchar_t command[1000];
    int steps=0,types=0;
    ProError error=0;
#define CHECK(label,expr) do{error=(expr);fwprintf(log,L"%hs=%d\n",label,error);fflush(log);if(error)goto done;steps++;}while(0)
#define BUILD(fmt,...) (swprintf_s(command,_countof(command),L"CREO_OP " fmt,__VA_ARGS__),expansion_build(model,command,log))
    CHECK("SOLID_STOCK",fixture(&model,name,&stock));
    CHECK("TOP_FRAME",BUILD(L"DATUM_CSYS %ls TOPO_TOP CSYS:PRT_CSYS_DEF 0 0 20 0 0 0",name));
    CHECK("CREATE_SIMPLE_THROUGH",BUILD(L"PN_BUTTON %ls THROUGH_H SURFACE:%d CSYS:TOPO_TOP PLANE:RIGHT 20 15 20 8 0 0",name,stock.top));
    FdTxn tx={0};
    ShopSetup setup;
    PartCommand native_cut={0};
    swprintf_s(native_cut.arg[3],PART_TOKEN_MAX,L"SURFACE:%d",stock.top);
    wcscpy_s(native_cut.arg[4],PART_TOKEN_MAX,L"CSYS:TOPO_TOP");
    wcscpy_s(native_cut.arg[5],PART_TOKEN_MAX,L"PLANE:RIGHT");
    CHECK("BLIND_NATIVE_TRANSACTION",fd_begin(&tx,model,L"BLIND_H",PRO_FEAT_CUT));
    CHECK("BLIND_CIRCULAR_FRAME",mp_setup(&tx,&native_cut,3,&setup));
    const PanelProfile blind_profile=mp_circle(as_point(55,15),8);
    CHECK("CREATE_NATIVE_BLIND",mp_blind(&tx,&setup,L"SK",&blind_profile,1,5));
    CHECK("BLIND_ACTUAL_REMOVED_VOLUME",md_expected_volume(&tx,-80*AS_PI));
    fd_cleanup(&tx,0);
    CHECK("CREATE_STEP_PILOT",BUILD(L"PN_BUTTON %ls STEP_PILOT SURFACE:%d CSYS:TOPO_TOP PLANE:RIGHT 20 43 20 8 0 0",name,stock.top));
    int pilot=pressfit_topology_get(model,4,20,43);
    CHECK("FIND_STEP_PILOT",!pilot?PRO_TK_E_NOT_FOUND:0);
    CHECK("STEP_BACK_FRAME",BUILD(L"DATUM_CSYS %ls TOPO_BACK CSYS:PRT_CSYS_DEF 20 43 0 180 0 0",name));
    CHECK("CREATE_COUNTERBORE",BUILD(L"SHOP_BACK %ls STEP_H SURFACE:%d CSYS:TOPO_BACK PLANE:RIGHT SURFACE:%d 12 5 5",
        name,stock.bottom,pilot));
    CHECK("CREATE_NATIVE_SLOT",BUILD(L"VENT_SLOT %ls SLOT_H SURFACE:%d CSYS:TOPO_TOP PLANE:RIGHT 58 43 20 1 1 1 1 14 4",name,stock.top));
    CHECK("CREATE_COUNTERSINK_PILOT",BUILD(L"PN_BUTTON %ls SINK_PILOT SURFACE:%d CSYS:TOPO_TOP PLANE:RIGHT 40 26 20 8 0 0",name,stock.top));
    int sink=pressfit_topology_get(model,4,40,26);
    CHECK("FIND_COUNTERSINK_PILOT",!sink?PRO_TK_E_NOT_FOUND:0);
    CHECK("CREATE_AXISYMMETRIC_CONE",BUILD(L"SHOP_DEBURR %ls SINK_H 1 1 SURFACE:%d SURFACE:%d",name,sink,stock.top));
    sink=pressfit_topology_get(model,4,40,26);
    CHECK("FIND_SURVIVING_COUNTERSINK_CYLINDER",!sink?PRO_TK_E_NOT_FOUND:0);
    AxComponent part={.model=model};
    ShopMouth mouth={0};
    int through=pressfit_topology_get(model,4,20,15);
    int blind=pressfit_topology_get(model,4,55,15);
    int counter=pressfit_topology_get(model,6,20,43);
    CHECK("THROUGH_CYLINDER",!through?PRO_TK_E_NOT_FOUND:0);
    CHECK("BLIND_CYLINDER",!blind?PRO_TK_E_NOT_FOUND:0);
    CHECK("COUNTERBORE_CYLINDER",!counter?PRO_TK_E_NOT_FOUND:0);
    types+=pressfit_topology_report(log,&part,stock.top,through)==PF_OPENING_THROUGH;
    fwprintf(log,L"SIMPLE_THROUGH=%d\n",types==1);
    types+=pressfit_topology_report(log,&part,stock.top,blind)==PF_OPENING_BLIND;
    fwprintf(log,L"BLIND=%d\n",types==2);
    types+=pressfit_topology_report(log,&part,stock.bottom,counter)==PF_OPENING_STEPPED;
    fwprintf(log,L"COUNTERBORE=%d\n",types==3);
    BsoFaces faces={.model=model,.mm=1,.radius=2.0};
    if(!fastener_bso_model_faces(&faces))for(int i=0;i<faces.hole_count;i++){
        if(pressfit_topology_report(log,&part,stock.top,faces.holes[i])==PF_OPENING_SLOT){types++;break;}
    }
    fwprintf(log,L"SLOT=%d\n",types==4);
    const int cone=pressfit_topology_report(log,&part,stock.top,sink)==PF_OPENING_STEPPED;
    types+=cone;
    fwprintf(log,L"CONICAL_COUNTERSINK=%d\n",cone);
done:
    fwprintf(log,L"FIXTURE_STEPS=%d\nTOPOLOGY_ACCEPTED=%d/5\n",steps,types);
    fclose(log);
    ProEngineerEnd();
    return 0;
#undef BUILD
#undef CHECK
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    uiCmdCmdId id;
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    if(!error)error=ProCmdActionAdd("BWTLWorkbenchTopology",topology_fixture,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLWorkbenchTopology\x60;");
}