#define user_initialize bwtl_delivery_reopen_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static ProError reopen_delivery(const wchar_t *folder,FILE *log,int min_views,int is_assembly){
    ProPath pattern, drawing_path;
    if(swprintf_s(pattern,_countof(pattern),L"%ls\\*.drw.1",folder)<0)return PRO_TK_BAD_INPUTS;
    WIN32_FIND_DATAW file;
    HANDLE match=FindFirstFileW(pattern,&file);
    if(match==INVALID_HANDLE_VALUE)return PRO_TK_E_NOT_FOUND;
    const int one_file=!FindNextFileW(match,&file);
    FindClose(match);
    if(!one_file||swprintf_s(drawing_path,_countof(drawing_path),L"%ls\\%ls",folder,file.cFileName)<0)
        return PRO_TK_BAD_CONTEXT;
    ProPath chosen_folder;
    if(wcscpy_s(chosen_folder,_countof(chosen_folder),folder))return PRO_TK_BAD_INPUTS;
    ProError error=ProDirectoryChange(chosen_folder);
    ProMdl model=NULL;
    if(!error)error=ProMdlFiletypeLoad(drawing_path,PRO_MDLFILE_DRAWING,PRO_B_FALSE,&model);
    ProView *views=NULL;
    if(!error)error=ProDrawingViewsCollect((ProDrawing)model,&views);
    int count=0,attached=0,components=0;
    if(!error)error=ProArraySizeGet((ProArray)views,&count);
    ProMdl linked=NULL;
    for(int i=0;!error&&i<count;i++){
        ProSolid solid=NULL;
        if(ProDrawingViewSolidGet((ProDrawing)model,views[i],&solid)||!solid)continue;
        attached++;
        if(linked&&linked!=(ProMdl)solid)error=PRO_TK_BAD_CONTEXT;
        linked=(ProMdl)solid;
    }
    if(views)ProArrayFree((ProArray *)&views);
    if(!error&&(attached<min_views||!linked))error=PRO_TK_INCOMPLETE;
    ProMdlType kind=PRO_MDL_UNUSED;
    if(!error)error=ProMdlTypeGet(linked,&kind);
    if(!error&&kind!=(is_assembly?PRO_MDL_ASSEMBLY:PRO_MDL_PART))error=PRO_TK_INVALID_TYPE;
    wchar_t origin[PRO_PATH_SIZE]=L"";
    if(!error)error=ProMdlOriginGet(linked,origin);
    if(!error&&(wcslen(origin)<wcslen(folder)||_wcsnicmp(origin,folder,wcslen(folder))||
       origin[wcslen(folder)]!=L'\\'))error=PRO_TK_BAD_CONTEXT;
    if(!error&&is_assembly){
        AxScan *scan=NULL;
        error=ax_scan(linked,1,&scan);
        if(!error){
            components=scan->count;
            if(components!=1||!scan->occurrences[0].model)error=PRO_TK_INCOMPLETE;
            else{
                ProPath dependency;
                error=ProMdlOriginGet(scan->occurrences[0].model,dependency);
                if(!error&&(_wcsnicmp(dependency,folder,wcslen(folder))||
                   dependency[wcslen(folder)]!=L'\\'))error=PRO_TK_BAD_CONTEXT;
            }
        }
        free(scan);
    }
    fwprintf(log,L"%ls\nVIEWS=%d COMPONENTS=%d NATIVE_ORIGIN=%ls ERROR=%d\n",drawing_path,attached,components,origin,error);
    fflush(log);
    return error;
}

static int acceptance(uiCmdCmdId id,uiCmdValue *value,void *data){
    ProPath base;
    FILE *log=_wfopen(L"delivery-reopen.log",L"wt, ccs=UTF-8");
    DWORD size=GetEnvironmentVariableW(L"BWTL_TEST_DELIVERY_REOPEN",base,_countof(base));
    ProError error=!size||size>=_countof(base)||!log?PRO_TK_BAD_CONTEXT:PRO_TK_NO_ERROR;
    wchar_t *folders[]={L"sheet-delivery",L"assembly-delivery"};
    for(int i=0;!error&&i<2;i++){
        ProPath folder;
        if(swprintf_s(folder,_countof(folder),L"%ls\\%ls",base,folders[i])<0)error=PRO_TK_BAD_INPUTS;
        if(!error)error=reopen_delivery(folder,log,i?3:4,i!=0);
    }
    if(log){
        fwprintf(log,L"NATIVE_DELIVERY_REOPEN_PASS=%d (Creo %d)\n",!error,error);
        fclose(log);
    }
    ProEngineerEnd();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_delivery_reopen_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId cmd;
    if(!error)error=ProCmdActionAdd("BWTLDeliveryReopen",acceptance,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLDeliveryReopen\x0060;");
}