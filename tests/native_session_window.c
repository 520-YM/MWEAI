#define user_initialize bwtl_window_production_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

static const wchar_t fixture_name[]=L"AI_ASM_261119_133845_1V00";
static const wchar_t memory_name[]=L"BWTL_MEMORY_ONLY_ASSEMBLY";
static const wchar_t fixture_tick_command[]=L"~ Command \x0060" L"BWTLWindowFixtureTick\x0060;";
static ProMdl assembly,homonym,standalone,memory_assembly;
static ProMdlName standalone_name;
static ProWVerstamp stamps[4];
static ProPath directory;
static ProBoolean dirty_initial[4];
static FILE *log_file;
static int tick_requested,phase,failures;
static unsigned saved_files_count;
static ULONGLONG saved_files_digest;
static ULONGLONG deadline;
static unsigned checked;

static int check(int condition,const wchar_t *label){
    checked++;
    if(!condition)failures++;
    fwprintf(log_file,L"%ls=%d\n",label,!!condition);
    fflush(log_file);
    return condition;
}

static int fixture_finish(void){
    KillTimer(native_backend_window,6681);
    for(int i=0;i<4;i++)if(stamps[i])ProVerstampFree(&stamps[i]);
    if(log_file){
        fwprintf(log_file,L"CHECKS=%u FAILURES=%d\nSESSION_WINDOW_NATIVE_PASS=%d\n",
            checked,failures,!failures);
        fclose(log_file);
        log_file=NULL;
    }
    ProEngineerEnd();
    return 0;
}

static void CALLBACK fixture_timer(HWND window,UINT message,UINT_PTR id,DWORD now){
    (void)window;(void)message;(void)id;(void)now;
    if(!tick_requested&&!ProMacroLoad((wchar_t *)fixture_tick_command))tick_requested=1;
}

static ULONGLONG fixture_files(const wchar_t *folder,unsigned *count){
    const wchar_t *patterns[]={L"*.prt*",L"*.asm*"};
    ULONGLONG digest=0;
    *count=0;
    for(int i=0;i<_countof(patterns);i++){
        ProPath path;
        WIN32_FIND_DATAW entry;
        if(swprintf_s(path,_countof(path),L"%ls\\%ls",folder,patterns[i])<0)return 0;
        HANDLE find=FindFirstFileW(path,&entry);
        if(find==INVALID_HANDLE_VALUE)continue;
        do{
            if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
            ULONGLONG hash=14695981039346656037ULL;
            for(const wchar_t *p=entry.cFileName;*p;p++)hash=(hash^*p)*1099511628211ULL;
            hash^=((ULONGLONG)entry.nFileSizeHigh<<32)|entry.nFileSizeLow;
            hash*=1099511628211ULL;
            hash^=((ULONGLONG)entry.ftLastWriteTime.dwHighDateTime<<32)|entry.ftLastWriteTime.dwLowDateTime;
            digest^=hash;
            (*count)++;
        }while(FindNextFileW(find,&entry));
        FindClose(find);
    }
    return digest;
}

static int fixture_same_state(void){
    const ProMdl values[]={assembly,homonym,standalone,memory_assembly};
    ProPath current;
    unsigned count;
    if(ProDirectoryCurrentGet(current)||_wcsicmp(current,directory)||
       fixture_files(current,&count)!=saved_files_digest||count!=saved_files_count)return 0;
    for(int i=0;i<_countof(values);i++){
        ProWVerstamp after=NULL;
        ProBoolean dirty;
        int same=!ProMdlVerstampGet(values[i],&after)&&ProVerstampEqual(after,stamps[i])&&
            !ProMdlModificationVerify(values[i],&dirty)&&dirty==dirty_initial[i];
        if(after)ProVerstampFree(&after);
        if(!same)return 0;
    }
    return 1;
}

static void send_chat(const wchar_t *text){
    history[0]=0;
    native_prompt_set(text);
    run_local(NULL,NULL,NULL);
}

static int fixture_step(void){
    if(GetTickCount64()>deadline){check(0,L"TIMELY_COMPLETION");return fixture_finish();}
    if(smart_active||ai_process)return 0;
    ProMdl current=NULL;
    ProMdlCurrentGet(&current);
    int window=-1;
    ProWindowCurrentGet(&window);
    wchar_t request[256];
    int original_window;
    if(phase==0){
        if(current!=assembly)return 0;
        model_background_mode=1;
        ProMdlName name;
        const char *stage=NULL;
        ProError error=model_create_empty_named(3,memory_name,name,&stage);
        if(!error)error=ProMdlnameInit(name,PRO_MDLFILE_ASSEMBLY,&memory_assembly);
        model_background_mode=0;
        if(!error)error=ProMdlWindowGet(assembly,&original_window);
        wchar_t text[1200]=L"";
        PartText out={text,_countof(text),0};
        ProAsmcomp occurrence;
        if(!error)error=ax_new_part_in(assembly,fixture_name,1,&homonym,&occurrence,&out);
        fwprintf(log_file,L"CREATE_HOMONYM Creo %d %ls\n",error,text);
        if(!check(!error&&homonym&&homonym!=assembly,L"SAME_NAME_ASM_AND_PRT_LOADED"))return fixture_finish();
        const wchar_t *where=L"";
        const ShPlateSize size={45,32,1.5};
        error=sh_plate_create(&size,standalone_name,&where);
        if(!error)error=ProMdlnameInit(standalone_name,PRO_MDLFILE_PART,&standalone);
        if(!check(!error&&standalone&&standalone!=homonym,L"INDEPENDENT_SHEET_CREATED"))return fixture_finish();
        phase++;
        return 0;
    }
    if(phase==1){
        if(current!=standalone)return 0;
        const ProMdl values[]={assembly,homonym,standalone,memory_assembly};
        ProError error=ProDirectoryCurrentGet(directory);
        if(!error)saved_files_digest=fixture_files(directory,&saved_files_count);
        for(int i=0;!error&&i<_countof(values);i++){
            error=ProMdlVerstampGet(values[i],&stamps[i]);
            if(!error)error=ProMdlModificationVerify(values[i],&dirty_initial[i]);
        }
        AxScan *scan=NULL;
        if(!error)error=ax_scan(assembly,0,&scan);
        int unrelated=!error;
        if(scan){for(int i=0;i<scan->count;i++)if(scan->occurrences[i].model==standalone)unrelated=0;}
        free(scan);
        if(!check(!error&&unrelated&&dirty_initial[2]&&fixture_same_state(),L"SOURCE_UNSAVED_AND_UNRELATED"))return fixture_finish();
        check(ProMdlWindowGet(memory_assembly,&original_window)==PRO_TK_E_NOT_FOUND,L"MEMORY_ASM_HAS_NO_WINDOW");
        send_chat(L"读取会话模型与窗口清单");
        fwprintf(log_file,L"%ls\n",history);
        check(wcsstr(history,L"AI_ASM_261119_133845_1V00.ASM | 窗口 ID ")&&
            wcsstr(history,L"AI_ASM_261119_133845_1V00.PRT | 已加载，无独立窗口")&&
            wcsstr(history,L"BWTL_MEMORY_ONLY_ASSEMBLY.ASM | 已加载，无独立窗口")&&
            wcsstr(history,L" | 活动 | 有未保存改动"),L"INVENTORY_SEPARATES_TYPES_WINDOWS_AND_ACTIVE");
        send_chat(L"回到父装配");
        fwprintf(log_file,L"%ls\n",history);
        check(wcsstr(history,L"实际包含")&&!ax_session_activation_waiting()&&fixture_same_state(),
            L"STRICT_PARENT_REQUEST_NEVER_GUESSES_UNRELATED_ASSEMBLY");
        send_chat(L"回总装");
        phase=10;
        return 0;
    }
    if(phase==10){
        fwprintf(log_file,L"%ls\n",history);
        if(!check(current==assembly&&request_snapshot.model==assembly&&fixture_same_state(),
            L"UNRELATED_PRT_TO_SOLE_OPEN_ASSEMBLY_ON_GENERIC_RETURN"))return fixture_finish();
        if(!check(!ProMdlWindowGet(standalone,&original_window),L"ORIGINAL_PRT_WINDOW_STILL_OPEN_AFTER_GENERIC_RETURN"))return fixture_finish();
        swprintf_s(request,_countof(request),L"激活模型窗口 窗口 ID %d",original_window);
        send_chat(request);
        phase=11;
        return 0;
    }
    if(phase==11){
        if(!check(current==standalone&&fixture_same_state(),L"BACK_TO_INDEPENDENT_SOURCE_BEFORE_EXACT_TEST"))return fixture_finish();
        send_chat(L"回到 AI_ASM_261119_133845_1V00.ASM");
        phase=2;
        return 0;
    }
    if(phase==2){
        fwprintf(log_file,L"%ls\n",history);
        if(!check(current==assembly&&request_snapshot.model==assembly&&request_snapshot.type==PRO_MDL_ASSEMBLY&&
            wcsstr(history,L"已激活 AI_ASM_261119_133845_1V00.ASM，窗口 ID ")&&fixture_same_state(),
            L"UNRELATED_PART_TO_EXACT_EXISTING_ASM_AND_NEW_BINDING"))return fixture_finish();
        send_chat(L"读取装配树");
        fwprintf(log_file,L"%ls\n",history);
        check(wcsstr(history,L"组件实例 1")&&wcsstr(history,L"零件 AI_ASM_261119_133845_1V00"),
            L"NATIVE_ASSEMBLY_TREE_SHOWS_SAME_NAME_CHILD");
        send_chat(L"激活模型窗口 BWTL_MEMORY_ONLY_ASSEMBLY.ASM");
        phase++;
        return 0;
    }
    if(phase==3){
        fwprintf(log_file,L"%ls\n",history);
        if(!check(current==memory_assembly&&request_snapshot.model==memory_assembly&&
            !ProMdlWindowGet(memory_assembly,&original_window)&&original_window==window&&fixture_same_state(),
            L"OPEN_EXISTING_LOADED_ASM_WITHOUT_WINDOW"))return fixture_finish();
        send_chat(L"激活模型窗口 AI_ASM_261119_133845_1V00");
        fwprintf(log_file,L"%ls\n",history);
        check(wcsstr(history,L"同时有 AI_ASM_261119_133845_1V00.ASM 和 AI_ASM_261119_133845_1V00.PRT")&&
            !ax_session_activation_waiting()&&fixture_same_state(),L"AMBIGUOUS_UNTYPED_NAME_REJECTED");
        send_chat(L"激活模型窗口 BWTL_DOES_NOT_EXIST.PRT");
        fwprintf(log_file,L"%ls\n",history);
        check(wcsstr(history,L"未加载 BWTL_DOES_NOT_EXIST.PRT")&&!ax_session_activation_waiting()&&fixture_same_state(),
            L"UNKNOWN_FILE_IS_NOT_RETRIEVED_OR_CREATED");
        if(!check(!ProMdlWindowGet(standalone,&original_window),L"ORIGINAL_UNSAVED_WINDOW_STILL_EXISTS"))return fixture_finish();
        swprintf_s(request,_countof(request),L"激活模型窗口 窗口 ID %d",original_window);
        send_chat(request);
        phase++;
        return 0;
    }
    if(phase==4){
        fwprintf(log_file,L"%ls\n",history);
        check(current==standalone&&request_snapshot.model==standalone&&
            request_snapshot.type==PRO_MDL_PART&&fixture_same_state(),L"SELECT_BY_WINDOW_ID_AND_UNSAVED_STATE_UNCHANGED");
        send_chat(L"回到装配体");
        fwprintf(log_file,L"%ls\n",history);
        ProMdl after=NULL;
        check(wcsstr(history,L"有多个已打开的装配")&&wcsstr(history,L"AI_ASM_261119_133845_1V00.ASM")&&
            wcsstr(history,L"BWTL_MEMORY_ONLY_ASSEMBLY.ASM")&&!ProMdlCurrentGet(&after)&&
            after==standalone&&!ax_session_activation_waiting()&&fixture_same_state(),
            L"GENERIC_RETURN_WITH_MULTIPLE_ASMS_KEEPS_ORIGINAL_PRT_ACTIVE");
        send_chat(L"回到装配 AI_ASM_261119_133845_1V00，然后读取装配树");
        phase++;
        return 0;
    }
    fwprintf(log_file,L"%ls\n",history);
    if(phase==5){
        if(!check(current==assembly&&request_snapshot.model==assembly&&wcsstr(history,L"组件实例 1")&&
            fixture_same_state(),L"COMPOUND_ACTIVATION_THEN_NATIVE_TREE"))return fixture_finish();
        send_chat(L"打开子零件 AI_ASM_261119_133845_1V00.PRT");
        phase=6;
        return 0;
    }
    if(phase==6){
        if(!check(current==homonym&&fixture_same_state(),L"REAL_CHILD_PRT_IS_ACTIVE_BEFORE_PARENT_RETURN"))return fixture_finish();
        send_chat(L"回到父装配");
        phase=7;
        return 0;
    }
    check(current==assembly&&wcsstr(history,L"已回到装配 AI_ASM_261119_133845_1V00.ASM")&&
        fixture_same_state(),L"EXISTING_PARENT_RELATION_WINS_WITH_MULTIPLE_OPEN_ASMS");
    return fixture_finish();
}

static int fixture_tick(uiCmdCmdId command,uiCmdValue *value,void *data){
    (void)command;(void)value;(void)data;
    int result=fixture_step();
    tick_requested=0;
    return result;
}

static int fixture_begin(uiCmdCmdId command,uiCmdValue *value,void *data){
    (void)command;(void)value;(void)data;
    log_file=_wfopen(L"session-window-native.log",L"wt, ccs=UTF-8");
    if(!log_file){ProEngineerEnd();return 0;}
    const char *stage=NULL;
    ProMdlName name;
    ProError error=model_create_empty_named(3,fixture_name,name,&stage);
    if(!error)error=ProMdlnameInit(name,PRO_MDLFILE_ASSEMBLY,&assembly);
    if(!check(!error,L"CREATE_PRIVATE_NUMBERED_ASM"))return fixture_finish();
    if(!check(native_ui_open_threaded(NULL)!=0,L"CREATE_PRIVATE_BWTL_CHAT"))return fixture_finish();
    deadline=GetTickCount64()+35000;
    if(!check(SetTimer(native_backend_window,6681,250,fixture_timer)!=0,L"NATIVE_FIXTURE_TIMER"))return fixture_finish();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    ProError error=bwtl_window_production_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId cmd;
    if(!error)error=ProCmdActionAdd("BWTLWindowFixtureBegin",fixture_begin,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!error)error=ProCmdActionAdd("BWTLWindowFixtureTick",fixture_tick,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLWindowFixtureBegin\x0060;");
}
