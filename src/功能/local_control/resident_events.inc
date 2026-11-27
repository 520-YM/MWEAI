/* In-process event delivery for the live pipe. The existing journal still
   persists the complete record; this ring is only a bounded reconnect window. */
#define BWTL_RESIDENT_EVENT_COUNT 24

typedef struct {
    ULONGLONG sequence;
    DWORD length;
    char json[BWTL_LIVE_LINE];
} BwtlResidentEvent;

static SRWLOCK bwtl_resident_event_lock=SRWLOCK_INIT;
static BwtlResidentEvent bwtl_resident_events[BWTL_RESIDENT_EVENT_COUNT];
static HANDLE bwtl_resident_event_changed;
static ULONGLONG bwtl_resident_event_latest;
static unsigned bwtl_resident_event_total;

static void bwtl_resident_event_record(ULONGLONG sequence,const char *bytes,DWORD length){
    if(!length||length>BWTL_LIVE_LINE)return;
    AcquireSRWLockExclusive(&bwtl_resident_event_lock);
    BwtlResidentEvent *row=&bwtl_resident_events[(sequence-1)%BWTL_RESIDENT_EVENT_COUNT];
    row->sequence=sequence;
    row->length=length;
    memcpy(row->json,bytes,length);
    bwtl_resident_event_latest=sequence;
    if(bwtl_resident_event_total<BWTL_RESIDENT_EVENT_COUNT)bwtl_resident_event_total++;
    if(bwtl_resident_event_changed)SetEvent(bwtl_resident_event_changed);
    ReleaseSRWLockExclusive(&bwtl_resident_event_lock);
}

static ULONGLONG bwtl_resident_event_cursor(void){
    AcquireSRWLockShared(&bwtl_resident_event_lock);
    const ULONGLONG sequence=bwtl_resident_event_latest;
    ReleaseSRWLockShared(&bwtl_resident_event_lock);
    return sequence;
}

static DWORD bwtl_resident_event_next(ULONGLONG *cursor,char *body,size_t capacity){
    DWORD length=0;
    AcquireSRWLockExclusive(&bwtl_resident_event_lock);
    const ULONGLONG latest=bwtl_resident_event_latest;
    if(*cursor<latest&&bwtl_resident_event_total){
        const ULONGLONG oldest=latest-bwtl_resident_event_total+1;
        if(*cursor+1<oldest){
            const int written=sprintf_s(body,capacity,
                "{\"kind\":\"gap\",\"seq\":%llu,\"firstAvailable\":%llu}",oldest-1,oldest);
            if(written>0){length=(DWORD)written;*cursor=oldest-1;}
        }else{
            const BwtlResidentEvent *row=&bwtl_resident_events[*cursor%BWTL_RESIDENT_EVENT_COUNT];
            if(row->sequence==*cursor+1&&row->length<=capacity){
                length=row->length;
                memcpy(body,row->json,length);
                *cursor=row->sequence;
            }
        }
    }
    if(!length)ResetEvent(bwtl_resident_event_changed);
    ReleaseSRWLockExclusive(&bwtl_resident_event_lock);
    return length;
}
