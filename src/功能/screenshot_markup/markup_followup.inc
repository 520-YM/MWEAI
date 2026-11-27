
typedef struct {
    ProMdl root;
    ProWVerstamp stamp;
    int count;
    int protect_neighbors;
    MarkupHole holes[MARKUP_MAX_MARKS];
} MarkupLastHoles;

static MarkupLastHoles markup_last_holes;

static void markup_remember_holes(const MarkupCapture *capture,const MarkupRequest *request,
    const MarkupEdit *edits,const int *indices,int count,const MarkupPreserved *preserved){
    MarkupLastHoles *last=&markup_last_holes;
    if(last->stamp)ProVerstampFree(&last->stamp);
    memset(last,0,sizeof(*last));
    last->root=capture->model;
    if(ProMdlVerstampGet(last->root,&last->stamp))return;
    unsigned keep;
    last->protect_neighbors=markup_preservation(request,&keep);
    const MarkupImage *image=capture->image;
    int recorded[MARKUP_MAX_MARKS];
    for(int i=0;i<count&&last->count<MARKUP_MAX_MARKS;i++){
        if(edits[i].measure!=MARKUP_DIAMETER)continue;
        int old=0;
        for(int j=0;j<last->count;j++)old|=recorded[j]==indices[i];
        if(old)continue;
        recorded[last->count]=indices[i];
        last->holes[last->count++]=image->holes[indices[i]];
    }
    for(int i=0;i<request->count&&last->count<MARKUP_MAX_MARKS;i++){
        int chosen=markup_match(image,&request->marks[i]);
        if(chosen<0)continue;
        int old=0;
        for(int j=0;j<last->count;j++)old|=recorded[j]==chosen;
        if(old)continue;
        recorded[last->count]=chosen;
        last->holes[last->count++]=image->holes[chosen];
    }
    for(int i=0;i<image->count&&last->count<MARKUP_MAX_MARKS;i++)if(preserved->holes[i]){
        int old=0;
        for(int j=0;j<last->count;j++)old|=recorded[j]==i;
        if(old)continue;
        recorded[last->count]=i;
        last->holes[last->count++]=image->holes[i];
    }
}

static int markup_same_bore(const MarkupHole *first,const MarkupHole *second){
    double delta[3];
    ad_sub(first->center,second->center,delta);
    const double axial=ad_dot(delta,first->axis);
    for(int i=0;i<3;i++)delta[i]-=axial*first->axis[i];
    return first->feature==second->feature&&markup_same_occurrence(&first->occurrence,&second->occurrence)&&
        fabs(ad_dot(first->axis,second->axis))>1-1e-7&&hypot(hypot(delta[0],delta[1]),delta[2])<1e-5;
}

static int markup_followup(const MarkupCapture *capture,MarkupRequest *request,MarkupEdit *edit,int *count,
    wchar_t *reply,size_t capacity,ProError *error){
    if(request->count)return 0;
    int side;
    double delta;
    if(!markup_followup_parse(request->text,&side,&delta))return 0;
    MarkupLastHoles *last=&markup_last_holes;
    const MarkupImage *image=capture->image;
    int found=-1,matches=0;
    *error=PRO_TK_BAD_CONTEXT;
    if(last->root==capture->model&&last->stamp&&capture->stamp&&ProVerstampEqual(last->stamp,capture->stamp)){
        double extreme=0;
        int tied=0;
        for(int i=0;i<image->count;i++){
            for(int j=0;j<last->count;j++){
                if(!markup_same_bore(&last->holes[j],&image->holes[i]))continue;
                const double x=image->holes[i].x;
                if(found<0||side*x>extreme+3){found=i;extreme=side*x;tied=0;}
                else if(side&&fabs(side*x-extreme)<=3)tied=1;
                matches++;
                break;
            }
        }
        if(tied)found=-1;
    }
    if(found<0||(!side&&matches!=1)||(side&&(last->count<2||matches<2))){
        wcscpy_s(reply,capacity,L"没能在当前截图里唯一找回刚才的目标孔。请在这张图上重新标一下。");
        return 1;
    }
    const MarkupHole *hole=&image->holes[found];
    const double scale=hole->occurrence.depth?hole->occurrence.mm_per_native:image->mm_per_native;
    request->marks[0]=(MarkupMark){MARKUP_CIRCLE,hole->x-4,hole->y-4,hole->x+4,hole->y+4,1};
    request->count=1;
    if(markup_match(image,&request->marks[0])!=found){
        wcscpy_s(reply,capacity,L"刚才的孔在这张截图中投影重叠。请换个角度再选。");
        return 1;
    }
    if(last->protect_neighbors)wcscat_s(request->text,_countof(request->text),L"，旁边的孔别动");
    *edit=(MarkupEdit){0,MARKUP_DIAMETER,hole->diameter*scale+delta};
    *count=1;
    *error=PRO_TK_NO_ERROR;
    return 1;
}

static int markup_measure_request(const wchar_t *text){
    if(!wcscmp(text,L"测量标注")||!wcscmp(text,L"检查干涉")||!wcscmp(text,L"测量间隙"))return 1;
    const RequestAssessment assessed=request_assess(text,0);
    const wchar_t *const terms[]={L"孔距多少",L"间隙多少",L"差多少间隙",L"有没有干涉",L"对齐没有",L"是否对齐",L"测量两处",L"孔间距"};
    return !assessed.execution_requested&&smart_has_text(text,terms,_countof(terms));
}

static void markup_measure_part(const MarkupCapture *capture,const MarkupRequest *request,wchar_t *reply,size_t capacity){
    const MarkupImage *image=capture->image;
    const double mm=image->mm_per_native;
    if(request->count==2){
        const int a=markup_match(image,&request->marks[0]),b=markup_match(image,&request->marks[1]);
        if(a>=0&&b>=0&&a!=b&&
           !markup_bore_read(capture->model,&image->holes[a],image->holes[a].diameter)&&
           !markup_bore_read(capture->model,&image->holes[b],image->holes[b].diameter)){
            const MarkupHole *first=&image->holes[a],*second=&image->holes[b];
            ProVector delta,normal;
            ad_sub(first->center,second->center,delta);
            memcpy(normal,first->axis,sizeof(ProVector));
            const double across=ad_dot(delta,normal);
            double center_distance=hypot(hypot(delta[0],delta[1]),delta[2]);
            for(int i=0;i<3;i++)delta[i]-=across*normal[i];
            const double axes=hypot(hypot(delta[0],delta[1]),delta[2])*mm;
            swprintf_s(reply,capacity,L"Creo 孔口中心距离 %.8g mm。1 号 Ø%.8g mm，2 号 Ø%.8g mm。",
                center_distance*mm,first->diameter*mm,second->diameter*mm);
            if(fabs(ad_dot(normal,second->axis))>1-1e-7){
                wchar_t report[120];
                swprintf_s(report,_countof(report),L"两条孔轴平行，间距 %.8g mm。",axes);
                if(wcslen(reply)+wcslen(report)<capacity)wcscat_s(reply,capacity,report);
            }
            return;
        }
        MarkupFace hits[2];
        ProPoint3d points[2];
        if(!markup_face_hit(capture,&request->marks[0],&hits[0],points[0])&&
           !markup_face_hit(capture,&request->marks[1],&hits[1],points[1])){
            ProVector delta;
            ad_sub(points[0],points[1],delta);
            swprintf_s(reply,capacity,L"Creo 标注处表面点距 %.8g mm。",hypot(hypot(delta[0],delta[1]),delta[2])*mm);
            return;
        }
    }else if(request->count==1){
        int i=markup_match(image,&request->marks[0]);
        if(i>=0&&!markup_bore_read(capture->model,&image->holes[i],image->holes[i].diameter)){
            const MarkupHole *hole=&image->holes[i];
            swprintf_s(reply,capacity,L"Creo 实测 1 号孔 Ø%.8g mm，圆柱孔壁深 %.8g mm。",hole->diameter*mm,hole->depth*mm);
            return;
        }
    }
    wcscpy_s(reply,capacity,L"请标出一个要测孔径的孔，或标出两处要测间距的目标。");
}


static int markup_opening_adjustment(const MarkupCapture *capture,const MarkupRequest *request,
    MarkupEdit *edit,wchar_t *reply,size_t capacity,ProError *error){
    if(!markup_keep_left(request->text))return 0;
    const wchar_t *text=markup_skip_space(request->text);
    const wchar_t *const targets[]={L"把这个窗口",L"把这个开口",L"这个窗口",L"这个开口",L"窗口",L"开口"};
    if(!markup_take_phrase(&text,targets,_countof(targets)))return 0;
    const wchar_t *const widen[]={L"加宽",L"放宽",L"宽度增加"};
    const wchar_t *const narrow[]={L"缩窄",L"收窄",L"宽度减少"};
    const int plus=markup_take_phrase(&text,widen,_countof(widen));
    double value;
    if((!plus&&!markup_take_phrase(&text,narrow,_countof(narrow)))||!markup_read_size(&text,&value))return 0;
    markup_read_mm(&text);
    if(*text!=L'，'&&*text!=L',')return 0;
    int index=request->count==1?markup_match_opening(capture->image,&request->marks[0]):-1;
    int edge=index>=0?markup_left_side(&capture->image->openings[index]):-1;
    if(edge<0){
        *error=PRO_TK_BAD_CONTEXT;
        wcscpy_s(reply,capacity,L"请正视并单独标出一个矩形开口，方便核对原来的左边位置。");
        return 1;
    }
    const MarkupOpening *opening=&capture->image->openings[index];
    const int dimension=1-(edge&1);
    const double scale=opening->occurrence.depth?opening->occurrence.mm_per_native:capture->image->mm_per_native;
    *edit=(MarkupEdit){0,dimension?MARKUP_HEIGHT:MARKUP_WIDTH,opening->size[dimension]*scale+(plus?value:-value)};
    *error=0;
    return 1;
}

