#define user_initialize bwtl_drawing_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static FILE *drawing_report;
static ProMdl drawing_part;
static ProMdl drawing_parent;
static SmartPartSignature old_signature;
static ProMdl old_part;
static int drawing_phase,drawing_tick_queued,drawing_feature_id;
static ULONGLONG drawing_deadline;
static const UINT_PTR drawing_timer_id=6810;
static const wchar_t drawing_number[]=L"M26A0181091V00";
static const wchar_t base_request[]=L"画一个u形加强筋，尺寸500-20-10mm，板厚2mm，图号M26A0181091，版本号V00。";

static int drawing_finish(int pass){
    KillTimer(native_backend_window,drawing_timer_id);
    fwprintf(drawing_report,L"DRAWING_CHAT_PASS=%d\n",pass);
    fclose(drawing_report);
    smart_cancel();
    ProEngineerEnd();
    return 0;
}

static int drawing_check(const wchar_t *name,int pass){
    fwprintf(drawing_report,L"%ls=%d\n",name,pass);
    fflush(drawing_report);
    return pass;
}

static void CALLBACK drawing_timer(HWND hwnd,UINT message,UINT_PTR id,DWORD time){
    static wchar_t command[]=L"~ Command \x0060" L"BWTLAIDrawingProbeTick\x0060;";
    if(!smart_active&&!ai_process&&!drawing_tick_queued)drawing_tick_queued=!ProMacroLoad(command);
}

static int drawing_submit(const wchar_t *message){
    history[0]=0;
    native_prompt_set(message);
    run_local(NULL,NULL,NULL);
    fwprintf(drawing_report,L"REQUEST %ls\n",message);
    fflush(drawing_report);
    return !ai_process;
}

static int drawing_measure(ProMdl model,double target_height){
    Pro3dPnt outline[2];
    ProDimension t;
    ProMdlsubtype subtype;
    double mm,thickness;
    ProMatrix axes={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    ProSolidOutlExclTypes omit[]={PRO_OUTL_EXC_DATUM_PLANE,PRO_OUTL_EXC_DATUM_POINT,
        PRO_OUTL_EXC_DATUM_CSYS,PRO_OUTL_EXC_DATUM_AXES,PRO_OUTL_EXC_FACETS,PRO_OUTL_EXC_ALL_CRVS};
    if(ProMdlSubtypeGet(model,&subtype)||subtype!=PROMDLSTYPE_PART_SHEETMETAL||
       qa_mm_per_unit(model,&mm)||ProSolidOutlineCompute((ProSolid)model,axes,omit,_countof(omit),outline)||
       ProSmtPartThicknessGet((ProPart)model,&t)||ProDimensionValueGet(&t,&thickness)||
       part_health(model))return 0;
    double spans[3];
    for(int i=0;i<3;i++)spans[i]=(outline[1][i]-outline[0][i])*mm;
    for(int i=0;i<2;i++)for(int j=i+1;j<3;j++)if(spans[i]<spans[j]){
        const double value=spans[i];spans[i]=spans[j];spans[j]=value;
    }
    fwprintf(drawing_report,L"ACTUAL_OUTSIDE=%g,%g,%g THICKNESS=%g\n",spans[0],spans[1],spans[2],thickness*mm);
    return fabs(spans[0]-500)<0.001&&fabs(spans[1]-20)<0.001&&
        fabs(spans[2]-target_height)<0.001&&fabs(thickness*mm-2)<0.001;
}

static int drawing_single_card(void){
    const wchar_t *response=wcsstr(history,L"【AI】");
    fwprintf(drawing_report,L"RECEIPT\n%ls\n",history);
    return response&&!wcsstr(response+4,L"【AI】");
}

static int drawing_tick(uiCmdCmdId id,uiCmdValue *value,void *data){
    drawing_tick_queued=0;
    if(GetTickCount64()>drawing_deadline)return drawing_finish(drawing_check(L"TIMELY",0));
    if(smart_active||ai_process)return 0;
    ProMdl active=NULL;
    if(ProMdlCurrentGet(&active))return 0;
    if(!drawing_phase){
        if(active!=old_part)return 0;
        if(!drawing_check(L"LOCAL_CREATE",drawing_submit(base_request)))return drawing_finish(0);
        drawing_phase=1;
        return 0;
    }
    if(drawing_phase==1){
        ProMdlName name;
        ProFeature channel;
        AxScan *scan=NULL;
        ProAsmcomp component;
        ProMdlName wanted;
        wcscpy_s(wanted,_countof(wanted),drawing_number);
        const int identity=!ProMdlMdlnameGet(active,name)&&!_wcsicmp(name,wanted)&&
            !ProMdlnameInit(wanted,PRO_MDL_ASSEMBLY,&drawing_parent)&&active!=old_part;
        if(!drawing_check(L"OPENED_NAMED_SAME_STEM_SHEET",identity))return drawing_finish(0);
        drawing_part=active;
        const int default_seat=!ax_scan(drawing_parent,1,&scan)&&scan->count==1&&
            scan->occurrences[0].model==active&&
            !ax_component_feature(drawing_parent,scan->occurrences[0].ids[0],&component)&&!ax_default_verify(&component);
        free(scan);
        if(!drawing_check(L"ONE_DEFAULT_POSITIONED_COMPONENT",default_seat)||
           !drawing_check(L"REAL_EDITABLE_NATIVE_FEATURE",!ux_named_feature(active,cad_u_feature,&channel))||
           !drawing_check(L"CREATION_VERIFIED_EXTERNAL_DIMENSIONS",drawing_measure(active,10))||
           !drawing_check(L"SINGLE_CREATION_REPLY",drawing_single_card()))return drawing_finish(0);
        drawing_feature_id=channel.id;
        if(!drawing_check(L"LOCAL_HEIGHT_EDIT",drawing_submit(L"把刚才那个U形加强筋的外高改成12mm。")))return drawing_finish(0);
        drawing_phase=2;
        return 0;
    }
    const SmartPartSignature after=smart_part_signature(old_part);
    ProFeature edited;
    if(!drawing_check(L"EDIT_SAME_CHILD",active==drawing_part)||
       !drawing_check(L"SAME_NATIVE_FEATURE_AFTER_EDIT",!ux_named_feature(active,cad_u_feature,&edited)&&edited.id==drawing_feature_id)||
       !drawing_check(L"EDIT_VERIFIED_EXTERNAL_DIMENSIONS",drawing_measure(active,12))||
       !drawing_check(L"SINGLE_EDIT_REPLY",drawing_single_card())||
       !drawing_check(L"ORIGINAL_UNRELATED_MODEL_PRESERVED",!after.error&&after.hash==old_signature.hash))return drawing_finish(0);
    return drawing_finish(1);
}

static int drawing_start(uiCmdCmdId id,uiCmdValue *value,void *data){
    drawing_report=_wfopen(L"ai-drawing-chat.log",L"wt, ccs=UTF-8");
    if(!drawing_report){ProEngineerEnd();return 0;}
    CadURequest spec={0};
    if(!drawing_check(L"FULL_CHINESE_REQUEST_PARSES",cad_u_parse(base_request,&spec)&&spec.length==500&&
       spec.width==20&&spec.height==10&&spec.thickness==2&&spec.radius==2&&spec.template.require_company)||
       !drawing_check(L"QUESTIONS_DO_NOT_START_CREATION",!cad_u_parse(L"能画一个U形加强筋吗？",&spec))||
       !drawing_check(L"PDF_CAN_GROUND_EXPLICIT_NEW",cad_reference_scope(L"根据这份PDF，画一个钣金支架。")==SMART_INTENT_NEW)||
       !drawing_check(L"IMAGE_CAN_GROUND_EXPLICIT_EDIT",cad_reference_scope(L"照这张截图，修改当前钣金零件。")==SMART_INTENT_CURRENT)||
       !drawing_check(L"PDF_REVIEW_STAYS_READ_ONLY",!cad_reference_scope(L"根据这份PDF，只分析，不建模。")))return drawing_finish(0);
    ProMdlName old_name=L"BWTL_DRAWING_ORIGINAL";
    const char *stage;
    ModelPlan old_plan={.ready=1,.a=60,.b=40,.height=6};
    ProError error=quick_seed(old_name,&old_part);
    if(!error)error=model_build_internal(&old_plan,old_name,&stage,old_part,NULL,NULL);
    if(!error){old_signature=smart_part_signature(old_part);error=old_signature.error;}
    if(!error)error=dv_display_new(old_part);
    if(!drawing_check(L"PRIVATE_BASELINE_MODEL",!error)||
       !drawing_check(L"PRIVATE_USER_WINDOW",native_ui_open_threaded(NULL)))return drawing_finish(0);
    drawing_deadline=GetTickCount64()+45000;
    return SetTimer(native_backend_window,drawing_timer_id,350,drawing_timer)?0:drawing_finish(0);
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t message[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_drawing_initialize(argc,argv,version,build,message);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLAIDrawingProbeStart",drawing_start,uiProe2ndImmediate,access_command,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLAIDrawingProbeTick",drawing_tick,uiProe2ndImmediate,access_command,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLAIDrawingProbeStart\x0060;");
}
