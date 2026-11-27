typedef struct {
    int id,width;
    const wchar_t *label;
} MarkupToolbarButton;

static const MarkupToolbarButton markup_toolbar[]={
    {MARKUP_TOOL_CIRCLE,56,L"圈选"},{MARKUP_TOOL_ARROW,56,L"箭头"},{MARKUP_TOOL_BOX,56,L"框选"},
    {MARKUP_MEASURE,56,L"测量"},{MARKUP_UNDO,56,L"撤销"},{MARKUP_CLEAR,56,L"清空"},
    {MARKUP_RECAPTURE,56,L"重截"},{MARKUP_FIT,56,L"适合"},
    {MARKUP_ZOOM_OUT,30,L"−"},{MARKUP_ZOOM_IN,30,L"+"},{MARKUP_PANEL,66,L"收起面板"},
    {MARKUP_CLOSE,75,L"关闭"}
};

static int markup_px(int value){
    return MulDiv(value,markup_dpi?markup_dpi:native_dpi?native_dpi:96,96);
}

static COLORREF markup_mark_color(int index){
    static const COLORREF colors[]={
        RGB(186,101,9), RGB(0,117,106), RGB(42,103,180), RGB(144,82,157)
    };
    return colors[index%_countof(colors)];
}

static void markup_instance_hint(wchar_t *out,size_t capacity,const MarkupOccurrence *occurrence){
    if(occurrence->depth<1)return;
    wcscat_s(out,capacity,L" · 实例 ");
    for(int i=0;i<occurrence->depth&&i<_countof(occurrence->ids);i++){
        const size_t used=wcslen(out);
        if(_snwprintf_s(out+used,capacity-used,_TRUNCATE,L"%ls%d",i?L"/":L"",occurrence->ids[i])<0)break;
    }
}

static void markup_describe_mark(const MarkupImage *image,const MarkupMark *mark,const wchar_t *input,int only,
    wchar_t *out,size_t capacity){
    int hits[MARKUP_MAX_EDITS],axis;
    int count=markup_hole_hits(image,mark,hits,mark->tool==MARKUP_ARROW?1:MARKUP_MAX_EDITS);
    int opening=markup_match_opening(image,mark);
    int edge=markup_opening_edge(image,mark,&axis);
    if(edge>=0&&!markup_named_hole(input)){
        const MarkupOpening *target=&image->openings[edge];
        const double mm=image->assembly?target->occurrence.mm_per_native:image->mm_per_native;
        swprintf_s(out,capacity,L"开口 %lc 向边 %.8g mm",axis?L'Y':L'X',target->size[axis]*mm);
        markup_instance_hint(out,capacity,&target->occurrence);
    }else if(count==1&&(opening==-1||markup_named_hole(input))){
        const MarkupHole *target=&image->holes[hits[0]];
        const double mm=image->assembly?target->occurrence.mm_per_native:image->mm_per_native;
        double next;
        if(target->dimension>=0&&only&&markup_display_diameter(input,&next))
            swprintf_s(out,capacity,L"孔径 Ø%.8g → Ø%.8g mm",target->diameter*mm,next);
        else
            swprintf_s(out,capacity,L"圆孔 Ø%.8g mm%ls",target->diameter*mm,target->dimension<0?L" · 无独立尺寸":L"");
        markup_instance_hint(out,capacity,&target->occurrence);
    }else if(count>1){
        swprintf_s(out,capacity,L"选区内 %d 个圆孔",count);
    }else if(opening>=0&&count==0){
        const MarkupOpening *target=&image->openings[opening];
        const double mm=image->assembly?target->occurrence.mm_per_native:image->mm_per_native;
        swprintf_s(out,capacity,L"矩形开口 %.8g × %.8g mm",target->size[0]*mm,target->size[1]*mm);
        markup_instance_hint(out,capacity,&target->occurrence);
    }else if(count==-2||opening==-2||(count>0&&opening>=0)){
        wcscpy_s(out,capacity,L"多个候选，请缩小标注范围");
    }else{
        wcscpy_s(out,capacity,L"待识别 · 可检查参照");
    }
}

static void markup_place(HWND control,int x,int y,int width,int height,int visible){
    if(!control)return;
    SetWindowPos(control,NULL,x,y,max(1,width),max(1,height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS|
        (visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
}

static void markup_editor_relayout(HWND window){
    RECT client;
    GetClientRect(window,&client);
    int margin=markup_px(10),gap=markup_px(5),row=markup_px(30),step=row+gap;
    int x=margin,y=margin;
    for(int i=0;i<_countof(markup_toolbar);i++){
        const MarkupToolbarButton *button=&markup_toolbar[i];
        const int width=markup_px(button->width);
        if(x>margin&&x+width>client.right-margin){x=margin;y+=step;}
        markup_place(GetDlgItem(window,button->id),x,y,width,row,1);
        x+=width+gap;
    }
    HWND scope=GetDlgItem(window,MARKUP_CONTEXT);
    int scope_width=min(markup_px(285),client.right-x-margin);
    if(scope_width<markup_px(175)){
        x=margin;
        y+=step;
        scope_width=client.right-2*margin;
    }else x=client.right-margin-scope_width;
    markup_place(scope,x,y,scope_width,row,1);
    SetWindowTextW(GetDlgItem(window,MARKUP_PANEL),markup_panel_hidden?L"展开面板":L"收起面板");

    const int top=y+step+gap,bottom=client.bottom-margin;
    int panel_x,panel_y,panel_w,panel_h;
    int canvas_w=client.right-2*margin,canvas_h=bottom-top;
    const int wide=client.right>=markup_px(820);
    if(markup_panel_hidden)panel_x=panel_y=panel_w=panel_h=0;
    else if(wide){
        panel_w=max(markup_px(286),min(markup_px(408),client.right*31/100));
        panel_x=client.right-margin-panel_w;
        panel_y=top;
        panel_h=canvas_h;
        canvas_w=panel_x-2*margin;
    }else{
        panel_x=margin;
        panel_w=canvas_w;
        panel_h=max(markup_px(126),min(markup_px(194),canvas_h*52/100));
        panel_y=bottom-panel_h;
        canvas_h=panel_y-top-margin;
    }
    markup_place(markup_canvas,margin,top,canvas_w,canvas_h,1);
    HWND heading=GetDlgItem(window,MARKUP_SUBMIT_LABEL);
    HWND check=GetDlgItem(window,MARKUP_CHECK);
    int x0=panel_x+gap,y0=panel_y+gap,inner=panel_w-2*gap;
    int last=panel_y+panel_h-gap-row,heading_h=markup_px(23);
    int show=!markup_panel_hidden;
    markup_place(heading,x0,y0,inner,heading_h,show);
    int body_y=y0+heading_h+gap,body_h=max(1,last-body_y-gap);
    if(wide){
        const int summary_h=min(markup_px(112),body_h*28/100);
        const int input_h=body_h*37/100;
        const int result_y=body_y+summary_h+input_h+2*gap;
        markup_place(markup_summary,x0,body_y,inner,summary_h,show);
        markup_place(markup_input,x0,body_y+summary_h+gap,inner,input_h,show);
        markup_place(markup_status,x0,result_y,inner,max(1,last-result_y-gap),show);
    }else{
        const int first=inner*28/100,second=inner*35/100;
        markup_place(markup_summary,x0,body_y,first-gap,body_h,show);
        markup_place(markup_input,x0+first,body_y,second-gap,body_h,show);
        markup_place(markup_status,x0+first+second,body_y,inner-first-second,body_h,show);
    }
    const int action=min(markup_px(130),(inner-gap)/2);
    markup_place(check,x0+inner-2*action-gap,last,action,row,show);
    markup_place(markup_send,x0+inner-action,last,action,row,show);
    RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}

static void markup_frame(HDC dc,HWND parent,HWND child,COLORREF edge){
    RECT rect;
    if(!child||!IsWindowVisible(child)||!GetWindowRect(child,&rect))return;
    MapWindowPoints(HWND_DESKTOP,parent,(POINT *)&rect,2);
    InflateRect(&rect,1,1);
    native_theme_round(dc,&rect,RGB(255,255,255),edge,markup_px(7));
}

static void markup_paint_chrome(HWND window,HDC dc){
    RECT client,canvas;
    GetClientRect(window,&client);
    native_theme_fill(dc,&client,RGB(255,255,255));
    if(markup_canvas&&GetWindowRect(markup_canvas,&canvas)){
        MapWindowPoints(HWND_DESKTOP,window,(POINT *)&canvas,2);
        long split=canvas.top-markup_px(5);
        RECT rule={0,split,client.right,split+1};
        native_theme_fill(dc,&rule,RGB(218,228,233));
    }
    const COLORREF edge=RGB(210,222,230);
    markup_frame(dc,window,markup_canvas,edge);
    markup_frame(dc,window,markup_summary,edge);
    markup_frame(dc,window,markup_input,edge);
    markup_frame(dc,window,markup_status,edge);
}

static int markup_toolbar_icon(HDC dc,int id,RECT *text,COLORREF ink){
    if(!((id>=MARKUP_TOOL_CIRCLE&&id<=MARKUP_TOOL_BOX)||id==MARKUP_MEASURE||id==MARKUP_FIT))return 0;
    const int inset=markup_px(5),size=markup_px(12);
    const int x=text->left+inset,y=(text->top+text->bottom-size)/2;
    HPEN pen=CreatePen(PS_SOLID,max(1,markup_px(1)),ink);
    if(!pen)return 0;
    HGDIOBJ old=SelectObject(dc,pen),fill=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    if(id==MARKUP_TOOL_CIRCLE)Ellipse(dc,x,y,x+size,y+size);
    else if(id==MARKUP_TOOL_BOX)Rectangle(dc,x,y,x+size,y+size);
    else if(id==MARKUP_TOOL_ARROW){
        MoveToEx(dc,x,y+size,NULL);LineTo(dc,x+size,y);
        LineTo(dc,x+size/2,y);
        MoveToEx(dc,x+size,y,NULL);LineTo(dc,x+size,y+size/2);
    }else if(id==MARKUP_MEASURE){
        MoveToEx(dc,x,y+size/2,NULL);LineTo(dc,x+size,y+size/2);
        for(int i=0;i<4;i++){
            int tick=x+i*size/3;
            MoveToEx(dc,tick,y+size/2,NULL);LineTo(dc,tick,y+size/2+markup_px(i&1?4:3));
        }
    }else{
        Rectangle(dc,x,y,x+size,y+size);
        MoveToEx(dc,x+size/3,y+size/2,NULL);LineTo(dc,x+2*size/3,y+size/2);
    }
    SelectObject(dc,fill);
    SelectObject(dc,old);
    DeleteObject(pen);
    text->left=x+size+markup_px(3);
    return 1;
}

static void markup_draw_button(const DRAWITEMSTRUCT *item){
    RECT rect=item->rcItem;
    const int primary=item->CtlID==MARKUP_SEND;
    const int tool=item->CtlID>=MARKUP_TOOL_CIRCLE&&item->CtlID<=MARKUP_TOOL_BOX&&
        item->CtlID==MARKUP_TOOL_CIRCLE+markup_tool;
    const int enabled=!(item->itemState&ODS_DISABLED);
    const int pressed=(item->itemState&ODS_SELECTED)!=0;
    COLORREF fill=RGB(248,251,252),edge=RGB(213,224,230),ink=RGB(19,44,61);
    if(!enabled){fill=RGB(234,240,243);ink=RGB(97,118,131);}
    else if(primary){fill=pressed?RGB(0,92,81):RGB(0,114,101);edge=fill;ink=RGB(255,255,255);}
    else if(tool||pressed){fill=RGB(222,246,243);edge=RGB(143,210,204);ink=RGB(0,88,81);}
    int saved=SaveDC(item->hDC);
    if(!saved)return;
    native_theme_fill(item->hDC,&rect,RGB(255,255,255));
    InflateRect(&rect,-1,-1);
    native_theme_round(item->hDC,&rect,fill,edge,markup_px(8));
    SelectObject(item->hDC,native_font);
    SetBkMode(item->hDC,TRANSPARENT);
    SetTextColor(item->hDC,ink);
    wchar_t label[32];
    GetWindowTextW(item->hwndItem,label,_countof(label));
    RECT text=rect;
    const int icon=markup_toolbar_icon(item->hDC,item->CtlID,&text,ink);
    DrawTextW(item->hDC,label,-1,&text,(icon?DT_LEFT:DT_CENTER)|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    if((item->itemState&(ODS_FOCUS|ODS_NOFOCUSRECT))==ODS_FOCUS){
        InflateRect(&rect,-3,-3);
        DrawFocusRect(item->hDC,&rect);
    }
    RestoreDC(item->hDC,saved);
}
