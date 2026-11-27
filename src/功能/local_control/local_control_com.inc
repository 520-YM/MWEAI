/* Native attestation for the independently connected Creo VB API client.
   The external client may change the viewport while BWTL holds a bounded lease. */
#include <ProCore.h>

#define LOCAL_CONTROL_COM_TIMER 7405
#define LOCAL_CONTROL_COM_LEASE_MS 25000u

typedef struct {
    AxSessionActivation identity;
    LocalControlContext source;
    ProPath path;
    ULONGLONG deadline;
    LONG64 client;
    wchar_t token[80];
} LocalControlComLease;

static LocalControlComLease local_control_com;
static ULONGLONG local_control_com_sequence;

static int local_control_com_busy(void){return local_control_com.deadline!=0;}

static ProError local_control_com_id(wchar_t result[PRO_CONNECTID_SIZE]){
    ProConnectionId external={0};
    const ProError error=ProEngineerConnectIdGet(external);
    if(error)return error;
    const char *end=memchr(external,0,sizeof(external));
    if(!end||end==external)return PRO_TK_BAD_CONTEXT;
    for(size_t i=0;i<(size_t)(end-external);i++){
        const unsigned char byte=(unsigned char)external[i];
        if(byte<32||byte>126)return PRO_TK_BAD_CONTEXT;
        result[i]=byte;
    }
    result[end-external]=0;
    return 0;
}

static void local_control_com_release(void){
    if(!local_control_com_busy())return;
    KillTimer(local_control_window,LOCAL_CONTROL_COM_TIMER);
    ax_session_activation_clear(&local_control_com.identity);
    local_control_com=(LocalControlComLease){0};
    local_control_invalidate_context();
    InterlockedExchange(&native_busy,0);
    native_set_busy(0);
}

static void local_control_com_event(ProError error,const wchar_t *message){
    bwtl_live_refresh_model();
    bwtl_live_begin(0);
    bwtl_live_emit(L"receipt",1,error,message);
}

static VOID CALLBACK local_control_com_timeout(HWND window,UINT message,UINT_PTR id,DWORD now){
    if(local_control_com_busy()&&(GetTickCount64()>=local_control_com.deadline||
       InterlockedCompareExchange64(&local_control_resident_client,0,0)!=local_control_com.client)){
        local_control_com_release();
        local_control_com_event(PRO_TK_INCOMPLETE,L"COM 窗口交接已结束或断开，需重新读取 Creo 活动窗口。");
    }
}

static ProError local_control_com_reply(const LocalControlContext *current,const wchar_t *connect_id,
                                      const LocalControlComLease *lease,wchar_t *body,size_t capacity){
    ProError error=local_control_status(current,body,capacity);
    if(error)return error;
    size_t used=wcslen(body);
    int count=swprintf_s(body+used,capacity-used,L"\nconnectId=%ls",connect_id);
    if(count<0)return PRO_TK_OUT_OF_RANGE;
    if(!lease)return 0;
    used+=(size_t)count;
    const AxSessionIdentity *target=&lease->identity.target;
    count=swprintf_s(body+used,capacity-used,
        L"\nlease=%ls\ntargetModel=%ls\ntargetType=%ls\ntargetPath=%ls\ntargetWindow=%d\nleaseMs=%u",
        lease->token,target->name,ax_session_model_suffix(target->type),lease->path,target->window,LOCAL_CONTROL_COM_LEASE_MS);
    return count<0?PRO_TK_OUT_OF_RANGE:0;
}

static ProError local_control_com_prepare(const LocalControlContext *current,LocalControlRequest *request,
                                         const wchar_t *connect_id){
    LocalControlComLease next={0};
    AxSessionActivation *identity=&next.identity;
    AxSessionSelector selector={0};
    PartText out={request->body,_countof(request->body),0};
    ProError error=PRO_TK_NO_ERROR;
    if(!storage_directory_on_drive(current->directory,L'E')||
       ax_session_activation_waiting()||ax_return_pending.link.root||ax_open_pending.target)
        return PRO_TK_BAD_CONTEXT;
    const wchar_t *payload=request->payload;
    if(!wcsncmp(payload,L"OPEN ",5)&&payload[5]){
        ProMdl model=NULL;
        error=local_control_open_exact(current,payload+5,&selector,&model,next.path,NULL,&out);
        if(!error)error=ax_session_identity_take(model,&identity->target);
    }else if(!wcsncmp(payload,L"SWITCH ",7)&&local_control_model_selector(payload+7,0,&selector)){
        const wchar_t *leaf=payload+7;
        if(!_wcsicmp(leaf,L".prt")||!_wcsicmp(leaf,L".asm"))return PRO_TK_BAD_INPUTS;
        error=ax_session_resolve(&selector,&identity->target,&out);
    }else return PRO_TK_BAD_INPUTS;
    if(!error){
        identity->source.window=current->window;
        if(current->owner)error=ax_session_identity_take(current->owner,&identity->source);
        if(!error){
            identity->target.window=-1;
            const ProError window=ProMdlWindowGet(identity->target.model,&identity->target.window);
            if(window==PRO_TK_E_NOT_FOUND)identity->target.window=-1;
            else if(window)error=window;
        }
    }
    if(!error&&(wcspbrk(identity->target.name,L"\r\n")||wcspbrk(next.path,L"\r\n")))error=PRO_TK_BAD_CONTEXT;
    if(!error){
        next.source=*current;
        if(swprintf_s(next.token,_countof(next.token),L"%lu:%llu:%llu",local_control_pid,
            local_control_birth,++local_control_com_sequence)<0)error=PRO_TK_OUT_OF_RANGE;
    }
    if(!error)error=local_control_com_reply(current,connect_id,&next,request->body,_countof(request->body));
    if(!error&&!SetTimer(local_control_window,LOCAL_CONTROL_COM_TIMER,250,local_control_com_timeout))
        error=PRO_TK_GENERAL_ERROR;
    if(!error){
        next.client=request->client;
        next.deadline=GetTickCount64()+LOCAL_CONTROL_COM_LEASE_MS;
        local_control_com=next;
        InterlockedExchange(&native_busy,4);
        native_set_busy(4);
    }else ax_session_activation_clear(identity);
    return error;
}

static ProError local_control_com_completed(LocalControlRequest *request,int cancel){
    if(request->context[0]||!request->payload[0])return PRO_TK_BAD_INPUTS;
    if(!local_control_com_busy()||request->client!=local_control_com.client||
       wcscmp(request->payload,local_control_com.token))return PRO_TK_BAD_CONTEXT;
    ProError error=PRO_TK_NO_ERROR;
    LocalControlContext current;
    AxSessionIdentity *target=&local_control_com.identity.target;
    if(!cancel){
        error=local_control_collect(&current);
        if(!error&&(current.model!=target->model||current.owner!=target->model||
           current.type!=target->type||_wcsicmp(current.name,target->name)||_wcsicmp(current.directory,local_control_com.source.directory)||
           (target->window>=0&&target->window!=current.window)))error=PRO_TK_BAD_CONTEXT;
        if(!error){
            target->window=current.window;
            error=ax_session_identity_verify(target,1);
        }
        if(!error)error=ax_session_identity_verify(&local_control_com.identity.source,0);
        if(!error&&local_control_com.path[0]){
            ProPath origin;
            wchar_t canonical[MAX_PATH];
            if(ProMdlOriginGet(target->model,origin)||!or_real_path(origin,0,canonical)||
               _wcsicmp(canonical,local_control_com.path))error=PRO_TK_E_IN_USE;
        }
    }
    local_control_com_release();
    if(cancel){
        wcscpy_s(request->body,_countof(request->body),L"已退出 COM 窗口交接，请重新读取 Creo 活动窗口。");
        local_control_com_event(PRO_TK_INCOMPLETE,request->body);
        return 0;
    }
    if(!error){
        error=local_control_status(&current,request->body,_countof(request->body));
        if(!error&&wcscat_s(request->body,_countof(request->body),L"\nroute=COM"))error=PRO_TK_OUT_OF_RANGE;
    }
    local_control_com_event(error,error?L"COM 窗口交接未通过 MWEAI 原生读回，请核对活动模型。":
        L"COM 窗口交接已通过 MWEAI 原生读回。");
    return error;
}

static ProError local_control_com_check_lease(const LocalControlRequest *request){
    if(!local_control_com_busy()||GetTickCount64()>=local_control_com.deadline||
       wcscmp(request->context,local_control_com.token)||
       InterlockedCompareExchange64(&local_control_resident_client,0,0)!=local_control_com.client)
        return PRO_TK_BAD_CONTEXT;
    const AxSessionIdentity *target=&local_control_com.identity.target;
    AxSessionSelector selector={0};
    if(!local_control_model_selector(request->payload,0,&selector)||
       selector.type!=target->type||_wcsicmp(selector.name,target->name))return PRO_TK_BAD_CONTEXT;
    LocalControlContext current;
    const ProError error=local_control_collect(&current);
    if(error)return error;
    return local_control_same_context(&current,&local_control_com.source)?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}

static ProError local_control_com_command(LocalControlRequest *request){
    const int identify=!strcmp(request->action,"COM_IDENT");
    const int finish=!strcmp(request->action,"COM_FINISH"),cancel=!strcmp(request->action,"COM_CANCEL");
    ProError error=PRO_TK_NO_ERROR;
    if(!strcmp(request->action,"COM_CHECK_LEASE"))error=local_control_com_check_lease(request);
    else if(finish||cancel)error=local_control_com_completed(request,cancel);
    else if(local_control_com_busy())error=PRO_TK_E_IN_USE;
    else if(!request->client||(!request->payload[0]!=identify))error=PRO_TK_BAD_INPUTS;
    else{
        LocalControlContext current;
        wchar_t connect_id[PRO_CONNECTID_SIZE];
        error=local_control_collect(&current);
        if(!error&&!local_control_context_matches(&current,request->context))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=local_control_com_id(connect_id);
        if(!error)error=identify?local_control_com_reply(&current,connect_id,NULL,request->body,_countof(request->body)):
            local_control_com_prepare(&current,request,connect_id);
    }
    if(error&&(!request->body[0]||wcsstr(request->body,L"\nconnectId=")))
        swprintf_s(request->body,_countof(request->body),
            L"COM 通路未通过当前 Creo 会话、窗口或模型核对（Creo %d）。请重新读取状态。",error);
    return error;
}
