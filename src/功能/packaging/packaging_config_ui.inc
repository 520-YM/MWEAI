/* The layout choices are session-local until their modeling handlers are added. */
typedef struct { const wchar_t *label; int image, group, value; RECT hit; } PkConfigCard;
static PkConfigCard pk_config_cards[48];
static int pk_config_count,pk_config_scroll,pk_config_limit;
static int pk_config_choice[]={1,0,1,1,0,1};
static HBITMAP pk_config_atlas;
static RECT pk_config_next;
static HWND pk_config_scrollbar;

static void pk_config_row(HDC dc,int width,int top,int height,int group,
    const wchar_t *const *labels,const int *images,int count,int reserved){
    const int columns=count+reserved;
    const int gap=6,margin=12;
    const int cell=(width-2*margin-(columns-1)*gap)/columns;
    HDC source=pk_config_atlas?CreateCompatibleDC(dc):NULL;
    HGDIOBJ old=source?SelectObject(source,pk_config_atlas):NULL;
    for(int i=0;i<columns;i++){
        PkConfigCard *card=&pk_config_cards[pk_config_count++];
        *card=(PkConfigCard){i<count?labels[i]:L"预留",i<count?images[i]:-1,group,i, 
            {margin+i*(cell+gap),top,margin+i*(cell+gap)+cell,top+height}};
        const int active=i<count&&pk_config_choice[group]==i;
        native_theme_round(dc,&card->hit,RGB(244,247,251),active?RGB(4,136,124):CHAT_FRAME_COLOR,6);
        RECT art=card->hit;InflateRect(&art,-6,-5);art.bottom-=22;
        if(source&&card->image>=0){
            int w=min(art.right-art.left,art.bottom-art.top),h=art.bottom-art.top;
            int x=(art.left+art.right-w)/2;
            SetStretchBltMode(dc,HALFTONE);
            StretchBlt(dc,x,art.top,w,h,source,(card->image%6)*160,(card->image/6)*150,160,150,SRCCOPY);
        }else if(i>=count){
            DrawTextW(dc,L"＋",-1,&art,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        RECT caption=card->hit;caption.left+=3;caption.right-=3;caption.top=caption.bottom-25;
        DrawTextW(dc,card->label,-1,&caption,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if(source){SelectObject(source,old);DeleteDC(source);}
}

static void pk_config_heading(HDC dc,int width,int y,const wchar_t *label){
    RECT bar={12,y,width-12,y+26};
    native_theme_round(dc,&bar,RGB(222,233,240),CHAT_FRAME_COLOR,5);
    bar.left+=9;
    DrawTextW(dc,label,-1,&bar,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
}

static int pk_config_opening_rows(void){
    const int type=pk_config_choice[0];
    return type==3||type==4?2:1;
}

static void pk_config_paint(HDC dc,RECT client){
    HGDIOBJ font=SelectObject(dc,native_font);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,CHAT_TEXT_COLOR);
    const int width=client.right-19;
    int y=50-pk_config_scroll;
    pk_config_count=0;
    int saved=SaveDC(dc);
    IntersectClipRect(dc,0,43,width,client.bottom-40);
    const wchar_t *types[]={L"飞机盒",L"重力纸箱",L"航空箱",L"飞机盒＋重力纸箱",L"航空箱＋重力纸箱"};
    const int type_art[]={0,1,2,3,4};
    const wchar_t *types2[]={L"木箱",L"天地盖",L"托盘纸箱"};
    const int type_art2[]={5,6,7};
    pk_config_heading(dc,width,y,L"包装类型");y+=30;
    pk_config_row(dc,width,y,100,0,types,type_art,5,0);y+=105;
    const int start=pk_config_count;
    int original=pk_config_choice[0];
    pk_config_choice[0]-=5;
    pk_config_row(dc,width,y,100,0,types2,type_art2,3,2);y+=107;
    pk_config_choice[0]=original;
    for(int i=start;i<pk_config_count;i++)pk_config_cards[i].value+=5;

    const wchar_t *flight[]={L"翻盖",L"双边翻盖",L"顶盖分离",L"底盖分离",L"双边盖分离"};
    const int flight_art[]={8,9,10,11,12};
    const wchar_t *paper[]={L"半翼",L"全翼"};
    const int paper_art[]={13,14};
    const int type=pk_config_choice[0];
    if(type==2||type==4){
        pk_config_heading(dc,width,y,type==4?L"开启方式 · 内层航空箱":L"开启方式 · 航空箱");y+=30;
        pk_config_row(dc,width,y,100,1,flight,flight_art,5,2);y+=107;
    }
    if(type==0||type==3||type>=5){
        const wchar_t *other[]={type==6?L"顶盖分离":type==5?L"顶盖分离":L"翻盖"};
        const int other_art[]={type==6?6:type==5?5:type==7?7:0};
        pk_config_heading(dc,width,y,type==3?L"开启方式 · 内层飞机盒":L"开启方式");y+=30;
        pk_config_row(dc,width,y,100,4,other,other_art,1,2);y+=107;
    }
    if(type==1||type==3||type==4){
        pk_config_heading(dc,width,y,(type==3||type==4)?L"开启方式 · 外层重力纸箱":L"开启方式 · 重力纸箱");y+=30;
        pk_config_row(dc,width,y,100,2,paper,paper_art,2,2);y+=107;
    }
    const wchar_t *materials[]={L"EVA",L"EPE泡棉",L"EPS",L"蜂窝纸板"};
    const int material_art[]={15,16,17,18};
    pk_config_heading(dc,width,y,L"填充材质");y+=30;
    pk_config_row(dc,width,y,100,3,materials,material_art,4,1);y+=107;
    const wchar_t *patterns[]={L"全包",L"半包",L"卡棉",L"护角"};
    const int pattern_art[]={19,20,21,22};
    /* Store padding style independently from the generic opening choice. */
    pk_config_heading(dc,width,y,L"填充形式");y+=30;
    pk_config_row(dc,width,y,115,5,patterns,pattern_art,4,0);y+=120;
    pk_config_row(dc,width,y,43,5,NULL,NULL,0,4);
    RestoreDC(dc,saved);
    pk_config_next=(RECT){max(14,width-133),client.bottom-35,width-12,client.bottom-5};
    native_theme_round(dc,&pk_config_next,RGB(12,116,108),RGB(12,116,108),6);
    SetTextColor(dc,RGB(255,255,255));
    DrawTextW(dc,L"下一步",-1,&pk_config_next,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SetTextColor(dc,CHAT_TEXT_COLOR);
    RECT note={14,client.bottom-35,pk_config_next.left-8,client.bottom-5};
    DrawTextW(dc,L"图卡仅用于方案配置，建模暂用预览页参数。",-1,&note,DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,font);
}
static void pk_config_scroll_to(HWND window,int offset){
    RECT r;GetClientRect(window,&r);
    SCROLLINFO info={sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
    info.nMax= 30+137*pk_config_opening_rows()+210+137+30+120+43;
    info.nPage=max(1,r.bottom-90);
    pk_config_limit=max(0,info.nMax-(int)info.nPage+1);
    pk_config_scroll=max(0,min(offset,pk_config_limit));
    info.nPos=pk_config_scroll;
    SetScrollInfo(pk_config_scrollbar,SB_CTL,&info,TRUE);
    InvalidateRect(window,NULL,FALSE);
}


static void pk_config_summary(wchar_t *text,size_t capacity){
    const wchar_t *type[]={L"飞机盒",L"重力纸箱",L"航空箱",L"飞机盒＋重力纸箱",L"航空箱＋重力纸箱",L"木箱",L"天地盖",L"托盘纸箱"};
    const wchar_t *flight[]={L"翻盖",L"双边翻盖",L"顶盖分离",L"底盖分离",L"双边盖分离"};
    const wchar_t *paper[]={L"半翼",L"全翼"};
    const wchar_t *materials[]={L"EVA",L"EPE泡棉",L"EPS",L"蜂窝纸板"};
    const wchar_t *padding[]={L"全包",L"半包",L"卡棉",L"护角"};
    int selected=pk_config_choice[0];
    wchar_t opening[64];
    if(selected==3||selected==4)
        swprintf_s(opening,_countof(opening),L"内层 %ls / 外层 %ls",selected==3?L"翻盖":flight[pk_config_choice[1]],paper[pk_config_choice[2]]);
    else wcscpy_s(opening,_countof(opening),selected==1?paper[pk_config_choice[2]]:
        selected==2?flight[pk_config_choice[1]]:selected==5||selected==6?L"顶盖分离":L"翻盖");
    swprintf_s(text,capacity,L"已选配置  %ls · %ls · %ls · %ls",type[selected],opening,materials[pk_config_choice[3]],padding[pk_config_choice[5]]);
}
