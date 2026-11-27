#define user_initialize bwtl_short_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

typedef struct {
    int id,shaft,seat,length,face_only;
    const wchar_t *request,*component;
} DirectExample;
static const DirectExample examples[]={
    {5,139,90,5,0,L"装配bsoM3X5",L"BSO_3_5M3X5_BWTL"},
    {6,139,90,6,0,L"装配bsoM3X6",L"BSO_3_5M3X6_BWTL"},
    {8,139,90,8,0,L"装配bsoM3X8",L"BSO_3_5M3X8_BWTL"},
    {80,139,90,8,1,L"装配6个bsoM3X8",L"BSO_3_5M3X8_BWTL"},
    {41,89,66,0,0,L"装配S-M4-1",L"S-M4-1_BWTL_"},
    {42,136,63,0,0,L"装配BS-M4-2",L"BS-M4-2_BWTL_"},
    {88,139,90,8,0,L"装配BSOSM3X8",L"BSOS_3_5M3X8_BWTL_"},
    {71,139,90,8,1,L"这一面全部装配BSO M3×8",L"BSO_3_5M3X8_BWTL"},
    {72,139,90,8,0,L"这六孔装M3×8的BSO",L"BSO_3_5M3X8_BWTL"},
    {73,139,90,8,1,L"装2个BSO M3×8",L"BSO_3_5M3X8_BWTL"},
    {74,139,90,8,1,L"",L""},
    {75,139,90,8,1,L"这一面全部装配BSO M3×8 并核对干涉",L"BSO_3_5M3X8_BWTL"},
    {77,139,90,8,1,L"装配 BSO M3×6",L"BSO_3_5M3X8_BWTL"}
};
static const wchar_t *assembly_name=L"AI_ASM_261119_103135_1V00";
static const DirectExample *example;
static ProMdl assembly;
static FILE *report;
static const wchar_t command[]=L"~ Command `BWTLDirectPressfit`;";
static ProError test_select(AxComponent *panel,int id){
    ProSelection selection=NULL;
    ProModelitem item;
    ProError error=ProModelitemInit(panel->model,id,PRO_SURFACE,&item);
    if(!error)error=ProSelectionAlloc(&panel->path,&item,&selection);
    if(!error)error=ProSelbufferSelectionAdd(selection);
    if(selection)ProSelectionFree(&selection);
    return error;
}

static ProError check_six(ProMdl model,AxComponent *panel,double mm){
    AxScan *scan=NULL;
    ProAsmcomppath root;
    ProError error=ax_scan(model,1,&scan);
    if(!error)error=ax_root_path(model,&root);
    if(!error&&scan->count!=7)error=PRO_TK_INCOMPLETE;
    int verified=0,bores[6]={0};
    for(int i=0;!error&&i<scan->count;i++){
        AxOccurrence *e=scan->occurrences+i;
        if(e->model==panel->model)continue;
        ProName expected, actual_key;
        if(e->depth!=1||e->type!=PRO_MDL_PART||
           !pressfit_name_key(example->component,expected)||!pressfit_name_key(e->name,actual_key)||
           wcsncmp(actual_key,expected,wcslen(expected))){
            error=PRO_TK_BAD_CONTEXT;break;
        }
        AxComponent fitting_owner={.model=e->model};
        BsoFitting fit={.shaft=example->shaft,.seat=example->seat,.model=e->model};
        wchar_t shaft[32],seat[32],hole[32],face[32];
        fastener_bso_surface(fit.shaft,shaft);
        fastener_bso_surface(fit.seat,seat);
        if(example->length){
            wchar_t detail[300]=L"";
            PartText out={detail,_countof(detail),0};
            error=fastener_bso_measure_length(e->model,example->length,&out);
        }
        if(!error)error=ax_hw_site(&fitting_owner,shaft,seat,mm,&fit.geometry);
        ProAsmcomp comp;
        if(!error)error=ax_component_feature(model,e->ids[0],&comp);
        ProBoolean packaged=PRO_B_TRUE;
        if(!error)error=ProAsmcompIsPackaged(&comp,&packaged);
        ProAsmcompconstraint *rows=NULL;
        int n=0;
        if(!error)error=ax_read_constraints(&comp,&root,&rows,&n);
        if(!error&&(packaged||n!=2))error=PRO_TK_BAD_CONTEXT;
        int actual_hole=0,actual_face=0;
        for(int j=0;!error&&j<n;j++){
            ProAsmcompConstrType type;
            ProDatumside side;
            ProSelection selection;
            ProModelitem base_item;
            error=ProAsmcompconstraintTypeGet(rows[j],&type);
            if(!error&&(type!=(j?PRO_ASM_MATE:PRO_ASM_INSERT)))error=PRO_TK_BAD_CONTEXT;
            if(!error)error=ProAsmcompconstraintAsmreferenceGet(rows[j],&selection,&side);
            if(!error)error=ProSelectionModelitemGet(selection,&base_item);
            if(!error&&base_item.owner!=panel->model)error=PRO_TK_BAD_CONTEXT;
            if(!error&&(j==0))actual_hole=base_item.id;
            if(!error&&(j==1))actual_face=base_item.id;
        }
        if(rows)ProAsmcompconstraintArrayFree(rows);
        if(!error&&(!actual_hole||!actual_face))error=PRO_TK_BAD_CONTEXT;
        fastener_bso_surface(actual_hole,hole);
        fastener_bso_surface(actual_face,face);
        BsoSite site={.panel=*panel,.hole_id=actual_hole,.face_id=actual_face};
        if(!error)error=ax_hw_site(panel,hole,face,mm,&site.geometry);
        if(!error&&fabs(2*site.geometry.cylinder.radius*mm-5.4)>0.001)error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ax_hw_world(panel,&site.geometry);
        ProMatrix actual;
        if(!error)error=ax_actual_pose(model,&comp,actual);
        double radial=0,gap=0,facing=0,parallel=0;
        if(!error)error=fastener_bso_measure(&fit,&site,actual,mm,&radial,&gap,&facing,&parallel);
        if(!error&&(radial>0.001||fabs(gap)>0.001||facing>-1+1e-7||parallel<1-1e-7))error=PRO_TK_BAD_CONTEXT;
        for(int i=0;!error&&i<verified;i++)if(bores[i]==actual_hole)error=PRO_TK_E_FOUND;
        if(!error)bores[verified++]=actual_hole;
        fwprintf(report,L"JOINT %d %ls SURFACE:%d SURFACE:%d n=%d radial=%g gap=%g normals=%g CREO=%d\n",
            e->ids[0],e->name,actual_hole,actual_face,n,radial,gap,facing,error);
    }
    free(scan);
    return error?error:verified==6?0:PRO_TK_INCOMPLETE;
}

static ProError active_count(int expected){
    AxScan *scan=NULL;
    ProError status=ax_scan(assembly,1,&scan);
    if(!status&&scan->count!=expected)status=PRO_TK_BAD_CONTEXT;
    free(scan);
    return status;
}

static ProError native_dialog_request(const wchar_t *request){
    wchar_t fallback[MAX_PATH];
    swprintf_s(fallback,_countof(fallback),L"%ls\\prompt.txt",ai_dir);
    if(GetFileAttributesW(fallback)!=INVALID_FILE_ATTRIBUTES)return PRO_TK_BAD_CONTEXT;
    history[0]=0;
    native_prompt_set(request);
    run_local(NULL,NULL,NULL);
    fwprintf(report,L"REQUEST=%ls\nRECEIPT\n%ls\n",request,history);
    fflush(report);
    return ai_process||GetFileAttributesW(fallback)!=INVALID_FILE_ATTRIBUTES?
        PRO_TK_BAD_CONTEXT:PRO_TK_NO_ERROR;
}

static int run_direct_fixture(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    if(!report)report=_wfopen(L"pressfit-direct-native.log",L"wt, ccs=UTF-8");
    if(!report){ProEngineerEnd();return 0;}
    ProError error=0;
    ProWVerstamp before=NULL,after=NULL;
    double mm=0;
    AxComponent panel={0};
    AxScan *scan=NULL;
#define ASSERT(label,expr) do{error=(expr);fwprintf(report,L"%hs=%d\n",label,error);fflush(report);if(error)goto done;}while(0)
    if(!assembly){
        wchar_t mode[16];
        int wanted=8;
        if(GetEnvironmentVariableW(L"BWTL_TEST_DIRECT_SPEC",mode,_countof(mode)))wanted=_wtoi(mode);
        for(int i=0;i<_countof(examples);i++)if(examples[i].id==wanted){example=examples+i;break;}
        ASSERT("KNOWN_FIXTURE_MODE",!example?PRO_TK_BAD_INPUTS:0);
        ASSERT("LOAD_SEPARATE_SIX_HOLE_ASSEMBLY",ProMdlnameRetrieve((wchar_t*)assembly_name,PRO_MDL_ASSEMBLY,&assembly));
        ASSERT("DISPLAY_PRIVATE_ASSEMBLY",ProMdlDisplay(assembly));
        int window;
        ASSERT("GET_PRIVATE_WINDOW",ProMdlWindowGet(assembly,&window));
        ASSERT("CURRENT_PRIVATE_WINDOW",ProWindowCurrentSet(window));
        ASSERT("ACTIVATE_PRIVATE_WINDOW",ProWindowActivate(window));
        ASSERT("WAIT_FOR_ACTIVE_WINDOW",ProMacroLoad((wchar_t*)command));
        return 0;
    }

    ProMdl active=NULL;
    ASSERT("CHECK_ACTIVE_ASSEMBLY",ProMdlCurrentGet(&active)||active!=assembly?PRO_TK_BAD_CONTEXT:0);
    ASSERT("OPEN_TEST_NATIVE_CHAT",!native_ui_open_threaded(NULL)?PRO_TK_BAD_CONTEXT:0);
    ASSERT("DISABLE_EXTERNAL_PLANNER",!WritePrivateProfileStringW(L"Runtime",L"CodexPath",
        L"E:\\BWTL-Data\\BWTL-200-Roadmap\\pressfit-direct-validation\\never-start-external-test.exe",runtime_settings)?
        PRO_TK_BAD_CONTEXT:0);
    ASSERT("ASSEMBLY_MM",qa_mm_per_unit(assembly,&mm));
    ASSERT("ACTUAL_HISTORICAL_PANEL",ax_scan(assembly,1,&scan));
    ASSERT("ONE_ORIGINAL_COMPONENT",scan->count!=1?PRO_TK_BAD_CONTEXT:0);
    wchar_t path[48];
    ax_path_label(scan->occurrences[0].ids,scan->occurrences[0].depth,path,_countof(path));
    ASSERT("READ_PANEL_OCCURRENCE",ax_hw_occurrence(assembly,path,mm,&panel));
    free(scan);scan=NULL;
    ASSERT("CLEAR_TEST_SELECTION",ProSelbufferClear());
    BsoSite sites[12]={0};
    wchar_t reason[1000]=L"";
    PartText out={reason,_countof(reason),0};
    int count=0;
    ASSERT("READ_ACTUAL_SIX_VALIDATED_HOLES",fastener_bso_collect(&panel,159,6,mm,sites,&count,&out));
    ASSERT("SELECT_CONFIRMED_INSTALLATION_FACE",test_select(&panel,159));
    if(!example->face_only)
        for(int i=0;i<count;i++)ASSERT("SELECT_REAL_HOLE",test_select(&panel,sites[i].hole_id));

    if(example->id==8){
        ASSERT("READ_ORIGINAL_STAMP",ProMdlVerstampGet(assembly,&before));
        ASSERT("SAME_AS_SCREENSHOT_BARE_BSO_LOCAL",native_dialog_request(L"我说的是装bso"));
        ASSERT("TYPE_AND_LENGTH_QUESTION",!wcsstr(history,L"请告诉我螺纹和长度")?PRO_TK_INCOMPLETE:0);
        ASSERT("NO_BARE_GENERIC_COMPONENT",active_count(1));
        ASSERT("UNKNOWN_EXACT_SIZE_STAYS_LOCAL",native_dialog_request(L"装配bsoM3X999"));
        ASSERT("UNKNOWN_SIZE_NO_COMPONENT",active_count(1));
        ASSERT("EXACT_FAMILY_NAME_STAYS_LOCAL",native_dialog_request(L"装配BSO_3_5M3XX"));
        ASSERT("EXACT_FAMILY_FORBIDS_GENERIC_COMPONENT",active_count(1));
        ASSERT("WRONG_M2_MATCH_STAYS_LOCAL",native_dialog_request(L"装配S-M2-0"));
        ASSERT("MISMATCH_DIA_EXPLAINED",!wcsstr(history,L"Ø5.4 mm")||!wcsstr(history,L"Ø4.3 mm")||
            !wcsstr(history,L"请核对库件和孔规格")?PRO_TK_INCOMPLETE:0);
        ASSERT("M2_DID_NOT_ADD_COMPONENT",active_count(1));
        ASSERT("GET_TARGET_STAMP_AFTER_NEGATIVES",ProMdlVerstampGet(assembly,&after));
        ASSERT("NO_MODEL_WRITE_ON_NEGATIVE_CASES",!ProVerstampEqual(before,after)?PRO_TK_BAD_CONTEXT:0);
    }
    if(example->id==77){
        ASSERT("SELECT_SINGLE_EXISTING_BORED_HOLE",test_select(&panel,sites[0].hole_id));
        ASSERT("FIRST_X6_NATIVE_CHAT",native_dialog_request(example->request));
        ASSERT("EXACTLY_ONE_X6",active_count(2));
        ASSERT("NATIVE_X6_RECEIPT",!wcsstr(history,L"实测长度 6 mm")||!wcsstr(history,L"实际核对 1/1 孔")?PRO_TK_INCOMPLETE:0);
        ASSERT("CONTEXTUAL_REPLACEMENT",native_dialog_request(L"换成8长的"));
        ASSERT("NATIVE_X8_REPLACEMENT_RECEIPT",!wcsstr(history,L"实测长度 8 mm")||!wcsstr(history,L"已安全替换 1")?PRO_TK_INCOMPLETE:0);
        ASSERT("NO_DUPLICATE_AFTER_CONTEXTUAL_REPLACEMENT",active_count(2));
    }else if(example->id==74){
        ProAsmcomp shifted={0};
        ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{250,0,0,1}};
        ASSERT("MAKE_SECOND_INSTANCE_OF_THE_SAME_PANEL",ax_place_model(assembly,panel.model,pose,AX_PLACE_FIXED,mm,&shifted,&out));
        wchar_t instruction[180];
        swprintf_s(instruction,_countof(instruction),L"组件 %ls 面 SURFACE:159 装配6个BSO M3×8；组件 ID:%d 面 SURFACE:159 装配6个BSO M3×5",path,shifted.id);
        ASSERT("SINGLE_REQUEST_MIXED_BOARD_INSTALL",native_dialog_request(instruction));
        ASSERT("REPORT_BOTH_GROUPS",!wcsstr(history,L"第 1 组")||!wcsstr(history,L"第 2 组")||!wcsstr(history,L"实际核对 6/6 孔")?PRO_TK_INCOMPLETE:0);
        ASSERT("ACTUAL_TWELVE_FASTENERS",active_count(14));
        ASSERT("READ_SEPARATE_TARGETS",ax_scan(assembly,1,&scan));
        int x5=0,x8=0;
        ProName five,eight;
        ASSERT("NORMALIZE_MIXED_NATIVE_NAMES",
            !pressfit_name_key(L"BSO-3_5M3X5_BWTL",five)||!pressfit_name_key(L"BSO_3_5M3X8_BWTL",eight)?PRO_TK_INCOMPLETE:0);
        for(int i=0;i<scan->count;i++){
            ProName key;
            if(!pressfit_name_key(scan->occurrences[i].name,key))continue;
            if(!wcsncmp(key,eight,wcslen(eight)))x8++;
            if(!wcsncmp(key,five,wcslen(five)))x5++;
        }
        ASSERT("SIX_X8_SIX_X5",x8!=6||x5!=6?PRO_TK_INCOMPLETE:0);
    }else if(example->id==73){
        for(int step=0;step<3;step++){
            ASSERT("RESET_TARGETS_FOR_NEXT_MESSAGE",ProSelbufferClear());
            ASSERT("SELECT_SEATING_SIDE",test_select(&panel,159));
            ASSERT("SELECT_FIRST_NEW_HOLE",test_select(&panel,sites[step*2].hole_id));
            ASSERT("SELECT_SECOND_NEW_HOLE",test_select(&panel,sites[step*2+1].hole_id));
            ASSERT("CHAT_INSTALL_WITHIN_SAME_ASSEMBLY",native_dialog_request(step?L"这两个也装一样的":example->request));
            ASSERT("TWO_ACTUAL_PLACEMENTS_REPORTED",!wcsstr(history,L"实际核对 2/2 孔")?PRO_TK_INCOMPLETE:0);
            ASSERT("OTHER_HOLES_UNCHANGED",active_count(1+2*(step+1)));
        }
    }else{
        if(example->id==75){
            ASSERT("PICK_BORE_FOR_HOLE_TYPE",test_select(&panel,sites[0].hole_id));
            ASSERT("READ_OPENING_KIND_VIA_NATIVE_CHAT",native_dialog_request(L"识别压铆孔型"));
            ASSERT("REPORT_MEASURED_CIRCULAR_THROUGH_HOLE",!wcsstr(history,L"圆形通孔")||
                !wcsstr(history,L"Ø5.4 mm")?PRO_TK_INCOMPLETE:0);
            ASSERT("RESET_FACE",ProSelbufferClear());
            ASSERT("PICK_FACE_ONLY",test_select(&panel,159));
            ASSERT("PREFLIGHT_READ_ONLY",native_dialog_request(L"这一面全部装配BSO M3×8 只核对不安装 最小边距1mm"));
            ASSERT("PREFLIGHT_ACTUAL_SIX",!wcsstr(history,L"已核对 6 个孔及占位状态")?PRO_TK_INCOMPLETE:0);
            ASSERT("PREFLIGHT_NO_PLACEMENTS",active_count(1));
        }
        ASSERT("ACTUAL_NATIVE_DIALOG_DISPATCH_NO_REMOTE",native_dialog_request(example->request));
        ASSERT("SIX_ACTUAL_PLACEMENTS_REPORTED",!wcsstr(history,L"实际核对 6/6 孔")?PRO_TK_INCOMPLETE:0);
        if(example->id==75){
            ASSERT("POSTFIT_AUDIT_SIX",!wcsstr(history,L"实体间隙已核对 6/6 孔")?PRO_TK_INCOMPLETE:0);
            ASSERT("RELOAD_DURABLE_AUDIT",native_dialog_request(L"核对压铆干涉"));
            ASSERT("DURABLE_AUDIT_SIX",!wcsstr(history,L"实体间隙已核对 6/6 孔")?PRO_TK_INCOMPLETE:0);
        }
    }
    if(example->id!=74&&example->id!=77)ASSERT("ALL_SIX_INDIVIDUAL_CONSTRAINTS_AND_ZERO_OFFSET",check_six(assembly,&panel,mm));
    ASSERT("SAVE_PRIVATE_ASSEMBLY",ProMdlSave(assembly));
    ProPath current,saved;
    ASSERT("TEST_WORKDIR",ProDirectoryCurrentGet(current));
    company_trim_path(current);
    ASSERT("FIND_NATIVE_FILE",ax_native_source(current,assembly_name,PRO_MDL_ASSEMBLY,saved));
    fwprintf(report,L"SAVED_ASSY=%ls\n",saved);
done:
    if(scan)free(scan);
    if(before)ProVerstampFree(&before);
    if(after)ProVerstampFree(&after);
    fwprintf(report,L"PRESSFIT_DIRECT_PASS=%d\n",!error);
    fclose(report);
    ProEngineerEnd();
    return 0;
#undef ASSERT
}

#ifndef BWTL_WORKBENCH_EMBEDDED
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_short_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLDirectPressfit",run_direct_fixture,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t*)command);
}
#endif
