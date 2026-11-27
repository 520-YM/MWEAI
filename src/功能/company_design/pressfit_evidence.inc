/* The digest alone grants no access. Every lookup reopens the same registered
   regular file; its held Windows handle excludes concurrent writers and renames. */
typedef struct {
    wchar_t path[MAX_PATH],hash[65];
    DWORD volume,id_high,id_low,size_high,size_low;
    LONGLONG changed;
} PressfitHashEntry;

static PressfitHashEntry pressfit_hash_cache[64];
static unsigned pressfit_hash_next;

static void pressfit_hash_refresh(void){
    ZeroMemory(pressfit_hash_cache,sizeof(pressfit_hash_cache));
    pressfit_hash_next=0;
}

static int pressfit_evidence_hash(const wchar_t *path,wchar_t digest[65]){
    wchar_t resolved[MAX_PATH];
    HANDLE locked=company_open_path(path,0,resolved);
    if(locked==INVALID_HANDLE_VALUE)return 0;
    BY_HANDLE_FILE_INFORMATION info;
    FILE_BASIC_INFO times;
    const int cacheable=GetFileInformationByHandle(locked,&info)&&
        GetFileInformationByHandleEx(locked,FileBasicInfo,&times,sizeof(times))&&
        (info.nFileIndexHigh||info.nFileIndexLow)&&times.ChangeTime.QuadPart>0;
    PressfitHashEntry *use=NULL;
    if(cacheable){
        for(size_t i=0;i<_countof(pressfit_hash_cache);i++){
            PressfitHashEntry *entry=pressfit_hash_cache+i;
            if(!entry->path[0]||_wcsicmp(entry->path,resolved))continue;
            use=entry;
            if(entry->volume==info.dwVolumeSerialNumber&&entry->id_high==info.nFileIndexHigh&&
               entry->id_low==info.nFileIndexLow&&entry->size_high==info.nFileSizeHigh&&
               entry->size_low==info.nFileSizeLow&&entry->changed==times.ChangeTime.QuadPart){
                wcscpy_s(digest,65,entry->hash);
                CloseHandle(locked);
                return 1;
            }
            break;
        }
    }
    ULONGLONG bytes;
    const int ok=dv_sha256(resolved,digest,&bytes);
    CloseHandle(locked);
    if(cacheable&&ok){
        if(!use)use=pressfit_hash_cache+(pressfit_hash_next++%_countof(pressfit_hash_cache));
        wcscpy_s(use->path,_countof(use->path),resolved);
        wcscpy_s(use->hash,_countof(use->hash),digest);
        use->volume=info.dwVolumeSerialNumber;
        use->id_high=info.nFileIndexHigh;
        use->id_low=info.nFileIndexLow;
        use->size_high=info.nFileSizeHigh;
        use->size_low=info.nFileSizeLow;
        use->changed=times.ChangeTime.QuadPart;
    }
    return ok;
}

static int pressfit_acceptance_source_matches(const PressfitAcceptance *row,const wchar_t *source){
    wchar_t digest[65];
    return pressfit_evidence_hash(source,digest)&&!_wcsicmp(digest,row->sha256);
}

static void pressfit_acceptance_find_source(const CompanyFastenerCatalog *catalog,
    const PressfitSelector *selector,PressfitMatch *match){
    for(size_t i=0;i+1<_countof(pressfit_acceptance_rows);i++){
        const PressfitAcceptance *row=pressfit_acceptance_rows+i;
        const int score=pressfit_match_name(row->instance,selector->instance,selector->instance_key);
        if(!score||score<match->score)continue;
        for(size_t j=0;j<catalog->count;j++){
            const CompanyFastenerItem *item=catalog->items+j;
            ProName family;
            if((j&&!company_fastener_catalog_key(item,item-1))||
               _wcsicmp(item->path+item->name_offset,row->source)||
               !pressfit_model_name(item,family)||
               (selector->generic[0]&&!pressfit_match_name(family,selector->generic,selector->generic_key)))continue;
            int indexed=0;
            for(unsigned k=0;k<match->matches&&k<_countof(match->choices);k++){
                const PressfitSource *choice=match->choices+k;
                if(!_wcsicmp(choice->path,item->path)&&!_wcsicmp(choice->instance,row->instance)&&
                   !_wcsicmp(choice->generic,family)){indexed=1;break;}
            }
            if(!indexed&&pressfit_acceptance_source_matches(row,item->path))
                pressfit_add_match(match,score,row->instance,family,item);
        }
    }
}

/* A missing index may hide a known alternate family, even if only one of
   the two exact rows passed acceptance. A reservation can deny a bare
   spelling but cannot select, authorize, or place an unverified model. */
static void pressfit_preserve_registered_ambiguity(const CompanyFastenerCatalog *catalog,
    const PressfitSelector *selector,PressfitMatch *match){
    if(selector->generic[0])return;
    static const struct { const wchar_t *instance,*family; } conflicts[]={
#include "pressfit_duplicate_rows.inc"
    };
    PressfitMatch known={0};
    for(size_t i=0;i<_countof(conflicts);i++){
        const int score=pressfit_match_name(conflicts[i].instance,selector->instance,selector->instance_key);
        if(!score||score<match->score)continue;
        for(size_t j=0;j<catalog->count;j++){
            const CompanyFastenerItem *item=catalog->items+j;
            ProName family;
            if((j&&!company_fastener_catalog_key(item,item-1))||!pressfit_model_name(item,family)||
               _wcsicmp(family,conflicts[i].family))continue;
            pressfit_add_match(&known,score,conflicts[i].instance,family,item);
        }
    }
    if(match->matches<2&&known.matches>1&&known.score>=match->score)*match=known;
}

