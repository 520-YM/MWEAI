
/* Exact edit/preservation clauses stay in Creo; ambiguous language goes to
   the existing image planner. Preservation itself is always checked natively. */
static int markup_keep_number(const wchar_t *text,int count){
    const wchar_t *p=markup_skip_space(text);
    if(*p<L'1'||*p>L'8')return -1;
    int index=*p++-L'1';
    if(index>=count)return -1;
    if(*p==L'号')p++;
    p=markup_skip_space(p);
    const wchar_t *const phrases[]={L"保留",L"保持原样",L"保持不变",L"不动",L"别动"};
    if(!markup_take_phrase(&p,phrases,_countof(phrases)))return -1;
    while(iswspace(*p)||*p==L'。')p++;
    return !*p?index:-1;
}

static int markup_preservation(const MarkupRequest *request,unsigned *keep){
    wchar_t clauses[_countof(request->text)];
    wcscpy_s(clauses,_countof(clauses),request->text);
    *keep=0;
    int neighbors=0;
    wchar_t *context=NULL;
    for(wchar_t *clause=wcstok_s(clauses,L"，,；;\r\n",&context);clause;clause=wcstok_s(NULL,L"，,；;\r\n",&context)){
        if(markup_neighbor_clause(clause))neighbors=1;
        int index=markup_keep_number(clause,request->count);
        if(index>=0)*keep|=1u<<index;
    }
    return neighbors;
}

static int markup_direct_partial(const MarkupRequest *request,MarkupEdit *edits,int *count){
    wchar_t clauses[_countof(request->text)];
    wcscpy_s(clauses,_countof(clauses),request->text);
    unsigned edited=0,protected=0;
    *count=0;
    int has_preservation=0;
    wchar_t *context=NULL;
    for(wchar_t *clause=wcstok_s(clauses,L"，,；;\r\n",&context);clause;clause=wcstok_s(NULL,L"，,；;\r\n",&context)){
        if(markup_neighbor_clause(clause)){has_preservation=1;continue;}
        int keep=markup_keep_number(clause,request->count);
        if(keep>=0){protected|=1u<<keep;has_preservation=1;continue;}
        const wchar_t *text=markup_skip_space(clause);
        unsigned group=0;
        const int numbered=*text>=L'1'&&*text<=L'8'&&
            (text[1]==L'号'||text[1]==L'、'||iswspace(text[1])||text[1]==L'改');
        if(numbered){
            for(;;){
                int index=*text++-L'1';
                if(index<0||index>=request->count||(group&(1u<<index)))return 0;
                group|=1u<<index;
                if(*text==L'号')text++;
                text=markup_skip_space(text);
                if(*text!=L'、')break;
                text=markup_skip_space(text+1);
                if(!*text)return 0;
            }
        }else{
            if(request->count!=1)return 0;
            group=1;
        }
        double value;
        if((edited&group)||!markup_parse_diameter(text,&value))return 0;
        edited|=group;
        for(int i=0;i<request->count;i++)if(group&(1u<<i)){
            edits[*count]=(MarkupEdit){i,MARKUP_DIAMETER,value};
            (*count)++;
        }
    }
    return has_preservation&&*count>0&&!(edited&protected);
}

typedef struct {
    unsigned char holes[MARKUP_MAX_HOLES],openings[MARKUP_MAX_OPENINGS];
    int count;
} MarkupPreserved;

static ProError markup_prepare_preserved(const MarkupCapture *capture,const MarkupRequest *request,
    const MarkupEdit *edits,const int *indices,int count,MarkupPreserved *preserved){
    unsigned keep;
    const int neighbors=markup_preservation(request,&keep);
    memset(preserved,0,sizeof(*preserved));
    if(!neighbors&&!keep)return PRO_TK_NO_ERROR;
    const MarkupImage *image=capture->image;
    if(neighbors&&image->truncated)return PRO_TK_BAD_CONTEXT;
    const MarkupOccurrence *scope=edits[0].measure==MARKUP_DIAMETER?&image->holes[indices[0]].occurrence:
        &image->openings[indices[0]].occurrence;
    if(neighbors)for(int i=0;i<image->count;i++)
        preserved->holes[i]=markup_same_occurrence(scope,&image->holes[i].occurrence);
    for(int i=0;i<count;i++){
        if(keep&(1u<<edits[i].mark))return PRO_TK_BAD_INPUTS;
        if(edits[i].measure==MARKUP_DIAMETER)preserved->holes[indices[i]]=0;
    }
    for(int i=0;i<request->count;i++){
        if(!(keep&(1u<<i)))continue;
        int hole=markup_match(image,&request->marks[i]);
        int opening=markup_match_opening(image,&request->marks[i]);
        if(hole>=0&&opening==-1&&markup_same_occurrence(scope,&image->holes[hole].occurrence))preserved->holes[hole]=1;
        else if(opening>=0&&hole==-1&&markup_same_occurrence(scope,&image->openings[opening].occurrence))preserved->openings[opening]=1;
        else return PRO_TK_BAD_CONTEXT;
    }
    for(int i=0;i<count;i++){
        if(edits[i].measure==MARKUP_DIAMETER?preserved->holes[indices[i]]:preserved->openings[indices[i]])return PRO_TK_BAD_INPUTS;
    }
    for(int i=0;i<image->count;i++)preserved->count+=preserved->holes[i];
    for(int i=0;i<image->opening_count;i++)preserved->count+=preserved->openings[i];
    /* A promise to retain a neighboring bore needs a resolvable bore. */
    return neighbors&&!preserved->count?PRO_TK_E_NOT_FOUND:PRO_TK_NO_ERROR;
}

static ProError markup_verify_preserved(ProMdl model,const MarkupImage *image,const MarkupPreserved *preserved){
    for(int i=0;i<image->count;i++)if(preserved->holes[i]){
        const MarkupHole *hole=&image->holes[i];
        ProError error=markup_bore_read(model,hole,hole->diameter);
        if(error)return error;
    }
    for(int i=0;i<image->opening_count;i++)if(preserved->openings[i]){
        const MarkupOpening *opening=&image->openings[i];
        ProError error=markup_opening_read(model,opening,opening->size[0],opening->size[1]);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}


static int markup_keep_left(const wchar_t *text){
    wchar_t line[256];
    const wchar_t *consent=markup_shared_consent_clause(text);
    if(consent){
        if(consent-text>=_countof(line))return 0;
        wcsncpy_s(line,_countof(line),text,consent-text);
        text=line;
    }
    const wchar_t *tail=wcsrchr(text,L'，');
    if(!tail)tail=wcsrchr(text,L',');
    if(!tail)return 0;
    tail=markup_skip_space(tail+1);
    const wchar_t *const words[]={L"左边位置别动",L"左边位置保持不变",L"左边别动",L"左边不动",L"保持左边位置"};
    if(!markup_take_phrase(&tail,words,_countof(words)))return 0;
    return tail[wcsspn(tail,L"。！! \t")]==0;
}

static int markup_left_side(const MarkupOpening *opening){
    int edge=0;
    for(int i=1;i<4;i++)
        if(opening->corners[i][0]+opening->corners[(i+1)%4][0]<
           opening->corners[edge][0]+opening->corners[(edge+1)%4][0])edge=i;
    const double dx=opening->corners[edge][0]-opening->corners[(edge+1)%4][0];
    const double dy=opening->corners[edge][1]-opening->corners[(edge+1)%4][1];
    return fabs(dy)>fabs(dx)*1.35?edge:-1;
}

static ProError markup_verify_opening_left(const MarkupImage *image,ProMdl model,
    const MarkupEdit *edits,const int *indices,const double *requested,int count,double scale,int after){
    if(count!=1||edits[0].measure==MARKUP_DIAMETER)return PRO_TK_BAD_INPUTS;
    const MarkupOpening *opening=&image->openings[indices[0]];
    const int edge=markup_left_side(opening);
    if(edge<0)return PRO_TK_BAD_CONTEXT;
    double sizes[2]={opening->size[0],opening->size[1]},corners[4][3];
    if(after)sizes[edits[0].measure-MARKUP_WIDTH]=requested[0]/scale;
    ProError error=markup_opening_read_geometry(model,opening,sizes[0],sizes[1],corners);
    if(error)return error;
    for(int j=0;j<2;j++){
        const int n=(edge+j)%4;
        double delta[3];
        ad_sub(corners[n],opening->native_corners[n],delta);
        if(hypot(hypot(delta[0],delta[1]),delta[2])*scale>0.001)return PRO_TK_BAD_CONTEXT;
    }
    return 0;
}

