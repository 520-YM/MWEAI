#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static ProMdl sample_asm,sample_panel,sample_moving;
static ProAsmcomp mounted_panel,moving;
static ProMdlName sample_name,panel_name,moving_name=L"MARKUP_MOVE_TEST";
static FILE *result;
static int phase;
static const wchar_t case_macro[]=L"~ Command \x60" L"BWTLMarkupActionsTest\x60;";

static ProError sample_feature(const wchar_t *format){
    wchar_t text[512];
    swprintf_s(text,_countof(text),format,panel_name);
    return expansion_build(sample_panel,text,result);
}

static int find_component_mark(const MarkupCapture *capture,ProMdl model,int component,MarkupMark *mark){
    for(int y=1;y<24;y++)for(int x=1;x<32;x++){
        const double px=capture->face_view.left+
            (capture->face_view.right-capture->face_view.left)*(double)x/32.0;
        const double py=capture->face_view.top+
            (capture->face_view.bottom-capture->face_view.top)*(double)y/24.0;
        MarkupMark selected={MARKUP_ARROW,px-9,py-8,px,py,1};
        MarkupAssemblyPick pick={0};
        if(!markup_assembly_pick(capture,&selected,&pick)&&pick.component.model==model&&
           pick.component.path.table_num==1&&pick.component.feature.id==component){
            *mark=selected;
            return 1;
        }
    }
    return 0;
}

static ProError request_action(MarkupRequest *request,const wchar_t *text){
    wchar_t output[2000]=L"";
    wcscpy_s(request->text,_countof(request->text),text);
    ProError status=PRO_TK_E_NOT_FOUND;
    int routed=markup_assembly_action(&markup_capture,request,output,_countof(output),&status);
    fwprintf(result,L"REQUEST=%ls\nROUTED=%d CREO=%d\n%ls\n",text,routed,status,output);
    fflush(result);
    return routed?status:PRO_TK_E_NOT_FOUND;
}

static int assembly_actions_run(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    if(!result)result=_wfopen(L"markup-assembly-actions.log",L"wt, ccs=UTF-8");
    if(!result){ProEngineerEnd();return 0;}
    ProError error=0;
    AxScan *scan=NULL;
    const char *stage="private markup assembly";
#define CHECK(label,call) do{error=(call);fwprintf(result,L"%hs=%d\n",label,error);fflush(result);if(error)goto done;}while(0)
    if(!phase){
        model_background_mode=1;
        CHECK("CREATE_PANEL",model_create_empty(2,panel_name,&stage));
        CHECK("LOAD_PANEL",ProMdlnameInit(panel_name,PRO_MDL_PART,&sample_panel));
        CHECK("PANEL_PROFILE",sample_feature(L"CREO_OP SMT_SKETCH %ls PROFILE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60"));
        CHECK("PANEL_WALL",sample_feature(L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:PROFILE 1.5 POS"));
        CHECK("LEFT_PROFILE",sample_feature(L"CREO_OP SMT_SKETCH %ls H1_SK PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 2.7"));
        CHECK("LEFT_CUT",sample_feature(L"CREO_OP SMT_CUT %ls H1 SKETCH:H1_SK NEG"));
        CHECK("RIGHT_PROFILE",sample_feature(L"CREO_OP SMT_SKETCH %ls H2_SK PLANE:FRONT PLANE:RIGHT CIRCLE 50 20 2.7"));
        CHECK("RIGHT_CUT",sample_feature(L"CREO_OP SMT_CUT %ls H2 SKETCH:H2_SK NEG"));
        const ModelPlan shape={0,20,20,8,0,1};
        CHECK("SEED_MOVING_BLOCK",quick_seed(moving_name,&sample_moving));
        CHECK("BUILD_MOVING_BLOCK",model_build_internal(&shape,moving_name,&stage,sample_moving,NULL,NULL));
        CHECK("CREATE_ASSEMBLY",model_create_empty(3,sample_name,&stage));
        CHECK("LOAD_ASSEMBLY",ProMdlnameInit(sample_name,PRO_MDL_ASSEMBLY,&sample_asm));
        ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        CHECK("ADD_MOUNTING_PANEL",ProAsmcompAssemble((ProAssembly)sample_asm,(ProSolid)sample_panel,pose,&mounted_panel));
        pose[3][0]=135;
        CHECK("ADD_MOVING_BLOCK",ProAsmcompAssemble((ProAssembly)sample_asm,(ProSolid)sample_moving,pose,&moving));
        wchar_t command[300],receipt[700]=L"";
        PartCommand placement={0};
        swprintf_s(command,_countof(command),
            L"CREO_OP ASM_OFFSET %ls ID:%d ROOT PLANE:ASM_RIGHT PLANE:RIGHT 135",sample_name,moving.id);
        CHECK("PARSE_EXISTING_OFFSET",part_command_parse(command,&placement)==1?0:PRO_TK_BAD_INPUTS);
        CHECK("APPLY_EXISTING_OFFSET",ax_execute(sample_asm,&placement,receipt,_countof(receipt)));
        model_background_mode=0;
        CHECK("DISPLAY_ASSEMBLY",dv_display_new(sample_asm));
        phase=1;
        CHECK("SCHEDULE_VIEW",ProMacroLoad((wchar_t *)case_macro));
        return 0;
    }
    if(phase==1){
        int window;
        CHECK("ASSEMBLY_WINDOW",ProWindowCurrentGet(&window));
        (void)ProWindowBrowserSizeSet(window,0);
        ProMatrix front={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        CHECK("ORTHOGRAPHIC_VIEW",ProViewMatrixSet(sample_asm,NULL,front));
        CHECK("REPAINT_VIEW",ProWindowRepaint(window));
        phase=2;
        CHECK("SCHEDULE_CAPTURE",ProMacroLoad((wchar_t *)case_macro));
        return 0;
    }
    CHECK("ASSEMBLY_CAPTURE",markup_take(&markup_capture));
    CHECK("ASSEMBLY_CAPTURE_TYPED",markup_capture.assembly?0:PRO_TK_INVALID_TYPE);
    MarkupRequest request={.ticket=markup_capture.ticket,.count=2};
    CHECK("MARK_VISIBLE_MOVING_INSTANCE",find_component_mark(&markup_capture,sample_moving,moving.id,&request.marks[0])?0:PRO_TK_E_NOT_FOUND);
    CHECK("MARK_CLEARANCE_REFERENCE",find_component_mark(&markup_capture,sample_panel,mounted_panel.id,&request.marks[1])?0:PRO_TK_E_NOT_FOUND);
    ProMatrix before,after,panel_before,panel_after;
    CHECK("READ_MOVING_POSE_BEFORE",ax_actual_pose(sample_asm,&moving,before));
    CHECK("READ_PANEL_POSE_BEFORE",ax_actual_pose(sample_asm,&mounted_panel,panel_before));
    CHECK("RIGHT_FIVE_FROM_MARK",request_action(&request,L"把这个组件往右移 5 mm，再检查间隙"));
    CHECK("READ_MOVING_POSE_AFTER",ax_actual_pose(sample_asm,&moving,after));
    CHECK("READ_PANEL_POSE_AFTER",ax_actual_pose(sample_asm,&mounted_panel,panel_after));
    CHECK("EXACT_WORLD_MOVEMENT",fabs(after[3][0]-before[3][0]-5)<0.001&&
        fabs(after[3][1]-before[3][1])<0.001&&fabs(after[3][2]-before[3][2])<0.001?0:PRO_TK_INCOMPLETE);
    CHECK("OTHER_INSTANCE_PRESERVED",ax_same_pose(panel_before,panel_after,1)?0:PRO_TK_INCOMPLETE);
    ProError stale=request_action(&request,L"把这个组件往右移 5 mm，再检查间隙");
    CHECK("REJECT_STALE_CAPTURE",stale==PRO_TK_BAD_CONTEXT?0:PRO_TK_INCOMPLETE);
    markup_capture_clear();
    CHECK("RECAPTURE_FOR_FASTENERS",markup_take(&markup_capture));
    request=(MarkupRequest){.ticket=markup_capture.ticket};
    for(int i=0;i<markup_capture.image->count;i++){
        const MarkupHole *hole=&markup_capture.image->holes[i];
        if(hole->occurrence.depth!=1||hole->occurrence.ids[0]!=mounted_panel.id||
           fabs(hole->diameter*hole->occurrence.mm_per_native-5.4)>0.001)continue;
        CHECK("MARK_CAPACITY",request.count<MARKUP_MAX_MARKS?0:PRO_TK_OUT_OF_RANGE);
        request.marks[request.count++]=(MarkupMark){MARKUP_ARROW,hole->x-8,hole->y-8,hole->x,hole->y,1};
    }
    fwprintf(result,L"CAPTURED_MARKED_HOLES=%d\n",request.count);
    CHECK("TWO_ANNOTATED_HOLES",request.count==2?0:PRO_TK_INCOMPLETE);
    CHECK("FIT_EXACT_BSO_FROM_MARKS",request_action(&request,L"这些孔装 BSO M3×8"));
    CHECK("READ_ALL_INSTANCES",ax_scan(sample_asm,1,&scan));
    const int installed=scan->count-2;
    CHECK("TWO_NEW_FASTENERS",installed==2?0:PRO_TK_INCOMPLETE);
    for(int i=0;i<scan->count;i++){
        AxOccurrence *item=&scan->occurrences[i];
        if(item->model==sample_panel||item->model==sample_moving)continue;
        ProAsmcomp fastener;
        int constraints=0;
        CHECK("FIT_NATIVE_INSTANCE",ax_component_feature(sample_asm,item->ids[0],&fastener));
        CHECK("FIT_REAL_CONSTRAINTS",ax_constraint_count(&fastener,&constraints));
        CHECK("TWO_REAL_LOCATORS",constraints==2?0:PRO_TK_INCOMPLETE);
    }
    CHECK("ASSEMBLY_HEALTH",part_health(sample_asm));
done:
    free(scan);
    markup_capture_clear();
    fwprintf(result,L"MARKUP_ASSEMBLY_ACTIONS_PASS=%d\n",!error);
    fclose(result);result=NULL;
    ProEngineerEnd();
    return 0;
#undef CHECK
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLMarkupActionsTest",assembly_actions_run,
        uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t *)case_macro);
}
