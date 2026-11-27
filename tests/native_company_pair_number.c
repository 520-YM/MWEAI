#define user_initialize pair_numbering_original_init
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static FILE *numbering_log;
static int numbering_failed,numbering_phase,numbering_queued;
static ULONGLONG numbering_started;
static WNDPROC numbering_original_proc;
static ProMdl numbering_parent,numbering_part;
static ProMdlName numbering_original;
static int numbering_occurrence;

static int numbering_check(int condition,const char *label){
    fprintf(numbering_log,"%s=%d\n",label,!!condition);
    fflush(numbering_log);
    if(!condition)numbering_failed++;
    return condition;
}
static int numbering_end(void){
    KillTimer(native_backend_window,6542);
    if(numbering_original_proc)SetWindowLongPtrW(native_backend_window,GWLP_WNDPROC,(LONG_PTR)numbering_original_proc);
    fprintf(numbering_log,"COMPANY_PAIR_NUMBER_PASS=%d\n",!numbering_failed);
    fclose(numbering_log);
    ProEngineerEnd();
    return 0;
}
static void numbering_send(const wchar_t *text){
    native_prompt_set(text);
    run_local(NULL,NULL,NULL);
}
static int numbering_readback(const wchar_t *expected){
    numbering_send(L"刚才建的图号是多少？");
    return !ai_process&&wcsstr(history,expected)&&wcsstr(history,L".ASM")&&wcsstr(history,L".PRT");
}
static int numbering_pair(const wchar_t *expected,int verify_same_objects){
    ProMdl parent=NULL,part=NULL,active=NULL;
    ProMdlsubtype subtype;
    int id=0;
    ProMatrix pose;
    const int good=!chat_task_resolve_pair(&parent,&part,&id)&&
        company_pair_model_name(parent,PRO_MDL_ASSEMBLY,expected)&&
        company_pair_model_name(part,PRO_MDL_PART,expected)&&
        !ProMdlSubtypeGet(part,&subtype)&&subtype==PROMDLSTYPE_PART_SHEETMETAL&&
        !company_pair_occurrence(parent,part,id,pose)&&
        !ProMdlCurrentGet(&active)&&active==part&&!template_empty((ProSolid)part);
    return good&&(!verify_same_objects||
        (parent==numbering_parent&&part==numbering_part&&id==numbering_occurrence));
}

static int numbering_tick(uiCmdCmdId id,uiCmdValue *value,void *data){
    numbering_queued=0;
    if(smart_active||ai_process||default_part.phase!=DEFAULT_PART_IDLE){
        if(GetTickCount64()-numbering_started<60000)return 0;
        numbering_check(0,"NATIVE_NUMBERING_TIMEOUT");
        return numbering_end();
    }
    if(numbering_phase==0){
        if(!numbering_check(!chat_task_resolve_pair(&numbering_parent,&numbering_part,&numbering_occurrence)&&
           !ProMdlMdlnameGet(numbering_parent,numbering_original)&&
           !_wcsnicmp(numbering_original,L"AI_ASM_",7),"VERIFIED_PRE_PROJECT_TEMP_PAIR")||
           !numbering_check(numbering_readback(numbering_original),"IMMEDIATE_TEMP_NUMBER_READBACK"))return numbering_end();
        numbering_phase++;
        numbering_send(L"创建总装，图号 M26A1400000V00");
        return 0;
    }
    if(numbering_phase==1){
        ProMdl project=NULL;
        int window;
        if(!numbering_check(!chat_task_company_assembly(&project)&&
           company_pair_model_name(project,PRO_MDL_ASSEMBLY,L"M26A1400000V00"),"VERIFIED_PROJECT_14_TOTAL")||
           !numbering_check(!native_activate_model_window(numbering_part,numbering_original,PRO_PART,&window),
               "RETURN_TO_ACTUAL_PRE_PROJECT_PART"))return numbering_end();
        numbering_phase++;
        return 0;
    }
    if(numbering_phase==2){
        if(!numbering_check(numbering_readback(numbering_original),"OLD_PAIR_NUMBER_RECOVERED") )return numbering_end();
        numbering_send(L"那就帮我改成正确的");
        if(!numbering_check(!ai_process&&numbering_pair(L"M26A1481001V00",1),"SCREENSHOT_CORRECTION_SAME_NATIVE_PAIR")||
           !numbering_check(numbering_readback(L"M26A1481001V00"),"CORRECTED_NUMBER_IMMEDIATELY_AVAILABLE"))return numbering_end();
        ProMdl project=NULL;
        if(!numbering_check(!chat_task_company_assembly(&project)&&
            company_pair_model_name(project,PRO_MDL_ASSEMBLY,L"M26A1400000V00"),
            "PROJECT_TOTAL_RETAINED"))return numbering_end();

        HANDLE existing=CreateFileW(L"M26A1481002V03.prt.1",GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(!numbering_check(existing!=INVALID_HANDLE_VALUE,"PREPARE_OTHER_REVISION_DISK_COLLISION"))return numbering_end();
        CloseHandle(existing);
        numbering_phase++;
        numbering_send(L"创建一个零件");
        return 0;
    }
    if(numbering_phase==3){
        if(!numbering_check(numbering_pair(L"M26A1481003V00",0),"CREATION_SKIPS_LIVE_001_AND_DISK_002")||
           !numbering_check(numbering_readback(L"M26A1481003V00"),"IMMEDIATE_AUTO_NUMBER_READBACK"))return numbering_end();
        numbering_send(L"把刚才那个改成正确的");
        if(!numbering_check(!ai_process&&numbering_pair(L"M26A1481003V00",0),
           "CORRECTION_OF_COMPLIANT_PAIR_KEEPS_SAME_NUMBER"))return numbering_end();
        numbering_send(L"把刚才那对图号改成 M26A1481003V01");
        if(!numbering_check(!ai_process&&numbering_pair(L"M26A1481003V01",0),
           "EXPLICIT_VERSION_REUSES_OWN_SEQUENCE_RESERVATION"))return numbering_end();

        const char *stage="private saved root fixture";
        ProMdlName actual;
        ProMdl fixture=NULL;
        model_background_mode=1;
        ProError error=model_create_empty_named(3,L"M26A1500000V00",actual,&stage);
        model_background_mode=0;
        if(!error)error=ProMdlnameInit(actual,PRO_MDL_ASSEMBLY,&fixture);
        if(!error)error=ProMdlSave(fixture);
        if(!error)error=ProMdlErase(fixture);
        fprintf(numbering_log,"SAVED_FIXTURE_STATUS=%d\n",error);
        if(!numbering_check(!error,"PREPARE_FRESH_SAVED_PROJECT_TOTAL"))return numbering_end();
        numbering_phase++;
        return 0;
    }
    if(numbering_phase==4){
        ProMdl fixture=NULL;
        ProPath directory,source;
        ProMdlName name=L"M26A1500000V00";
        int window;
        ProError error=ProDirectoryCurrentGet(directory);
        if(!error)error=ax_native_source(directory,name,PRO_MDL_ASSEMBLY,source);
        if(!error)error=ProMdlFiletypeLoad(source,PRO_MDLFILE_ASSEMBLY,PRO_B_FALSE,&fixture);
        if(!error)error=native_activate_model_window(fixture,name,PRO_ASSEMBLY,&window);
        if(!numbering_check(!error,"RELOAD_ACTIVE_SAVED_PROJECT_TOTAL"))return numbering_end();
        chat_task_context_reset();
        numbering_phase++;
        return 0;
    }
    if(numbering_phase==5){
        ProMdl fixture=NULL;
        if(!numbering_check(chat_task_company_assembly(&fixture)==PRO_TK_E_NOT_FOUND,
           "SAVED_PROJECT_FIXTURE_STARTS_WITHOUT_CHAT_ANCHOR"))return numbering_end();
        numbering_phase++;
        numbering_send(L"创建一个零件");
        return 0;
    }
    numbering_check(numbering_pair(L"M26A1581001V00",0)&&numbering_readback(L"M26A1581001V00"),
        "ACTIVE_SAVED_PROJECT_WORKS_WITHOUT_PRIOR_CHAT");
    return numbering_end();
}

static LRESULT CALLBACK numbering_backend(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_TIMER&&w==6542){
        if(!numbering_queued&&((!smart_active&&!ai_process&&default_part.phase==DEFAULT_PART_IDLE)||
            GetTickCount64()-numbering_started>60000))
            numbering_queued=!ProMacroLoad(L"~ Command \x0060" L"BWTLPairNumberTick\x0060;");
        return 0;
    }
    return CallWindowProcW(numbering_original_proc,window,message,w,l);
}

static int numbering_begin(uiCmdCmdId id,uiCmdValue *value,void *data){
    numbering_log=fopen("company-pair-number-native.log","w");
    if(!numbering_log){ProEngineerEnd();return 0;}
    ModelingRuleState rules;
    int status=modeling_rule_read(&rules);
    if(status==MODELING_RULE_OK&&rules.current.mode!=MODELING_RULE_COMPANY){
        ModelingRuleValues company=rules.current;
        company.mode=MODELING_RULE_COMPANY;
        status=modeling_rule_save(&company,rules.generation,&rules);
    }
    if(!numbering_check(status==MODELING_RULE_OK&&rules.current.mode==MODELING_RULE_COMPANY,
       "PRIVATE_COMPANY_RULES_IN_TEST")||open_window(0,NULL,NULL))return numbering_end();
    numbering_started=GetTickCount64();
    numbering_original_proc=(WNDPROC)SetWindowLongPtrW(native_backend_window,GWLP_WNDPROC,(LONG_PTR)numbering_backend);
    if(!numbering_check(numbering_original_proc!=NULL&&SetTimer(native_backend_window,6542,250,NULL)!=0,
       "NATIVE_CONTINUATION_OBSERVER"))return numbering_end();
    numbering_send(L"创建一个零件");
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t error[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError status=pair_numbering_original_init(argc,argv,version,build,error);
    uiCmdCmdId command;
    if(!status)status=ProCmdActionAdd("BWTLPairNumberTick",numbering_tick,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&command);
    if(!status)status=ProCmdActionAdd("BWTLPairNumberBegin",numbering_begin,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&command);
    return status?status:ProMacroLoad(L"~ Command \x0060" L"BWTLPairNumberBegin\x0060;");
}

