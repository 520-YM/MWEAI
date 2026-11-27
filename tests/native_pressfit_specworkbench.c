#define user_initialize pressfit_specs_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static int tally(FILE *log,const wchar_t *name,int pass,const wchar_t *description){
    fwprintf(log,L"%ls\tPASS=%d\n",name,!!pass);
    if(description&&description[0])fwprintf(log,L"%ls",description);
    fflush(log);
    return !!pass;
}

static int digest_test(void){
    wchar_t directory[MAX_PATH],file[MAX_PATH],one[65],two[65];
    if(!GetCurrentDirectoryW(MAX_PATH,directory)||
       swprintf_s(file,MAX_PATH,L"%ls\\pressfit-hash-refresh.fixture",directory)<0)return 0;
    HANDLE cache=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(cache==INVALID_HANDLE_VALUE)return 0;
    DWORD written;
    FILETIME first;
    int ok=WriteFile(cache,"abc",3,&written,NULL)&&written==3&&FlushFileBuffers(cache)&&GetFileTime(cache,NULL,NULL,&first);
    CloseHandle(cache);
    if(!ok||!pressfit_evidence_hash(file,one))return 0;
    cache=CreateFileW(file,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(cache==INVALID_HANDLE_VALUE)return 0;
    ok=WriteFile(cache,"abd",3,&written,NULL)&&written==3&&FlushFileBuffers(cache)&&SetFileTime(cache,NULL,NULL,&first);
    CloseHandle(cache);
    return ok&&pressfit_evidence_hash(file,two)&&wcscmp(one,two);
}

static int probe(uiCmdCmdId command,uiCmdValue *value,void *data){
    FILE *log=_wfopen(L"pressfit-specworkbench.tsv",L"wt, ccs=UTF-8");
    if(!log){ProEngineerEnd();return 0;}
    if(!GetEnvironmentVariableW(L"BWTL_NATIVE_PRESSFIT_CATALOG_DATA",runtime_data,MAX_PATH)){
        fclose(log);ProEngineerEnd();return 0;
    }
    model_background_mode=1;
    wchar_t result[PART_TEXT_MAX],path[MAX_PATH],selector[MAX_PATH];
    PartText out={result,_countof(result),0};
    ProMdl source=NULL;
    int count=0;
#define RESET() do{out.truncated=0;result[0]=0;}while(0)
    RESET();
    if(GetEnvironmentVariableW(L"BWTL_NATIVE_PRESSFIT_REOPEN",NULL,0)){
        count+=tally(log,L"Preference read after restarting independent Creo",pressfit_preference_read(5.4,1.5)!=NULL,L"");
        wchar_t recent[12][MAX_PATH],folder[MAX_PATH];
        unsigned total;
        experience_recent(recent,folder,&total);
        count+=tally(log,L"Preference belongs only to dedicated data parser",total==0,L"");
        const int query=pressfit_acceptance_chat(L"查询压铆件验收 BSO M3X8");
        count+=tally(log,L"Exact specification receipt through native chat",query&&wcsstr(experience_last_reply,L"BSO_3_5M3X8")&&
            wcsstr(experience_last_reply,L"最小板厚"),experience_last_reply);
        const int refresh=pressfit_acceptance_chat(L"刷新压铆件目录");
        count+=tally(log,L"Refresh through native chat",refresh&&wcsstr(experience_last_reply,L"已清除"),experience_last_reply);
        fwprintf(log,L"PRESSFIT_SPEC_REOPEN\t%d/4\n",count);
        fclose(log);
        ProEngineerEnd();
        return 0;
    }
    ProError error=pressfit_read_exact(L"S-M4-1",&source,path,&out);
    const PressfitAcceptance *row=!error?pressfit_acceptance_lookup(source,path):NULL;
    count+=tally(log,L"Index-independent exact registered model",row&&pressfit_acceptance_nominal(row,path)==5.4,result);
    RESET();
    ProMdl generic=NULL,normalized=NULL;
    PressfitSource requested={0};
    wcscpy_s(requested.instance,_countof(requested.instance),L"S_M4_1");
    wcscpy_s(requested.generic,_countof(requested.generic),L"S_M2_10_XX");
    error=source?ProFaminstanceGenericGet(source,PRO_B_TRUE,&generic):PRO_TK_BAD_CONTEXT;
    if(!error)error=pressfit_native_instance(generic,&requested,&normalized,&out);
    count+=tally(log,L"Native family punctuation recovery",!error&&normalized==source&&!wcscmp(requested.instance,L"S-M4-1"),result);
    RESET();
    if(source)pressfit_acceptance_describe(source,path,5.3,2.0,&out);
    count+=tally(log,L"Source-bound evidence distinguishes real hole and fixture",wcsstr(result,L"5.4")&&
        wcsstr(result,L"5.3")&&wcsstr(result,L"最小板厚")&&wcsstr(result,L"保存和重开"),result);
    RESET();
    error=pressfit_acceptance_recommend(5.4,1.5,L"M4",L"压铆螺母",selector,&out);
    count+=tally(log,L"No default for distinct accepted variants",error==PRO_TK_E_FOUND&&!selector[0]&&wcsstr(result,L"请指定规格"),result);
    RESET();
    error=pressfit_acceptance_recommend(5.4,1.5,L"M2",L"压铆螺母",selector,&out);
    count+=tally(log,L"Different thread shows exact hole change",error==PRO_TK_E_NOT_FOUND&&!selector[0]&&
        wcsstr(result,L"改到该尺寸"),result);
    RESET();
    error=source?pressfit_preference_save(source,path,&out):PRO_TK_BAD_CONTEXT;
    count+=tally(log,L"Company preference persisted and read back",!error&&pressfit_preference_read(5.4,1.5)==row,result);
    RESET();
    pressfit_hash_refresh();
    error=pressfit_acceptance_recommend(5.4,1.5,L"M4",L"压铆螺母",selector,&out);
    count+=tally(log,L"Compatible stored preference resolves a later request",!error&&!_wcsicmp(selector,L"S_M2_10_XX:S-M4-1")&&
        wcsstr(result,L"先前保存"),result);
    RESET();
    error=pressfit_acceptance_recommend(5.4,2.0,L"M4",L"压铆螺母",selector,&out);
    count+=tally(log,L"Different material thickness does not inherit preference",error==PRO_TK_E_FOUND&&!selector[0]&&
        wcsstr(result,L"没有等厚试装回执"),result);
    RESET();
    ProMdl bso=NULL;
    error=pressfit_read_exact(L"BSO M3X8",&bso,path,&out);
    if(!error)pressfit_acceptance_describe(bso,path,5.4,1.5,&out);
    count+=tally(log,L"BSO keeps exact length and trial-only distinction",!error&&wcsstr(result,L"实测长度 8 mm")&&
        wcsstr(result,L"没有标注该规格的正式底孔"),result);
    RESET();
    count+=tally(log,L"Same-length edited file invalidates digest",digest_test(),L"");
    RESET();
    ProMdl unknown=NULL;
    error=pressfit_read_exact(L"NOT_A_BWTL_PRESSFIT_PART",&unknown,path,&out);
    count+=tally(log,L"No generic or arbitrary part on lookup failure",error==PRO_TK_E_NOT_FOUND&&!unknown&&
        wcsstr(result,L"索引缺失或无法读取"),result);
    RESET();
    error=pressfit_read_exact(L"FH-M3-8",&unknown,path,&out);
    count+=tally(log,L"Known ambiguity survives missing index and unaccepted peer",error==PRO_TK_E_FOUND&&!unknown&&
        wcsstr(result,L"fh_m3_xx:FH-M3-8")&&wcsstr(result,L"Nfh_m3_xx:FH-M3-8"),result);
    fwprintf(log,L"PRESSFIT_SPECWORKBENCH\t%d/12\n",count);
    fclose(log);
    ProEngineerEnd();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=pressfit_specs_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLPressfitSpecsProbe",probe,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLPressfitSpecsProbe\x0060;");
}