#define BWTL_WORKBENCH_EMBEDDED
#include "native_pressfit_workbench_baseline.c"

static ProMdl panel_model;
static int phase;
static const wchar_t automatic_command[]=L"~ Command \x60" L"BWTLPressfitAutoFace\x60;";

static ProError panel_step(const wchar_t *format,const wchar_t *name){
    wchar_t command_text[512],receipt[PART_TEXT_MAX]=L"";
    swprintf_s(command_text,_countof(command_text),format,name);
    PartCommand command={0};
    ProError error=part_command_parse(command_text,&command)==1?
        foundation_execute(&command,panel_model,receipt,_countof(receipt)):PRO_TK_BAD_INPUTS;
    fwprintf(report,L"%ls CREO=%d\n%ls\n",command_text,error,receipt);
    return error;
}

static int auto_face_probe(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    if(!report)report=_wfopen(L"pressfit-automatic-face.log",L"wt, ccs=UTF-8");
    if(!report){ProEngineerEnd();return 0;}
    ProError error=0;
    AxScan *scan=NULL;
    wchar_t text[12000]=L"";
    PartText out={text,_countof(text),0};
    AxComponent panel={0};
    wchar_t key[AX_PATH_DEPTH*12+4];
#define CHECK(label,expr) do{error=(expr);fwprintf(report,L"%hs=%d\n",label,error);fflush(report);if(error)goto done;}while(0)
    if(!phase){
        const char *stage="independent automatic selection sample";
        model_background_mode=1;
        ProMdlName name;
        CHECK("CREATE_SHEET",model_create_empty(2,name,&stage));
        CHECK("LOAD_NEW_PANEL",ProMdlnameInit(name,PRO_MDL_PART,&panel_model));
        CHECK("PROFILE",panel_step(L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60",name));
        CHECK("BASE",panel_step(L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 1.5 POS",name));
        CHECK("A_PROFILE",panel_step(L"CREO_OP SMT_SKETCH %ls A_SK PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 2.7",name));
        CHECK("A_CUT",panel_step(L"CREO_OP SMT_CUT %ls A_CUT SKETCH:A_SK NEG",name));
        CHECK("B_PROFILE",panel_step(L"CREO_OP SMT_SKETCH %ls B_SK PLANE:FRONT PLANE:RIGHT CIRCLE 50 20 2.7",name));
        CHECK("B_CUT",panel_step(L"CREO_OP SMT_CUT %ls B_CUT SKETCH:B_SK NEG",name));
        CHECK("CREATE_ASSEMBLY",model_create_empty(3,name,&stage));
        CHECK("LOAD_NEW_ASSEMBLY",ProMdlnameInit(name,PRO_MDL_ASSEMBLY,&assembly));
        ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        CHECK("ASSEMBLE_TEST_PANEL",ax_place_model(assembly,panel_model,pose,AX_PLACE_FIXED,1,NULL,&out));
        model_background_mode=0;
        CHECK("DISPLAY_TEST_ASSEMBLY",dv_display_new(assembly));
        phase=1;
        CHECK("WAIT_FOR_CREO_WINDOW",ProMacroLoad((wchar_t *)automatic_command));
        return 0;
    }
    ProMdl active=NULL;
    CHECK("CURRENT_TEST_ASSEMBLY",ProMdlCurrentGet(&active)||active!=assembly?PRO_TK_BAD_CONTEXT:0);
    CHECK("NATIVE_CHAT_WINDOW",native_ui_open_threaded(NULL)?0:PRO_TK_BAD_CONTEXT);
    CHECK("ASSEMBLY_TREE",ax_scan(assembly,1,&scan));
    CHECK("ONE_PANEL",scan->count!=1||!ax_path_label(scan->occurrences[0].ids,1,key,_countof(key))?PRO_TK_BAD_CONTEXT:0);
    free(scan);scan=NULL;
    CHECK("PANEL_OCCURRENCE",ax_hw_occurrence(assembly,key,1,&panel));
    BsoFaces faces={.model=panel.model,.mm=1,.radius=2.7,.read_planes=1};
    CHECK("READ_PANEL_FACES",fastener_bso_model_faces(&faces));
    int face=0;
    for(int i=0;!face&&i<faces.plane_count;i++)
        if(fastener_pressfit_uniform_bores(&panel,faces.planes[i],faces.holes,faces.hole_count,2,1,NULL)>0)
            face=faces.planes[i];
    CHECK("VERIFY_TWO_BORES_ON_FACE",face?0:PRO_TK_INCOMPLETE);
    CHECK("CLEAR_SELECTION",ProSelbufferClear());
    CHECK("SELECT_FACE_ONLY",test_select(&panel,face));
    CHECK("KNOWN_AMBIGUITY",native_dialog_request(L"这一面全部装配压铆螺母，型号你定"));
    CHECK("REFUSE_AMBIGUOUS_WITHOUT_WRITING",wcsstr(history,L"请指定规格")?active_count(1):PRO_TK_INCOMPLETE);
    ProMdl exact=NULL;
    ProPath library;
    CHECK("READ_EXACT_COMPANY_SPEC",pressfit_read_exact(L"S-M4-1",&exact,library,&out));
    CHECK("RECORD_PRIVATE_TEST_PREFERENCE",pressfit_preference_save(exact,library,&out));
    CHECK("WRONG_COUNT",native_dialog_request(L"这一面全部装配3个压铆螺母，型号你定"));
    CHECK("WRONG_COUNT_KEEPS_ASSEMBLY",wcsstr(history,L"数量吻合")?active_count(1):PRO_TK_INCOMPLETE);
    CHECK("ASSEMBLE_TWO_AUTOMATICALLY",native_dialog_request(L"这一面全部装配压铆螺母，型号你定"));
    CHECK("NATIVE_INSTALL_AND_CLEARANCE",wcsstr(history,L"实际核对 2/2 孔")&&wcsstr(history,L"间隙已核对 2/2 孔")?
        active_count(3):PRO_TK_INCOMPLETE);
    CHECK("RELOAD_TWO_NATIVE_INSTALLATIONS",native_dialog_request(L"核对压铆干涉"));
    CHECK("CHECKPOINT_REAL_GEOMETRY",wcsstr(history,L"间隙已核对 2/2 孔")?0:PRO_TK_INCOMPLETE);
    CHECK("SAVE_SAMPLE",ProMdlSave(assembly));
done:
    free(scan);
    fwprintf(report,L"%ls\nPRESSFIT_AUTO_FACE_PASS=%d\n",text,!error);
    fclose(report);
    ProEngineerEnd();
    return 0;
#undef CHECK
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_short_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLPressfitAutoFace",auto_face_probe,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t *)automatic_command);
}
