#define user_initialize task_context_original_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static int task_context_read_journal(wchar_t *content,size_t capacity){
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,_countof(path),L"%ls\\任务接续.txt",store_session_root)<0)return 0;
    FILE *file=_wfopen(path,L"rt, ccs=UTF-8");
    if(!file)return 0;
    size_t length=0;
    wint_t value;
    while(length+1<capacity&&(value=fgetwc(file))!=WEOF)content[length++]=(wchar_t)value;
    const int ok=length>0&&!ferror(file);
    fclose(file);
    content[length]=0;
    return ok;
}

static int task_context_fixture(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    FILE *log=_wfopen(L"task-context-native.log",L"wt, ccs=UTF-8");
    if(!log){ProEngineerEnd();return 0;}
    const char *stage="template";
    ProMdlName name;
    ProMdl total=NULL,assembly=NULL,part=NULL,a=NULL,p=NULL,model=NULL;
    ProAsmcomp placement;
    int occurrence=0;
    ProPath home,other;
    wchar_t messages[600]=L"";
    PartText out={messages,_countof(messages),0};
    int success=1;
#define CHECK(label,test) do{int pass=!!(test);fwprintf(log,L"%hs=%d\n",label,pass);fflush(log);if(!pass){success=0;goto done;}}while(0)
    CHECK("E_DRIVE_PROJECT",chat_task_project_path(home)&&towupper(home[0])==L'E');
    CHECK("NEW_PROCESS_EMPTY",chat_task_resolve_pair(&a,&p,&occurrence)==PRO_TK_E_NOT_FOUND&&
        chat_task_last_followup(&model)==PRO_TK_E_NOT_FOUND);
    model_background_mode=1;
    CHECK("REAL_NAMED_TOTAL",!model_create_empty_named(3,L"M26A1400000V00",name,&stage)&&
        !ProMdlnameInit(name,PRO_MDLFILE_ASSEMBLY,&total));
    chat_task_record_company_assembly(total,L"实测样件：总装 M26A1400000V00.ASM 已创建并回读。");
    CHECK("REAL_ASM_PRT_PAIR",!model_create_empty_named(3,L"AI_CONTEXT_PAIR_V00",name,&stage)&&
        !ProMdlnameInit(name,PRO_MDLFILE_ASSEMBLY,&assembly)&&
        !ax_new_part_in(assembly,L"AI_CONTEXT_PAIR_V00",1,&part,&placement,&out));
    chat_task_record_pair(assembly,part,placement.id,L"实测样件：同号装配与默认定位的钣金子件已核对。");

    smart_begin(L"为什么是刚才这个图号？");
    smart_finish();
    CHECK("FOLLOWUP_PAIR_AFTER_CHAT",!chat_task_resolve_pair(&a,&p,&occurrence)&&a==assembly&&p==part&&occurrence==placement.id);
    CHECK("EXACT_TYPED_LATEST",!chat_task_last_followup(&model)&&model==part&&!chat_task_followup_pending());
    CHECK("TOTAL_ANCHOR_SURVIVES_PAIR",!chat_task_company_assembly(&model)&&model==total);
    chat_task_record_company_assembly(assembly,L"普通装配的回执。");
    CHECK("UNRELATED_ASSEMBLY_CANNOT_REPLACE_PROJECT_NUMBER",!chat_task_company_assembly(&model)&&model==total);

    chat_task_record_target(part,L"模型几何核对",PRO_TK_BAD_INPUTS,L"当前样件的输入值尚不完整。",0);
    CHECK("SAFE_FAILURE_IS_CONTINUABLE",chat_task_followup_pending()&&!chat_task_last_followup(&model)&&model==part);
    chat_task_record_target(part,L"需复核模型状态",PRO_TK_BAD_CONTEXT,L"状态回读待确认。",1);
    CHECK("UNSAFE_FAILURE_NOT_CONTINUED",!chat_task_followup_pending()&&chat_task_last_followup(&model)==PRO_TK_BAD_CONTEXT);

    CHECK("MAKE_SECOND_E_PROJECT",swprintf_s(other,PRO_PATH_SIZE,L"%ls\\other-project",home)>=0&&
        CreateDirectoryW(other,NULL)&&!ProDirectoryChange(other));
    CHECK("OTHER_PROJECT_CANNOT_RESOLVE",chat_task_resolve_pair(&a,&p,&occurrence)==PRO_TK_E_NOT_FOUND&&
        chat_task_company_assembly(&model)==PRO_TK_E_NOT_FOUND);
    CHECK("ORIGINAL_PROJECT_RESUMES",!ProDirectoryChange(home)&&!chat_task_resolve_pair(&a,&p,&occurrence)&&a==assembly&&p==part);

    static wchar_t journal[8000];
    CHECK("E_SESSION_AUDIT_WRITTEN",towupper(store_session_root[0])==L'E'&&
        task_context_read_journal(journal,_countof(journal))&&
        wcsstr(journal,L"M26A1400000V00.ASM")&&wcsstr(journal,L"AI_CONTEXT_PAIR_V00.PRT")&&
        wcsstr(journal,L"状态存疑 1")&&wcsstr(journal,L"项目目录"));
    chat_task_context_reset();
    CHECK("NEW_CHAT_DROPS_LIVE_CONTEXT",chat_task_resolve_pair(&a,&p,&occurrence)==PRO_TK_E_NOT_FOUND);
    CHECK("E_AUDIT_RETAINED",task_context_read_journal(journal,_countof(journal))&&wcsstr(journal,L"M26A1400000V00.ASM"));
done:
    fwprintf(log,L"DETAIL=%hs\n%ls\nTASK_CONTEXT_NATIVE_PASS=%d\n",stage,messages,success);
    fclose(log);
    ProEngineerEnd();
    return 0;
#undef CHECK
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t report[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=task_context_original_initialize(argc,argv,version,build,report);
    uiCmdCmdId command;
    if(!error)error=ProCmdActionAdd("BWTLTaskContextFixture",task_context_fixture,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&command);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLTaskContextFixture\x0060;");
}
