#define BWTL_WORKBENCH_EMBEDDED
#include "native_pressfit_workbench_baseline.c"
#undef user_initialize

static const wchar_t batch_command[]=L"~ Command " L"\x60" L"BWTLWorkbenchBatch\x60;";
static ProMdl batch_assembly=NULL;

static ProError workbench_test_part(const wchar_t *selector,ProMdl *source,ProMdl *project,
    wchar_t library[MAX_PATH],wchar_t saved[MAX_PATH],BsoFitting *fitting,PartText *out){
    ProError error=pressfit_read_exact(selector,source,library,out);
    if(!error)error=pressfit_library_project(*source,library,project,saved,out);
    if(!error)error=fastener_pressfit_fitting(*project,1,139,90,fitting,out);
    return error;
}

static ProError workbench_test_place(ProMdl parent,ProMdl source,ProMdl project,const wchar_t *saved,
        const BsoFitting *fitting,BsoSite sites[12],int n,int replace,PressfitCheckpoint *checkpoint,
        PressfitBatchResult *result,PartText *out){
    FastenerSavedView *guard=pressfit_project_placement_begin(project,saved);
    ProError error=fastener_pressfit_place_batch(parent,project,fitting,sites,n,1,0,replace,checkpoint,result,out);
    if(!ax_recovery_uncertain)pressfit_project_placement_end(source,project,saved,guard);
    return error;
}

static int pressfit_fixture_phase(void){
    wchar_t mode[24]=L"";
    GetEnvironmentVariableW(L"BWTL_TEST_WORKBENCH_PHASE",mode,_countof(mode));
    if(!_wcsicmp(mode,L"pause"))return 1;
    if(!_wcsicmp(mode,L"resume"))return 2;
    return 0;
}


static int run_workbench_fixture(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    if(!report)report=_wfopen(L"pressfit-workbench-native.log",L"wt, ccs=UTF-8");
    if(!report){ProEngineerEnd();return 0;}
    ProError error=0;
    double mm=0;
    AxScan *scan=NULL;
    AxComponent panel={0};
    PressfitCheckpoint journal={0},other_guard={0};
    PressfitBatchResult result={0};
    ProMdl source=NULL,project=NULL,short_source=NULL,short_project=NULL;
    BsoFitting fit={0},short_fit={0};
    BsoSite six[12]={0},restored[12]={0},single[12]={0};
    wchar_t library[MAX_PATH]=L"",saved[MAX_PATH]=L"",short_library[MAX_PATH]=L"",short_saved[MAX_PATH]=L"";
    wchar_t reason[11000]=L"",path[48];
    PartText out={reason,_countof(reason),0};
    static const wchar_t selector[]=L"BSO_3_5M3X8";
    static const wchar_t changed_selector[]=L"BSO_3_5M3X6";
    int count=0,phase=pressfit_fixture_phase();
#define ASSERT(label,expr) do{error=(expr);fwprintf(report,L"%hs=%d\n",label,error);fflush(report);if(error)goto done;}while(0)
    if(!batch_assembly){
        ASSERT("LOAD_USABLE_PRT_ASM",ProMdlnameRetrieve((wchar_t*)assembly_name,PRO_MDL_ASSEMBLY,&batch_assembly));
        ASSERT("DISPLAY_PRIVATE_ASSEMBLY",ProMdlDisplay(batch_assembly));
        int window;
        ASSERT("GET_PRIVATE_WINDOW",ProMdlWindowGet(batch_assembly,&window));
        ASSERT("CURRENT_PRIVATE_WINDOW",ProWindowCurrentSet(window));
        ASSERT("ACTIVATE_PRIVATE_WINDOW",ProWindowActivate(window));
        ASSERT("WAIT_FOR_ACTIVE_WINDOW",ProMacroLoad((wchar_t*)batch_command));
        return 0;
    }

    ASSERT("ACTUAL_ACTIVE_ASSEMBLY",ProMdlCurrentGet(&assembly)||assembly!=batch_assembly?PRO_TK_BAD_CONTEXT:0);
    ASSERT("TEST_CHAT_RUNTIME",!native_ui_open_threaded(NULL)?PRO_TK_BAD_CONTEXT:0);
    ASSERT("MILLIMETER_ASSEMBLY",qa_mm_per_unit(assembly,&mm));
    ASSERT("MILLIMETER_SCALE",fabs(mm-1)>1e-10?PRO_TK_BAD_CONTEXT:0);
    ASSERT("ACTUAL_PANEL",ax_scan(assembly,1,&scan));
    ASSERT("EXACT_EXPECTED_INITIAL_COMPONENTS",scan->count!=(phase==2?2:1)?PRO_TK_BAD_CONTEXT:0);
    ax_path_label(scan->occurrences[0].ids,scan->occurrences[0].depth,path,_countof(path));
    ASSERT("READ_PANEL_OCCURRENCE",ax_hw_occurrence(assembly,path,mm,&panel));
    free(scan);scan=NULL;
    ASSERT("SIX_PHYSICAL_HOLES",fastener_bso_collect(&panel,159,6,mm,six,&count,&out));
    ASSERT("MATCH_EXACT_X8",workbench_test_part(selector,&source,&project,library,saved,&fit,&out));

    ASSERT("READ_ONLY_PREASSEMBLY_OCCUPANCY",fastener_pressfit_batch_preflight(assembly,project,&fit,six,count,mm,&out));
    if(phase!=2){
    ASSERT("START_DURABLE_E_DRIVE_BATCH",pressfit_checkpoint_start(assembly,selector,library,&fit,six,count,mm,0,0,&journal,&out));
    ASSERT("SECOND_BATCH_LOCKED",pressfit_checkpoint_lock(assembly,&other_guard)==PRO_TK_E_IN_USE?0:PRO_TK_INCOMPLETE);
    ASSERT("WRITE_BEFORE_NATIVE_CHANGE",pressfit_checkpoint_mark(&journal,1,0,0,NULL));
    single[0]=six[0];
    ASSERT("NATIVE_FIRST_ENTRY",workbench_test_place(assembly,source,project,saved,&fit,single,1,0,NULL,&result,&out));
    ASSERT("FIRST_ENTRY_CONSTRAINTS",result.added==1&&result.rows[0].regenerated&&result.rows[0].constraints==2?0:PRO_TK_INCOMPLETE);
    ASSERT("WRITE_AFTER_NATIVE_CHANGE",pressfit_checkpoint_mark(&journal,0,0,0,&result.rows[0]));
    pressfit_checkpoint_close(&journal);
    ASSERT("EXACT_ONE_HOLE_PRESERVED",active_count(2));
    ASSERT("USER_SAVES_PARTIAL_ASSEMBLY",ProMdlSave(assembly));
    if(phase==1)goto done;
    }
    single[0]=six[0];
    ASSERT("LOAD_EXISTING_E_DRIVE_BATCH",pressfit_checkpoint_load(assembly,&journal,&out));
    ASSERT("RESTORE_EXACT_STAMP_AND_GEOMETRY",pressfit_checkpoint_sites(assembly,library,&fit,&journal,restored,&out));
    ASSERT("NATIVE_REUSE_PLUS_FIVE",workbench_test_place(assembly,source,project,saved,&fit,restored,6,0,&journal,&result,&out));
    ASSERT("NO_REPLAY_OF_COMPLETED_HOLE",result.added==5&&result.reused==1&&result.rows[0].id==journal.sites[0].id?0:PRO_TK_INCOMPLETE);
    pressfit_checkpoint_close(&journal);
    ASSERT("ALL_HOLES_PLACED",active_count(7));
    ASSERT("EXPLICIT_TRUE_GEOMETRY_POSTFIT_ALL_SIX",fastener_pressfit_batch_audit(assembly,restored,&result,mm,0,&out));
    ASSERT("READ_ONLY_LATEST_BATCH_AUDIT",fastener_pressfit_audit_last(assembly,&out));
    ASSERT("LEARN_FROM_VERIFIED_ACTUAL_CONSTRAINTS",pressfit_method_learn(assembly,&out));
    ASSERT("RECALL_MEASURED_NATIVE_METHOD",pressfit_method_match(L"BSO M3×8",library,&fit,restored,6,1,&out)?0:PRO_TK_INCOMPLETE);
    int duplicate_id=0;
    const wchar_t *phase_name=L"";
    ASSERT("TEST_CREATE_IDENTICAL_DUPLICATE",fastener_bso_install_site(assembly,project,&single[0],&fit,1,&duplicate_id,&phase_name,&out));
    ASSERT("MATCH_DUPLICATE_WITHOUT_FURTHER_MUTATION",workbench_test_place(assembly,source,project,saved,&fit,single,1,0,NULL,&result,&out)==PRO_TK_INCOMPLETE&&result.rows[0].disposition==PRESSFIT_DUPLICATE?0:PRO_TK_INCOMPLETE);
    fastener_pressfit_remove_trial(assembly,duplicate_id,&out);
    ASSERT("RECOVER_DUPLICATE_TEST_ONLY",active_count(7));
    ASSERT("EXACT_SAVED_ASSEMBLY_BEFORE_REPLACE",ProMdlSave(assembly));
    ASSERT("LOAD_CORRECT_SHORT_EXACT_SPEC",workbench_test_part(changed_selector,&short_source,&short_project,
        short_library,short_saved,&short_fit,&out));

    ASSERT("DO_NOT_REPLACE_UNLESS_REQUESTED",workbench_test_place(assembly,short_source,short_project,short_saved,
        &short_fit,single,1,0,NULL,&result,&out)==PRO_TK_INCOMPLETE&&
        result.rows[0].disposition==PRESSFIT_WRONG?0:PRO_TK_INCOMPLETE);
    ASSERT("KEEP_ALL_ORIGINAL_POSITIONS",active_count(7));
    ASSERT("BACKUP_AND_REPLACE_WITH_NATIVE_READBACK",workbench_test_place(assembly,short_source,short_project,short_saved,
        &short_fit,single,1,1,NULL,&result,&out));
    ASSERT("ACTUAL_REPLACEMENT_NOT_DUPLICATED",result.replaced==1&&result.rows[0].constraints==2&&
        result.rows[0].radial<=0.001&&fabs(result.rows[0].gap)<=0.001?0:PRO_TK_INCOMPLETE);
    ASSERT("SAME_COMPONENT_COUNT_AFTER_REPLACE",active_count(7));
    ASSERT("SAVE_MIXED_5_X8_AND_1_X6",ProMdlSave(assembly));
    ProPath dir,saved_assembly;
    ASSERT("TEST_WORKDIR",ProDirectoryCurrentGet(dir));
    company_trim_path(dir);
    ASSERT("FIND_SAVED_NATIVE_MIXED_ASSEMBLY",ax_native_source(dir,assembly_name,PRO_MDL_ASSEMBLY,saved_assembly));
    fwprintf(report,L"SAVED_ASSEMBLY=%ls\n",saved_assembly);
done:
    if(scan)free(scan);
    pressfit_checkpoint_close(&journal);
    pressfit_checkpoint_close(&other_guard);
    fwprintf(report,L"RECEIPTS\n%ls\nWORKBENCH_BATCH_PASS=%d\n",reason,!error);
    fclose(report);
    ProEngineerEnd();
    return 0;
#undef ASSERT
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_short_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLWorkbenchBatch",run_workbench_fixture,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t*)batch_command);
}
