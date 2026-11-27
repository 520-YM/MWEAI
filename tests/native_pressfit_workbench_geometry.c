#define BWTL_WORKBENCH_EMBEDDED
#include "native_pressfit_workbench_baseline.c"

static ProMdl geometry_panel;
static PressfitParentSelection parent_selection;
static BsoSite actual[12];
static wchar_t panel_key[AX_PATH_DEPTH*12+4];
static int geometry_phase;
static const wchar_t geometry_macro[]=L"~ Command \x60" L"BWTLWorkbenchGeometry\x60;";
static wchar_t detail[11000];
static PartText diagnostics={detail,_countof(detail),0};
static int geometry_wait_queued;
static ULONGLONG geometry_wait_stop;
static void CALLBACK geometry_wait_for_parent(HWND window,UINT message,UINT_PTR timer,DWORD time){
    (void)message;(void)time;
    if(geometry_wait_queued|| (pressfit_parent_pending.waiting&&GetTickCount64()<geometry_wait_stop))return;
    geometry_wait_queued=!ProMacroLoad((wchar_t*)geometry_macro);
    if(geometry_wait_queued)KillTimer(window,timer);
}


static ProError geometry_select(ProMdl model,const ProAsmcomppath *path,ProType type,int id){
    ProModelitem item;
    ProSelection selection=NULL;
    ProError error=ProModelitemInit(model,id,type,&item);
    if(!error)error=ProSelectionAlloc((ProAsmcomppath *)path,&item,&selection);
    if(!error)error=ProSelbufferSelectionAdd(selection);
    if(selection)ProSelectionFree(&selection);
    return error;
}

static int run_geometry_fixture(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    int error=0,window;
    if(!report)report=_wfopen(L"pressfit-geometry-native.log",L"wt, ccs=UTF-8");
    if(!report){ProEngineerEnd();return 0;}
    AxComponent panel={0};
    BsoFitting fitting={0};
    AxScan *scan=NULL;
    ProMdl source=NULL,active=NULL;
    wchar_t library[MAX_PATH];
    int count=0;
    BsoSite chosen[12]={0};
    FastenerPressfitRequest request={.hole=5.4};
#define CHECK(label,expr) do{error=(expr);fwprintf(report,L"%hs=%d\n",label,error);fflush(report);if(error)goto done;}while(0)
    if(geometry_phase==0){
        CHECK("OPEN_REAL_PANEL_ASSEMBLY",ProMdlnameRetrieve((wchar_t*)assembly_name,PRO_MDL_ASSEMBLY,&assembly));
        CHECK("SHOW_ASSEMBLY",ProMdlDisplay(assembly));
        CHECK("GET_ASSEMBLY_WINDOW",ProMdlWindowGet(assembly,&window));
        CHECK("SET_ASSEMBLY_WINDOW",ProWindowCurrentSet(window));
        CHECK("ACTIVATE_ASSEMBLY_WINDOW",ProWindowActivate(window));
        geometry_phase++;
        CHECK("WAIT_ASSEMBLY",ProMacroLoad((wchar_t*)geometry_macro));
        return 0;
    }
    if(geometry_phase==1){
        CHECK("LIVE_ASSEMBLY",ProMdlCurrentGet(&active)||active!=assembly?PRO_TK_BAD_CONTEXT:0);
        CHECK("START_PROFILE",native_ui_open_threaded(NULL)?0:PRO_TK_BAD_CONTEXT);
        CHECK("ENUM_PANEL",ax_scan(assembly,1,&scan));
        CHECK("UNIQUE_PANEL",scan->count!=1||!ax_path_label(scan->occurrences[0].ids,scan->occurrences[0].depth,
            panel_key,_countof(panel_key))?PRO_TK_BAD_CONTEXT:0);
        CHECK("EXACT_PANEL",ax_hw_occurrence(assembly,panel_key,1,&panel));
        geometry_panel=panel.model;
        free(scan);scan=NULL;
        CHECK("READ_X8",pressfit_read_exact(L"BSO_3_5M3X8",&source,library,&diagnostics));
        CHECK("DETERMINE_FITTING",fastener_pressfit_fitting(source,1,139,90,&fitting,&diagnostics));
        CHECK("COLLECT_SIX_PHYSICAL_HOLES",fastener_bso_collect(&panel,159,6,1,actual,&count,&diagnostics));
        CHECK("CLEAR_INITIAL_SEL",ProSelbufferClear());
        CHECK("SELECT_FACE",test_select(&panel,159));
        request.all_on_face=1;
        CHECK("FACE_ALL_SIX",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics));
        CHECK("FACE_EXACT_SIX",count!=6?PRO_TK_BAD_CONTEXT:0);
        CHECK("THROUGH_AND_EDGE",fastener_pressfit_site_preflight(&chosen[0],1,0,0,NULL,&diagnostics));
        const double tool[]={10,15,10,15};
        CHECK("ACTUAL_TOOL_ENVELOPE",fastener_pressfit_site_preflight(&chosen[0],1,0,0,tool,&diagnostics));
        request.all_on_face=0;request.flip_side=1;request.expected=6;
        CHECK("OTHER_SIDE",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics));
        CHECK("OTHER_SIDE_NORMAL",count!=6||ad_dot(chosen[0].geometry.plane.direction,actual[0].geometry.plane.direction)>-0.99999?
            PRO_TK_BAD_CONTEXT:0);
        request.flip_side=0;request.expected=1;
        CHECK("CLEAR_FACE_SEL",ProSelbufferClear());
        CHECK("SELECT_ONLY_WALL",test_select(&panel,actual[0].hole_id));
        CHECK("HOLE_SIDE_AMBIGUITY",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics)
            ==PRO_TK_BAD_CONTEXT?0:PRO_TK_BAD_CONTEXT);
        ShopMouth mouth={0};
        CHECK("TRUE_HOLE_MOUTH",fastener_pressfit_classify(&panel,actual[0].face_id,actual[0].hole_id,1,&mouth)
            ==PF_OPENING_THROUGH?0:PRO_TK_BAD_CONTEXT);
        CHECK("CLEAR_WALL_SEL",ProSelbufferClear());
        CHECK("SELECT_ONLY_CIRCULAR_EDGE",geometry_select(panel.model,&panel.path,PRO_EDGE,mouth.ring.ids[0]));
        CHECK("EDGE_PROVES_CONTACT_SIDE",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics));
        CHECK("EDGE_ONE_EXACT_HOLE",count!=1||chosen[0].face_id!=159?PRO_TK_BAD_CONTEXT:0);
        CHECK("CLEAR_EDGE_SEL",ProSelbufferClear());
        CHECK("SELECT_FACE_FOR_VIEW",test_select(&panel,159));
        request.view_side=-1;request.expected=2;
        CHECK("LEFT_TWO_IN_CURRENT_VIEW",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics));
        CHECK("TWO_HOLES",count!=2?PRO_TK_BAD_CONTEXT:0);
        fastener_pressfit_highlight(assembly,chosen,count,&diagnostics);
        CHECK("HIGHLIGHT_TWO_EXACT_BORE_OCCURRENCES",!fastener_pressfit_highlights[0]||!fastener_pressfit_highlights[1]||
            fastener_pressfit_highlights[2]?PRO_TK_BAD_CONTEXT:0);
        fastener_pressfit_highlight_clear();
        CHECK("HIGHLIGHT_RELEASED",fastener_pressfit_highlights[0]||fastener_pressfit_highlights[1]?PRO_TK_BAD_CONTEXT:0);
        request.view_side=0;request.nearest_bend=1;request.expected=0;
        for(int i=0;i<6;i++){
            int edge=0;
            double distance=0;
            CHECK("OBSERVED_ACTUAL_FOLD_DISTANCE",fastener_pressfit_bend_distance(&actual[i],1,&distance,&edge));
            fwprintf(report,L"RANK_HOLE=%d EDGE=%d DISTANCE=%g\n",actual[i].hole_id,edge,distance);
        }
        CHECK("SYMMETRICAL_FOLD_ROWS_ARE_AMBIGUOUS",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics)
            ==PRO_TK_BAD_CONTEXT?0:PRO_TK_BAD_CONTEXT);
        CHECK("CLEAR_SEL_BEFORE_FOLD",ProSelbufferClear());
        CHECK("SELECT_FOLD_FACE",test_select(&panel,159));
        CHECK("SELECT_EXACT_FOLD_ROW_HOLE",test_select(&panel,actual[0].hole_id));
        CHECK("FOLD_RANK_WITH_EXACT_ROW_CHOICES",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics));
        int installed;
        const wchar_t *stage;
        ProMdl manual_sample=NULL;
        wchar_t project_file[MAX_PATH];
        BsoFitting sample_fitting={0};
        CHECK("SEPARATE_FIXTURE_COPY_OF_COMPANY_LIBRARY",pressfit_library_project(source,library,&manual_sample,project_file,&diagnostics));
        CHECK("ACTUAL_FITTING_COPY_SURFACES",fastener_pressfit_fitting(manual_sample,1,139,90,&sample_fitting,&diagnostics));
        CHECK("INSTALL_NATIVE_EXAMPLE",fastener_bso_install_site(assembly,manual_sample,&actual[0],&sample_fitting,1,&installed,&stage,&diagnostics));
        wchar_t id_text[24];
        swprintf_s(id_text,_countof(id_text),L"ID:%d",installed);
        PressfitInstalledExample sample={0};
        CHECK("READ_INSTALLED_EXAMPLE",fastener_pressfit_installed_example(assembly,id_text,1,&sample,&diagnostics));
        CHECK("SAME_PLACEMENT_AND_SKU",sample.installed.model!=manual_sample||sample.site.hole_id!=actual[0].hole_id||
             wcscmp(sample.panel_path,panel_key)?PRO_TK_BAD_CONTEXT:0);
        ProMdlName panel_name;
        CHECK("GET_BOARD_NAME",ProMdlMdlnameGet(geometry_panel,panel_name));
        CHECK("NEW_INDEPENDENT_PART_WINDOW",ProObjectwindowMdlnameCreate(panel_name,PRO_PART,&window));
        CHECK("SET_BOARD_WINDOW",ProWindowCurrentSet(window));
        CHECK("SHOW_BOARD",ProMdlDisplay(geometry_panel));
        CHECK("ACTIVATE_BOARD_WINDOW",ProWindowActivate(window));
        geometry_phase++;
        CHECK("WAIT_BOARD",ProMacroLoad((wchar_t*)geometry_macro));
        return 0;
    }
    if(geometry_phase==2){
        CHECK("LIVE_PART_ON_OWN_WINDOW",ProMdlCurrentGet(&active)||active!=geometry_panel?PRO_TK_BAD_CONTEXT:0);
        CHECK("CLEAR_PART_SEL",ProSelbufferClear());
        CHECK("PART_HOLE_SEL",geometry_select(geometry_panel,NULL,PRO_SURFACE,actual[1].hole_id));
        CHECK("CAPTURE_EXACT_PARENT",fastener_pressfit_capture_parent_selection(NULL,&parent_selection,&diagnostics));
        CHECK("PARENT_ID_AND_HOLE",parent_selection.link.root!=assembly||
            wcscmp(parent_selection.link.occurrence,panel_key)||parent_selection.count!=1?PRO_TK_BAD_CONTEXT:0);
        CHECK("SET_VERIFIED_PARENT_WINDOW",ProWindowCurrentSet(parent_selection.link.root_window));
        CHECK("ACTIVATE_VERIFIED_PARENT_WINDOW",ProWindowActivate(parent_selection.link.root_window));
        geometry_phase++;
        CHECK("WAIT_PARENT",ProMacroLoad((wchar_t*)geometry_macro));
        return 0;
    }
    if(geometry_phase==3){
        CHECK("USE_ORIGINAL_HOLE_ON_VERIFIED_PARENT",fastener_pressfit_apply_parent_selection(&parent_selection,&request,&diagnostics));
        CHECK("PARENT_FIELD_NOT_GUI_RESELECT",request.mapped_count!=1||request.mapped_holes[0]!=actual[1].hole_id||
            wcscmp(request.panel_path,panel_key)?PRO_TK_BAD_CONTEXT:0);
        fastener_pressfit_parent_clear(&parent_selection);
        CHECK("GET_PART_FOR_ASYNC_CHAT",ProMdlWindowGet(geometry_panel,&window));
        CHECK("SET_PART_FOR_ASYNC_CHAT",ProWindowCurrentSet(window));
        CHECK("ACTIVATE_PART_FOR_ASYNC_CHAT",ProWindowActivate(window));
        geometry_phase++;
        CHECK("WAIT_PART_FOR_ASYNC_CHAT",ProMacroLoad((wchar_t*)geometry_macro));
        return 0;
    }
    if(geometry_phase==4){
        CHECK("ASYNC_CHAT_STARTS_ON_BOARD",ProMdlCurrentGet(&active)||active!=geometry_panel?PRO_TK_BAD_CONTEXT:0);
        CHECK("CLEAR_SEL_BEFORE_ASYNC_CHAT",ProSelbufferClear());
        CHECK("PICK_FACE_FOR_ASYNC_CHAT",geometry_select(geometry_panel,NULL,PRO_SURFACE,actual[1].face_id));
        CHECK("PICK_HOLE_FOR_ASYNC_CHAT",geometry_select(geometry_panel,NULL,PRO_SURFACE,actual[1].hole_id));
        CHECK("SUBMIT_REAL_CHAT_FROM_ACTIVE_PART",native_dialog_request(L"装配bsoM3X6"));
        CHECK("ASYNC_ACTIVATION_IS_PENDING",!pressfit_parent_pending.waiting?PRO_TK_BAD_CONTEXT:0);
        geometry_phase++;
        geometry_wait_stop=GetTickCount64()+15000;
        CHECK("WAIT_FOR_PARENT_CHAT_CONFIRMATION",!SetTimer(native_backend_window,4330,250,geometry_wait_for_parent)?
            PRO_TK_BAD_CONTEXT:0);
        return 0;
    }
    if(geometry_phase==5){
        fwprintf(report,L"FINAL CHAT\n%ls\n",history);
        CHECK("ASYNC_ACTION_FINISHED",pressfit_parent_pending.waiting?PRO_TK_BAD_CONTEXT:0);
        CHECK("PARENT_CONFIRMED_IN_CHAT",wcsstr(history,L"已把选中的孔和安装面映射到原装配实例")&&
            wcsstr(history,L"新增 1")?0:PRO_TK_BAD_CONTEXT);
        CHECK("ASYNC_PARENT_AND_PHYSICAL_COMPONENT",active_count(3));
        CHECK("FIND_ASYNC_M3X6_AS_EXAMPLE",ax_scan(assembly,1,&scan));
        int example_id=0;
        for(int i=0;i<scan->count;i++){
            const AxOccurrence *item=&scan->occurrences[i];
            if(item->depth==1&&item->type==PRO_MDL_PART&&wcsstr(item->name,L"BSO_3_5M3X6_BWTL"))
                example_id=item->ids[0];
        }
        CHECK("UNIQUE_ACTUAL_M3X6",!example_id?PRO_TK_BAD_CONTEXT:0);
        free(scan);scan=NULL;
        CHECK("SAME_PANEL_FOR_SAMPLE",ax_hw_occurrence(assembly,panel_key,1,&panel));
        CHECK("CLEAR_BEFORE_SAMPLE",ProSelbufferClear());
        CHECK("SET_SEAT_FOR_SAMPLE",test_select(&panel,actual[2].face_id));
        CHECK("SET_NEW_HOLE_FOR_SAMPLE",test_select(&panel,actual[2].hole_id));
        wchar_t ask[128];
        swprintf_s(ask,_countof(ask),L"照已装组件 ID:%d 装配 同样的",example_id);
        CHECK("REUSE_INSTALLED_EXAMPLE_CHAT",native_dialog_request(ask));
        CHECK("CONFIRMED_SAME_EXAMPLE_REFERENCE",wcsstr(history,L"已读取样件")&&wcsstr(history,L"新增 1")?
            0:PRO_TK_BAD_CONTEXT);
        CHECK("NATIVE_SAME_EXAMPLE_USED_AT_NEW_HOLE",active_count(4));
        const char *stage="private nested fixture";
        model_background_mode=1;
        ProMdl sub=NULL;
        ProMdlName name;
        CHECK("CREATE_NESTED_ASSEMBLY",model_create_empty(3,name,&stage));
        CHECK("NESTED_ASSEMBLY_MODEL",ProMdlnameInit(name,PRO_MDL_ASSEMBLY,&sub));
        ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        CHECK("BOARD_OCCURRENCE_INSIDE_SUBASM",ax_place_model(sub,geometry_panel,pose,AX_PLACE_FIXED,1,NULL,&diagnostics));
        pose[3][0]=600;
        CHECK("ASSEMBLE_NESTED_REPEATED_BOARD",ax_place_model(assembly,sub,pose,AX_PLACE_FIXED,1,NULL,&diagnostics));
        model_background_mode=0;
        CHECK("ENUM_NESTED_COMPONENT",ax_scan(assembly,1,&scan));
        wchar_t nested[AX_PATH_DEPTH*12+4]=L"";
        int matches=0;
        for(int i=0;i<scan->count;i++){
            const AxOccurrence *item=&scan->occurrences[i];
            if(item->model==geometry_panel&&item->depth==2&&ax_path_label(item->ids,item->depth,nested,_countof(nested)))matches++;
        }
        CHECK("DISTINCT_NESTED_PATH",matches==1&&wcscmp(nested,panel_key)?0:PRO_TK_BAD_CONTEXT);
        free(scan);scan=NULL;
        CHECK("READ_X8_AGAIN",ProMdlnameInit(L"BSO_3_5M3X8",PRO_MDL_PART,&source));
        CHECK("READ_SAME_FITTING",fastener_pressfit_fitting(source,1,139,90,&fitting,&diagnostics));
        request=(FastenerPressfitRequest){.face_id=159,.all_on_face=1,.hole=5.4};
        wcscpy_s(request.panel_path,_countof(request.panel_path),nested);
        CHECK("EXACT_EXPLICIT_NESTED_TARGET",fastener_pressfit_short_sites(assembly,1,&request,&fitting,chosen,&count,&diagnostics));
        CHECK("SIX_NESTED_HOLES_SHIFTED",count!=6||chosen[0].panel.path.table_num!=2||
            fabs(chosen[0].geometry.contact[0]-actual[0].geometry.contact[0]-600)>0.001?PRO_TK_BAD_CONTEXT:0);
        CHECK("GET_EXISTING_PART_WINDOW",ProMdlWindowGet(geometry_panel,&window));
        CHECK("SET_PART_FOR_REPEATED_CASE",ProWindowCurrentSet(window));
        CHECK("ACTIVATE_PART_FOR_REPEATED_CASE",ProWindowActivate(window));
        geometry_phase++;
        CHECK("WAIT_PART_FOR_REPEATED_CASE",ProMacroLoad((wchar_t*)geometry_macro));
        return 0;
    }
    CHECK("CLEAR_REPEAT_SEL",ProSelbufferClear());
    CHECK("SELECT_REPEAT_BORE",geometry_select(geometry_panel,NULL,PRO_SURFACE,actual[1].hole_id));
    CHECK("MULTIPLE_OCCURRENCES_REFUSE_TO_GUESS",fastener_pressfit_capture_parent_selection(NULL,&parent_selection,&diagnostics)
        ==PRO_TK_E_FOUND?0:PRO_TK_BAD_CONTEXT);
done:
    fastener_pressfit_parent_clear(&parent_selection);
    free(scan);
    fwprintf(report,L"DETAILS\n%ls\nWORKBENCH_GEOMETRY_PASS=%d\n",detail,!error);
    fclose(report);
    ProEngineerEnd();
    return 0;
#undef CHECK
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_short_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLWorkbenchGeometry",run_geometry_fixture,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad((wchar_t*)geometry_macro);
}