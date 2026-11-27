#define BWTL_WORKBENCH_EMBEDDED
#include "native_pressfit_workbench_baseline.c"

static ProMdl camera_panel;
static ProAsmcomp camera_panel_occurrence;
static int camera_panel_face,camera_panel_holes[2];
static const wchar_t electronics_source[]=L"E:\\BWTL-Data\\BWTL-55-Learning\\models\\2m0859-v1.prt.3";
static const wchar_t camera_cmd[]=L"装配公司库电子件 2M0859-V1 SURFACE:42 SURFACE:80 SURFACE:76";
static wchar_t details[16000];
static PartText diagnostics={details,_countof(details),0};

static int camera_done(ProError error){
    fwprintf(report,L"DETAIL=%ls\nPASS=%d CODE=%d\n",details,!error,error);
    fclose(report);
    ProEngineerEnd();
    return 0;
}
static ProError camera_panel_native(const wchar_t *format,...){
    wchar_t protocol[400],reply[2000]=L"";
    va_list args;
    va_start(args,format);
    const int written=vswprintf_s(protocol,_countof(protocol),format,args);
    va_end(args);
    PartCommand parsed;
    if(written<0||part_command_parse(protocol,&parsed)!=1)return PRO_TK_BAD_INPUTS;
    ProError error=foundation_execute(&parsed,camera_panel,reply,_countof(reply));
    fwprintf(report,L"%ls (%d)\n%ls\n",protocol,error,reply);
    fflush(report);
    return error;
}
static ProError camera_fixture_site(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    ProSrftype shape;
    ProFeature feature;
    ProGeomitem item;
    if(ProSurfaceTypeGet(surface,&shape)||shape!=PRO_SRF_CYL||
       ProSurfaceToGeomitem((ProSolid)camera_panel,surface,&item)||
       ProGeomitemFeatureGet(&item,&feature))return 0;
    ProName name=L"";
    if(ProModelitemNameGet((ProModelitem *)&feature,name))return 0;
    int i=!_wcsicmp(name,L"BORE_A")?0:!_wcsicmp(name,L"BORE_B")?1:-1;
    if(i<0||camera_panel_holes[i])return 0;
    AxComponent board={.model=camera_panel};
    AxHardwareSite site={0};
    if(!company_device_site(&board,camera_panel_face,item.id,1,&site))camera_panel_holes[i]=item.id;
    return 0;
}
static int prepare_camera_board(uiCmdCmdId id,uiCmdValue *v,void *data){
    report=_wfopen(L"company-device-chat.log",L"wt, ccs=UTF-8");
    if(!report)return ProEngineerEnd();
    const char *stage="empty assembly";
    ProMdlName name;
    model_background_mode=1;
    ProError error=model_create_empty(3,name,&stage);
    if(!error)error=ProMdlnameInit(name,PRO_MDL_ASSEMBLY,&assembly);
    if(!error)error=ax_new_part_in(assembly,L"CAMERA_ACCEPT_PANEL",1,&camera_panel,&camera_panel_occurrence,&diagnostics);
    if(!error)error=camera_panel_native(L"CREO_OP SMT_SKETCH CAMERA_ACCEPT_PANEL FOOTPRINT PLANE:FRONT PLANE:RIGHT RECT 0 0 120 45");
    if(!error)error=camera_panel_native(L"CREO_OP SMT_FIRST_WALL CAMERA_ACCEPT_PANEL WALL SKETCH:FOOTPRINT 1.5 POS");
    if(!error)error=camera_panel_native(L"CREO_OP SMT_SKETCH CAMERA_ACCEPT_PANEL SK_A PLANE:FRONT PLANE:RIGHT CIRCLE 25 22.5 1.2");
    if(!error)error=camera_panel_native(L"CREO_OP SMT_CUT CAMERA_ACCEPT_PANEL BORE_A SKETCH:SK_A NEG");
    if(!error)error=camera_panel_native(L"CREO_OP SMT_SKETCH CAMERA_ACCEPT_PANEL SK_B PLANE:FRONT PLANE:RIGHT CIRCLE 75.6 22.5 1.2");
    if(!error)error=camera_panel_native(L"CREO_OP SMT_CUT CAMERA_ACCEPT_PANEL BORE_B SKETCH:SK_B NEG");
    ProFeature feature;
    CabinetFace plane;
    if(!error)error=cm_feature_find(camera_panel,L"WALL",&feature);
    if(!error)error=cm_wall_face(&feature,&plane);
    if(!error){
        camera_panel_face=plane.id;
        error=ProSolidSurfaceVisit((ProSolid)camera_panel,camera_fixture_site,NULL,NULL);
    }
    if(!error&&(!camera_panel_holes[0]||!camera_panel_holes[1]))error=PRO_TK_BAD_CONTEXT;
    fwprintf(report,L"BOARD_DIMENSIONS=120 45 1.5 HOLE_PITCH=50.6 PANEL_FACE=%d HOLES=%d,%d (%d)\n",
        camera_panel_face,camera_panel_holes[0],camera_panel_holes[1],error);
    fflush(report);
    if(!error)error=part_regenerate(assembly);
    if(!error)error=dv_display_new(assembly);
    if(!error)error=ProMacroLoad(L"~ Command " L"\x60" L"CompanyDeviceChatCheck\x60;");
    return error?camera_done(error):0;
}
static int camera_chat_check(uiCmdCmdId id,uiCmdValue *v,void *data){
    if(!native_ui_open_threaded(NULL))return camera_done(PRO_TK_BAD_CONTEXT);
    ProPath root,standards,library,fixture;
    ProError error=imitation_library_paths(root,standards,library);
    if(!error&&(!imitation_library_folder(root)||!imitation_library_folder(standards)||!imitation_library_folder(library)))
        error=PRO_TK_CANT_WRITE;
    wchar_t digest[65];
    if(!error)error=ax_pack_copy(electronics_source,L"2m0859-v1.prt.3",library,digest);
    AxComponent panel={0};
    wchar_t key[24];
    swprintf_s(key,_countof(key),L"ID:%d",camera_panel_occurrence.id);
    if(!error)error=ax_hw_occurrence(assembly,key,1,&panel);
    if(!error)error=ProSelbufferClear();
    if(!error)error=test_select(&panel,camera_panel_face);
    for(int i=0;!error&&i<2;i++)error=test_select(&panel,camera_panel_holes[i]);
    if(!error)error=native_dialog_request(camera_cmd);
    AxScan *scan=NULL;
    if(!error)error=ax_scan(assembly,1,&scan);
    if(!error&&scan->count!=2)error=PRO_TK_BAD_CONTEXT;
    if(!error){
        ProMdl device=NULL;
        ProAsmcomp installed={0};
        for(int i=0;i<scan->count;i++)if(scan->occurrences[i].model!=camera_panel){
            device=scan->occurrences[i].model;
            error=ax_component_feature(assembly,scan->occurrences[i].ids[0],&installed);
            break;
        }
        ProMatrix pose;
        if(!error&&!device)error=PRO_TK_E_NOT_FOUND;
        if(!error)error=ax_actual_pose(assembly,&installed,pose);
        for(int i=0;!error&&i<2;i++){
            AxComponent source={.model=device};
            AxHardwareSite from={0},to={0};
            ProVector point,delta;
            error=company_device_site(&source,42,i?76:80,1,&from);
            if(!error)error=company_device_site(&panel,camera_panel_face,camera_panel_holes[i],1,&to);
            if(!error)error=ax_hw_world(&panel,&to);
            if(!error)error=ProPntTrfEval(from.contact,pose,point);
            if(!error){
                ad_sub(point,to.contact,delta);
                double distance=sqrt(ad_dot(delta,delta));
                fwprintf(report,L"AXIS_%d_CENTER_ERROR_MM=%.9g\n",i+1,distance);
                if(distance>0.001)error=PRO_TK_BAD_CONTEXT;
            }
        }
        fwprintf(report,L"NATIVE_OCCURRENCE=%d COUNT=%d\n",installed.id,scan->count);
    }
    free(scan);
    return camera_done(error);
}
__declspec(dllexport) int user_initialize(int n,char **a,char *v,char *b,wchar_t m[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_short_initialize(n,a,v,b,m);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("CompanyDeviceChatPrepare",prepare_camera_board,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("CompanyDeviceChatCheck",camera_chat_check,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command " L"\x60" L"CompanyDeviceChatPrepare\x60;");
}
