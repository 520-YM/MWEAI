/* A small, process-scoped journal. Existing chat and task history remain authoritative. */
#define BWTL_LIVE_LINE 32768
#define BWTL_LIVE_TEXT 2000
#define BWTL_LIVE_TIMER 7403
#define BWTL_LIVE_REFRESH (WM_APP+75)

#include "resident_events.inc"

typedef struct {
    ProMdlName name;
    wchar_t type[12], directory[PRO_PATH_SIZE], stamp[240];
    int window;
} BwtlLiveModel;

static BwtlLiveModel bwtl_live_model;
static wchar_t bwtl_live_path[MAX_PATH];
static HANDLE bwtl_live_file=INVALID_HANDLE_VALUE;
static ULONGLONG bwtl_live_seq,bwtl_live_task;
static wchar_t bwtl_live_task_directory[PRO_PATH_SIZE];
static wchar_t bwtl_live_goal[900];

static int bwtl_live_json_string(wchar_t *row,size_t room,size_t *used,const wchar_t *value){
    static const wchar_t hex[]=L"0123456789abcdef";
    if(*used+2>=room)return 0;
    row[(*used)++]=L'"';
    for(size_t i=0;value[i];i++){
        const wchar_t ch=value[i];
        if(i==BWTL_LIVE_TEXT||(i+1==BWTL_LIVE_TEXT&&ch>=0xd800&&ch<=0xdbff))break;
        if(*used+7>=room)return 0;
        if(ch<32||(ch>=0xd800&&ch<=0xdfff)){
            /* Escape each UTF-16 code unit, including complete surrogate pairs. */
            row[(*used)++]=L'\\';row[(*used)++]=L'u';
            for(int shift=12;shift>=0;shift-=4)row[(*used)++]=hex[(ch>>shift)&15];
        }else{
            if(ch==L'"'||ch==L'\\')row[(*used)++]=L'\\';
            row[(*used)++]=ch;
        }
    }
    row[(*used)++]=L'"';
    row[*used]=0;
    return 1;
}

static void bwtl_live_emit(const wchar_t *kind,int has_code,int code,const wchar_t *message){
    if(bwtl_live_file==INVALID_HANDLE_VALUE)return;
    const ULONGLONG next=bwtl_live_seq+1;
    SYSTEMTIME now;
    GetSystemTime(&now);
    wchar_t row[BWTL_LIVE_LINE/sizeof(wchar_t)];
    wchar_t code_value[24]=L"null";
    if(has_code)swprintf_s(code_value,_countof(code_value),L"%d",code);
    const int prefix=swprintf_s(row,_countof(row),
        L"{\"seq\":%llu,\"task\":%llu,\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\","
        L"\"kind\":\"%ls\",\"code\":%ls,\"window\":%d",
        next,bwtl_live_task,now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
        kind,code_value,bwtl_live_model.window);
    if(prefix<0)return;
    size_t used=(size_t)prefix;
    const struct {const wchar_t *key,*value;} fields[]={
        {L"model",bwtl_live_model.name},{L"type",bwtl_live_model.type},{L"workDir",bwtl_live_model.directory},
        {L"stamp",bwtl_live_model.stamp},{L"text",message?message:L""}
    };
    for(size_t i=0;i<_countof(fields);i++){
        int added=swprintf_s(row+used,_countof(row)-used,L",\"%ls\":",fields[i].key);
        if(added<0)return;
        used+=(size_t)added;
        if(!bwtl_live_json_string(row,_countof(row),&used,fields[i].value))return;
    }
    if(used+2>=_countof(row))return;
    row[used++]=L'}';row[used++]=L'\n';
    char bytes[BWTL_LIVE_LINE];
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,row,(int)used,bytes,sizeof(bytes),NULL,NULL);
    DWORD written=0;
    LARGE_INTEGER end={0};
    if(!length||!SetFilePointerEx(bwtl_live_file,end,NULL,FILE_END)||
       !WriteFile(bwtl_live_file,bytes,(DWORD)length,&written,NULL)||written!=(DWORD)length){
        trace("BWTL collaboration journal write failed",(int)GetLastError());
        CloseHandle(bwtl_live_file);
        bwtl_live_file=INVALID_HANDLE_VALUE;
        return;
    }
    bwtl_live_seq=next;
    bwtl_resident_event_record(next,bytes,(DWORD)length);
    if(!FlushFileBuffers(bwtl_live_file))trace("BWTL collaboration journal flush failed",(int)GetLastError());
}

static int bwtl_live_restore(HANDLE file){
    LARGE_INTEGER size,seek;
    if(!GetFileSizeEx(file,&size))return 0;
    if(!size.QuadPart)return 1;
    char tail[BWTL_LIVE_LINE*2+1];
    DWORD length=0;
    seek.QuadPart=size.QuadPart>(LONGLONG)(sizeof(tail)-1)?size.QuadPart-(sizeof(tail)-1):0;
    if(!SetFilePointerEx(file,seek,NULL,FILE_BEGIN)||
       !ReadFile(file,tail,sizeof(tail)-1,&length,NULL)||!length)return 0;
    tail[length]=0;
    char *end=strrchr(tail,'\n');
    if(!end)return 0;
    *end=0;
    char *start=strrchr(tail,'\n');
    if(!start&&seek.QuadPart)return 0;
    ULONGLONG task=0,sequence=0;
    if(sscanf_s(start?start+1:tail,"{\"seq\":%llu,\"task\":%llu,",&sequence,&task)!=2||
       sequence>=0x1fffffffffffffULL||task>sequence)return 0;
    if((DWORD)(end-tail+1)<length){
        /* A failed partial append at process termination is not an event. */
        seek.QuadPart+=(end-tail+1);
        if(!SetFilePointerEx(file,seek,NULL,FILE_BEGIN)||!SetEndOfFile(file))return 0;
    }
    bwtl_live_seq=sequence;
    bwtl_live_task=task;
    return 1;
}

static int bwtl_live_open(void){
    if(!store_ready||!store_session_root[0]||!storage_directory_on_drive(store_session_root,L'E')||
       swprintf_s(bwtl_live_path,_countof(bwtl_live_path),L"%ls\\live-events.jsonl",store_session_root)<0)return 0;
    HANDLE folder=CreateFileW(store_session_root,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(folder==INVALID_HANDLE_VALUE)return 0;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE file=INVALID_HANDLE_VALUE;
    if(modeling_rule_file_matches(folder,store_session_root,1,&info)){
        SECURITY_ATTRIBUTES attributes={sizeof(attributes),NULL,FALSE};
        file=CreateFileW(bwtl_live_path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,&attributes,
            OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    }
    int ready=file!=INVALID_HANDLE_VALUE&&modeling_rule_file_matches(file,bwtl_live_path,0,&info)&&bwtl_live_restore(file);
    if(ready)bwtl_live_file=file;
    else if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    CloseHandle(folder);
    if(!ready)bwtl_live_path[0]=0;
    return ready;
}

static void bwtl_live_close(void){
    if(bwtl_live_file!=INVALID_HANDLE_VALUE)CloseHandle(bwtl_live_file);
    bwtl_live_file=INVALID_HANDLE_VALUE;
    bwtl_live_path[0]=0;
}

static void bwtl_live_begin(int force){
    if(force||!bwtl_live_task||_wcsicmp(bwtl_live_task_directory,bwtl_live_model.directory)){
        bwtl_live_task=bwtl_live_seq+1;
        bwtl_live_goal[0]=0;
        wcscpy_s(bwtl_live_task_directory,_countof(bwtl_live_task_directory),bwtl_live_model.directory);
    }
}

static void bwtl_live_refresh_model(void);

static void bwtl_live_chat(const wchar_t *role,const wchar_t *text){
    if(bwtl_live_file==INVALID_HANDLE_VALUE)return;
    if(!wcscmp(role,L"用户")){
        bwtl_live_refresh_model();
        bwtl_live_begin(0);
        wcsncpy_s(bwtl_live_goal,_countof(bwtl_live_goal),text,_TRUNCATE);
        bwtl_live_emit(L"user",0,0,text);
    }else if(!wcscmp(role,L"助手")){
        bwtl_live_refresh_model();
        bwtl_live_emit(L"receipt",0,0,text);
    }
}
