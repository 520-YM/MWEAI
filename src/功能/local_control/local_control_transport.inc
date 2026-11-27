#include <sddl.h>
#include <process.h>

#define LOCAL_CONTROL_MESSAGE (WM_APP+74)
#define LOCAL_CONTROL_TIMER 7401
#define LOCAL_CONTROL_FRAME_LIMIT 65536u
#define LOCAL_CONTROL_BODY_LIMIT 20000u
#define LOCAL_CONTROL_QUEUE_MS 20000u
#define LOCAL_CONTROL_RESIDENT_IDLE_MS 90000u
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
#define LOCAL_CONTROL_PAIR_TIMER 7402
#define LOCAL_CONTROL_PAIR_TIMEOUT_MS 35000u
#endif

typedef enum {
    LC_IDLE,
    LC_PENDING,
    LC_RUNNING,
    LC_DONE
} LocalControlState;

typedef struct {
    LocalControlState state;
    ULONGLONG ticket, deadline;
    LONG64 client;
    char action[24];
    wchar_t context[129], payload[8192];
    ProError result;
    wchar_t body[LOCAL_CONTROL_BODY_LIMIT];
} LocalControlRequest;

typedef struct {
    HANDLE pipe, worker;
    int mode;
} LocalControlChannel;

enum { LC_CHANNEL_CONTROL, LC_CHANNEL_RESIDENT, LC_CHANNEL_EVENTS, LC_CHANNEL_COUNT };

static SRWLOCK local_control_lock=SRWLOCK_INIT;
static LocalControlRequest local_control_request;
static HWND local_control_window;
static HANDLE local_control_request_pipe,local_control_stop_event,local_control_completion,local_control_submission;
static LocalControlChannel local_control_channels[LC_CHANNEL_COUNT];
static DWORD local_control_pid,local_control_session,local_control_slot;
static ULONGLONG local_control_birth,local_control_macro,local_control_serial;
static LONG64 local_control_connection_serial;
static volatile LONG64 local_control_resident_client;
static wchar_t local_control_descriptor[MAX_PATH];
static int local_control_published;
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
enum { LC_WORKFLOW_IDLE, LC_WORKFLOW_PAIR, LC_WORKFLOW_PANEL };
static int local_control_active_workflow;
static ULONGLONG local_control_workflow_deadline;
#endif

#include "local_control_com.inc"
static PSECURITY_DESCRIPTOR local_control_security(void);
#include "local_control_com_startup.inc"

static DWORD local_control_remaining(ULONGLONG deadline){
    if(!deadline)return INFINITE;
    const ULONGLONG now=GetTickCount64();
    return now<deadline?(DWORD)min(deadline-now,0xfffffffeu):0;
}

static BOOL local_control_overlapped(HANDLE pipe,OVERLAPPED *op,DWORD *transferred,ULONGLONG deadline){
    const HANDLE signals[]={local_control_stop_event,op->hEvent};
    const DWORD waited=WaitForMultipleObjects(_countof(signals),signals,FALSE,local_control_remaining(deadline));
    if(waited==WAIT_OBJECT_0+1)return GetOverlappedResult(pipe,op,transferred,FALSE);
    CancelIoEx(pipe,op);
    (void)GetOverlappedResult(pipe,op,transferred,TRUE);
    return FALSE;
}

static BOOL local_control_transfer(HANDLE pipe,HANDLE event,void *buffer,DWORD length,BOOL write,ULONGLONG deadline){
    DWORD done=0;
    while(done<length){
        OVERLAPPED op={0};
        DWORD moved=0;
        op.hEvent=event;
        ResetEvent(event);
        BYTE *bytes=(BYTE *)buffer;
        const BOOL ready=write?WriteFile(pipe,bytes+done,length-done,&moved,&op):
            ReadFile(pipe,bytes+done,length-done,&moved,&op);
        if(!ready&&(GetLastError()!=ERROR_IO_PENDING||!local_control_overlapped(pipe,&op,&moved,deadline)))return FALSE;
        if(!moved)return FALSE;
        done+=moved;
    }
    return TRUE;
}

static BOOL local_control_connect(HANDLE pipe,HANDLE event){
    OVERLAPPED op={0};
    op.hEvent=event;
    ResetEvent(event);
    if(ConnectNamedPipe(pipe,&op))return TRUE;
    const DWORD error=GetLastError();
    if(error==ERROR_PIPE_CONNECTED)return TRUE;
    DWORD used=0;
    return error==ERROR_IO_PENDING&&local_control_overlapped(pipe,&op,&used,0);
}

static BOOL local_control_client_live(void){
    DWORD queued=0;
    /* Submit one frame at a time. A resident client stays connected between replies. */
    return local_control_request_pipe&&PeekNamedPipe(local_control_request_pipe,NULL,0,NULL,&queued,NULL)&&!queued;
}

static int local_control_split(wchar_t *decoded,LocalControlRequest *request){
    wchar_t *verb=wcschr(decoded,L'\n');
    if(!verb)return 0;
    *verb++=0;
    if(wcscmp(decoded,L"BWTL/1"))return 0;
    wchar_t *context=wcschr(verb,L'\n');
    if(!context)return 0;
    *context++=0;
    wchar_t *payload=wcschr(context,L'\n');
    if(!payload)return 0;
    *payload++=0;
    if(wcschr(payload,L'\n')||wcschr(context,L'\r')||wcschr(payload,L'\r')||
       wcslen(context)>=_countof(request->context)||wcslen(payload)>=_countof(request->payload))return 0;
    static const struct {const wchar_t *wide;const char *ascii;} verbs[]={
        {L"HELLO","HELLO"},{L"STATUS","STATUS"},{L"READ","READ"},{L"EXEC","EXEC"},{L"SAVE","SAVE"},
        {L"HANDOFF","HANDOFF"},{L"OPEN_MODEL","OPEN_MODEL"},{L"OPEN_BACKGROUND","OPEN_BACKGROUND"},{L"SWITCH_MODEL","SWITCH_MODEL"},
        {L"LIST_MODELS","LIST_MODELS"},{L"REBUILD_MODEL","REBUILD_MODEL"},{L"REFERENCE_DESIGN","REFERENCE_DESIGN"},
        {L"COM_IDENT","COM_IDENT"},{L"COM_PREPARE","COM_PREPARE"},{L"COM_FINISH","COM_FINISH"},
        {L"COM_CANCEL","COM_CANCEL"},{L"COM_CHECK_LEASE","COM_CHECK_LEASE"}
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
        ,{L"CREATE_PAIR","CREATE_PAIR"},{L"PRESSFIT","PRESSFIT"},{L"BUILD_PANEL","BUILD_PANEL"},{L"REV_PANEL","REV_PANEL"}
        ,{L"GROUP_HOLES","GROUP_HOLES"},{L"GROUP_FIT","GROUP_FIT"},{L"REV_GROUP","REV_GROUP"},{L"FIT_WORK","FIT_WORK"}
#endif
    };
    for(size_t i=0;i<_countof(verbs);i++)if(!wcscmp(verb,verbs[i].wide)){
        strcpy_s(request->action,_countof(request->action),verbs[i].ascii);
        wcscpy_s(request->context,_countof(request->context),context);
        wcscpy_s(request->payload,_countof(request->payload),payload);
        return 1;
    }
    return 0;
}

static void local_control_finish_locked(ProError code,const wchar_t *message){
    local_control_request.result=code;
    if(message)wcsncpy_s(local_control_request.body,_countof(local_control_request.body),message,_TRUNCATE);
    local_control_request.state=LC_DONE;
    SetEvent(local_control_completion);
}

static BOOL local_control_wait_native(void){
    const HANDLE signals[]={local_control_stop_event,local_control_completion};
    for(;;){
        const DWORD result=WaitForMultipleObjects(_countof(signals),signals,FALSE,75);
        if(result==WAIT_OBJECT_0+1)return TRUE;
        if(result==WAIT_OBJECT_0||result==WAIT_FAILED)return FALSE;
        AcquireSRWLockExclusive(&local_control_lock);
        if(local_control_request.state==LC_PENDING){
            if(!local_control_client_live()){
                local_control_finish_locked(PRO_TK_BAD_CONTEXT,L"请求在执行前已断开，Creo 未操作。");
                ReleaseSRWLockExclusive(&local_control_lock);
                return FALSE;
            }
            if(GetTickCount64()>=local_control_request.deadline)
                local_control_finish_locked(PRO_TK_E_IN_USE,L"Creo 当前忙或等待窗口操作，排队超时，原生指令未开始。");
        }
        ReleaseSRWLockExclusive(&local_control_lock);
    }
}

static BOOL local_control_native(void){
    ResetEvent(local_control_completion);
    AcquireSRWLockExclusive(&local_control_lock);
    local_control_request.ticket=++local_control_serial;
    local_control_request.deadline=GetTickCount64()+LOCAL_CONTROL_QUEUE_MS;
    local_control_request.state=LC_PENDING;
    const WPARAM ticket=(WPARAM)local_control_request.ticket;
    ReleaseSRWLockExclusive(&local_control_lock);
    if(!PostMessageW(local_control_window,LOCAL_CONTROL_MESSAGE,ticket,0)){
        AcquireSRWLockExclusive(&local_control_lock);
        local_control_finish_locked(PRO_TK_GENERAL_ERROR,L"无法向 Creo 窗口提交请求，原生指令未开始。");
        ReleaseSRWLockExclusive(&local_control_lock);
        return TRUE;
    }
    return local_control_wait_native();
}

static BOOL local_control_frame(HANDLE pipe,HANDLE event,char *wire,DWORD length){
    const ULONGLONG deadline=GetTickCount64()+5000;
    return length&&local_control_transfer(pipe,event,&length,sizeof(length),TRUE,deadline)&&
        local_control_transfer(pipe,event,wire,length,TRUE,deadline);
}

static BOOL local_control_response(HANDLE pipe,HANDLE event,char *wire,wchar_t *response){
    int chars=swprintf_s(response,LOCAL_CONTROL_BODY_LIMIT+40u,L"BWTL/1\n%d\n%ls",
        local_control_request.result,local_control_request.body);
    if(chars<0)return FALSE;
    const int encoded=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,response,chars,wire,LOCAL_CONTROL_FRAME_LIMIT,NULL,NULL);
    return encoded>0&&local_control_frame(pipe,event,wire,(DWORD)encoded);
}

static int local_control_read_frame(HANDLE pipe,HANDLE event,char *wire,DWORD idle,DWORD *length){
    if(!local_control_transfer(pipe,event,length,sizeof(*length),FALSE,GetTickCount64()+idle))return 0;
    return *length&&*length<=LOCAL_CONTROL_FRAME_LIMIT&&
        local_control_transfer(pipe,event,wire,*length,FALSE,GetTickCount64()+5000);
}

static BOOL local_control_take_submission(void){
    const HANDLE signals[]={local_control_stop_event,local_control_submission};
    const DWORD waited=WaitForMultipleObjects(_countof(signals),signals,FALSE,INFINITE);
    return waited==WAIT_OBJECT_0+1||waited==WAIT_ABANDONED_0+1;
}

static BOOL local_control_submit(HANDLE pipe,HANDLE event,LONG64 client,char *wire,wchar_t *decoded,DWORD length){
    if(!local_control_take_submission())return FALSE;
    DWORD queued=0;
    BOOL reply=PeekNamedPipe(pipe,NULL,0,NULL,&queued,NULL)&&!queued;
    if(!reply)goto done;
    const int chars=memchr(wire,0,length)?0:
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,wire,(int)length,decoded,(int)LOCAL_CONTROL_FRAME_LIMIT);
    if(chars>0)decoded[chars]=0;
    local_control_request.body[0]=0;
    local_control_request.client=client;
    if(!chars||!local_control_split(decoded,&local_control_request)||
       (!client&&!strncmp(local_control_request.action,"COM_",4)&&strcmp(local_control_request.action,"COM_CHECK_LEASE"))){
        local_control_request.result=PRO_TK_BAD_INPUTS;
        wcscpy_s(local_control_request.body,_countof(local_control_request.body),L"请求的版本、命令或字段格式无效。");
    }else if(!strcmp(local_control_request.action,"HELLO")){
        if(local_control_request.context[0]||local_control_request.payload[0]){
            local_control_request.result=PRO_TK_BAD_INPUTS;
            wcscpy_s(local_control_request.body,_countof(local_control_request.body),L"HELLO 不接收参数。");
        }else{
            local_control_request.result=PRO_TK_NO_ERROR;
            swprintf_s(local_control_request.body,_countof(local_control_request.body),
                L"pid=%lu\nprocessStart=%llu\nslot=%lu\nliveJournal=%ls\nresident=1\neventStream=1\ncomStartup=%ls",
                local_control_pid,local_control_birth,local_control_slot,bwtl_live_path,local_control_com_startup_state());
        }
    }else{
        AcquireSRWLockExclusive(&local_control_lock);
        local_control_request_pipe=pipe;
        ReleaseSRWLockExclusive(&local_control_lock);
        reply=local_control_native();
        AcquireSRWLockExclusive(&local_control_lock);
        local_control_request_pipe=NULL;
        local_control_request.state=LC_IDLE;
        ReleaseSRWLockExclusive(&local_control_lock);
    }
    if(reply)reply=local_control_response(pipe,event,wire,decoded);
done:
    ReleaseMutex(local_control_submission);
    return reply;
}

static void local_control_live_stream(HANDLE pipe,HANDLE event,char *wire){
    DWORD length=0;
    if(!local_control_read_frame(pipe,event,wire,5000,&length))return;
    static const char subscription[]="BWTL/1\nSUBSCRIBE\n\n";
    if(length<sizeof(subscription)-1||memcmp(wire,subscription,sizeof(subscription)-1))return;
    const ULONGLONG latest=bwtl_resident_event_cursor();
    ULONGLONG cursor=latest;
    if(length>sizeof(subscription)-1){
        cursor=0;
        for(DWORD i=sizeof(subscription)-1;i<length;i++){
            if(wire[i]<'0'||wire[i]>'9'||cursor>0x1fffffffffffffULL/10)return;
            cursor=cursor*10+(wire[i]-'0');
        }
        if(cursor>latest||cursor>=0x1fffffffffffffULL)return;
    }
    const int ack=sprintf_s(wire,LOCAL_CONTROL_FRAME_LIMIT,"BWTL/1\n0\nstream=1\nafterSequence=%llu",cursor);
    if(ack<0||!local_control_frame(pipe,event,wire,(DWORD)ack))return;
    static const char response[]="BWTL/1\n0\n";
    const DWORD prefix=sizeof(response)-1;
    memcpy(wire,response,prefix);
    const HANDLE signals[]={local_control_stop_event,bwtl_resident_event_changed};
    while(PeekNamedPipe(pipe,NULL,0,NULL,NULL,NULL)){
        const DWORD bytes=bwtl_resident_event_next(&cursor,wire+prefix,LOCAL_CONTROL_FRAME_LIMIT-prefix);
        if(bytes){
            if(!local_control_frame(pipe,event,wire,prefix+bytes))break;
        }else{
            const DWORD waited=WaitForMultipleObjects(_countof(signals),signals,FALSE,1000);
            if(waited==WAIT_OBJECT_0||waited==WAIT_FAILED)break;
        }
    }
}

static unsigned __stdcall local_control_thread(void *parameter){
    const LocalControlChannel *channel=parameter;
    const HANDLE pipe=channel->pipe;
    HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL);
    char *wire=malloc(LOCAL_CONTROL_FRAME_LIMIT);
    wchar_t *decoded=channel->mode==LC_CHANNEL_EVENTS?NULL:malloc((LOCAL_CONTROL_FRAME_LIMIT+1u)*sizeof(*decoded));
    if(event&&wire&&(decoded||channel->mode==LC_CHANNEL_EVENTS)){
        while(WaitForSingleObject(local_control_stop_event,0)==WAIT_TIMEOUT){
            if(!local_control_connect(pipe,event))break;
            ULONG session=0;
            if(GetNamedPipeClientSessionId(pipe,&session)&&session==local_control_session){
                if(channel->mode==LC_CHANNEL_EVENTS)local_control_live_stream(pipe,event,wire);
                else{
                    const BOOL resident=channel->mode==LC_CHANNEL_RESIDENT;
                    const LONG64 client=resident?InterlockedIncrement64(&local_control_connection_serial):0;
                    if(resident)InterlockedExchange64(&local_control_resident_client,client);
                    DWORD length=0;
                    while(local_control_read_frame(pipe,event,wire,resident?LOCAL_CONTROL_RESIDENT_IDLE_MS:5000,&length)&&
                          local_control_submit(pipe,event,client,wire,decoded,length)){
                        if(!resident){
                            /* DisconnectNamedPipe discards unread data at close. */
                            BYTE closed;
                            (void)local_control_transfer(pipe,event,&closed,1,FALSE,GetTickCount64()+2000);
                            break;
                        }
                    }
                    if(resident)InterlockedExchange64(&local_control_resident_client,0);
                }
            }
            DisconnectNamedPipe(pipe);
        }
    }
    if(event)CloseHandle(event);
    free(decoded);
    free(wire);
    return 0;
}

static int local_control_gui_busy(void){
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
    if(panel_flow_busy())return 1;
#endif
    return native_busy||smart_active||ai_process||markup_pending||markup_ai.process||
        imitation_pending||pending_workdir_action||assembly_child_pending||fastener_menu_pending;
}

static void local_control_schedule(void){
    if(!local_control_window||!local_control_stop_event||WaitForSingleObject(local_control_stop_event,0)!=WAIT_TIMEOUT)return;
    KillTimer(local_control_window,LOCAL_CONTROL_TIMER);
    AcquireSRWLockExclusive(&local_control_lock);
    const BOOL pending=local_control_request.state==LC_PENDING&&GetTickCount64()<local_control_request.deadline;
    const BOOL can_queue=pending&&!local_control_macro&&(local_control_com_busy()||!local_control_gui_busy());
    if(can_queue)local_control_macro=local_control_request.ticket;
    ReleaseSRWLockExclusive(&local_control_lock);
    if(can_queue){
        if(ProMacroLoad(L"~ Command \x60" L"BWTLLocalControlApply\x60;")){
            local_control_macro=0;
            SetTimer(local_control_window,LOCAL_CONTROL_TIMER,100,NULL);
        }
    }else if(pending)SetTimer(local_control_window,LOCAL_CONTROL_TIMER,100,NULL);
}

#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
static void local_control_finish_workflow(ProError error,const wchar_t *reply){
    if(!local_control_active_workflow)return;
    if(local_control_active_workflow==LC_WORKFLOW_PAIR)local_control_pair_clear();
    local_control_active_workflow=LC_WORKFLOW_IDLE;
    KillTimer(local_control_window,LOCAL_CONTROL_PAIR_TIMER);
    local_control_invalidate_context();
    trace("BWTL local control workflow receipt",error);
    AcquireSRWLockExclusive(&local_control_lock);
    local_control_finish_locked(error,reply);
    ReleaseSRWLockExclusive(&local_control_lock);
}

static void local_control_pair_finish(ProError error,const wchar_t *reply){
    if(local_control_active_workflow==LC_WORKFLOW_PAIR)local_control_finish_workflow(error,reply);
}

static void local_control_workflow_reply(const wchar_t *text){
    ProError panel_result;
    if(panel_flow_take_reply(&panel_result)&&local_control_active_workflow==LC_WORKFLOW_PANEL)
        local_control_finish_workflow(panel_result,text);
    if(local_control_active_workflow==LC_WORKFLOW_PAIR)local_control_pair_reply(text);
}

static void local_control_pair_timeout(void){
    if(!local_control_active_workflow||GetTickCount64()<local_control_workflow_deadline)return;
    const int stopped=local_control_active_workflow==LC_WORKFLOW_PANEL?panel_flow_stop():default_part_stop();
    if(!stopped)local_control_finish_workflow(PRO_TK_INCOMPLETE,
        L"工作流程未完成核对。先查看当前 Creo 模型及执行明细。");
}

static void local_control_queue_workflow(LocalControlRequest *request,int workflow){
    if(!SetTimer(local_control_window,LOCAL_CONTROL_PAIR_TIMER,250,NULL)){
        AcquireSRWLockExclusive(&local_control_lock);
        local_control_finish_locked(PRO_TK_GENERAL_ERROR,L"无法监看新建流程，原生操作未开始。");
        ReleaseSRWLockExclusive(&local_control_lock);
        return;
    }
    local_control_workflow_deadline=GetTickCount64()+LOCAL_CONTROL_PAIR_TIMEOUT_MS;
    local_control_active_workflow=workflow;
    const ProError error=local_control_start_workflow(workflow==LC_WORKFLOW_PANEL,request->context,
        request->payload,request->body,_countof(request->body));
    if(error)local_control_finish_workflow(error,request->body[0]?NULL:L"无法启动工作流程。");
}
#endif


#define LOCAL_CONTROL_MODEL_TIMER 7404
static int local_control_model_pending;
static ULONGLONG local_control_model_deadline;

static void bwtl_live_schedule(void){
    KillTimer(native_backend_window,BWTL_LIVE_TIMER);
    if(bwtl_live_file==INVALID_HANDLE_VALUE&&!ax_interference_overlay.exists)return;
    if(local_control_gui_busy()||local_control_model_pending||
       ProMacroLoad(L"~ Command \x60" L"BWTLLiveSnapshot\x60;"))
        SetTimer(native_backend_window,BWTL_LIVE_TIMER,250,NULL);
}

static int local_control_is_mutation(const char *action){
    return strcmp(action,"STATUS")&&strcmp(action,"READ")&&strcmp(action,"LIST_MODELS")&&strcmp(action,"GROUP_HOLES");
}

static void local_control_live_receipt(LocalControlRequest *request,ProError result){
    if(!local_control_is_mutation(request->action))return;
    bwtl_live_refresh_model();
    bwtl_live_begin(0);
    bwtl_live_emit(L"receipt",1,result,request->body);
    if(!result&&(!strcmp(request->action,"OPEN_MODEL")||!strcmp(request->action,"SWITCH_MODEL")||
       !strcmp(request->action,"EXEC")||!strcmp(request->action,"SAVE"))){
        LocalControlContext current;
        if(!local_control_collect(&current)&&current.model&&current.model==current.owner&&current.stamp[0])
            chat_task_record_target_goal(current.model,L"Codex 原生操作",result,request->body,0,bwtl_live_goal);
    }
}

static void local_control_finish_model(ProError result,const wchar_t *detail){
    KillTimer(native_backend_window,LOCAL_CONTROL_MODEL_TIMER);
    local_control_model_pending=0;
    local_control_invalidate_context();
    LocalControlRequest *request=&local_control_request;
    if(detail){
        const size_t used=wcslen(request->body);
        if(used+1<_countof(request->body))
            wcsncpy_s(request->body+used,_countof(request->body)-used,detail,_TRUNCATE);
        ax_session_activation_clear(&ax_session_pending);
    }
    append_card(L"Codex 本机回执",request->body);
    if(store_ready)save_turn(L"Codex 本机回执",request->body);
    local_control_live_receipt(request,result);
    InterlockedExchange(&native_busy,0);
    native_set_busy(0);
    trace("BWTL native model activation receipt",result);
    AcquireSRWLockExclusive(&local_control_lock);
    local_control_finish_locked(result,NULL);
    ReleaseSRWLockExclusive(&local_control_lock);
}

static int local_control_model_confirm(uiCmdCmdId id,uiCmdValue *value,void *data){
    if(local_control_model_pending){
        ProError result=local_control_model_activation_reply(local_control_request.body,_countof(local_control_request.body));
        local_control_finish_model(result,NULL);
    }
    return 0;
}

static void local_control_model_timeout(void){
    if(local_control_model_pending&&GetTickCount64()>=local_control_model_deadline)
        local_control_finish_model(PRO_TK_INCOMPLETE,L"Creo 已受理窗口切换，活动目标尚未核对。请先重新读取当前窗口。");
}

static void local_control_wait_for_activation(void){
    local_control_model_pending=1;
    local_control_model_deadline=GetTickCount64()+LOCAL_CONTROL_QUEUE_MS;
    if(!SetTimer(native_backend_window,LOCAL_CONTROL_MODEL_TIMER,250,NULL)||
       ProMacroLoad(L"~ Command \x60" L"BWTLLocalModelConfirm\x60;"))
        local_control_finish_model(PRO_TK_INCOMPLETE,L"Creo 已受理窗口切换，但无法排队核对结果。请先重新读取当前窗口。");
}

static int local_control_gui_command(uiCmdCmdId id,uiCmdValue *value,void *data){
    const ULONGLONG ticket=local_control_macro;
    local_control_macro=0;
    BOOL accepted=FALSE;
    AcquireSRWLockExclusive(&local_control_lock);
    LocalControlRequest *request=&local_control_request;
    if(ticket&&request->state==LC_PENDING&&request->ticket==ticket){
        if(GetTickCount64()>=request->deadline||!local_control_client_live())
            local_control_finish_locked(PRO_TK_E_IN_USE,L"请求失效，原生指令未开始。");
        else if(local_control_com_busy()||!local_control_gui_busy()){
            request->state=LC_RUNNING;
            accepted=TRUE;
        }
    }
    ReleaseSRWLockExclusive(&local_control_lock);
    if(accepted){
        /* An unrelated client must get a busy reply immediately. Waiting while
           holding the submission mutex would block the COM owner's finish. */
        const int com_command=!strncmp(request->action,"COM_",4);
        if(com_command||local_control_com_busy()){
            const ProError result=com_command?local_control_com_command(request):
                !strcmp(request->action,"STATUS")?local_control_dispatch(request->action,request->context,request->payload,
                    request->body,_countof(request->body)):
                local_control_error(PRO_TK_E_IN_USE,request->body,_countof(request->body),L"当前 Creo 正在核对 COM 窗口交接，请稍后重读状态。");
            trace("BWTL local connection coordination",result);
            AcquireSRWLockExclusive(&local_control_lock);
            local_control_finish_locked(result,NULL);
            ReleaseSRWLockExclusive(&local_control_lock);
            local_control_schedule();
            return 0;
        }
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
        const int workflow=!strcmp(request->action,"BUILD_PANEL")?LC_WORKFLOW_PANEL:
            !strcmp(request->action,"CREATE_PAIR")?LC_WORKFLOW_PAIR:LC_WORKFLOW_IDLE;
        if(workflow){
            local_control_queue_workflow(request,workflow);
            local_control_schedule();
            return 0;
        }
#endif
        InterlockedExchange(&native_busy,4);
        native_set_busy(4);
        const ProError result=local_control_dispatch(request->action,request->context,request->payload,
            request->body,_countof(request->body));
        const int activating=!strcmp(request->action,"OPEN_MODEL")||!strcmp(request->action,"SWITCH_MODEL");
        if(!result&&activating&&ax_session_activation_waiting()){
            local_control_wait_for_activation();
            return 0;
        }
        if(activating||!strcmp(request->action,"OPEN_BACKGROUND")||!strcmp(request->action,"REBUILD_MODEL")||!strcmp(request->action,"REFERENCE_DESIGN")){
            local_control_invalidate_context();
            append_card(L"Codex 本机回执",request->body);
            if(store_ready)save_turn(L"Codex 本机回执",request->body);
        }
        local_control_live_receipt(request,result);
        InterlockedExchange(&native_busy,0);
        native_set_busy(0);
        trace("BWTL local control native receipt",result);
        AcquireSRWLockExclusive(&local_control_lock);
        local_control_finish_locked(result,NULL);
        ReleaseSRWLockExclusive(&local_control_lock);
    }
    local_control_schedule();
    return 0;
}

static PSECURITY_DESCRIPTOR local_control_security(void){
    HANDLE token=NULL;
    union {TOKEN_USER user;BYTE bytes[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];} info;
    DWORD size=0;
    LPWSTR sid=NULL;
    PSECURITY_DESCRIPTOR descriptor=NULL;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return NULL;
    if(GetTokenInformation(token,TokenUser,info.bytes,sizeof(info.bytes),&size)&&
       IsValidSid(info.user.User.Sid)&&ConvertSidToStringSidW(info.user.User.Sid,&sid)){
        wchar_t rule[256];
        if(swprintf_s(rule,_countof(rule),L"D:P(A;;GA;;;%ls)",sid)>=0)
            (void)ConvertStringSecurityDescriptorToSecurityDescriptorW(rule,SDDL_REVISION_1,&descriptor,NULL);
        LocalFree(sid);
    }
    CloseHandle(token);
    return descriptor;
}

static BOOL local_control_publish(const wchar_t *name){
    wchar_t staging[MAX_PATH];
    if(swprintf_s(local_control_descriptor,_countof(local_control_descriptor),
       L"%ls\\local-control.endpoint",runtime_data)<0||
       swprintf_s(staging,_countof(staging),L"%ls.%lu.pending",local_control_descriptor,local_control_pid)<0)return FALSE;
    char encoded[384];
    const int count=sprintf_s(encoded,_countof(encoded),"BWTL/1\n%ls\n%lu\n%llu\n%lu\n",
        name,local_control_pid,local_control_birth,local_control_slot);
    if(count<0)return FALSE;
    HANDLE file=CreateFileW(staging,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    DWORD written=0;
    const BOOL copy=WriteFile(file,encoded,(DWORD)count,&written,NULL)&&written==(DWORD)count&&FlushFileBuffers(file);
    CloseHandle(file);
    if(copy&&MoveFileExW(staging,local_control_descriptor,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        local_control_published=1;
        return TRUE;
    }
    DeleteFileW(staging);
    return FALSE;
}

static void local_control_stop(void){
    local_control_com_auto_stop();
    local_control_com_release();
    if(local_control_window){
        KillTimer(local_control_window,LOCAL_CONTROL_TIMER);
        KillTimer(local_control_window,LOCAL_CONTROL_MODEL_TIMER);
    }
    if(local_control_model_pending)ax_session_activation_clear(&ax_session_pending);
    local_control_model_pending=0;
    if(local_control_stop_event)SetEvent(local_control_stop_event);
    for(int i=0;i<LC_CHANNEL_COUNT;i++){
        LocalControlChannel *channel=&local_control_channels[i];
        if(channel->worker){
            WaitForSingleObject(channel->worker,INFINITE);
            CloseHandle(channel->worker);
        }
        if(channel->pipe)CloseHandle(channel->pipe);
        *channel=(LocalControlChannel){0};
    }
    if(local_control_submission)CloseHandle(local_control_submission);
    if(local_control_completion)CloseHandle(local_control_completion);
    if(local_control_stop_event)CloseHandle(local_control_stop_event);
    AcquireSRWLockExclusive(&bwtl_resident_event_lock);
    if(bwtl_resident_event_changed)CloseHandle(bwtl_resident_event_changed);
    bwtl_resident_event_changed=NULL;
    ReleaseSRWLockExclusive(&bwtl_resident_event_lock);
    if(local_control_published)DeleteFileW(local_control_descriptor);
#ifdef BWTL_LOCAL_CONTROL_WORKFLOWS
    if(local_control_window)KillTimer(local_control_window,LOCAL_CONTROL_PAIR_TIMER);
    local_control_active_workflow=LC_WORKFLOW_IDLE;
    local_control_pair_clear();
#endif
    local_control_request_pipe=local_control_stop_event=local_control_completion=local_control_submission=NULL;
    local_control_window=NULL;
    local_control_macro=0;
    local_control_request.state=LC_IDLE;
    local_control_published=0;
}

static BOOL local_control_start(HWND window){
    if(!GetPrivateProfileIntW(L"LocalControl",L"Enabled",1,runtime_settings))return TRUE;
    if(!window||local_control_channels[LC_CHANNEL_CONTROL].worker||!storage_directory_on_drive(runtime_data,L'E'))return FALSE;
    local_control_pid=GetCurrentProcessId();
    FILETIME started,ended,kernel,user;
    if(!ProcessIdToSessionId(local_control_pid,&local_control_session)||
       !GetProcessTimes(GetCurrentProcess(),&started,&ended,&kernel,&user))return FALSE;
    local_control_birth=((ULONGLONG)started.dwHighDateTime<<32)|started.dwLowDateTime;
    local_control_slot=1;
    wchar_t slot[16];
    DWORD chars=GetEnvironmentVariableW(L"BWTL_INSTANCE_NUMBER",slot,_countof(slot));
    if(chars){
        wchar_t *end;
        if(chars>=_countof(slot))return FALSE;
        const unsigned long parsed=wcstoul(slot,&end,10);
        if(end==slot||*end||parsed<1||parsed>8)return FALSE;
        local_control_slot=parsed;
    }
    wchar_t name[96];
    if(swprintf_s(name,_countof(name),L"BWTL.Control.%lu.%llu.%lu",
       local_control_pid,local_control_birth,local_control_slot)<0)return FALSE;
    PSECURITY_DESCRIPTOR acl=local_control_security();
    if(!acl)return FALSE;
    SECURITY_ATTRIBUTES security={sizeof(security),acl,FALSE};
    local_control_window=window;
    local_control_stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    local_control_completion=CreateEventW(NULL,TRUE,FALSE,NULL);
    local_control_submission=CreateMutexW(NULL,FALSE,NULL);
    AcquireSRWLockExclusive(&bwtl_resident_event_lock);
    bwtl_resident_event_changed=CreateEventW(NULL,TRUE,FALSE,NULL);
    ReleaseSRWLockExclusive(&bwtl_resident_event_lock);
    BOOL ready=local_control_stop_event&&local_control_completion&&local_control_submission&&bwtl_resident_event_changed;
    static const wchar_t *const labels[]={L"Control",L"Resident",L"Events"};
    for(int i=0;ready&&i<LC_CHANNEL_COUNT;i++){
        wchar_t path[128];
        if(swprintf_s(path,_countof(path),L"\\\\.\\pipe\\BWTL.%ls.%lu.%llu.%lu",
           labels[i],local_control_pid,local_control_birth,local_control_slot)<0){ready=FALSE;break;}
        HANDLE pipe=CreateNamedPipeW(path,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,
            LOCAL_CONTROL_FRAME_LIMIT+4,LOCAL_CONTROL_FRAME_LIMIT+4,5000,&security);
        if(pipe==INVALID_HANDLE_VALUE){ready=FALSE;break;}
        LocalControlChannel *channel=&local_control_channels[i];
        channel->pipe=pipe;
        channel->mode=i;
        channel->worker=(HANDLE)_beginthreadex(NULL,0,local_control_thread,channel,0,NULL);
        ready=channel->worker!=NULL;
    }
    LocalFree(acl);
    if(ready&&local_control_publish(name)){
        local_control_com_auto_start();
        return TRUE;
    }
    local_control_stop();
    return FALSE;
}

