#define user_initialize pressfit_resolver_probe_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static int check_library_case(FILE *log,const wchar_t *selector,ProError expected,int stage){
    wchar_t result[PART_TEXT_MAX]=L"",path[MAX_PATH]=L"",project_path[MAX_PATH]=L"";
    PartText out={result,_countof(result),0};
    ProMdl source=NULL,project=NULL;
    ProMdlName name=L"";
    ProError error=pressfit_read_exact(selector,&source,path,&out);
    if(!error&&stage)error=pressfit_library_project(source,path,&project,project_path,&out);
    if(source)ProMdlMdlnameGet(source,name);
    int passed=error==expected&&(expected?source==NULL:source&&(!stage||project&&dv_file_nonempty(project_path)));
    if(passed&&stage){
        ProMdl reused=NULL;
        wchar_t reused_path[MAX_PATH]=L"";
        ProError reuse=pressfit_library_project(source,path,&reused,reused_path,&out);
        passed=!reuse&&reused==project&&!_wcsicmp(reused_path,project_path);
        fwprintf(log,L"REPEAT\t%ls\tstatus=%d\tsame_model_and_path=%d\n",selector,reuse,passed);
    }
    fwprintf(log,L"CASE\t%ls\texpected=%d\tactual=%d\tpass=%d\tmodel=%ls\n%ls",selector,expected,error,passed,name,result);
    if(path[0])fwprintf(log,L"SOURCE\t%ls\n",path);
    if(project_path[0])fwprintf(log,L"PROJECT\t%ls\n",project_path);
    fflush(log);
    return passed;
}

static int check_standoff_size(FILE *log,const wchar_t *selector,const wchar_t *instance,
    const wchar_t *family,int length){
    wchar_t text[PART_TEXT_MAX]=L"",path[MAX_PATH]=L"",dimensions[80];
    PartText out={text,_countof(text),0};
    ProMdl source=NULL;
    ProMdlName name=L"";
    ProError error=pressfit_read_exact(selector,&source,path,&out);
    if(!error)error=ProMdlMdlnameGet(source,name);
    swprintf_s(dimensions,_countof(dimensions),L"沿零件轴线实测长度 %d mm",length);
    const int passed=!error&&!_wcsicmp(name,instance)&&wcsstr(text,family)&&wcsstr(text,dimensions);
    fwprintf(log,L"SHORT\t%ls\tstatus=%d\tpass=%d\tmodel=%ls\n%ls",selector,error,passed,name,text);
    fflush(log);
    return passed;
}

static ProError create_standalone_fixture(const wchar_t *profile){
    wchar_t data[MAX_PATH];
    if(wcscpy_s(data,_countof(data),profile))return PRO_TK_INVALID_DIR;
    ProPath directory;
    ProError error=ProDirectoryCurrentGet(directory);
    if(error)return error;
    const wchar_t *folders[]={L"公司资料",L"公司标准",L"紧固件",native_company_fasteners[4].folder};
    wchar_t folder[MAX_PATH],next[MAX_PATH];
    if(!company_default_root(folder))return PRO_TK_INVALID_DIR;
    wchar_t *leaf=wcsrchr(folder,L'\\');
    if(!leaf)return PRO_TK_INVALID_DIR;
    *leaf=0;
    for(size_t i=0;i<_countof(folders);i++){
        if(swprintf_s(next,_countof(next),L"%ls\\%ls",folder,folders[i])<0||
           (!CreateDirectoryW(next,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS))return PRO_TK_CANT_WRITE;
        wcscpy_s(folder,_countof(folder),next);
    }
    ProMdl model=NULL;
    ProMdlName name=L"BWTL_PRESSFIT_SINGLE";
    const char *stage="standalone resolver fixture";
    error=quick_seed(name,&model);
    ModelPlan stock={0,20,20,3,0,1};
    if(!error)error=model_build_internal(&stock,name,&stage,model,NULL,NULL);
    if(!error)error=ProDirectoryChange(folder);
    if(!error)error=ProMdlSave(model);
    ProError restored=ProDirectoryChange(directory);
    return restored?restored:error;
}

static void inspect_attached_copy(FILE *log,const wchar_t *stage,ProMdl model,ProWVerstamp saved){
    ProBoolean changed=PRO_B_TRUE;
    ProWVerstamp now=NULL;
    ProPath origin=L"";
    ProError dirty=ProMdlModificationVerify(model,&changed);
    ProError stamp=ProMdlVerstampGet(model,&now);
    ProError source=ProMdlOriginGet(model,origin);
    fwprintf(log,L"STATE\t%ls\tdirty=%d, %d\tstamp_status=%d\tsame_stamp=%d\torigin_status=%d\torigin=%ls\trecord=%d\n",
        stage,dirty,changed,stamp,now&&ProVerstampEqual(now,saved),source,origin,!!fastener_saved_model(model));
    if(now)ProVerstampFree(&now);
    fflush(log);
}

static int inspect_first_place(FILE *log){
    wchar_t buffer[PART_TEXT_MAX]=L"",path[MAX_PATH],saved[MAX_PATH],again[MAX_PATH];
    PartText out={buffer,_countof(buffer),0};
    ProMdl source=NULL,project=NULL,assembly=NULL,reused=NULL;
    ProMdlName request=L"BWTL_INSPECT_REUSE",actual;
    const char *stage="target for attachment diagnostics";
    ProError error=pressfit_read_exact(L"B-M3-1",&source,path,&out);
    if(!error)error=pressfit_library_project(source,path,&project,saved,&out);
    if(!error)error=model_create_empty_named(3,request,actual,&stage);
    if(!error)error=ProMdlnameInit(actual,PRO_MDL_ASSEMBLY,&assembly);
    FastenerSavedView *prepared=error?NULL:pressfit_project_placement_begin(project,saved);
    ProWVerstamp prior=NULL;
    if(!error)error=ProMdlVerstampGet(project,&prior);
    if(!error)inspect_attached_copy(log,L"pre-assemble",project,prior);
    ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    ProAsmcomp component={0};
    if(!error)error=ProAsmcompAssemble((ProAssembly)assembly,(ProSolid)project,pose,&component);
    if(!error)inspect_attached_copy(log,L"post-assemble",project,prior);
    if(!error)error=part_regenerate(assembly);
    if(!error)inspect_attached_copy(log,L"post-regen",project,prior);
    if(!error)pressfit_project_placement_end(source,project,saved,prepared);
    if(!error)error=pressfit_library_project(source,path,&reused,again,&out);
    if(prior)ProVerstampFree(&prior);
    int reused_ok=!error&&reused==project&&!_wcsicmp(saved,again);
    fwprintf(log,L"INSPECT_RESULT\tstatus=%d\tsame_model_and_path=%d\n%ls",error,reused_ok,buffer);
    return reused_ok;
}

static int pressfit_resolver_probe(uiCmdCmdId command,uiCmdValue *value,void *data){
    FILE *log=_wfopen(L"pressfit-library.tsv",L"wt, ccs=UTF-8");
    if(!log){ProEngineerEnd();return 0;}
    model_background_mode=1;
    wchar_t profile[MAX_PATH];
    wcscpy_s(profile,_countof(profile),runtime_data);
    wcscpy_s(runtime_data,_countof(runtime_data),L"E:\\BWTL-Data\\AppData\\Local\\BWTL\\data");
    if(GetEnvironmentVariableW(L"BWTL_PRESSFIT_ATTACH_DIAGNOSTIC",NULL,0)){
        inspect_first_place(log);
        fclose(log);
        ProEngineerEnd();
        return 0;
    }
    int passed=0;
    ProPath reopen=L"";
    if(GetEnvironmentVariableW(L"BWTL_PRESSFIT_REOPEN_ROOT",reopen,_countof(reopen))){
        ProError switched=ProDirectoryChange(reopen);
        if(!switched)passed=check_library_case(log,L"B-M3-1",PRO_TK_NO_ERROR,1);
        fwprintf(log,L"REOPEN_PROJECT_REUSE\t%d/1\tdirectory_status=%d\n",passed,switched);
        fclose(log);
        ProEngineerEnd();
        return 0;
    }
    passed+=check_library_case(log,L"BSO M3×8",PRO_TK_NO_ERROR,1);
    passed+=check_standoff_size(log,L"BSO M3×5",L"BSO-3_5M3X5",L"BSO_3_5M3XX",5);
    passed+=check_standoff_size(log,L"BSO M3×6",L"BSO_3_5M3X6",L"BSO_3_5M3XX",6);
    passed+=check_standoff_size(log,L"BSO M3×8",L"BSO_3_5M3X8",L"BSO_3_5M3XX",8);
    passed+=check_standoff_size(log,L"BSO M3×20",L"BSO_3_5M3X20",L"BSO_3_5M3XX",20);
    passed+=check_standoff_size(log,L"BSO M4×8",L"BSO_M4X8",L"BSO_M4XX",8);
    passed+=check_standoff_size(log,L"BSO 3.5 M4×8",L"BSO_3_5M4X8",L"BSO_3_5M4XX",8);
    passed+=check_standoff_size(log,L"BSO M5×10",L"BSO-M5X10",L"BSO_M5XX",10);
    passed+=check_standoff_size(log,L"BSO M2.5×5",L"BSO_M2_5X5",L"BSO_M2_5XX",5);
    passed+=check_standoff_size(log,L"BSOS M3×8",L"BSOS_3_5M3X8",L"BSOS_3_5M3XX",8);
    passed+=check_standoff_size(log,L"BSOS M3×5",L"BSOS-3_5M3X5",L"BSOS_3_5M3XX",5);
    passed+=check_standoff_size(log,L"BSO_M3X5",L"BSO_M3X5",L"BSO_M3XX",5);
    passed+=check_library_case(log,L"BSOS M4×8",PRO_TK_E_NOT_FOUND,0);
    passed+=check_library_case(log,L"S-M3-1",PRO_TK_NO_ERROR,1);
    passed+=check_library_case(log,L"B-M3-1",PRO_TK_NO_ERROR,1);
    passed+=check_library_case(log,L"FH-M3-8",PRO_TK_E_FOUND,0);
    passed+=check_library_case(log,L"FH_M3_XX:FH-M3-8",PRO_TK_NO_ERROR,1);
    passed+=check_library_case(log,L"BSO M3×999",PRO_TK_E_NOT_FOUND,0);
    passed+=check_library_case(log,L"BSO_3_5M3XX.prt",PRO_TK_BAD_CONTEXT,0);
    wcscpy_s(runtime_data,_countof(runtime_data),profile);
    ProError fixture=create_standalone_fixture(profile);
    fwprintf(log,L"FIXTURE\tstandalone library part\t%d\n",fixture);
    if(!fixture)passed+=check_library_case(log,L"BWTL_PRESSFIT_SINGLE.prt.1",PRO_TK_NO_ERROR,1);
    fwprintf(log,L"PRESSFIT_LIBRARY_CASES\t%d/20\n",passed);
    fclose(log);
    ProEngineerEnd();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=pressfit_resolver_probe_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLPressfitLibraryProbe",pressfit_resolver_probe,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLPressfitLibraryProbe\x0060;");
}

