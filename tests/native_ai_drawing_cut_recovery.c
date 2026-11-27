#define user_initialize assistant_initialize_for_cut_fixture
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static ProMdl cut_model;
static ProMdlName cut_name;
static FILE *cut_report;

static int cut_done(int success){
    if(cut_report){
        fwprintf(cut_report,L"CUT_FOUNDATION_NATIVE_PASS=%d\n",success);
        fclose(cut_report);
    }
    smart_cancel();
    ProEngineerEnd();
    return 0;
}

static int cut_step(const wchar_t *name,const wchar_t *direction){
    wchar_t protocol[420];
    swprintf_s(protocol,_countof(protocol),L"CREO_OP SMT_CUT %ls %ls SKETCH:CUT_PROFILE %ls",cut_name,name,direction);
    if(part_command_parse(protocol,&pending_part)!=1||part_snapshot_take_mode(cut_model,&request_snapshot,0))return 0;
    execute_part_command();
    smart_followup=0;
    return 1;
}

static int checked_sheet_step(const wchar_t *format){
    wchar_t protocol[550];
    swprintf_s(protocol,_countof(protocol),format,cut_name);
    if(part_command_parse(protocol,&pending_part)!=1||part_snapshot_take_mode(cut_model,&request_snapshot,0))return 0;
    execute_part_command();
    smart_followup=0;
    return 1;
}


static int cut_run(uiCmdCmdId id,uiCmdValue *value,void *data){
    ProMdl active=NULL;
    ProMassProperty before,after;
    ProError error=ProMdlCurrentGet(&active);
    if(!error&&active!=cut_model)error=PRO_TK_BAD_CONTEXT;
    if(!error){
        smart_begin(L"在这个空钣金件画矩形草绘，然后用它创建首壁和贯穿孔草绘。");
        smart_intent=SMART_INTENT_CURRENT;
        smart_readonly=0;
        smart_target_model=cut_model;
        const int missing=checked_sheet_step(
            L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:NO_SUCH_DATUM PLANE:RIGHT RECT 0 0 80 60")&&
            smart_active&&smart_binding_valid()&&smart_progress==0&&smart_cad_pending_count==1&&
            sh_cad_rolled_back_model==cut_model&&wcsstr(smart_last_failure_brief,L"NO_SUCH_DATUM");
        fwprintf(cut_report,L"CLEAN_SHEET_SKETCH_FAILURE_RETAINS_TASK=%d\n",missing);
        const int resumed=missing&&checked_sheet_step(
            L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60")&&
            checked_sheet_step(L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 2 POS")&&
            checked_sheet_step(L"CREO_OP SMT_SKETCH %ls CUT_PROFILE PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 3")&&
            smart_progress==3&&!smart_cad_pending_count;
        fwprintf(cut_report,L"CONTINUE_ACTUAL_EMPTY_SHEET_WALL_AND_SKETCH=%d\n%ls\n",resumed,smart_feedback);
        if(!resumed)error=PRO_TK_INCOMPLETE;
    }

    if(!error)error=qa_geometry_mass(active,&before);
    fwprintf(cut_report,L"SHEET_AND_CLOSED_ROUND_PROFILE=%d\n",error);
    if(error)return cut_done(0);
    smart_begin(L"在已打开的钣金板上沿正方向用当前草绘开贯穿孔，保持方向。");
    smart_intent=SMART_INTENT_CURRENT;
    smart_readonly=0;
    smart_target_model=active;
    if(!cut_step(L"ACTUAL_CUT",L"POS"))return cut_done(0);
    const int exact_direction=!smart_progress&&smart_cad_pending_count==1&&
        !qa_geometry_mass(active,&after)&&part_close(before.volume,after.volume)&&smart_binding_valid();
    fwprintf(cut_report,L"EXPLICIT_DIRECTION_PRESERVED=%d\n",exact_direction);
    if(!exact_direction)return cut_done(0);
    smart_begin(L"在已打开的钣金板现有闭合草绘处开一个直径6毫米贯穿孔，尺寸不变。");
    smart_intent=SMART_INTENT_CURRENT;
    smart_readonly=0;
    smart_target_model=active;
    if(!cut_step(L"ACTUAL_CUT",L"POS"))return cut_done(0);
    fwprintf(cut_report,L"FIRST_FLOW progress=%d failures=%d unresolved=%d rolledback_no_material=%d\n%ls\n",
        smart_progress,smart_total_failures,smart_cad_pending_count,sh_cut_rolled_back_no_material,smart_feedback);
    fflush(cut_report);
    if(!smart_progress){
        if(!smart_active||!smart_binding_valid()||!cut_step(L"ACTUAL_CUT",L"NEG"))return cut_done(0);
        fwprintf(cut_report,L"SECOND_FLOW progress=%d failures=%d unresolved=%d\n%ls\n",
            smart_progress,smart_total_failures,smart_cad_pending_count,smart_feedback);
    }
    ProFeature feature;
    ProFeatStatus state;
    const int pass=!qa_geometry_mass(active,&after)&&fabs(before.volume-after.volume-18*AS_PI)<0.001&&
        !ux_named_feature(active,L"ACTUAL_CUT",&feature)&&!ProFeatureStatusGet(&feature,&state)&&state==PRO_FEAT_ACTIVE&&
        smart_progress==1&&!smart_cad_pending_count&&wcsstr(smart_feedback,L"原生回读")&&wcsstr(smart_feedback,L"实际板厚 2 mm");
    if(pass)fwprintf(cut_report,L"NATIVE_MATERIAL_REMOVED=%.10g\n",before.volume-after.volume);
    if(!pass)return cut_done(0);
    before=after;

    smart_begin(L"在这块钣金板居中打四个直径4毫米的通孔，孔距固定110×90毫米。");
    smart_intent=SMART_INTENT_CURRENT;
    smart_readonly=0;
    smart_target_model=active;
    const int exact_spacing=checked_sheet_step(L"CREO_OP SMT_FLAT4 %ls 4 110 90")&&
        smart_active&&smart_binding_valid()&&smart_cad_pending_count==1&&!smart_progress&&
        !qa_geometry_mass(active,&after)&&part_close(before.volume,after.volume)&&
        !wcsstr(smart_feedback,L"改用");
    fwprintf(cut_report,L"EXPLICIT_FOUR_HOLE_PITCH_PRESERVED=%d\n",exact_spacing);
    if(!exact_spacing)return cut_done(0);

    smart_begin(L"在这块钣金板居中打四个直径4毫米的通孔，孔距优先110×90毫米。要是放不下，请你按实测自行决定横纵孔距。");
    smart_intent=SMART_INTENT_CURRENT;
    smart_readonly=0;
    smart_target_model=active;
    if(part_snapshot_take_mode(active,&request_snapshot,0))return cut_done(0);
    const int wrong_diameter=smart_four_task_unverified()&&
        checked_sheet_step(L"CREO_OP SMT_FLAT4 %ls 5 40 30")&&
        smart_four_task_unverified()&&!smart_progress&&
        !qa_geometry_mass(active,&after)&&part_close(before.volume,after.volume)&&
        wcsstr(smart_feedback,L"孔径须使用你指定的 4 mm");
    fwprintf(cut_report,L"UNVERIFIED_FOUR_HOLES_AND_WRONG_DIAMETER_BLOCKED=%d\n",wrong_diameter);
    if(!wrong_diameter)return cut_done(0);

    smart_begin(L"在这块钣金板居中打四个直径4毫米的通孔，孔距优先110×90毫米。要是放不下，请你按实测自行决定横纵孔距。");
    smart_intent=SMART_INTENT_CURRENT;
    smart_readonly=0;
    smart_target_model=active;
    const int corrected=checked_sheet_step(L"CREO_OP SMT_FLAT4 %ls 4 110 90")&&
        smart_progress==1&&smart_total_failures==1&&!smart_cad_pending_count&&!smart_four_task_unverified()&&
        !qa_geometry_mass(active,&after)&&fabs(before.volume-after.volume-32*AS_PI)<0.001&&
        wcsstr(smart_feedback,L"已按实测 80 × 60 mm 板面")&&
        wcsstr(smart_feedback,L"4 个 Ø4 mm 通孔")&&
        wcsstr(smart_feedback,L"已从 Creo 实体回读孔径、孔位和贯穿结果")&&
        !ux_named_feature(active,L"ACTUAL_CUT",&feature)&&!ProFeatureStatusGet(&feature,&state)&&state==PRO_FEAT_ACTIVE;
    fwprintf(cut_report,L"AUTONOMOUS_FOUR_HOLE_REPLAN=%d\n%ls\n",corrected,smart_feedback);
    return cut_done(corrected);
}

static int cut_setup(uiCmdCmdId id,uiCmdValue *value,void *data){
    cut_report=_wfopen(L"cad-foundation-cut.log",L"wt, ccs=UTF-8");
    if(!cut_report)return cut_done(0);
    const char *phase=NULL;
    ProError error=model_create_empty(2,cut_name,&phase);
    if(!error)error=ProMdlnameInit(cut_name,PRO_MDL_PART,&cut_model);
    if(!error)error=dv_display_new(cut_model);
    fwprintf(cut_report,L"NATIVE_SHEET_SETUP=%d\n",error);
    fflush(cut_report);
    return error||ProMacroLoad(L"~ Command \x0060" L"BWTLCadCutRun\x0060;")?cut_done(0):0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=assistant_initialize_for_cut_fixture(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLCadCutSetup",cut_setup,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLCadCutRun",cut_run,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLCadCutSetup\x0060;");
}
