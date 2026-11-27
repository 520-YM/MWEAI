#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static const wchar_t pair_test_name[]=L"M26A9581001V03";
static FILE *pair_test_log;
static ProMdl pair_test_source;
static ProWVerstamp pair_test_initial;
static ProPath pair_test_directory,pair_test_trigger;
static UINT_PTR pair_test_timer;
static ULONGLONG pair_test_deadline;

static int pair_test_done(int passed){
    if(pair_test_timer)KillTimer(native_backend_window,pair_test_timer);
    if(pair_test_initial)ProVerstampFree(&pair_test_initial);
    if(pair_test_log){
        fwprintf(pair_test_log,L"BWTL_LOCAL_PAIR_PASS=%d\n",passed);
        fclose(pair_test_log);
        pair_test_log=NULL;
    }
    ProEngineerEnd();
    return 0;
}

static int pair_test_verify(uiCmdCmdId command,uiCmdValue *value,void *data){
    ProMdl assembly=NULL,part=NULL,actual=NULL;
    ProMdlName assembly_name,part_name;
    ProMdlsubtype subtype;
    ProWVerstamp current_stamp=NULL;
    ProAsmcomp linked;
    int component=0,assembly_window=-1,part_window=-1;
    ProPath directory;
    TemplateDatums assembly_datums={0};
    ProError error=chat_task_resolve_pair(&assembly,&part,&component);
    if(!error)error=ProMdlMdlnameGet(assembly,assembly_name);
    if(!error)error=ProMdlMdlnameGet(part,part_name);
    if(!error&&(_wcsicmp(assembly_name,pair_test_name)||_wcsicmp(part_name,pair_test_name)))
        error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlSubtypeGet(part,&subtype);
    if(!error&&subtype!=PROMDLSTYPE_PART_SHEETMETAL)error=PRO_TK_INVALID_TYPE;
    if(!error)error=template_empty((ProSolid)part);
    if(!error)error=ProSolidFeatVisit((ProSolid)assembly,template_feature,NULL,&assembly_datums);
    if(!error&&assembly_datums.components!=1)error=PRO_TK_INCOMPLETE;
    if(!error)error=ax_component_feature(assembly,component,&linked);
    if(!error)error=ax_default_verify(&linked);
    if(!error)error=ProAsmcompMdlGet(&linked,&actual);
    if(!error&&actual!=part)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlWindowGet(assembly,&assembly_window);
    if(!error)error=default_part_active(part,-1,&part_window);
    if(!error&&assembly_window==part_window)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProDirectoryCurrentGet(directory);
    if(!error&&_wcsicmp(pair_test_directory,directory))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlVerstampGet(pair_test_source,&current_stamp);
    if(!error&&!ProVerstampEqual(pair_test_initial,current_stamp))error=PRO_TK_INCOMPLETE;
    if(current_stamp)ProVerstampFree(&current_stamp);
    if(error)fwprintf(pair_test_log,L"NATIVE_PAIR_READBACK_ERROR=%d\n",error);
    else fwprintf(pair_test_log,
        L"ASSEMBLY=%ls.ASM\nPART=%ls.PRT\nCOMPONENT_ID=%d\nASSEMBLY_WINDOW=%d\nACTIVE_PART_WINDOW=%d\n"
        L"SHEETMETAL_EMPTY=1\nDEFAULT_PLACEMENT=1\nPRIOR_SOURCE_UNCHANGED=1\nWORKDIR_UNCHANGED=1\n",
        assembly_name,part_name,component,assembly_window,part_window);
    return pair_test_done(!error);
}

static void CALLBACK pair_test_tick(HWND window,UINT message,UINT_PTR timer,DWORD time){
    if(!pair_test_log)return;
    if(GetTickCount64()>=pair_test_deadline){
        pair_test_done(0);
        return;
    }
    if(GetFileAttributesW(pair_test_trigger)==INVALID_FILE_ATTRIBUTES)return;
    KillTimer(native_backend_window,pair_test_timer);
    pair_test_timer=0;
    if(ProMacroLoad(L"~ Command \x60" L"BWTLPairFixtureReadback\x60;"))pair_test_done(0);
}

static int pair_test_prepare(uiCmdCmdId command,uiCmdValue *value,void *data){
    pair_test_log=_wfopen(L"local-control-pair.log",L"wt, ccs=UTF-8");
    if(!pair_test_log)return pair_test_done(0);
    setvbuf(pair_test_log,NULL,_IONBF,0);
    ProMdlName source_name;
    Fixture geometry;
    model_background_mode=1;
    ProError error=fixture(&pair_test_source,source_name,&geometry);
    model_background_mode=0;
    if(!error)error=ProMdlVerstampGet(pair_test_source,&pair_test_initial);
    if(!error)error=dv_display_new(pair_test_source);
    if(!error)error=ProDirectoryCurrentGet(pair_test_directory);
    if(error||swprintf_s(pair_test_trigger,_countof(pair_test_trigger),L"%ls\\local-control-pair-verify.txt",pair_test_directory)<0){
        fwprintf(pair_test_log,L"FIXTURE_SETUP_ERROR=%d\n",error);
        return pair_test_done(0);
    }
    pair_test_deadline=GetTickCount64()+180000;
    pair_test_timer=SetTimer(native_backend_window,7250,200,pair_test_tick);
    if(!pair_test_timer)return pair_test_done(0);
    fwprintf(pair_test_log,L"FIXTURE_READY=1\nSOURCE=%ls\nREQUEST_DRAWING=%ls\n",source_name,pair_test_name);
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    uiCmdCmdId cmd;
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    if(!error)error=ProCmdActionAdd("BWTLPairFixturePrepare",pair_test_prepare,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!error)error=ProCmdActionAdd("BWTLPairFixtureReadback",pair_test_verify,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLPairFixturePrepare\x60;");
}
