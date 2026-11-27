#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static const wchar_t start_pressfit[]=L"~ Command `BWTLProbeLocalPressfit`;";
static const wchar_t audit_pressfit[]=L"~ Command `BWTLProbeLocalPressfitAudit`;";
static FILE *pressfit_probe_log;
static ProMdl pressfit_probe_assembly;
static ProMdlName pressfit_probe_name;
static UINT_PTR pressfit_probe_timer;
static ULONGLONG pressfit_probe_end;
static int pressfit_probe_closed;

static void pressfit_probe_finish(int ok){
    if(pressfit_probe_closed)return;
    pressfit_probe_closed=1;
    if(pressfit_probe_timer)KillTimer(native_backend_window,pressfit_probe_timer);
    if(pressfit_probe_log){
        fwprintf(pressfit_probe_log,L"BWTL_PRESSFIT_LOCAL_NATIVE_PASS=%d\n",ok);
        fclose(pressfit_probe_log);
    }
    ProEngineerEnd();
}

static int pressfit_probe_audit(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    wchar_t text[4096]=L"";
    PartText out={text,_countof(text),0};
    AxScan *tree=NULL;
    ProError error=ax_scan(pressfit_probe_assembly,1,&tree);
    if(!error&&tree->count!=3)error=PRO_TK_INCOMPLETE;
    if(!error)error=fastener_pressfit_audit_last(pressfit_probe_assembly,&out);
    fwprintf(pressfit_probe_log,L"PLUGIN_NATIVE_AUDIT_CODE=%d\n%ls\n",error,text);
    free(tree);
    pressfit_probe_finish(!error&&wcsstr(text,L"2/2 孔")!=NULL);
    return 0;
}

static void CALLBACK pressfit_probe_tick(HWND window,UINT message,UINT_PTR timer,DWORD now){
    (void)window;(void)message;(void)timer;(void)now;
    if(GetTickCount64()>pressfit_probe_end){
        pressfit_probe_finish(0);
        return;
    }
    if(GetFileAttributesW(L"audit-pressfit.now")!=INVALID_FILE_ATTRIBUTES){
        KillTimer(native_backend_window,pressfit_probe_timer);
        pressfit_probe_timer=0;
        if(ProMacroLoad((wchar_t *)audit_pressfit))pressfit_probe_finish(0);
    }
}

static int pressfit_probe_setup(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    const char *stage="local press-fit synthetic fixture";
    ProError error=0;
    ProMdl panel=NULL;
    ProMdlName name;
    AxScan *scan=NULL;
    AxComponent occurrence={0};
    wchar_t operation[PART_TOKEN_MAX],path[AX_PATH_DEPTH*12+4],text[400]=L"";
    PartText details={text,_countof(text),0};
    BsoSite sites[12];
    int face=0,count=0;
    pressfit_probe_log=_wfopen(L"local-control-pressfit-native.log",L"wt, ccs=UTF-8");
    if(!pressfit_probe_log){pressfit_probe_finish(0);return 0;}
    setvbuf(pressfit_probe_log,NULL,_IONBF,0);
#define CHECK(label,expr) do {error=(expr);fwprintf(pressfit_probe_log,L"%hs=%d\n",label,error);if(error)goto done;} while(0)
#define MAKE(format) do {swprintf_s(operation,_countof(operation),format,name);CHECK("STOCK_OP",expansion_build(panel,operation,pressfit_probe_log));} while(0)
    model_background_mode=1;
    CHECK("SHEET_MODEL",model_create_empty(2,name,&stage));
    CHECK("GET_SHEET_MODEL",ProMdlnameInit(name,PRO_MDL_PART,&panel));
    MAKE(L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60");
    MAKE(L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 1.5 POS");
    MAKE(L"CREO_OP SMT_SKETCH %ls A_SK PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 2.7");
    MAKE(L"CREO_OP SMT_CUT %ls A_CUT SKETCH:A_SK NEG");
    MAKE(L"CREO_OP SMT_SKETCH %ls B_SK PLANE:FRONT PLANE:RIGHT CIRCLE 50 20 2.7");
    MAKE(L"CREO_OP SMT_CUT %ls B_CUT SKETCH:B_SK NEG");
    CHECK("ASSEMBLY_MODEL",model_create_empty(3,pressfit_probe_name,&stage));
    CHECK("GET_ASSEMBLY_MODEL",ProMdlnameInit(pressfit_probe_name,PRO_MDL_ASSEMBLY,&pressfit_probe_assembly));
    ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    CHECK("PLACE_PANEL",ax_place_model(pressfit_probe_assembly,panel,pose,AX_PLACE_FIXED,1,NULL,&details));
    CHECK("ONE_PANEL",ax_scan(pressfit_probe_assembly,1,&scan));
    CHECK("PANEL_PATH",scan->count==1&&ax_path_label(scan->occurrences[0].ids,1,path,_countof(path))?0:PRO_TK_BAD_CONTEXT);
    CHECK("REAL_OCCURRENCE",ax_hw_occurrence(pressfit_probe_assembly,path,1,&occurrence));
    BsoFaces faces={.model=panel,.mm=1,.radius=2.7,.read_planes=1};
    CHECK("READ_PLANES",fastener_bso_model_faces(&faces));
    for(int i=0;!face&&i<faces.plane_count;i++){
        text[0]=0;
        details.truncated=0;
        if(!fastener_bso_collect(&occurrence,faces.planes[i],2,1,sites,&count,&details))face=faces.planes[i];
    }
    CHECK("TWO_BORES",face&&count==2?0:PRO_TK_E_NOT_FOUND);
    model_background_mode=0;
    CHECK("OPEN_TEST_WINDOW",dv_display_new(pressfit_probe_assembly));
    pressfit_probe_end=GetTickCount64()+300000;
    pressfit_probe_timer=SetTimer(native_backend_window,7250,200,pressfit_probe_tick);
    CHECK("POLL_TEST_WINDOW",pressfit_probe_timer?0:PRO_TK_GENERAL_ERROR);
    fwprintf(pressfit_probe_log,L"MODEL=%ls\nPANEL=%ls\nFACE=%d\nHOLE_ONE=%d\nHOLE_TWO=%d\nPRESSFIT_FIXTURE_READY=1\n",
        pressfit_probe_name,path,face,sites[0].hole_id,sites[1].hole_id);
done:
    free(scan);
    if(error)pressfit_probe_finish(0);
    return 0;
#undef CHECK
#undef MAKE
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLProbeLocalPressfit",pressfit_probe_setup,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLProbeLocalPressfitAudit",pressfit_probe_audit,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t *)start_pressfit);
}
