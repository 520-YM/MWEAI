#define user_initialize company_intent_original_init
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static FILE *ci_log;
static int ci_failures,ci_stage,ci_tick_queued,ci_callback_calls,ci_callback_fail;
static ULONGLONG ci_started;
static ProMdl ci_parent,ci_sheet,ci_single;
static int ci_parent_window;
static WNDPROC ci_original_proc;

static void ci_check(int condition,const char *name){
    if(!condition)ci_failures++;
    fprintf(ci_log,"%s=%d\n",name,!!condition);
    fflush(ci_log);
}
static int ci_end(void){
    KillTimer(native_backend_window,6531);
    if(ci_original_proc)SetWindowLongPtrW(native_backend_window,GWLP_WNDPROC,(LONG_PTR)ci_original_proc);
    fprintf(ci_log,"COMPANY_INTENT_PASS=%d\n",!ci_failures);
    fclose(ci_log);
    ProEngineerEnd();
    return 0;
}
static void ci_send(const wchar_t *text){
    native_prompt_set(text);
    run_local(NULL,NULL,NULL);
}
static int ci_active(ProMdlType kind,const wchar_t *name,ProMdl *model){
    ProMdl active=NULL;
    ProMdlName actual;
    ProMdlType type;
    int current_window,model_window;
    int ok=!ProMdlCurrentGet(&active)&&!ProMdlTypeGet(active,&type)&&type==kind&&
        !ProMdlMdlnameGet(active,actual)&&!_wcsicmp(name,actual)&&
        !ProWindowCurrentGet(&current_window)&&!ProMdlWindowGet(active,&model_window)&&
        current_window==model_window;
    if(ok&&model)*model=active;
    return ok;
}
static int ci_pair(const wchar_t *name,ProMdl *parent,ProMdl *part){
    ProMdl assembly=NULL,sheet=NULL;
    ProMdlsubtype subtype;
    AxScan *scan=NULL;
    int good=!ProMdlnameInit((wchar_t *)name,PRO_MDL_ASSEMBLY,&assembly)&&
        !ProMdlnameInit((wchar_t *)name,PRO_MDL_PART,&sheet)&&
        !ProMdlSubtypeGet(sheet,&subtype)&&subtype==PROMDLSTYPE_PART_SHEETMETAL&&
        !ax_scan(assembly,1,&scan)&&scan->count==1&&scan->occurrences[0].model==sheet;
    if(good){
        ProAsmcomp occurrence;
        good=!ax_component_feature(assembly,scan->occurrences[0].ids[0],&occurrence)&&
            !ax_default_verify(&occurrence)&&!template_empty((ProSolid)sheet)&&
            ci_active(PRO_MDL_PART,name,NULL);
    }
    free(scan);
    if(good){if(parent)*parent=assembly;if(part)*part=sheet;}
    return good;
}
static ProError ci_ready(ProMdl part,ProMdl parent,PartText *receipt){
    ci_callback_calls++;
    ci_check(part&&parent&&ci_active(PRO_MDL_PART,
        ci_callback_fail?L"CI_CALLBACK_FAILUREV00":L"CI_CALLBACKV00",NULL)&&
        !template_empty((ProSolid)part),"CALLBACK_ONLY_AFTER_ACTUAL_CHILD_WINDOW_READY");
    part_text_add(receipt,L"已核对连续建模入口中的真实父装配和空钣金件。\n");
    return ci_callback_fail?PRO_TK_E_NOT_FOUND:PRO_TK_NO_ERROR;
}
static int ci_tick(uiCmdCmdId id,uiCmdValue *value,void *data){
    ci_tick_queued=0;
    if(smart_active||ai_process||default_part.phase!=DEFAULT_PART_IDLE){
        if(GetTickCount64()-ci_started<60000)return 0;
        ci_check(0,"NATIVE_COMPANY_WORKFLOW_TIMEOUT");
        return ci_end();
    }
    if(ci_stage==0){
        ci_check(ci_pair(L"CI_GENERICV00",&ci_parent,&ci_sheet),"GENERIC_NEW_CREATES_SAME_NUMBER_ASM_NATIVE_SHEET_AND_OPENS");
        if(ci_failures)return ci_end();
        ci_stage=1;
        ci_send(L"单独创建一个零件，图号 CI_SOLO");
        return 0;
    }
    if(ci_stage==1){

        ProMdlsubtype subtype;
        ProMdl impossible=NULL;
        ci_check(ci_active(PRO_MDL_PART,L"CI_SOLOV00",&ci_single)&&
            !ProMdlSubtypeGet(ci_single,&subtype)&&subtype==PROMDLSTYPE_PART_SOLID&&
            ProMdlnameInit(L"CI_SOLOV00",PRO_MDL_ASSEMBLY,&impossible)==PRO_TK_E_NOT_FOUND,
            "EXPLICIT_SINGLE_CREATES_ONLY_NATIVE_SOLID");
        AxScan *scan=NULL;
        ci_check(!ax_scan(ci_parent,1,&scan)&&scan->count==1&&scan->occurrences[0].model==ci_sheet,
            "SINGLE_CREATION_PRESERVES_PREVIOUS_COMPANY_ASSEMBLY");
        free(scan);
        if(ci_failures)return ci_end();
        ci_stage=2;
        if(native_activate_model_window(ci_parent,L"CI_GENERICV00",PRO_ASSEMBLY,&ci_parent_window)){
            ci_check(0,"ACTIVATE_EXISTING_COMPANY_PARENT");
            return ci_end();
        }
        return 0;
    }
    if(ci_stage==2){
        ci_check(ci_active(PRO_MDL_ASSEMBLY,L"CI_GENERICV00",NULL),"PARENT_IS_ACTIVE_BEFORE_CONTEXTUAL_NEW_CHILD");
        if(ci_failures)return ci_end();
        ci_stage=3;
        ci_send(L"在里面创建一个钣金子零件，图号 CI_CHILD");
        return 0;
    }
    if(ci_stage==3){
        ProMdl child=NULL;
        ProMdlsubtype subtype;
        AxScan *scan=NULL;
        int prior=0,added=0;
        int good=ci_active(PRO_MDL_PART,L"CI_CHILDV00",&child)&&
            !ProMdlSubtypeGet(child,&subtype)&&subtype==PROMDLSTYPE_PART_SHEETMETAL&&
            !ax_scan(ci_parent,1,&scan)&&scan->count==2;
        for(int i=0;good&&i<scan->count;i++){
            prior+=scan->occurrences[i].model==ci_sheet;
            added+=scan->occurrences[i].model==child;
        }
        free(scan);
        ci_check(good&&prior==1&&added==1,"CONTEXTUAL_INSIDE_CREATES_CHILD_IN_CURRENT_PARENT");
        if(ci_failures)return ci_end();
        ci_stage=4;
        ModelingPartIntent intent={.scope=MODELING_PART_NEW,.kind=2};
        wcscpy_s(intent.name,_countof(intent.name),L"CI_CALLBACK");
        ci_check(default_part_begin_request(L"按公司流程创建新钣金件并接续绘制",&intent,ci_ready),
            "TYPED_CONTINUATION_STARTS_COMPANY_WORKFLOW");
        return 0;
    }
    if(ci_stage==4){
        ci_check(ci_callback_calls==1&&ci_pair(L"CI_CALLBACKV00",NULL,NULL),
            "VERIFIED_READY_CALLBACK_EXECUTED_ONCE");
        if(ci_failures)return ci_end();
        ci_stage=5;
        ci_callback_fail=1;
        ModelingPartIntent intent={.scope=MODELING_PART_NEW,.kind=2};
        wcscpy_s(intent.name,_countof(intent.name),L"CI_CALLBACK_FAILURE");
        default_part_begin_request(L"按公司流程创建并核对后续失败保留",&intent,ci_ready);
        return 0;
    }
    ci_check(ci_callback_calls==2&&ci_pair(L"CI_CALLBACK_FAILUREV00",NULL,NULL),
        "LATER_FAILURE_RETAINS_VERIFIED_PARENT_CHILD_AND_POSITION");
    return ci_end();
}

static LRESULT CALLBACK ci_backend(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_TIMER&&w==6531){
        if(!ci_tick_queued&&((!smart_active&&!ai_process&&default_part.phase==DEFAULT_PART_IDLE)||GetTickCount64()-ci_started>60000))ci_tick_queued=!ProMacroLoad(L"~ Command \x0060" L"BWTLCompanyIntentTick\x0060;");
        return 0;
    }
    return CallWindowProcW(ci_original_proc,window,message,w,l);
}
static int ci_begin(uiCmdCmdId id,uiCmdValue *value,void *data){
    ci_log=fopen("company-intent-native.log","w");
    if(!ci_log){ProEngineerEnd();return 0;}
    ModelingPartIntent parsed;
    ci_check(modeling_part_parse(L"创建一个零件",&parsed)&&parsed.scope==MODELING_PART_NEW&&!parsed.kind,
        "GENERIC_CREATE_INTENT");
    ci_check(modeling_part_parse(L"单独创建一个零件",&parsed)&&parsed.scope==MODELING_PART_SINGLE,
        "EXPLICIT_SINGLE_INTENT");
    ci_check(modeling_part_parse(L"在里面创建子零件",&parsed)&&parsed.scope==MODELING_PART_CURRENT,
        "CURRENT_PARENT_REFERENCE_INTENT");
    ci_check(!modeling_part_parse(L"把刚才那个的尺寸改成 600",&parsed)&&
        !modeling_part_parse(L"创建一个零件，先不要执行",&parsed)&&
        !modeling_part_parse(L"创建一个零件为什么不成功？",&parsed),
        "EDIT_NEGATION_AND_QUESTION_DO_NOT_CREATE_NEW_MODEL");
    ModelingRuleState rules;
    int status=modeling_rule_read(&rules);
    if(status==MODELING_RULE_OK&&rules.current.mode!=MODELING_RULE_COMPANY){
        ModelingRuleValues company=rules.current;
        company.mode=MODELING_RULE_COMPANY;
        status=modeling_rule_save(&company,rules.generation,&rules);
    }
    ci_check(status==MODELING_RULE_OK&&rules.current.mode==MODELING_RULE_COMPANY,
        "INDEPENDENT_TEST_COMPANY_RULES_AVAILABLE");
    if(open_window(0,NULL,NULL)||ci_failures)return ci_end();
    ci_started=GetTickCount64();
    ci_original_proc=(WNDPROC)SetWindowLongPtrW(native_backend_window,GWLP_WNDPROC,(LONG_PTR)ci_backend);
    ci_check(ci_original_proc!=NULL&&SetTimer(native_backend_window,6531,250,NULL)!=0,
        "NATIVE_CONTINUATION_OBSERVER");
    ci_send(L"创建一个零件，图号 CI_GENERIC");
    return 0;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t error[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError status=company_intent_original_init(argc,argv,version,build,error);
    uiCmdCmdId cmd;
    if(!status)status=ProCmdActionAdd("BWTLCompanyIntentTick",ci_tick,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!status)status=ProCmdActionAdd("BWTLCompanyIntentBegin",ci_begin,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    return status?status:ProMacroLoad(L"~ Command \x0060" L"BWTLCompanyIntentBegin\x0060;");
}