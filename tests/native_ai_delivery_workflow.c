#define user_initialize bwtl_delivery_fixture_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static ProMdl selected_model;
static FILE *receipt;
static wchar_t text[PART_TEXT_MAX];
static PartText out={text,_countof(text),0};
static const wchar_t run_macro[]=L"~ Command \x0060" L"BWTLDeliveryFixtureRun\x0060;";
static int phase;

static int finish_delivery(int ok){
    if(receipt){
        fwprintf(receipt,L"NATIVE_DELIVERY_PASS=%d\n",ok);
        fclose(receipt);
    }
    ProEngineerEnd();
    return 0;
}

static ProError new_delivery_sheet(ProMdl *model,int bent){
    ProMdlName name;
    const char *step="";
    ProError error=model_create_empty(2,name,&step);
    if(!error)error=ProMdlnameInit(name,PRO_MDL_PART,model);
    if(!error&&bent)error=bwtl_u_reinforcement(*model,L"DELIVERY_U",500,20,10,2,2,&out);
    if(!error&&!bent)error=cm_native_command(*model,L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60",name);
    if(!error&&!bent)error=cm_native_command(*model,L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 2 POS",name);
    if(!error&&!bent)error=cm_native_command(*model,L"CREO_OP SMT_SKETCH %ls CUT_PROFILE PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 3",name);
    if(!error&&!bent)error=cm_native_command(*model,L"CREO_OP SMT_CUT %ls CUT_A SKETCH:CUT_PROFILE NEG",name);
    return error;
}

static int delivery_run(uiCmdCmdId id,uiCmdValue *value,void *data){
    ProMdl active=NULL;
    ProPath prior,after;
    wchar_t before[256],now[256];
    ProError error=ProMdlCurrentGet(&active);
    if(!error&&active!=selected_model)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProDirectoryCurrentGet(prior);
    if(!error)error=smy_stamp(active,before);
    if(error)return finish_delivery(0);
    memset(text,0,sizeof(text));
    out.truncated=0;
    const wchar_t *request=phase==0?L"生成交付包 sheet-delivery！":
        phase==1?L"给当前装配生成交付包 包名assembly-delivery。":L"给当前模型生成交付包。";
    history[0]=0;
    native_prompt_set(request);
    run_local(NULL,NULL,NULL);
    fwprintf(receipt,L"PHASE=%d REQUEST=%ls\n%ls\n",phase,request,history);
    const wchar_t *reply=wcsstr(history,L"【AI】");
    if(!reply||wcsstr(reply+4,L"【AI】")||!wcsstr(reply,L"交付包已写入")||ai_process)
        error=PRO_TK_GENERAL_ERROR;
    fwprintf(receipt,L"BUNDLE=%d SINGLE_LOCAL_RESULT=%d\n",error,!error);
    fflush(receipt);
    if(error)return finish_delivery(0);
    if(ProDirectoryCurrentGet(after)||_wcsicmp(prior,after)||smy_stamp(active,now)||wcscmp(now,before)||
       ProMdlCurrentGet(&active)||active!=selected_model)return finish_delivery(0);
    fwprintf(receipt,L"RESTORED_MODEL_AND_DIRECTORY=1\n");
    if(phase==2)return finish_delivery(1);
    phase++;
    model_background_mode=1;
    if(phase==1){
        ProMdl sheet=selected_model;
        ProMdlName name;
        const char *stage="";
        const ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        error=ProMdlSave(sheet);
        if(!error)error=model_create_empty(3,name,&stage);
        if(!error)error=ProMdlnameInit(name,PRO_MDL_ASSEMBLY,&selected_model);
        if(!error)error=ax_place_model(selected_model,sheet,(double (*)[4])pose,AX_PLACE_FIXED,1,NULL,&out);
        if(!error)error=ProMdlSave(selected_model);
    }else error=new_delivery_sheet(&selected_model,1);
    model_background_mode=0;
    if(!error&&phase==1){
        ProMdlName name;int window;
        error=ProMdlMdlnameGet(selected_model,name);
        if(!error)error=ProObjectwindowMdlnameCreate(name,PRO_ASSEMBLY,&window);
        if(!error)error=ProWindowCurrentSet(window);
        if(!error)error=ProMdlDisplay(selected_model);
        if(!error)error=ProWindowActivate(window);
    }else if(!error)error=dv_display_new(selected_model);
    fwprintf(receipt,L"PREPARE_NEXT=%d\n",error);
    fflush(receipt);
    return error||ProMacroLoad((wchar_t *)run_macro)?finish_delivery(0):0;
}

static int delivery_setup(uiCmdCmdId id,uiCmdValue *value,void *data){
    receipt=_wfopen(L"company-delivery-native.log",L"wt, ccs=UTF-8");
    if(!receipt)return finish_delivery(0);
    model_background_mode=1;
    ProError error=new_delivery_sheet(&selected_model,0);
    model_background_mode=0;
    if(!error&&phase==1){
        ProMdlName name;int window;
        error=ProMdlMdlnameGet(selected_model,name);
        if(!error)error=ProObjectwindowMdlnameCreate(name,PRO_ASSEMBLY,&window);
        if(!error)error=ProWindowCurrentSet(window);
        if(!error)error=ProMdlDisplay(selected_model);
        if(!error)error=ProWindowActivate(window);
    }else if(!error)error=dv_display_new(selected_model);
    if(!error&&!native_ui_open_threaded(NULL))error=PRO_TK_BAD_CONTEXT;
    if(!error&&(bwtl_delivery_chat(L"能不能生成交付包？")||bwtl_delivery_chat(L"只分析怎么生成交付包。")))
        error=PRO_TK_BAD_CONTEXT;
    fwprintf(receipt,L"INITIAL_PHYSICAL_SHEET=%d\n",error);
    fflush(receipt);
    return error||ProMacroLoad((wchar_t *)run_macro)?finish_delivery(0):0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_delivery_fixture_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLDeliveryFixtureSetup",delivery_setup,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLDeliveryFixtureRun",delivery_run,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLDeliveryFixtureSetup\x0060;");
}
