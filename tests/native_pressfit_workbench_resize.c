#define BWTL_WORKBENCH_EMBEDDED
#include "native_pressfit_workbench_baseline.c"

static ProMdl resize_panel;
static ProMdlName resize_panel_name;
static int resize_phase;
static const wchar_t resize_macro[]=L"~ Command \x60" L"BWTLWorkbenchResize\x60;";
static wchar_t details[9500];
static PartText resize_out={details,_countof(details),0};

static ProError resize_sheet_step(const wchar_t *format){
    wchar_t line[512],receipt[PART_TEXT_MAX]=L"";
    swprintf_s(line,_countof(line),format,resize_panel_name);
    PartCommand command={0};
    const ProError error=part_command_parse(line,&command)==1?
        foundation_execute(&command,resize_panel,receipt,_countof(receipt)):PRO_TK_BAD_INPUTS;
    fwprintf(report,L"%ls CREO=%d\n%ls\n",line,error,receipt);
    fflush(report);
    return error;
}

static int run_resize_fixture(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    ProError error=0;
    if(!report)report=_wfopen(L"pressfit-workbench-resize.log",L"wt, ccs=UTF-8");
    if(!report){ProEngineerEnd();return 0;}
    AxScan *scan=NULL;
    ProMdl active=NULL;
    FastenerPressfitTarget target={0};
    wchar_t key[AX_PATH_DEPTH*12+4];
    AxComponent panel={0};
#define CHECK(label,expr) do{error=(expr);fwprintf(report,L"%hs=%d\n",label,error);fflush(report);if(error)goto done;}while(0)
    if(resize_phase==0){
        const char *stage="private resize fixture";
        model_background_mode=1;
        CHECK("NEW_RAW_SHEET",model_create_empty(2,resize_panel_name,&stage));
        CHECK("ACTUAL_SHEET_MODEL",ProMdlnameInit(resize_panel_name,PRO_MDL_PART,&resize_panel));
        CHECK("SHEET_PROFILE",resize_sheet_step(L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60"));
        CHECK("SHEET_WALL",resize_sheet_step(L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 1.5 POS"));
        CHECK("ONE_CIRCULAR_PROFILE",resize_sheet_step(L"CREO_OP SMT_SKETCH %ls CUT_PROFILE PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 2.7"));
        CHECK("ONE_NATIVE_SHEET_CUT",resize_sheet_step(L"CREO_OP SMT_CUT %ls CUT_A SKETCH:CUT_PROFILE NEG"));
        ProMdlName asm_name;
        CHECK("NEW_ASSEMBLY",model_create_empty(3,asm_name,&stage));
        CHECK("ACTUAL_ASSEMBLY",ProMdlnameInit(asm_name,PRO_MDL_ASSEMBLY,&assembly));
        const ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        CHECK("INSTALL_TEST_BOARD",ax_place_model(assembly,resize_panel,(double (*)[4])pose,AX_PLACE_FIXED,1,NULL,&resize_out));
        model_background_mode=0;
        CHECK("SHOW_TEST_ASSEMBLY",dv_display_new(assembly));
        resize_phase++;
        CHECK("WAIT_TEST_ASSEMBLY",ProMacroLoad((wchar_t*)resize_macro));
        return 0;
    }
    CHECK("LIVE_TEST_ASSEMBLY",ProMdlCurrentGet(&active)||active!=assembly?PRO_TK_BAD_CONTEXT:0);
    CHECK("START_ISOLATED_BWTL",native_ui_open_threaded(NULL)?0:PRO_TK_BAD_CONTEXT);
    CHECK("SNAPSHOT_ASM",fastener_pressfit_capture(&target,&resize_out));
    CHECK("READ_COMPONENT_TREE",ax_scan(assembly,1,&scan));
    CHECK("ONE_BOARD_IN_ASSEMBLY",scan->count!=1||!ax_path_label(scan->occurrences[0].ids,1,key,_countof(key))?
        PRO_TK_BAD_CONTEXT:0);
    free(scan);scan=NULL;
    CHECK("READ_BOARD_OCCURRENCE",ax_hw_occurrence(assembly,key,1,&panel));
    BsoFaces faces={.model=panel.model,.mm=1,.radius=2.7,.read_planes=1};
    CHECK("READ_ORIGINAL_CIRCULAR_TOPOLOGY",fastener_bso_model_faces(&faces));
    int face=0,hole=0;
    for(int i=0;i<faces.plane_count&&!hole;i++)for(int j=0;j<faces.hole_count&&!hole;j++){
        ShopMouth mouth={0};
        if(fastener_pressfit_classify(&panel,faces.planes[i],faces.holes[j],1,&mouth)==PF_OPENING_THROUGH){
            face=faces.planes[i];
            hole=faces.holes[j];
        }
    }
    CHECK("PROVE_ORIGINAL_5_4_ROUND_THROUGH",!face||!hole?PRO_TK_BAD_CONTEXT:0);
    CHECK("CLEAR_CURRENT_SELECTION",ProSelbufferClear());
    CHECK("SELECT_PANEL_SEAT",test_select(&panel,face));
    CHECK("SELECT_SINGLE_HOLE",test_select(&panel,hole));
    CHECK("CHAT_RESIZE_EXACT_M2",native_dialog_request(L"装配S-M2-1，孔径不符就改孔"));
    fwprintf(report,L"MODEL_FEEDBACK\\n%ls\\n",history);
    CHECK("ACTUAL_CHAT_RESIZE_RECEIPT",wcsstr(history,L"已把所选独立圆孔从 Ø5.4 改为 Ø4.3 mm")?0:PRO_TK_INCOMPLETE);
    AxGeometry shape;
    CHECK("REMEASURE_TRUE_BORE_DIAMETER",fabs(fastener_pressfit_read_bore(&panel,hole,1,&shape)-4.3)>0.001?PRO_TK_INCOMPLETE:0);
    CHECK("AUDIT_REAL_PLACEMENT",ax_scan(assembly,1,&scan));
    CHECK("EXACTLY_BOARD_AND_ONE_M2",scan->count!=2||!wcsstr(scan->occurrences[1].name,L"S-M2-1")?PRO_TK_INCOMPLETE:0);
    wchar_t installed_key[24];
    swprintf_s(installed_key,_countof(installed_key),L"ID:%d",scan->occurrences[1].ids[0]);
    PressfitInstalledExample sample={0};
    CHECK("NATIVE_TWO_CONSTRAINTS_AND_AXIS",fastener_pressfit_installed_example(assembly,installed_key,1,&sample,&resize_out));
    CHECK("M2_IN_SAME_SELECTED_4_3_HOLE",sample.site.hole_id!=hole||sample.site.face_id!=face||fabs(sample.hole-4.3)>0.001?
        PRO_TK_INCOMPLETE:0);
    CHECK("SAVE_INDEPENDENT_REVIEW_MODEL",ProMdlSave(assembly));
done:
    free(scan);
    if(target.stamp)ProVerstampFree(&target.stamp);
    fwprintf(report,L"DETAILS\n%ls\nWORKBENCH_RESIZE_PASS=%d\n",details,!error);
    fclose(report);
    ProEngineerEnd();
    return 0;
#undef CHECK
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_short_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLWorkbenchResize",run_resize_fixture,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t*)resize_macro);
}