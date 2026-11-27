#define user_initialize company_access_probe_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static FILE *library_audit;
static int failures;

static void expected(int ok,const wchar_t *label,int slot){
    fwprintf(library_audit,L"CHECK\t%d\t%ls\t%d\n",slot,label,ok!=0);
    fflush(library_audit);
    failures+=!ok;
}

static void current_request(const wchar_t *text){
    wcscpy_s(smart_latest_request,_countof(smart_latest_request),text);
    smart_prior_goal[0]=0;
    company_root_count=0;
    smart_diagnostic_failures=0;
    company_source_reset();
}

static int read_protocol(const wchar_t *operation,int success,const wchar_t *phrase,int slot){
    static wchar_t output[32000];
    wchar_t *body=NULL;
    swprintf_s(resident_operation,_countof(resident_operation),L"LIBRARY %ls",operation);
    resident_tool_queued=1;
    const int baseline=smart_active;
    const HANDLE process=ai_process;
    smart_active=1;
    ai_process=(HANDLE)1;
    resident_tool_command(0,NULL,NULL);
    ai_process=process;
    smart_active=baseline;
    int received=resident_read(L"resident-tool-result.txt",output,_countof(output))==1&&resident_frame(output,&body)&&body;
    int ok=received&&(!wcsncmp(body,L"0\n",2)==success)&&(!phrase||wcsstr(body,phrase));
    fwprintf(library_audit,L"TOOL\t%d\t%ls\t%d\t%ls\n",slot,operation,ok,received?body:L"no native receipt");
    fflush(library_audit);
    return ok;
}

static void inspect_profile(int slot){
    const wchar_t *const local=L"E:\\BWTL-Data\\AppData\\Local";
    if(slot==1)swprintf_s(runtime_data,_countof(runtime_data),L"%ls\\BWTL\\data",local);
    else swprintf_s(runtime_data,_countof(runtime_data),L"%ls\\BWTL-Instances\\%d\\data",local,slot);
    const wchar_t *continuation=L"那就装M2的吧，你只要给我装上就行";
    current_request(continuation);
    expected(company_request_scope()==COMPANY_SCOPE_FASTENERS,L"M2 continuation grants only fastener reads",slot);
    expected(read_protocol(L"check|||",1,L"公司标准\\紧固件",slot),L"registered default root opens",slot);
    expected(read_protocol(L"check|公司库||",1,L"公司标准\\紧固件",slot),L"explicit company alias opens fastener scope",slot);
    expected(read_protocol(L"search|紧固件|s_m2_10_xx|",1,L"s_m2_10_xx.prt.3",slot),L"fastener alias finds real latest PRT",slot);
    expected(read_protocol(L"fastener|压铆件|S_M2_10_XX:S-M2-1|",1,L"名称：S-M2-1.prt",slot),L"real exact instance, not a generic",slot);
    if(slot==1||slot==8){
        wchar_t folder[MAX_PATH],path[MAX_PATH],operation[600];
        expected(company_fastener_resolve(COMPANY_OPEN_FASTENER_FIRST+4,folder),L"native menu and host share factory path",slot);
        swprintf_s(path,_countof(path),L"%ls\\s_m2_10_xx.prt.3",folder);
        swprintf_s(operation,_countof(operation),L"model|公司库||%ls",path);
        expected(read_protocol(operation,1,L"已读取磁盘库模型（只读）",slot),L"original model tool can read authorized path",slot);
        expected(read_protocol(L"check|技术文件||",0,L"授权范围未通过",slot),L"scope cannot read technical material",slot);
        expected(read_protocol(L"check|E:\\BWTL-Data||",0,L"授权范围未通过",slot),L"scope cannot scan entire data disk",slot);
        swprintf_s(operation,_countof(operation),L"read|压铆件||%ls\\BWTL\\公司资料\\公司标准\\图号与画图规则\\画图规则.txt",local);
        expected(read_protocol(operation,0,L"授权范围未通过",slot),L"root mismatch cannot disclose real document",slot);
        current_request(L"请看看 Creo 当前窗口");
        expected(read_protocol(L"check|公司库||",0,L"授权范围未通过",slot),L"unrelated next turn cannot inherit authorization",slot);
        current_request(L"不要查公司库，只说明当前模型的状态");
        expected(read_protocol(L"check|||",0,L"授权范围未通过",slot),L"explicit denial is honored",slot);
        current_request(L"请浏览公司资料，确认技术文件分类");
        expected(read_protocol(L"check|技术文件||",1,L"公司资料\\技术文件",slot),L"explicit company technical alias is read-only",slot);
        expected(read_protocol(L"check|C:\\Windows||",0,L"路径格式",slot),L"operator drive rules reject C",slot);
    }
}

static int test_library_authorization(uiCmdCmdId cmd,uiCmdValue *value,void *data){
    library_audit=_wfopen(L"company-library-authorization.tsv",L"wt, ccs=UTF-8");
    if(!library_audit){ProEngineerEnd();return 0;}
    wchar_t profile[MAX_PATH];
    wcscpy_s(profile,_countof(profile),runtime_data);
    ProMdl original=NULL,after=NULL;
    ProMdlCurrentGet(&original);
    for(int slot=1;slot<=8;slot++)inspect_profile(slot);
    ProMdlCurrentGet(&after);
    expected(original==after,L"read tools do not change active Creo model",0);
    wcscpy_s(runtime_data,_countof(runtime_data),profile);
    fwprintf(library_audit,L"AUTHORIZATION_NATIVE_PASS\t%d\n",!failures);
    fclose(library_audit);
    ProEngineerEnd();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *release,char *build,wchar_t message[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=company_access_probe_initialize(argc,argv,release,build,message);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLCompanyAuthorizationTest",test_library_authorization,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLCompanyAuthorizationTest\x0060;");
}
