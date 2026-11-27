#define BWTL_SKETCH_RECOVERY_TEST
#define user_initialize assistant_initialize_for_cad_fixture
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static ProMdl fixture_model;
static ProMdlName fixture_name;
static FILE *receipt;

static int cad_fixture_finish(int pass){
    if(receipt){
        fprintf(receipt,"CAD_FOUNDATION_RECOVERY_PASS=%d\n",pass);
        fclose(receipt);
    }
    smart_cancel();
    ProEngineerEnd();
    return 0;
}

static int cad_fixture_command(const wchar_t *feature,const wchar_t *orientation,int force){
    wchar_t protocol[380];
    const wchar_t *curve=force?L"CIRCLE 15 15 2":L"CIRCLE 12 12 3";
    swprintf_s(protocol,_countof(protocol),L"CREO_OP SKETCH_CREATE %ls %ls PLANE:FRONT %ls %ls",
        fixture_name,feature,orientation,curve);
    if(part_command_parse(protocol,&pending_part)!=1||part_snapshot_take_mode(fixture_model,&request_snapshot,0))return 0;
    fd_test_first_sketch_failure=force;
    execute_part_command();
    /* This fixture supplies the next native operation in the same callback. */
    smart_followup=0;
    return 1;
}

static int cad_fixture_run(uiCmdCmdId id,uiCmdValue *value,void *data){
    ProMdl active=NULL;
    ProMassProperty baseline,finished;
    ProFeature accepted;
    ProFeatStatus status;
#define CHECK(label,condition) do{const int ok=(condition)!=0;fprintf(receipt,label "=%d\n",ok);fflush(receipt);if(!ok)return cad_fixture_finish(0);}while(0)
    CHECK("CURRENT_IS_EXACT_PRIVATE_PART",!ProMdlCurrentGet(&active)&&active==fixture_model);
    CHECK("ACTUAL_BASE_SOLID",!qa_geometry_mass(active,&baseline));
    smart_begin(L"给当前零件画草绘，接着完成所有草绘。保持当前实体。");
    smart_intent=SMART_INTENT_CURRENT;
    smart_readonly=0;
    smart_target_model=active;
    CHECK("NATIVE_PARALLEL_REFERENCES",cad_fixture_command(L"RECOVER_THIS",L"PLANE:FRONT",0));
    CHECK("SPECIFIC_NATIVE_REFERENCE_DIAGNOSIS",wcsstr(smart_last_failure,L"PLANE:FRONT")&&
        wcsstr(smart_last_failure_brief,L"平行")&&smart_cad_pending_count==1&&smart_binding_valid());
    CHECK("APPEND_VERIFIED_UNRELATED_SKETCH",cad_fixture_command(L"OTHER_SKETCH",L"PLANE:RIGHT",0)&&smart_progress==1);
    CHECK("UNRELATED_PROGRESS_DOES_NOT_COMPLETE_FAILED_SKETCH",smart_needs_native_result(0)&&smart_cad_pending_count==1);
    CHECK("VALIDATED_DIFFERENT_ORIENTATION_SAME_TARGET",cad_fixture_command(L"RECOVER_THIS",L"PLANE:RIGHT",0)&&
        smart_progress==2&&!smart_needs_native_result(0)&&!smart_cad_pending_count);
    CHECK("NATIVE_ACCEPTED_FEATURE_AND_HEALTH",!ux_named_feature(active,L"RECOVER_THIS",&accepted)&&
        !ProFeatureStatusGet(&accepted,&status)&&status==PRO_FEAT_ACTIVE&&!part_health(active)&&wcsstr(smart_feedback,L"原生回读"));

    CHECK("DELIBERATE_FAILURE_AFTER_CREO_CREATES_PARTIAL_SKETCH",cad_fixture_command(L"DIM_TEST",L"PLANE:RIGHT",1));
    CHECK("VALIDATED_ROLLBACK_ALLOW_CONTINUATION",fd_sketch_rolled_back_model==active&&smart_active&&smart_binding_valid()&&
        wcsstr(smart_last_failure_brief,L"轮廓")&&wcsstr(smart_last_failure,L"已核对原活动零件")&&smart_cad_pending_count==1);
    CHECK("NO_FAILED_FEATURE_LEFT",ux_named_feature(active,L"DIM_TEST",&accepted)==PRO_TK_E_NOT_FOUND);
    CHECK("CHANGE_SKETCH_GEOMETRY_AND_REPLAY_ON_SAME_MODEL",cad_fixture_command(L"DIM_TEST",L"PLANE:RIGHT",0)&&smart_progress==3&&
        !smart_cad_pending_count&&!smart_needs_native_result(0));
    CHECK("INITIAL_SOLID_VOLUME_AND_TARGET_PRESERVED",!qa_geometry_mass(active,&finished)&&part_close(baseline.volume,finished.volume)&&
        !ProMdlCurrentGet(&active)&&active==fixture_model&&!part_health(active));
    fprintf(receipt,"NATIVE_MEASURED_VOLUME=%.10g\n",finished.volume);
    return cad_fixture_finish(1);
#undef CHECK
}

static int cad_fixture_setup(uiCmdCmdId id,uiCmdValue *value,void *data){
    receipt=fopen("cad-foundation-recovery.log","w");
    if(!receipt)return cad_fixture_finish(0);
    const ModelPlan stock={0,100,70,2,0,1};
    const char *stage=NULL;
    ProError error=model_create(&stock,fixture_name,&stage);
    if(!error)error=ProMdlnameInit(fixture_name,PRO_MDL_PART,&fixture_model);
    if(!error)error=dv_display_new(fixture_model);
    fprintf(receipt,"PRIVATE_SOLID_SETUP=%d\n",error);
    fflush(receipt);
    return error||ProMacroLoad(L"~ Command \x0060" L"BWTLCadFixtureRun\x0060;")?cad_fixture_finish(0):0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=assistant_initialize_for_cad_fixture(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLCadFixtureSetup",cad_fixture_setup,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLCadFixtureRun",cad_fixture_run,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLCadFixtureSetup\x0060;");
}
