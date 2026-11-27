#define user_initialize continuity_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static const UINT_PTR probe_timer=6912;
static const wchar_t probe_macro[]=L"~ Command \x60" L"BWTLContinuityTick\x60;";
static FILE *report;
static int phase,queued;
static ULONGLONG deadline;
static ProPath project;
static ProMdl top,pair,part;
static int component;
static ProMdlName expected_top,number1,number2;
static ProMdlName draft=L"AI_CONTEXT_PROBEV00";

static int verify(const char *label,int pass){
    fwprintf(report,L"%hs=%d\n",label,!!pass);
    fflush(report);
    return !!pass;
}

static int complete(int pass){
    KillTimer(native_backend_window,probe_timer);
    fwprintf(report,L"CONTEXT_CONTINUITY_PASS=%d\n",pass);
    fclose(report);
    smart_cancel();
    ProEngineerEnd();
    return 0;
}

static int submit(const wchar_t *text){
    history[0]=0;
    native_prompt_set(text);
    run_local(NULL,NULL,NULL);
    fwprintf(report,L"REQUEST %ls\n",text);
    fflush(report);
    return !ai_process;
}

static int single_receipt(const wchar_t *word){
    const wchar_t *reply=wcsstr(history,L"【AI】");
    fwprintf(report,L"%ls\n",history);
    return reply&&wcsstr(reply,word)&&!wcsstr(reply+4,L"【AI】");
}

static int names_match(const wchar_t *wanted,int preserve_pair){
    ProMdl assembly=NULL,sheet=NULL;
    ProMdlName a,b;
    int id=0;
    if(chat_task_resolve_pair(&assembly,&sheet,&id)||ProMdlMdlnameGet(assembly,a)||ProMdlMdlnameGet(sheet,b)||
       _wcsicmp(a,wanted)||_wcsicmp(b,wanted))return 0;
    if(preserve_pair&&(assembly!=pair||sheet!=part||id!=component))return 0;
    pair=assembly;
    part=sheet;
    component=id;
    return 1;
}

static void CALLBACK schedule(HWND hwnd,UINT event,UINT_PTR timer,DWORD time){
    if(!smart_active&&!ai_process&&!queued)queued=!ProMacroLoad((wchar_t *)probe_macro);
}

static int tick(uiCmdCmdId id,uiCmdValue *value,void *data){
    queued=0;
    if(GetTickCount64()>deadline)return complete(verify("TIMELY",0));
    if(smart_active||ai_process)return 0;
#define NEED(label,condition) do{if(!verify(label,(condition)))return complete(0);}while(0)
    if(phase==0){
        NEED("NORMAL_COMPANY_TOTAL_REQUEST",submit(L"创建总装，图号14"));
    }else if(phase==1){
        ProMdl anchor=NULL;
        NEED("ACTUAL_TOTAL_NAME_AND_ANCHOR",!ProMdlnameInit(expected_top,PRO_MDLFILE_ASSEMBLY,&top)&&
            !chat_task_company_assembly(&anchor)&&anchor==top);
        NEED("SINGLE_TOTAL_RECEIPT",single_receipt(expected_top));
        NEED("NORMAL_PLAIN_PAIR_CREATION",submit(L"创建一个零件"));
    }else if(phase==2){
        NEED("AUTOMATIC_FIRST_FREE_001",names_match(number1,0)&&single_receipt(number1));
        NEED("DID_NOT_REQUIRE_RESTATING_DRAWING_NUMBER",submit(L"刚才建的图号是多少？")&&single_receipt(number1));
        NEED("ALREADY_CORRECT_SAME_PAIR",submit(L"把刚才那个改成正确的")&&single_receipt(number1)&&names_match(number1,1));
        NEED("CREATE_SCOPED_TEMPORARY_PAIR",submit(L"创建一个零件，图号AI_CONTEXT_PROBEV00"));
    }else if(phase==3){
        NEED("ACTUAL_UNSAVED_DRAFT_PAIR",names_match(draft,0)&&single_receipt(draft));
        for(int i=0;i<20;i++){
            save_turn(L"用户",L"讨论项目要求。");
            save_turn(L"助手",L"已讨论项目要求。");
        }
        NEED("NATIVE_NUMBER_READ_AFTER_TRANSCRIPT_ROLLOVER",submit(L"刚才建的图号是多少？")&&single_receipt(draft));
        NEED("CORRECTION_WITHOUT_RESTATING_NUMBER",submit(L"那就帮我改成正确的"));
        NEED("RENAMED_BOTH_AND_SKIPPED_OCCUPIED_001",names_match(number2,1)&&single_receipt(number2));
        ProMdlName actual_top;
        NEED("TOTAL_NAME_PRESERVED",!ProMdlMdlnameGet(top,actual_top)&&!_wcsicmp(actual_top,expected_top));
        NEED("READBACK_AFTER_RENAME",submit(L"刚才建的图号是多少？")&&single_receipt(number2));
        const int readonly_classified=!chat_followup_execute(L"刚才那个为什么没有按公司图号改成正确的？");
        const int new_classified=!chat_followup_execute(L"按刚才那个方案新建一个零件。");
        const int write_classified=chat_followup_execute(L"把刚才那块的孔改成8mm。");
        NEED("READ_NEW_AND_PREVIOUS_WRITE_SCOPES",readonly_classified&&new_classified&&write_classified);
        int window=-1;
        NEED("SWITCH_TO_UNRELATED_TOTAL_WINDOW",!native_activate_model_window(top,actual_top,PRO_ASSEMBLY,&window));
    }else{
        ProMdl active=NULL,target=NULL;
        NEED("LIVE_OTHER_WINDOW",!ProMdlCurrentGet(&active)&&active==top);
        NEED("REAL_PREVIOUS_TARGET_REACTIVATED",!chat_followup_activate(L"把刚才那块的孔改成8mm。",&target)&&
            target==part&&!ProMdlCurrentGet(&active)&&active==part&&names_match(number2,1));
        ProPath other;
        NEED("MAKE_OTHER_PROJECT",swprintf_s(other,PRO_PATH_SIZE,L"%ls\\unrelated-project",project)>0&&CreateDirectoryW(other,NULL));
        NEED("CHANGE_SCOPE",!ProDirectoryChange(other));
        NEED("NO_CROSS_PROJECT_TARGET",chat_task_last_followup(&target)==PRO_TK_E_NOT_FOUND&&
            !ProDirectoryChange(project)&&!chat_task_last_followup(&target)&&target==part);
        wchar_t journal[MAX_PATH];
        NEED("E_DRIVE_TASK_RECORD",swprintf_s(journal,_countof(journal),L"%ls\\任务接续.txt",store_session_root)>0&&
            towupper(journal[0])==L'E'&&dv_file_nonempty(journal));
        return complete(1);
    }
    phase++;
    return 0;
#undef NEED
}

static int begin(uiCmdCmdId id,uiCmdValue *value,void *data){
    report=_wfopen(L"bwtl-context-continuity.log",L"wt, ccs=UTF-8");
    if(!report){ProEngineerEnd();return 0;}
    SYSTEMTIME now;
    GetLocalTime(&now);
    swprintf_s(expected_top,PRO_NAME_SIZE,L"M%02uA1400000V00",now.wYear%100);
    swprintf_s(number1,PRO_NAME_SIZE,L"M%02uA1481001V00",now.wYear%100);
    swprintf_s(number2,PRO_NAME_SIZE,L"M%02uA1481002V00",now.wYear%100);
    if(ProDirectoryCurrentGet(project)||!native_ui_open_threaded(NULL))return complete(verify("INIT_BWTL",0));
    deadline=GetTickCount64()+50000;
    if(!SetTimer(native_backend_window,probe_timer,125,schedule))return complete(verify("NATIVE_TURN_TIMER",0));
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=continuity_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLContinuityTick",tick,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLContinuityStart",begin,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLContinuityStart\x60;");
}
