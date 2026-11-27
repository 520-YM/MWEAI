
#include <ProNotify.h>

/* Saves from both chat entry points are authorized against the model's last
   known disk iteration. This cache is owned by the Creo command thread. */
typedef struct {
    DWORD volume,high,low;
} BwtlSaveIdentity;

typedef struct {
    BwtlSaveIdentity identity;
    FILETIME written;
    DWORD size_high,size_low;
    int present;
    wchar_t leaf[PRO_NAME_SIZE+32];
} BwtlSaveFile;

typedef struct BwtlSaveBaseline {
    struct BwtlSaveBaseline *next;
    ProMdl model;
    ProMdlName name;
    ProMdlType type;
    BwtlSaveIdentity directory;
    BwtlSaveFile file;
} BwtlSaveBaseline;

static BwtlSaveBaseline *bwtl_save_baselines;
static unsigned bwtl_saved_baseline_count;
static unsigned bwtl_save_watches;

static BwtlSaveIdentity bwtl_save_identity(const BY_HANDLE_FILE_INFORMATION *info){
    return (BwtlSaveIdentity){info->dwVolumeSerialNumber,info->nFileIndexHigh,info->nFileIndexLow};
}

static int bwtl_save_same_identity(BwtlSaveIdentity a,BwtlSaveIdentity b){
    return a.volume==b.volume&&a.high==b.high&&a.low==b.low;
}

static int bwtl_save_same_file(const BwtlSaveFile *a,const BwtlSaveFile *b){
    if(!a->present||!b->present)return a->present==b->present;
    return !_wcsicmp(a->leaf,b->leaf)&&bwtl_save_same_identity(a->identity,b->identity)&&
        !CompareFileTime(&a->written,&b->written)&&a->size_high==b->size_high&&a->size_low==b->size_low;
}

static ProError bwtl_save_directory(const wchar_t *path,BwtlSaveIdentity *identity){
    if(!storage_directory_on_drive(path,L'E'))return PRO_TK_BAD_CONTEXT;
    HANDLE dir=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(dir==INVALID_HANDLE_VALUE)return PRO_TK_CANT_OPEN;
    BY_HANDLE_FILE_INFORMATION info;
    int ok=GetFileInformationByHandle(dir,&info)&&
        (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&(info.nFileIndexHigh||info.nFileIndexLow);
    CloseHandle(dir);
    if(ok)*identity=bwtl_save_identity(&info);
    return ok?0:PRO_TK_CANT_OPEN;
}

static ProError bwtl_save_read_file(const wchar_t *path,BwtlSaveFile *file){
    memset(file,0,sizeof(*file));
    HANDLE input=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(input==INVALID_HANDLE_VALUE)return PRO_TK_CANT_OPEN;
    BY_HANDLE_FILE_INFORMATION info;
    int ok=GetFileInformationByHandle(input,&info)&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        (info.nFileSizeHigh||info.nFileSizeLow);
    CloseHandle(input);
    const wchar_t *leaf=wcsrchr(path,L'\\');
    if(!ok||!leaf||wcslen(leaf+1)>=_countof(file->leaf))return PRO_TK_BAD_CONTEXT;
    wcscpy_s(file->leaf,_countof(file->leaf),leaf+1);
    file->identity=bwtl_save_identity(&info);
    file->written=info.ftLastWriteTime;
    file->size_high=info.nFileSizeHigh;
    file->size_low=info.nFileSizeLow;
    file->present=1;
    return 0;
}

static ProError bwtl_save_model_info(ProMdl model,ProMdlName name,ProMdlType *type){
    ProError error=ProMdlTypeGet(model,type);
    if(!error&&*type!=PRO_MDL_PART&&*type!=PRO_MDL_ASSEMBLY)error=PRO_TK_INVALID_TYPE;
    if(!error)error=ProMdlMdlnameGet(model,name);
    if(!error&&(!name[0]||wcspbrk(name,L"\\/:*?\"<>|")))error=PRO_TK_BAD_INPUTS;
    return error;
}

static ProError bwtl_save_latest(const wchar_t *directory,const wchar_t *name,ProMdlType type,BwtlSaveFile *file){
    ProPath path;
    memset(file,0,sizeof(*file));
    ProError error=ax_native_source(directory,name,type,path);
    return error==PRO_TK_E_NOT_FOUND?0:error?error:bwtl_save_read_file(path,file);
}

static BwtlSaveBaseline *bwtl_save_lookup(ProMdl model,const wchar_t *name,ProMdlType type,BwtlSaveIdentity directory){
    for(BwtlSaveBaseline *entry=bwtl_save_baselines;entry;entry=entry->next)
        if(entry->model==model&&entry->type==type&&!_wcsicmp(entry->name,name)&&
           bwtl_save_same_identity(entry->directory,directory))return entry;
    return NULL;
}

static ProError bwtl_save_note(ProMdl model,const wchar_t *name,ProMdlType type,
    BwtlSaveIdentity directory,const BwtlSaveFile *file,int replace){
    BwtlSaveBaseline *entry=bwtl_save_lookup(model,name,type,directory);
    if(entry){if(replace)entry->file=*file;return 0;}
    if(bwtl_saved_baseline_count>=4096)return PRO_TK_OUT_OF_RANGE;
    entry=calloc(1,sizeof(*entry));
    if(!entry)return PRO_TK_OUT_OF_MEMORY;
    entry->next=bwtl_save_baselines;
    entry->model=model;
    wcscpy_s(entry->name,_countof(entry->name),name);
    entry->type=type;
    entry->directory=directory;
    entry->file=*file;
    bwtl_save_baselines=entry;
    bwtl_saved_baseline_count++;
    return 0;
}

/* Only the publishing code may supply an image verified against its own
   staged model. An existing registered iteration is never replaced here. */
static ProError bwtl_save_note_published_copy(ProMdl model,const wchar_t *directory,const BwtlSaveFile *published){
    ProMdlName name;
    ProMdlType type;
    BwtlSaveIdentity identity;
    BwtlSaveFile latest;
    ProError error=bwtl_save_model_info(model,name,&type);
    if(!error)error=bwtl_save_directory(directory,&identity);
    if(!error)error=bwtl_save_latest(directory,name,type,&latest);
    if(error)return error;
    if(!published->present||!bwtl_save_same_file(published,&latest))return PRO_TK_E_IN_USE;
    const BwtlSaveBaseline *known=bwtl_save_lookup(model,name,type,identity);
    if(known&&!bwtl_save_same_file(&known->file,published))return PRO_TK_E_IN_USE;
    return bwtl_save_note(model,name,type,identity,published,0);
}

static void bwtl_save_observe(ProMdl model){
    ProMdlName name;
    ProMdlType type;
    ProPath origin,folder;
    BwtlSaveIdentity directory;
    BwtlSaveFile actual;
    if(!model||bwtl_save_model_info(model,name,&type)||ProMdlOriginGet(model,origin)||!origin[0])return;
    for(wchar_t *p=origin;*p;p++)if(*p==L'/')*p=L'\\';
    const wchar_t *leaf=wcsrchr(origin,L'\\');
    if(!leaf||leaf-origin<3||wcsncpy_s(folder,_countof(folder),origin,leaf-origin)||
       bwtl_save_directory(folder,&directory)||bwtl_save_lookup(model,name,type,directory))return;
    if(!bwtl_save_read_file(origin,&actual))
        (void)bwtl_save_note(model,name,type,directory,&actual,0);
}

static ProError bwtl_save_loaded(ProMdl model){
    bwtl_save_observe(model);
    return 0;
}

static ProError bwtl_live_window_change(void);

static ProError bwtl_save_erased(ProMdl model){
    ax_interference_mark_changed(model);
    if(ax_interference_overlay.exists)(void)bwtl_live_window_change();
    BwtlSaveBaseline **at=&bwtl_save_baselines;
    while(*at){
        BwtlSaveBaseline *entry=*at;
        if(entry->model!=model){at=&entry->next;continue;}
        *at=entry->next;
        free(entry);
        bwtl_saved_baseline_count--;
    }
    return 0;
}

typedef struct {
    ProMdl model;
    ProMdlName name;
    ProMdlType type;
    BwtlSaveFile disk;
} BwtlSaveTarget;

typedef struct {
    ProPath path;
    BwtlSaveIdentity directory;
    BwtlSaveTarget *target;
    int count,capacity,blocked;
    ProMdlName blocked_model;
} BwtlSaveTransaction;

static BwtlSaveTransaction *bwtl_active_save;

static ProError bwtl_save_check(BwtlSaveTransaction *transaction,ProMdl model){
    for(int i=0;i<transaction->count;i++)if(transaction->target[i].model==model)return 0;
    if(transaction->count==transaction->capacity)return PRO_TK_OUT_OF_RANGE;
    BwtlSaveTarget *target=&transaction->target[transaction->count];
    target->model=model;
    ProError error=bwtl_save_model_info(model,target->name,&target->type);
    if(!error)error=bwtl_save_latest(transaction->path,target->name,target->type,&target->disk);
    if(error)return error;
    bwtl_save_observe(model);
    const BwtlSaveBaseline *start=bwtl_save_lookup(model,target->name,target->type,transaction->directory);
    if((start&&!bwtl_save_same_file(&start->file,&target->disk))||(!start&&target->disk.present)){
        wcscpy_s(transaction->blocked_model,_countof(transaction->blocked_model),target->name);
        return PRO_TK_E_IN_USE;
    }
    transaction->count++;
    return 0;
}

static ProError bwtl_save_validate_member(ProMdl model){
    if(!bwtl_active_save)return 0;
    BwtlSaveTransaction *transaction=bwtl_active_save;
    const ProError error=bwtl_save_check(transaction,model);
    if(error)transaction->blocked=1;
    return error;
}

static void bwtl_save_watch_start(void){
    if(!ProNotificationSet(PRO_MODEL_RETRIEVE_POST_ALL,(ProFunction)bwtl_save_loaded))bwtl_save_watches|=1;
    if(!ProNotificationSet(PRO_MODEL_ERASE_POST,(ProFunction)bwtl_save_erased))bwtl_save_watches|=2;
    if(!ProNotificationSet(PRO_MODEL_SAVE_PRE_ALL,(ProFunction)bwtl_save_validate_member))bwtl_save_watches|=4;
}

static void bwtl_save_watch_stop(void){
    if(bwtl_save_watches&1)ProNotificationUnset(PRO_MODEL_RETRIEVE_POST_ALL);
    if(bwtl_save_watches&2)ProNotificationUnset(PRO_MODEL_ERASE_POST);
    if(bwtl_save_watches&4)ProNotificationUnset(PRO_MODEL_SAVE_PRE_ALL);
    bwtl_save_watches=0;
    bwtl_active_save=NULL;
    while(bwtl_save_baselines)bwtl_save_erased(bwtl_save_baselines->model);
}

static ProError bwtl_save_children(BwtlSaveTransaction *transaction,ProMdl model){
    AxScan *scan=NULL;
    ProError error=ax_scan(model,1,&scan);
    if(!error&&(scan->limited||scan->unavailable))error=PRO_TK_INCOMPLETE;
    ProPath save_mode=L"";
    const int only_changed=!ProConfigoptionGet(L"save_objects",save_mode)&&!_wcsicmp(save_mode,L"changed");
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *item=&scan->occurrences[i];
        ProBoolean modified=PRO_B_TRUE;
        if(item->state!=PRO_FEAT_ACTIVE)continue;
        if(!item->model){error=PRO_TK_INCOMPLETE;break;}
        if(only_changed)error=ProMdlModificationVerify(item->model,&modified);
        if(!error&&modified)error=bwtl_save_check(transaction,item->model);
    }
    free(scan);
    return error;
}

/* Creo writes a new iteration. Both BWTL entry points share the directory
   mutex and first prove that the authoritative iteration has not changed. */
static ProError bwtl_save_guarded(ProMdl model,wchar_t *reply,size_t capacity){
    if(!reply||!capacity)return PRO_TK_BAD_INPUTS;
    reply[0]=0;
    if(bwtl_active_save)return PRO_TK_E_IN_USE;
    BwtlSaveTransaction transaction={0};
    ProError error=ProDirectoryCurrentGet(transaction.path);
    if(!error)error=bwtl_save_directory(transaction.path,&transaction.directory);
    if(error){
        wcscpy_s(reply,capacity,L"保存需要当前 Creo 工作目录位于 E 盘且可以核对。");
        return error;
    }
    wchar_t mutex_name[92];
    swprintf_s(mutex_name,_countof(mutex_name),L"Local\\BWTL.Save.%08lX.%08lX%08lX",
        transaction.directory.volume,transaction.directory.high,transaction.directory.low);
    HANDLE mutex=CreateMutexW(NULL,FALSE,mutex_name);
    if(!mutex)return PRO_TK_CANT_OPEN;
    const DWORD lock=WaitForSingleObject(mutex,0);
    if(lock!=WAIT_OBJECT_0&&lock!=WAIT_ABANDONED){
        CloseHandle(mutex);
        wcscpy_s(reply,capacity,L"另一个 Creo 会话正在向同一工作目录保存。请等这次保存结束后再核对模型。");
        return lock==WAIT_TIMEOUT?PRO_TK_E_IN_USE:PRO_TK_GENERAL_ERROR;
    }
    transaction.capacity=AX_MAX_OCCURRENCES+1;
    transaction.target=calloc(transaction.capacity,sizeof(*transaction.target));
    int submitted=0;
    error=transaction.target?0:PRO_TK_OUT_OF_MEMORY;
    if(!error)error=bwtl_save_check(&transaction,model);
    if(!error)error=part_regenerate(model);
    if(!error&&transaction.target[0].type==PRO_MDL_ASSEMBLY)error=bwtl_save_children(&transaction,model);
    if(!error&&(!storage_prepare_output()||!(bwtl_save_watches&4)))error=PRO_TK_BAD_CONTEXT;
    if(!error){
        submitted=1;
        bwtl_active_save=&transaction;
        error=ProMdlSave(model);
        bwtl_active_save=NULL;
    }
    BwtlSaveFile root={0};
    if(submitted&&!error&&!transaction.blocked){
        ProPath directory_after;
        BwtlSaveIdentity after;
        if(ProDirectoryCurrentGet(directory_after)||bwtl_save_directory(directory_after,&after)||
           !bwtl_save_same_identity(transaction.directory,after))error=PRO_TK_BAD_CONTEXT;
        for(int i=0;i<transaction.count&&!error;i++){
            BwtlSaveTarget *target=&transaction.target[i];
            BwtlSaveFile actual;
            error=bwtl_save_latest(transaction.path,target->name,target->type,&actual);
            if(!error&&actual.present)error=bwtl_save_note(target->model,target->name,target->type,
                transaction.directory,&actual,1);
            if(!error&&!i)root=actual;
        }
        if(!error&&!root.present)error=PRO_TK_E_NOT_FOUND;
    }
    if(transaction.blocked&&!error)error=PRO_TK_E_IN_USE;
    if(!error)swprintf_s(reply,capacity,L"Creo 已保存并回读 %ls%ls%ls。",transaction.path,
        transaction.path[wcslen(transaction.path)-1]==L'\\'?L"":L"\\",root.leaf);
    else if(transaction.blocked_model[0]&&!submitted)
        swprintf_s(reply,capacity,L"保存冲突：%ls 的磁盘版本与此 Creo 会话打开时的版本不同。请保留当前内存修改并重新核对最新版本；本次未写入。",transaction.blocked_model);
    else if(submitted)
        swprintf_s(reply,capacity,L"Creo 未完整确认这次保存（%d）。磁盘上可能已有部分结果，请先核对保存目录和原生回执。",error);
    else
        swprintf_s(reply,capacity,L"保存前核对未通过（Creo %d），请核对工作目录、模型或组件读取状态。本次未写入。",error);
    free(transaction.target);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return error;
}
