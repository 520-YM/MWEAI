/* Start the Windows COM adapter from the loaded Creo process. The adapter
   obtains the live Creo ID over BWTL, then publishes only a state and PID. */
#define LOCAL_CONTROL_COM_STARTUP_TIMER 7406
#define LOCAL_CONTROL_COM_STARTUP_TRIES 3u
#define LOCAL_CONTROL_COM_STARTUP_WAIT_MS 35000u

enum { LC_COM_DISABLED, LC_COM_STARTING, LC_COM_CONNECTED, LC_COM_READY, LC_COM_UNAVAILABLE };

static volatile LONG local_control_com_state;
static HANDLE local_control_com_worker,local_control_com_stop_event;
static wchar_t local_control_com_worker_script[MAX_PATH],local_control_com_worker_dir[MAX_PATH];
static wchar_t local_control_com_worker_state[MAX_PATH],local_control_com_profile[MAX_PATH],local_control_com_stop_name[96];
static ULONGLONG local_control_com_startup_deadline;
static unsigned local_control_com_startup_attempts;

static const wchar_t *local_control_com_startup_state(void){
    static const wchar_t *const names[]={L"disabled",L"starting",L"connected",L"ready",L"unavailable"};
    return names[InterlockedCompareExchange(&local_control_com_state,0,0)];
}

static void local_control_com_set_startup_state(LONG state){
    if(InterlockedExchange(&local_control_com_state,state)!=state){
        static const char *const names[]={"BWTL COM automatic connection disabled","BWTL COM automatic connection starting",
            "BWTL COM automatic connection verified","BWTL COM automatic connection ready",
            "BWTL COM automatic connection unavailable"};
        trace(names[state],state==LC_COM_UNAVAILABLE?PRO_TK_E_NOT_FOUND:PRO_TK_NO_ERROR);
    }
}

static int local_control_com_worker_environment_name(const wchar_t *entry,const wchar_t *name){
    const size_t length=wcslen(name);
    return !_wcsnicmp(entry,name,length)&&entry[length]==L'=';
}

static int local_control_com_environment_compare(const void *left,const void *right){
    return _wcsicmp(*(const wchar_t *const *)left,*(const wchar_t *const *)right);
}

static wchar_t *local_control_com_worker_environment(void){
    static const wchar_t *const overrides[]={L"BWTL_COM_RESIDENT_PID",L"BWTL_COM_RESIDENT_PROCESS_START",
        L"BWTL_COM_RESIDENT_PROFILE",L"BWTL_COM_RESIDENT_SLOT",L"BWTL_COM_RESIDENT_STOP_EVENT",L"TEMP",L"TMP"};
    wchar_t fields[_countof(overrides)][MAX_PATH+48];
    wchar_t pid[16],birth[24],slot[16];
    if(swprintf_s(pid,_countof(pid),L"%lu",local_control_pid)<0||
       swprintf_s(birth,_countof(birth),L"%llu",local_control_birth)<0||
       swprintf_s(slot,_countof(slot),L"%lu",local_control_slot)<0)return NULL;
    const wchar_t *const values[]={pid,birth,local_control_com_profile,slot,local_control_com_stop_name,
        local_control_com_worker_dir,local_control_com_worker_dir};
    for(size_t i=0;i<_countof(overrides);i++)
        if(swprintf_s(fields[i],_countof(fields[i]),L"%ls=%ls",overrides[i],values[i])<0)return NULL;
    wchar_t *source=GetEnvironmentStringsW();
    if(!source)return NULL;
    size_t count=_countof(overrides);
    for(const wchar_t *entry=source;*entry;entry+=wcslen(entry)+1)count++;
    const wchar_t **entries=calloc(count,sizeof(*entries));
    wchar_t *block=NULL;
    if(entries){
        count=0;
        for(const wchar_t *entry=source;*entry;entry+=wcslen(entry)+1){
            size_t i=0;
            while(i<_countof(overrides)&&!local_control_com_worker_environment_name(entry,overrides[i]))i++;
            if(i==_countof(overrides))entries[count++]=entry;
        }
        for(size_t i=0;i<_countof(overrides);i++)entries[count++]=fields[i];
        qsort(entries,count,sizeof(*entries),local_control_com_environment_compare);
        size_t length=1;
        for(size_t i=0;i<count;i++)length+=wcslen(entries[i])+1;
        block=calloc(length,sizeof(*block));
        if(block){
            wchar_t *next=block;
            for(size_t i=0;i<count;i++){
                const size_t size=wcslen(entries[i])+1;
                memcpy(next,entries[i],size*sizeof(*next));
                next+=size;
            }
        }
        free(entries);
    }
    FreeEnvironmentStringsW(source);
    return block;
}

static void local_control_com_spawn_worker(void){
    local_control_com_startup_attempts++;
    local_control_com_startup_deadline=GetTickCount64()+LOCAL_CONTROL_COM_STARTUP_WAIT_MS;
    wchar_t executable[MAX_PATH],command[MAX_PATH*2+100];
    DWORD size=GetSystemDirectoryW(executable,_countof(executable));
    if(!size||size>=_countof(executable)||
       wcscat_s(executable,_countof(executable),L"\\WindowsPowerShell\\v1.0\\powershell.exe")||
       swprintf_s(command,_countof(command),L"\"%ls\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\"",
           executable,local_control_com_worker_script)<0){
        trace("Resolve BWTL COM startup worker",GetLastError());
        return;
    }
    wchar_t *environment=local_control_com_worker_environment();
    if(!environment){trace("Prepare BWTL COM startup environment",ERROR_NOT_ENOUGH_MEMORY);return;}
    STARTUPINFOW startup={.cb=sizeof(startup),.dwFlags=STARTF_USESHOWWINDOW,.wShowWindow=SW_HIDE};
    PROCESS_INFORMATION process={0};
    const BOOL created=CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,
        environment,local_control_com_worker_dir,&startup,&process);
    const DWORD error=created?ERROR_SUCCESS:GetLastError();
    wchar_t *end=environment;
    while(*end)end+=wcslen(end)+1;
    SecureZeroMemory(environment,(end-environment+1)*sizeof(*environment));
    free(environment);
    if(created){
        CloseHandle(process.hThread);
        local_control_com_worker=process.hProcess;
        local_control_com_set_startup_state(LC_COM_STARTING);
    }
    trace("Start BWTL COM resident worker",error);
}

/* Worker status is a small flat JSON object. Parse only its top-level scalar
   fields, so messages cannot be interpreted as state or process identity. */
static const char *local_control_com_json_field(const char *row,const char *name){
    const char *found=NULL;
    const size_t name_length=strlen(name);
    if(*row!='{')return NULL;
    row++;
    for(;;){
        while(*row==' '||*row=='\t'||*row=='\r'||*row=='\n')row++;
        if(*row!='"')return NULL;
        const char *key=++row;
        while(*row&&*row!='"'&&*row!='\\')row++;
        if(*row!='"')return NULL;
        const int match=(size_t)(row-key)==name_length&&!memcmp(key,name,name_length);
        row++;
        while(*row==' '||*row=='\t')row++;
        if(*row++!=':')return NULL;
        while(*row==' '||*row=='\t')row++;
        if(match){if(found)return NULL;found=row;}
        if(*row=='"'){
            row++;
            while(*row&&*row!='"'){
                if(*row++=='\\'&&*row)row++;
            }
            if(*row!='"')return NULL;
            row++;
        }else{
            while((*row>='0'&&*row<='9')||(*row>='a'&&*row<='z'))row++;
        }
        while(*row==' '||*row=='\t'||*row=='\r'||*row=='\n')row++;
        if(*row=='}')return found;
        if(*row++!=',')return NULL;
    }
}

static int local_control_com_json_number(const char *row,const char *name,ULONGLONG *result){
    const char *field=local_control_com_json_field(row,name);
    if(!field)return 0;
    const int quoted=*field=='"';
    if(quoted)field++;
    if(*field<'0'||*field>'9')return 0;
    ULONGLONG value=0;
    while(*field>='0'&&*field<='9'){
        const unsigned digit=(unsigned)(*field++-'0');
        if(value>(ULLONG_MAX-digit)/10u)return 0;
        value=value*10u+digit;
    }
    if(quoted?*field!='"':(*field!=','&&*field!='}'&&*field!=' '&&*field!='\t'))return 0;
    *result=value;
    return 1;
}

static int local_control_com_worker_report(void){
    char row[4096];
    HANDLE file=CreateFileW(local_control_com_worker_state,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file==INVALID_HANDLE_VALUE)return LC_COM_STARTING;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD length=0;
    const BOOL read=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
        !info.nFileSizeHigh&&info.nFileSizeLow<sizeof(row)&&ReadFile(file,row,sizeof(row)-1,&length,NULL)&&
        length==info.nFileSizeLow;
    CloseHandle(file);
    if(!read||!length||memchr(row,0,length))return LC_COM_STARTING;
    row[length]=0;
    const char *json=length>=3&&!memcmp(row,"\xef\xbb\xbf",3)?row+3:row;
    ULONGLONG pid,birth,slot;
    if(!local_control_com_json_number(json,"Pid",&pid)||pid!=local_control_pid||
       !local_control_com_json_number(json,"ProcessStart",&birth)||birth!=local_control_birth||
       !local_control_com_json_number(json,"Slot",&slot)||slot!=local_control_slot)return LC_COM_STARTING;
    const char *state=local_control_com_json_field(json,"State");
    if(!state)return LC_COM_STARTING;
    if(!strncmp(state,"\"unavailable\"",13))return LC_COM_UNAVAILABLE;
    const int report=!strncmp(state,"\"connected\"",11)?LC_COM_CONNECTED:
        !strncmp(state,"\"ready\"",7)?LC_COM_READY:LC_COM_STARTING;
    if(report==LC_COM_STARTING)return report;
    ULONGLONG worker,worker_birth;
    DWORD session=0;
    if(!local_control_com_json_number(json,"WorkerPid",&worker)||!worker||worker>MAXDWORD||
       !local_control_com_json_number(json,"WorkerStart",&worker_birth)||
       !ProcessIdToSessionId((DWORD)worker,&session)||session!=local_control_session)return LC_COM_UNAVAILABLE;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,(DWORD)worker);
    if(!process)return LC_COM_UNAVAILABLE;
    FILETIME started,ended,kernel,user;
    const BOOL running=WaitForSingleObject(process,0)==WAIT_TIMEOUT&&
        GetProcessTimes(process,&started,&ended,&kernel,&user)&&
        ((((ULONGLONG)started.dwHighDateTime<<32)|started.dwLowDateTime)==worker_birth);
    CloseHandle(process);
    return running?report:LC_COM_UNAVAILABLE;
}

static VOID CALLBACK local_control_com_startup_tick(HWND window,UINT message,UINT_PTR id,DWORD now){
    (void)window;(void)message;(void)id;(void)now;
    if(local_control_com_worker&&WaitForSingleObject(local_control_com_worker,0)==WAIT_OBJECT_0){
        CloseHandle(local_control_com_worker);
        local_control_com_worker=NULL;
    }
    const int report=local_control_com_worker_report();
    if(report==LC_COM_CONNECTED||report==LC_COM_READY){local_control_com_set_startup_state(report);return;}
    if(GetTickCount64()<local_control_com_startup_deadline){
        local_control_com_set_startup_state(report);
        return;
    }
    if(!local_control_com_worker&&local_control_com_startup_attempts<LOCAL_CONTROL_COM_STARTUP_TRIES){
        local_control_com_spawn_worker();
        return;
    }
    local_control_com_set_startup_state(LC_COM_UNAVAILABLE);
}

static void local_control_com_auto_stop(void){
    if(local_control_window)KillTimer(local_control_window,LOCAL_CONTROL_COM_STARTUP_TIMER);
    if(local_control_com_stop_event){
        SetEvent(local_control_com_stop_event);
        CloseHandle(local_control_com_stop_event);
        local_control_com_stop_event=NULL;
    }
    if(local_control_com_worker){CloseHandle(local_control_com_worker);local_control_com_worker=NULL;}
    local_control_com_startup_attempts=0;
    local_control_com_startup_deadline=0;
    InterlockedExchange(&local_control_com_state,LC_COM_DISABLED);
}

static void local_control_com_auto_start(void){
    if(!GetPrivateProfileIntW(L"LocalControl",L"AutoConnectCom",1,runtime_settings))return;
    wcscpy_s(local_control_com_profile,_countof(local_control_com_profile),runtime_data);
    wchar_t *leaf=wcsrchr(local_control_com_profile,L'\\');
    if(!leaf||_wcsicmp(leaf+1,L"data"))goto unavailable;
    *leaf=0;
    if(!storage_directory_on_drive(runtime_root,L'E')||
       swprintf_s(local_control_com_worker_script,_countof(local_control_com_worker_script),L"%ls\\scripts\\connection\\Start-CreoComResident.ps1",runtime_root)<0||
       swprintf_s(local_control_com_worker_dir,_countof(local_control_com_worker_dir),L"%ls\\connection-runtime",runtime_data)<0||
       swprintf_s(local_control_com_worker_state,_countof(local_control_com_worker_state),L"%ls\\resident-%lu-%llu.json",local_control_com_worker_dir,
           local_control_pid,local_control_birth)<0||
       swprintf_s(local_control_com_stop_name,_countof(local_control_com_stop_name),L"Local\\BWTL.ComStop.%lu.%llu.%lu",
           local_control_pid,local_control_birth,local_control_slot)<0)goto unavailable;
    DWORD attributes=GetFileAttributesW(local_control_com_worker_script);
    if(attributes==INVALID_FILE_ATTRIBUTES||attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))goto unavailable;
    if(!CreateDirectoryW(local_control_com_worker_dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)goto unavailable;
    attributes=GetFileAttributesW(local_control_com_worker_dir);
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||
       !storage_directory_on_drive(local_control_com_worker_dir,L'E'))goto unavailable;
    PSECURITY_DESCRIPTOR acl=local_control_security();
    if(!acl)goto unavailable;
    SECURITY_ATTRIBUTES security={sizeof(security),acl,FALSE};
    SetLastError(ERROR_SUCCESS);
    local_control_com_stop_event=CreateEventW(&security,TRUE,FALSE,local_control_com_stop_name);
    const DWORD error=GetLastError();
    LocalFree(acl);
    if(!local_control_com_stop_event||error==ERROR_ALREADY_EXISTS){
        if(local_control_com_stop_event){CloseHandle(local_control_com_stop_event);local_control_com_stop_event=NULL;}
        goto unavailable;
    }
    if(!SetTimer(local_control_window,LOCAL_CONTROL_COM_STARTUP_TIMER,1000,local_control_com_startup_tick)){
        local_control_com_auto_stop();
        goto unavailable;
    }
    local_control_com_set_startup_state(LC_COM_STARTING);
    local_control_com_spawn_worker();
    return;
unavailable:
    local_control_com_set_startup_state(LC_COM_UNAVAILABLE);
}
