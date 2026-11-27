#define MARKUP_OPEN (WM_APP+60)
#define MARKUP_APPLY (WM_APP+61)
#define MARKUP_READY (WM_APP+62)
#define MARKUP_RESULT (WM_APP+63)
#define MARKUP_PROGRESS (WM_APP+64)
#define MARKUP_CANCEL (WM_APP+65)
#define MARKUP_MAX_HOLES 256
#define MARKUP_MAX_OPENINGS 64
#define MARKUP_MAX_MARKS 8
#define MARKUP_MAX_EDITS (2*MARKUP_MAX_MARKS)
#define MARKUP_PATH_DEPTH 16

typedef struct {
    int depth,ids[MARKUP_PATH_DEPTH];
    double mm_per_native;
} MarkupOccurrence;

static int markup_same_occurrence(const MarkupOccurrence *a,const MarkupOccurrence *b){
    return a->depth==b->depth&&!memcmp(a->ids,b->ids,a->depth*sizeof(int));
}

typedef struct {
    int feature,dimension,dimension_owner;
    double old_value,x,y,rx,ry;
    double diameter;
    int surface;
    double center[3],axis[3],depth;
    MarkupOccurrence occurrence;
} MarkupHole;

typedef struct {
    int feature,owner,dimension[2];
    double size[2],x,y;
    double corners[4][2],native_corners[4][3];
    MarkupOccurrence occurrence;
} MarkupOpening;

typedef struct {
    HBITMAP bitmap;
    int width,height,count,truncated,opening_count,openings_truncated,assembly;
    unsigned long ticket;
    wchar_t name[128];
    double mm_per_native;
    MarkupHole holes[MARKUP_MAX_HOLES];
    MarkupOpening openings[MARKUP_MAX_OPENINGS];
} MarkupImage;

typedef enum { MARKUP_CIRCLE, MARKUP_ARROW, MARKUP_BOX } MarkupTool;
typedef struct {
    MarkupTool tool;
    double x1,y1,x2,y2;
    int valid;
} MarkupMark;

typedef struct {
    unsigned long ticket;
    MarkupMark marks[MARKUP_MAX_MARKS];
    int count;
    wchar_t text[256];
} MarkupRequest;

typedef struct {
    unsigned long ticket;
    int success,retake;
    wchar_t text[2000];
} MarkupResult;

static int markup_area_contains(const MarkupMark *mark,double px,double py){
    double x=(mark->x1+mark->x2)*0.5, y=(mark->y1+mark->y2)*0.5;
    double rx=fabs(mark->x1-mark->x2)*0.5, ry=fabs(mark->y1-mark->y2)*0.5;
    if(mark->tool==MARKUP_CIRCLE)
        return rx>=3&&ry>=3&&hypot((x-px)/rx,(y-py)/ry)<=1;
    return fabs(x-px)<=rx&&fabs(y-py)<=ry;
}

/* Negative counts mean a stale/incomplete target list or overflow. */
static int markup_hole_hits(const MarkupImage *image,const MarkupMark *mark,int *ids,int capacity){
    if(!mark->valid||image->truncated)return -1;
    int count=0;
    for(int i=0;i<image->count;i++){
        const MarkupHole *hole=&image->holes[i];
        int hit=mark->tool==MARKUP_ARROW?
            hypot(mark->x2-hole->x,mark->y2-hole->y)<=fmax(7,fmax(hole->rx,hole->ry)+3):
            markup_area_contains(mark,hole->x,hole->y);
        if(!hit)continue;
        if(count==capacity)return -2;
        ids[count++]=i;
    }
    return count;
}

static int markup_match(const MarkupImage *image,const MarkupMark *mark){
    int result;
    int count=markup_hole_hits(image,mark,&result,1);
    return count==1?result:count==-2?-2:-1;
}
static int markup_match_opening(const MarkupImage *image,const MarkupMark *mark){
    if(!mark->valid||image->openings_truncated)return -1;
    int found=-1;
    for(int i=0;i<image->opening_count;i++){
        const MarkupOpening *opening=&image->openings[i];
        int hit=markup_area_contains(mark,opening->x,opening->y);
        if(mark->tool==MARKUP_ARROW){
            int positive=0,negative=0;
            for(int j=0;j<4;j++){
                const double *a=opening->corners[j],*b=opening->corners[(j+1)%4];
                double dx=b[0]-a[0],dy=b[1]-a[1];
                double cross=(dx*(mark->y2-a[1])-dy*(mark->x2-a[0]))/hypot(dx,dy);
                positive|=cross>5;
                negative|=cross<-5;
            }
            hit=!(positive&&negative);
        }
        if(hit){
            if(found>=0)return -2;
            found=i;
        }
    }
    return found;
}


/* A tip on a single side (away from ambiguous corners) binds to the native
   rectangular cut and the corresponding sketch axis. */
static int markup_opening_edge(const MarkupImage *image,const MarkupMark *mark,int *axis){
    if(!mark->valid||mark->tool!=MARKUP_ARROW||image->openings_truncated)return -1;
    int match=-1;
    for(int i=0;i<image->opening_count;i++)for(int e=0;e<4;e++){
        const double *a=image->openings[i].corners[e],*b=image->openings[i].corners[(e+1)%4];
        double dx=b[0]-a[0],dy=b[1]-a[1],len=hypot(dx,dy);
        if(len<8)continue;
        double along=((mark->x2-a[0])*dx+(mark->y2-a[1])*dy)/len;
        if(along<5||along>len-5||fabs((mark->y2-a[1])*dx-(mark->x2-a[0])*dy)>5*len)continue;
        if(match>=0)return -2;
        match=i;
        *axis=e&1;
    }
    return match;
}

static void markup_image_free(MarkupImage *image){
    if(!image)return;
    if(image->bitmap)DeleteObject(image->bitmap);
    free(image);
}

#include "markup_diameter.inc"
