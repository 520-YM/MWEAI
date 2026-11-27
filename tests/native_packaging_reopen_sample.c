#define user_initialize packaging_production_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>
static int verify_sample(uiCmdCmdId id,uiCmdValue *v,void *data){
    FILE *log=_wfopen(L"packaging-reopen.log",L"w, ccs=UTF-8");
    if(!log)return 0;
    ProMdl model=NULL;
    ProError e=ProDirectoryChange(L"E:\\project\\21.5寸桌面游戏机包装样件\\Creo");
    if(!e)e=ProMdlFiletypeLoad(L"mwe215_pack_sample.asm",PRO_MDLFILE_ASSEMBLY,PRO_B_FALSE,&model);
    AxScan *scan=NULL;
    if(!e)e=ax_scan(model,1,&scan);
    if(!e&&(scan->count!=3||scan->parts!=3))e=PRO_TK_INCOMPLETE;
    fwprintf(log,L"reopen=%d components=%d\n",e,scan?scan->count:0);
    if(!e)for(int i=0;i<scan->count;i++){
        ProOutline b;
        ProError q=or_outline(scan->occurrences[i].model,b);
        if(!q)fwprintf(log,L"%ls %.3f %.3f %.3f\n",scan->occurrences[i].name,b[1][0]-b[0][0],b[1][1]-b[0][1],b[1][2]-b[0][2]);
        else fwprintf(log,L"outline=%d\n",q);
    }
    free(scan);fclose(log);ProEngineerEnd();return 0;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t message[80]){
    uiCmdCmdId cmd;
    ProError e=ProCmdActionAdd("MWEAIVerifyPackSample",verify_sample,uiProe2ndImmediate,access_command,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!e)e=ProMacroLoad(L"~ Command \x0060" L"MWEAIVerifyPackSample\x0060;");
    return e;
}