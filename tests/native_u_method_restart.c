#define user_initialize u_method_initialize_renamed
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static ProError um_new_sheet(ProMdl *sheet){
    ProMdlName name;
    const char *stage="empty sheet";
    model_background_mode=1;
    ProError error=model_create_empty(2,name,&stage);
    return error?error:ProMdlnameInit(name,PRO_MDL_PART,sheet);
}

static int um_trial(uiCmdCmdId id,uiCmdValue *value,void *data){
    wchar_t mode[8]=L"",shared[MAX_PATH]=L"";
    ProMdl model=NULL;
    ProError error=PRO_TK_BAD_CONTEXT;
    FILE *log=_wfopen(L"u-method-restart.log",L"wt, ccs=UTF-8");
    if(!log){ProEngineerEnd();return 0;}
    DWORD n=GetEnvironmentVariableW(L"BWTL_U_METHOD_TEST_DATA",shared,MAX_PATH);
    if(!GetEnvironmentVariableW(L"BWTL_U_METHOD_TEST_PHASE",mode,_countof(mode))||
       !n||n>=MAX_PATH||!storage_directory_on_drive(shared,L'E'))goto done;
    wcscpy_s(runtime_data,_countof(runtime_data),shared);
    wchar_t buffer[4096]=L"";
    PartText out={buffer,_countof(buffer),0};
    ProMdlName name=L"";
    double dims[3]={0},gauge=0;
    if(!wcscmp(mode,L"learn")){
        ProWVerstamp before=NULL,after=NULL;
        error=um_new_sheet(&model);
        if(!error)error=ProMdlMdlnameGet(model,name);
        if(!error)error=bwtl_u_reinforcement(model,L"ORIGINAL_STYLE",500,24,12,2,3,&out);
        if(!error)error=ProMdlVerstampGet(model,&before);
        if(!error&&!cad_u_learn_request(L"学习当前 U 形加强筋的做法"))error=PRO_TK_BAD_INPUTS;
        if(!error)error=cad_u_learn_recipe(model,&out);
        if(!error)error=ProMdlVerstampGet(model,&after);
        if(!error&&(!ProVerstampEqual(before,after)||!wcsstr(buffer,L".bwtlmethod.txt")))error=PRO_TK_BAD_CONTEXT;
        if(before)ProVerstampFree(&before);
        if(after)ProVerstampFree(&after);
        fwprintf(log,L"SOURCE_UNCHANGED=%d MODEL=%ls\n",!error,name);
    }else if(!wcscmp(mode,L"reuse")){
        CadURequest spec={0};
        if(!cad_u_parse(L"按学到的做法画一个 U 形加强筋，620×30×14mm，板厚2.5mm",&spec)||
           !spec.learned||!spec.default_radius)goto done;
        if(!cad_u_use_recipe(&spec)||!spec.recipe[0]||fabs(spec.radius-3)>1e-7)goto done;
        error=um_new_sheet(&model);
        if(!error)error=ProMdlMdlnameGet(model,name);
        cad_u_pending=spec;
        if(!error)error=cad_u_make(model,NULL,&out);
        if(!error)error=bwtl_u_reinforcement_measure(model,dims,&gauge);
        const double expected[]={620,30,14};
        if(!error&&(!bwtl_u_dimensions_same(dims,expected)||fabs(gauge-2.5)>0.002||
            !wcsstr(buffer,spec.recipe)))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=bwtl_u_reinforcement_resize_height(model,cad_u_feature,16,&out);
        if(!error)error=bwtl_u_reinforcement_measure(model,dims,&gauge);
        const double edited[]={620,30,16};
        if(!error&&(!bwtl_u_dimensions_same(dims,edited)||fabs(gauge-2.5)>0.002))error=PRO_TK_BAD_CONTEXT;
        fwprintf(log,L"NEW_MODEL_EDIT_READBACK=%d MODEL=%ls %.8g %.8g %.8g %.8g\n",
            !error,name,dims[0],dims[1],dims[2],gauge);
    }
    fwprintf(log,L"%ls\n",buffer);
done:
    fwprintf(log,L"U_METHOD_PASS=%d ERROR=%d PID=%lu PHASE=%ls\n",!error,error,GetCurrentProcessId(),mode);
    fclose(log);
    ProEngineerEnd();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t message[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=u_method_initialize_renamed(argc,argv,version,build,message);
    uiCmdCmdId command;
    if(!error)error=ProCmdActionAdd("BWTLURecipeFixture",um_trial,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&command);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLURecipeFixture\x60;");
}
