#define user_initialize assistant_initialize_renamed
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>
static int run_u_test(uiCmdCmdId id,uiCmdValue *value,void *data){
    ProMdl sheet=NULL;
    ProMdlName name;
    const char *stage="empty sheet";
    wchar_t receipt[4096]=L"";
    PartText out={receipt,_countof(receipt),0};
    double dims[3]={0},gauge=0;
    ProError error;
    FILE *log=_wfopen(L"u-reinforcement-native.log",L"wt, ccs=UTF-8");
    model_background_mode=1;
    error=model_create_empty(2,name,&stage);
    if(!error)error=ProMdlnameInit(name,PRO_MDL_PART,&sheet);
    if(log){fwprintf(log,L"create sheet: %d / %hs / %ls\n",error,stage,name);fflush(log);}
    if(!error)error=bwtl_u_reinforcement(sheet,L"BWTL_U",500,20,10,2,2,&out);
    if(log){fwprintf(log,L"create U: %d / %ls\n",error,out.text);fflush(log);}
    if(!error)error=bwtl_u_reinforcement_measure(sheet,dims,&gauge);
    if(log){fwprintf(log,L"readback: %d / %.8g %.8g %.8g mm, gauge %.8g\nDONE: %d\n",error,dims[0],dims[1],dims[2],gauge,!error);fclose(log);}
    if(!error){
        receipt[0]=0;
        error=bwtl_u_reinforcement_resize_height(sheet,L"BWTL_U",12,&out);
        FILE *update=_wfopen(L"u-reinforcement-native.log",L"at, ccs=UTF-8");
        if(update){fwprintf(update,L"resize: %d / %ls\nFINISH: %d\n",error,out.text,!error);fclose(update);}
    }
    ProEngineerEnd();
    return error;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t message[80]){
    uiCmdCmdId command;
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=ProCmdActionAdd("BWTL_UFixture",run_u_test,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&command);
    return error?error:ProMacroLoad(L"~ Command " L"\x60" L"BWTL_UFixture\x60;");
}
