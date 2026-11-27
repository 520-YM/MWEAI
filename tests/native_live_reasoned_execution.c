#define user_initialize bwtl_recovery_live_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static const UINT_PTR live_timer=6924;
static const wchar_t live_prompt[]=L"在当前钣金平板上居中打四个直径4毫米的通孔，先核对板是不是80×60毫米。"
    L"孔距优先110×90毫米。要是放不下，请你按实测自行决定横纵孔距。保留原有孔，不保存。";
static FILE *live_report;
static ProMdl live_sheet;
static ProMdlName live_name;
static ProOutline live_before_outline;
static ProMassProperty live_before_mass;
static DeliveryBore live_original_hole;
static int live_original_id;
static ULONGLONG live_deadline;
static int live_phase,live_queued,live_preparing,live_failed;
static WIN32_FIND_DATAW live_baseline[8];
static int live_baseline_files;

static int live_verify(const wchar_t *label,int ok){
    fwprintf(live_report,L"%ls=%d\n",label,!!ok);
    fflush(live_report);
    if(!ok)live_failed=1;
    return !!ok;
}

static int live_finish(void){
    KillTimer(native_backend_window,live_timer);
    if(live_report){
        fwprintf(live_report,L"BWTL_LIVE_REASONED_EXECUTION_PASS=%d\n",!live_failed);
        fclose(live_report);
        live_report=NULL;
    }
    smart_cancel();
    ProEngineerEnd();
    return 0;
}

static int live_stock_step(const wchar_t *format){
    wchar_t protocol[400];
    static wchar_t report[PART_TEXT_MAX];
    PartCommand command;
    if(swprintf_s(protocol,_countof(protocol),format,live_name)<0||
       part_command_parse(protocol,&command)!=1)return 0;
    const ProError error=foundation_execute(&command,live_sheet,report,_countof(report));
    fwprintf(live_report,L"STOCK %ls -> %d\n%ls\n",protocol,error,report);
    fflush(live_report);
    return error==PRO_TK_NO_ERROR;
}

static int live_outline(ProOutline outline,int *axis_x,int *axis_y){
    ProSolidBody body;
    double scale;
    if(qa_single_body(live_sheet,&body)||qa_mm_per_unit(live_sheet,&scale)||
       !part_close(scale,1)||ProSolidBodyOutlineGet(&body,outline))return 0;
    int thickness_axis=0;
    *axis_x=*axis_y=-1;
    for(int i=0;i<3;i++){
        const double dimension=outline[1][i]-outline[0][i];
        if(part_close(dimension,80))*axis_x=i;
        if(part_close(dimension,60))*axis_y=i;
        if(part_close(dimension,2))thickness_axis++;
    }
    return *axis_x>=0&&*axis_y>=0&&thickness_axis==1;
}

static int live_unchanged(const ProOutline outline){
    for(int end=0;end<2;end++)
        for(int axis=0;axis<3;axis++)
            if(!part_close(outline[end][axis],live_before_outline[end][axis]))return 0;
    return 1;
}

static int live_stock(void){
    ProMdl active=NULL;
    ProFeature original;
    int x,y;
    static DeliveryBores readback;
    if(ProMdlCurrentGet(&active)||active!=live_sheet||
       !live_stock_step(L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60")||
       !live_stock_step(L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 2 POS")||
       !live_stock_step(L"CREO_OP SMT_SKETCH %ls CUT_PROFILE PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 3")||
       !live_stock_step(L"CREO_OP SMT_CUT %ls ACTUAL_CUT SKETCH:CUT_PROFILE NEG")||
       ux_named_feature(live_sheet,L"ACTUAL_CUT",&original)||
       !live_outline(live_before_outline,&x,&y)||qa_geometry_mass(live_sheet,&live_before_mass)||
       dv_bores_scan(live_sheet,0,1,&readback)||readback.count!=1||
       readback.bores[0].classification!=2||!part_close(readback.bores[0].radius,3))return 0;
    live_original_hole=readback.bores[0];
    live_original_id=original.id;
    return 1;
}

static int live_verify_holes(void){
    ProOutline outline;
    ProMassProperty mass;
    ProFeatStatus state;
    ProFeature original;
    static DeliveryBores scan;
    int x,y;
    if(!live_outline(outline,&x,&y)||!live_unchanged(outline)||
       qa_geometry_mass(live_sheet,&mass)||fabs(live_before_mass.volume-mass.volume-32*AS_PI)>0.001||
       part_health(live_sheet)||ux_named_feature(live_sheet,L"ACTUAL_CUT",&original)||
       original.id!=live_original_id||ProFeatureStatusGet(&original,&state)||state!=PRO_FEAT_ACTIVE||
       dv_bores_scan(live_sheet,0,1,&scan)||scan.count!=5)return 0;
    double sites[4][2];
    int count=0,original_count=0;
    double lo[]={HUGE_VAL,HUGE_VAL},hi[]={-HUGE_VAL,-HUGE_VAL};
    for(int i=0;i<scan.count;i++){
        const DeliveryBore *bore=&scan.bores[i];
        const double bx=bore->centerline[x],by=bore->centerline[y];
        if(bore->classification!=2||!part_close(bore->high-bore->low,2))return 0;
        if(part_close(bore->radius,3)&&part_close(bx,live_original_hole.centerline[x])&&
           part_close(by,live_original_hole.centerline[y])){
            original_count++;
            continue;
        }
        if(count>=_countof(sites)||!part_close(bore->radius,2)||
           bx-2<=outline[0][x]||bx+2>=outline[1][x]||
           by-2<=outline[0][y]||by+2>=outline[1][y]||
           hypot(bx-live_original_hole.centerline[x],by-live_original_hole.centerline[y])<=5)return 0;
        sites[count][0]=bx;
        sites[count][1]=by;
        count++;
        lo[0]=fmin(lo[0],bx);hi[0]=fmax(hi[0],bx);
        lo[1]=fmin(lo[1],by);hi[1]=fmax(hi[1],by);
    }
    if(original_count!=1||count!=4||hi[0]-lo[0]<=4||hi[1]-lo[1]<=4||
       !part_close(lo[0]+hi[0],outline[0][x]+outline[1][x])||
       !part_close(lo[1]+hi[1],outline[0][y]+outline[1][y]))return 0;
    unsigned corners=0;
    for(int i=0;i<count;i++){
        const int right=part_close(sites[i][0],hi[0]),top=part_close(sites[i][1],hi[1]);
        if((!right&&!part_close(sites[i][0],lo[0]))||(!top&&!part_close(sites[i][1],lo[1])))return 0;
        corners|=1u<<(right+2*top);
    }
    fwprintf(live_report,L"READBACK_CENTERED_PITCH_MM=%.5f × %.5f\nACTUAL_VOLUME_REMOVED_MM3=%.8f\n",
             hi[0]-lo[0],hi[1]-lo[1],live_before_mass.volume-mass.volume);
    return corners==15;
}

static int live_disk_state(int baseline){
    ProPath directory,path;
    if(ProDirectoryCurrentGet(directory)||towupper(directory[0])!=L'E'||
       swprintf_s(path,_countof(path),L"%ls\\*.prt*",directory)<0)return 0;
    WIN32_FIND_DATAW entry;
    HANDLE find=FindFirstFileW(path,&entry);
    if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND&&!live_baseline_files;
    int count=0,valid=1;
    do{
        if(baseline){
            if(count>=_countof(live_baseline)){valid=0;break;}
            live_baseline[count]=entry;
        }else{
            int known=0;
            for(int i=0;i<live_baseline_files;i++){
                const WIN32_FIND_DATAW *before=&live_baseline[i];
                if(!_wcsicmp(before->cFileName,entry.cFileName)&&
                   !CompareFileTime(&before->ftLastWriteTime,&entry.ftLastWriteTime)&&
                   before->nFileSizeLow==entry.nFileSizeLow&&before->nFileSizeHigh==entry.nFileSizeHigh){
                    known=1;
                    break;
                }
            }
            if(!known){valid=0;break;}
        }
        count++;
    }while(FindNextFileW(find,&entry));
    valid=valid&&GetLastError()==ERROR_NO_MORE_FILES;
    FindClose(find);
    if(baseline)live_baseline_files=count;
    return valid&&count==live_baseline_files;
}

static void CALLBACK live_schedule(HWND window,UINT message,UINT_PTR timer,DWORD now){
    if(!live_preparing&&!live_queued&&((!smart_active&&!ai_process)||GetTickCount64()>live_deadline))
        live_queued=!ProMacroLoad(L"~ Command \x60" L"BWTLLiveReasonedTick\x60;");
}

static int live_tick(uiCmdCmdId id,uiCmdValue *value,void *data){
    live_queued=0;
    if(live_preparing)return 0;
    if(!live_verify(L"TIMELY",GetTickCount64()<=live_deadline))return live_finish();
    if(smart_active||ai_process)return 0;
    if(live_phase==0){
        live_preparing=1;
        if(!live_verify(L"SYNTHETIC_STOCK_80_60_2_ONE_ORIGINAL_6MM_THROUGH_HOLE",live_stock())||
           !live_verify(L"INITIAL_MODEL_DISK_STATE",live_disk_state(1)))
            return live_finish();
        live_phase=1;
        fwprintf(live_report,L"REQUEST %ls\n",live_prompt);
        fflush(live_report);
        native_prompt_set(live_prompt);
        run_local(NULL,NULL,NULL);
        live_preparing=0;
        return 0;
    }

    wchar_t connection[128],thread[128];
    ProMdl active=NULL;
    const wchar_t *reply=wcsstr(history,L"【AI】");
    fwprintf(live_report,L"CAD_STEPS=%d CAD_CHANGES=%d FAILURES=%d\n",smart_steps,smart_progress,smart_total_failures);
    fwprintf(live_report,L"%ls\n%ls\n",smart_details,history);
    live_verify(L"LIVE_RESIDENT_WORKER",resident_process&&GetProcessId(resident_process)!=0&&resident_sequence&&
        resident_read(L"connection.txt",connection,_countof(connection))==1&&!wcsncmp(connection,L"READY",5)&&
        resident_read(L"thread.txt",thread,_countof(thread))==1&&thread[0]&&resident_used_tool);
    live_verify(L"SAME_CURRENT_SHEET",!ProMdlCurrentGet(&active)&&active==live_sheet);
    live_verify(L"FOUR_NEW_CENTERED_THROUGH_BORES_AND_ORIGINAL",live_verify_holes());
    live_verify(L"SINGLE_ASSISTANT_REPLY",reply&&!wcsstr(reply+4,L"【AI】"));
    live_verify(L"NO_MODEL_CHANGES_SAVED",live_disk_state(0));
    return live_finish();
}

static int live_begin(uiCmdCmdId id,uiCmdValue *value,void *data){
    live_report=_wfopen(L"bwtl-live-reasoned-execution.log",L"wt, ccs=UTF-8");
    if(!live_report)return live_finish();
    const char *phase=NULL;
    live_deadline=GetTickCount64()+170000;
    if(!live_verify(L"PRIVATE_SHEET_AND_NORMAL_CHAT_READY",
        towupper(runtime_data[0])==L'E'&&!model_create_empty(2,live_name,&phase)&&
        !ProMdlnameInit(live_name,PRO_MDL_PART,&live_sheet)&&!dv_display_new(live_sheet)&&
        native_ui_open_threaded(NULL)&&SetTimer(native_backend_window,live_timer,250,live_schedule)))
        return live_finish();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_recovery_live_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLLiveReasonedBegin",live_begin,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLLiveReasonedTick",live_tick,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLLiveReasonedBegin\x60;");
}
