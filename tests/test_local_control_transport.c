#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <aclapi.h>

typedef int ProError,uiCmdCmdId,uiCmdValue;
#define PRO_TK_NO_ERROR 0
#define PRO_TK_GENERAL_ERROR -1
#define PRO_TK_BAD_INPUTS -2
#define PRO_TK_BAD_CONTEXT -3
#define PRO_TK_E_IN_USE -4
#define PRO_TK_INCOMPLETE -5
#define BWTL_LIVE_TIMER 7403
#define TEST_NATIVE_COMMAND (WM_APP+100)
#define TEST_MODEL_CONFIRM (WM_APP+102)
#define TEST_END (WM_APP+101)

static wchar_t runtime_data[MAX_PATH],runtime_settings[MAX_PATH];
static HWND test_window;
static volatile LONG native_busy,native_calls,hold_macro;
static int smart_active,markup_pending,imitation_pending,pending_workdir_action,assembly_child_pending;
static void *ai_process,*fastener_menu_pending;
static struct {HANDLE process;} markup_ai;
static HANDLE macro_ready,activation_waiting;
static HWND native_backend_window;
static HANDLE bwtl_live_file=INVALID_HANDLE_VALUE;
static wchar_t bwtl_live_path[MAX_PATH];
static volatile LONG activation_mismatch,activation_confirmations,activation_receipts;
static int ax_session_pending,store_ready;
static volatile LONG verified_activation_targets;
static wchar_t bwtl_live_goal[]=L"fixture goal";
typedef struct {int model,owner;char stamp[2];} LocalControlContext;

static ProError local_control_collect(LocalControlContext *context){
    *context=(LocalControlContext){42,42,"1"};
    return PRO_TK_NO_ERROR;
}

static void chat_task_record_target_goal(int model,const wchar_t *action,ProError error,
    const wchar_t *receipt,int unsafe,const wchar_t *goal){
    if(model==42&&*action&&!error&&!unsafe&&!wcscmp(goal,L"fixture goal")&&
       !wcscmp(receipt,L"activation verified"))InterlockedIncrement(&verified_activation_targets);
}
static int test_status;

static void trace(const char *name,int result){(void)name;(void)result;}
static void native_set_busy(int busy){(void)busy;}
static void local_control_invalidate_context(void){}
static void append_card(const wchar_t *role,const wchar_t *text){(void)role;(void)text;}
static void save_turn(const wchar_t *role,const wchar_t *text){(void)role;(void)text;}
static void bwtl_live_refresh_model(void){}
static void bwtl_live_begin(int force){(void)force;}
static void bwtl_live_emit(const wchar_t *kind,int has_code,int code,const wchar_t *text){
    (void)kind;
    (void)code;
    if(has_code&&wcsstr(text,L"activation"))InterlockedIncrement(&activation_receipts);
}
static int ax_session_activation_waiting(void){return ax_session_pending;}
static void ax_session_activation_clear(int *pending){*pending=0;}
static ProError local_control_model_activation_reply(wchar_t *out,size_t capacity){
    if(!ax_session_pending)return PRO_TK_BAD_CONTEXT;
    ax_session_activation_clear(&ax_session_pending);
    InterlockedIncrement(&activation_confirmations);
    if(InterlockedCompareExchange(&activation_mismatch,0,0)){
        wcscpy_s(out,capacity,L"activation mismatch");
        return PRO_TK_BAD_CONTEXT;
    }
    wcscpy_s(out,capacity,L"activation verified");
    return PRO_TK_NO_ERROR;
}
static int storage_directory_on_drive(const wchar_t *path,wchar_t drive){return path[0]==drive&&path[1]==L':';}
static ProError ProMacroLoad(const wchar_t *text){
    if(wcsstr(text,L"BWTLLocalModelConfirm")){
        SetEvent(activation_waiting);
        return PRO_TK_NO_ERROR;
    }
    if(InterlockedCompareExchange(&hold_macro,0,0)){SetEvent(macro_ready);return PRO_TK_NO_ERROR;}
    return PostMessageW(test_window,TEST_NATIVE_COMMAND,0,0)?PRO_TK_NO_ERROR:PRO_TK_GENERAL_ERROR;
}

static ProError local_control_dispatch(const char *action,const wchar_t *context,const wchar_t *payload,
    wchar_t *out,size_t capacity){
    InterlockedIncrement(&native_calls);
    if(wcscmp(context,L"fixture"))return PRO_TK_BAD_INPUTS;
    if(!strcmp(action,"OPEN_MODEL")||!strcmp(action,"SWITCH_MODEL")){
        if(!*payload)return PRO_TK_BAD_INPUTS;
        ax_session_pending=1;
        wcscpy_s(out,capacity,L"activation pending");
        return PRO_TK_NO_ERROR;
    }
    if(!strcmp(action,"LIST_MODELS")||!strcmp(action,"REBUILD_MODEL")){
        if(*payload)return PRO_TK_BAD_INPUTS;
        wcscpy_s(out,capacity,!strcmp(action,"LIST_MODELS")?L"M26G2081024.PRT":L"rebuild result");
        return PRO_TK_NO_ERROR;
    }
    if(!strcmp(action,"HANDOFF")){
        if(!*payload)return PRO_TK_BAD_INPUTS;
        wcscpy_s(out,capacity,payload);
        return PRO_TK_NO_ERROR;
    }
    if(strcmp(action,"EXEC"))return PRO_TK_BAD_INPUTS;
    if(swprintf_s(out,capacity,L"executed=%ls",payload)<0)return PRO_TK_BAD_INPUTS;
    return PRO_TK_NO_ERROR;
}

#include "../src/功能/local_control/local_control_transport.inc"

static HRESULT write_exact(HANDLE pipe,const void *data,DWORD size){
    DWORD used=0;
    return WriteFile(pipe,data,size,&used,NULL)&&used==size?S_OK:E_FAIL;
}

static HANDLE submit(const char *text){
    wchar_t path[128];
    swprintf_s(path,_countof(path),L"\\\\.\\pipe\\BWTL.Control.%lu.%llu.%lu",
        local_control_pid,local_control_birth,local_control_slot);
    const ULONGLONG deadline=GetTickCount64()+3000;
    HANDLE pipe=INVALID_HANDLE_VALUE;
    while(GetTickCount64()<deadline){
        pipe=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
        if(pipe!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_PIPE_BUSY)return INVALID_HANDLE_VALUE;
        (void)WaitNamedPipeW(path,100);
    }
    const DWORD length=(DWORD)strlen(text);
    if(pipe!=INVALID_HANDLE_VALUE&&(FAILED(write_exact(pipe,&length,sizeof(length)))||
       FAILED(write_exact(pipe,text,length)))){
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }
    return pipe;
}

static int reply(HANDLE pipe,char *text,DWORD capacity){
    DWORD size=0,used=0;
    if(pipe==INVALID_HANDLE_VALUE)return 0;
    const ULONGLONG deadline=GetTickCount64()+5000;
    DWORD available=0;
    while(GetTickCount64()<deadline){
        if(!PeekNamedPipe(pipe,NULL,0,NULL,&available,NULL))break;
        if(available>=sizeof(size))break;
        Sleep(15);
    }
    int received=available>=sizeof(size)&&ReadFile(pipe,&size,sizeof(size),&used,NULL)&&used==sizeof(size)&&size<capacity;
    DWORD consumed=0;
    while(received&&consumed<size){
        received=ReadFile(pipe,text+consumed,size-consumed,&used,NULL)&&used>0;
        consumed+=used;
    }
    CloseHandle(pipe);
    if(!received)return 0;
    text[size]=0;
    return 1;
}

static int owner_only(void){
    PSECURITY_DESCRIPTOR descriptor=NULL;
    PACL acl=NULL;
    ACCESS_ALLOWED_ACE *ace=NULL;
    PSID owner=NULL;
    if(GetSecurityInfo(local_control_pipe,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION|OWNER_SECURITY_INFORMATION,
       &owner,NULL,&acl,NULL,&descriptor)!=ERROR_SUCCESS)return 0;
    HANDLE token=NULL;
    union {TOKEN_USER user; BYTE bytes[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];} information;
    DWORD used=0;
    int pass=acl&&acl->AceCount==1&&GetAce(acl,0,(void **)&ace)&&
        ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE&&
        OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&
        GetTokenInformation(token,TokenUser,information.bytes,sizeof(information),&used)&&
        EqualSid((PSID)&ace->SidStart,information.user.User.Sid);
    if(token)CloseHandle(token);
    LocalFree(descriptor);
    return pass;
}

static int check(int pass,const char *name){
    printf("%s=%d\n",name,!!pass);
    fflush(stdout);
    return pass;
}

static int check_activation(const char *frame,int mismatch,LONG prior){
    ResetEvent(activation_waiting);
    InterlockedExchange(&activation_mismatch,mismatch);
    HANDLE pipe=submit(frame);
    if(pipe==INVALID_HANDLE_VALUE)return 0;
    if(WaitForSingleObject(activation_waiting,3000)!=WAIT_OBJECT_0){
        CloseHandle(pipe);
        return 0;
    }
    Sleep(250);
    DWORD available=0;
    const int held=PeekNamedPipe(pipe,NULL,0,NULL,&available,NULL)&&!available&&
        InterlockedCompareExchange(&activation_confirmations,0,0)==prior&&
        InterlockedCompareExchange(&activation_receipts,0,0)==prior;
    if(!PostMessageW(test_window,TEST_MODEL_CONFIRM,0,0)){
        CloseHandle(pipe);
        return 0;
    }
    char body[256];
    return reply(pipe,body,_countof(body))&&held&&
        !strcmp(body,mismatch?"BWTL/1\n-3\nactivation mismatch":"BWTL/1\n0\nactivation verified")&&
        InterlockedCompareExchange(&activation_confirmations,0,0)==prior+1&&
        InterlockedCompareExchange(&activation_receipts,0,0)==prior+1&&
        InterlockedCompareExchange(&verified_activation_targets,0,0)==1;
}

static unsigned __stdcall exercise(void *unused){
    int pass=0;
    char body[256];
    if(!check(owner_only(),"SINGLE_OWNER_ACE"))goto done;
    if(!check(reply(submit("BWTL/1\nHELLO\n\n"),body,_countof(body))&&
        !strncmp(body,"BWTL/1\n0\npid=",13)&&strstr(body,"\nprocessStart=")&&strstr(body,"\nslot="),
        "HELLO_IDENTITY"))goto done;
    if(!check(reply(submit("BWTL/1\nEXEC\nfixture\none\nextra"),body,_countof(body))&&
        !strncmp(body,"BWTL/1\n-2\n",10)&&!native_calls,"MULTILINE_REJECTED"))goto done;

    if(!check(reply(submit("BWTL/1\nEXEC\nfixture\nfirst"),body,_countof(body))&&
        !strcmp(body,"BWTL/1\n0\nexecuted=first")&&native_calls==1,"KERNEL_DISPATCH"))goto done;

    /* Let a request own a queued native macro and leave before it can execute. */
    ResetEvent(macro_ready);
    InterlockedExchange(&hold_macro,1);
    HANDLE abandoned=submit("BWTL/1\nEXEC\nfixture\nabandoned");
    if(abandoned==INVALID_HANDLE_VALUE)goto done;
    const DWORD queued=WaitForSingleObject(macro_ready,3000);
    CloseHandle(abandoned);
    if(!check(queued==WAIT_OBJECT_0,"DEFERRED_MACRO_READY"))goto done;

    /* The next command queues before the old macro callback. The old callback
       must never use the different request published on this connection. */
    HANDLE next=submit("BWTL/1\nEXEC\nfixture\nsecond");
    if(next==INVALID_HANDLE_VALUE)goto done;
    InterlockedExchange(&hold_macro,0);
    PostMessageW(test_window,TEST_NATIVE_COMMAND,0,0);
    if(!check(reply(next,body,_countof(body))&&!strcmp(body,"BWTL/1\n0\nexecuted=second")&&native_calls==2,
        "ABANDONED_REQUEST_NEVER_EXECUTED"))goto done;

    InterlockedExchange(&native_busy,4);
    HANDLE waits=submit("BWTL/1\nEXEC\nfixture\nqueued");
    if(waits==INVALID_HANDLE_VALUE)goto done;
    Sleep(250);
    const int delayed=native_calls==2;
    InterlockedExchange(&native_busy,0);
    if(!check(reply(waits,body,_countof(body))&&!strcmp(body,"BWTL/1\n0\nexecuted=queued")&&
        delayed&&native_calls==3,"WAITED_FOR_EXISTING_WORK"))goto done;
    if(!check(reply(submit("BWTL/1\nLIST_MODELS\nfixture\n"),body,_countof(body))&&
        !strcmp(body,"BWTL/1\n0\nM26G2081024.PRT"),"LIST_MODELS_ACCEPTED"))goto done;
    if(!check(reply(submit("BWTL/1\nHANDOFF\nfixture\ncontinue geometry"),body,_countof(body))&&
        !strcmp(body,"BWTL/1\n0\ncontinue geometry"),"HANDOFF_ACCEPTED"))goto done;
    if(!check(reply(submit("BWTL/1\nREBUILD_MODEL\nfixture\n"),body,_countof(body))&&
        !strcmp(body,"BWTL/1\n0\nrebuild result"),"REBUILD_MODEL_ACCEPTED"))goto done;
    const LONG before_malformed=InterlockedCompareExchange(&native_calls,0,0);
    if(!check(reply(submit("BWTL/1\nOPEN_MODEL\nfixture\nE:\\fixture.prt\nextra"),body,_countof(body))&&
        !strncmp(body,"BWTL/1\n-2\n",10)&&InterlockedCompareExchange(&native_calls,0,0)==before_malformed,
        "MALFORMED_OPEN_REJECTED"))goto done;
    if(!check(check_activation("BWTL/1\nOPEN_MODEL\nfixture\nE:\\fixture.prt",0,0),
        "OPEN_WAITED_FOR_ACTIVE_WINDOW_CONFIRMATION"))goto done;
    if(!check(check_activation("BWTL/1\nSWITCH_MODEL\nfixture\nM26G2081024.PRT",1,1),
        "SWITCH_FAILED_ACTIVE_WINDOW_VERIFICATION"))goto done;
    pass=1;
done:
    test_status=pass;
    PostMessageW(test_window,TEST_END,0,0);
    return 0;
}

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==LOCAL_CONTROL_MESSAGE||(message==WM_TIMER&&w==LOCAL_CONTROL_TIMER)){
        local_control_schedule();
        return 0;
    }
    if(message==TEST_NATIVE_COMMAND){local_control_gui_command(0,NULL,NULL);return 0;}
    if(message==TEST_MODEL_CONFIRM){local_control_model_confirm(0,NULL,NULL);return 0;}
    if(message==WM_TIMER&&w==LOCAL_CONTROL_MODEL_TIMER){local_control_model_timeout();return 0;}
    if(message==TEST_END){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}

int wmain(void){
    if(!GetCurrentDirectoryW(_countof(runtime_data),runtime_data)||runtime_data[0]!=L'E')return 2;
    swprintf_s(runtime_settings,_countof(runtime_settings),L"%ls\\test.ini",runtime_data);
    WNDCLASSW cls={0};
    cls.lpfnWndProc=procedure;
    cls.hInstance=GetModuleHandleW(NULL);
    cls.lpszClassName=L"BWTL_Local_Control_Test";
    if(!RegisterClassW(&cls))return 2;
    test_window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,NULL,cls.hInstance,NULL);
    native_backend_window=test_window;
    macro_ready=CreateEventW(NULL,TRUE,FALSE,NULL);
    activation_waiting=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!test_window||!macro_ready||!activation_waiting||!local_control_start(test_window))return 2;
    HANDLE client=(HANDLE)_beginthreadex(NULL,0,exercise,NULL,0,NULL);
    if(!client){local_control_stop();return 2;}
    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)>0)DispatchMessageW(&msg);
    WaitForSingleObject(client,3000);
    local_control_stop();
    CloseHandle(client);
    CloseHandle(macro_ready);
    CloseHandle(activation_waiting);
    DestroyWindow(test_window);
    check(GetFileAttributesW(local_control_descriptor)==INVALID_FILE_ATTRIBUTES,"DESCRIPTOR_REMOVED_ON_EXIT");
    return test_status?0:1;
}

