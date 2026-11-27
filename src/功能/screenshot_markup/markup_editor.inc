#include <windowsx.h>
#include "markup_shared.inc"
#define MARKUP_DISMISS (WM_APP+66)

static void native_markup_post(UINT message,LPARAM argument);
static HWND markup_window,markup_canvas,markup_input,markup_status,markup_send,markup_tools[3],markup_summary;
static MarkupImage *markup_display;
static MarkupMark markup_mark,markup_marks[MARKUP_MAX_MARKS];
static int markup_count,markup_retake,markup_thinking;
static MarkupTool markup_tool;
static int markup_dragging,markup_waiting,markup_panel_hidden,markup_panning;
static int markup_dpi;
static WNDPROC markup_input_previous;
static wchar_t markup_last_submit[256],markup_resume_draft[256];
static double markup_zoom,markup_focus_x,markup_focus_y;
static POINT markup_pan_origin;
static double markup_pan_x,markup_pan_y;

enum {MARKUP_TOOL_CIRCLE=901, MARKUP_TOOL_ARROW, MARKUP_TOOL_BOX, MARKUP_SEND, MARKUP_CLOSE, MARKUP_UNDO, MARKUP_CLEAR, MARKUP_INPUT,
    MARKUP_SUBMIT_LABEL=911, MARKUP_MEASURE, MARKUP_CHECK,
    MARKUP_RECAPTURE, MARKUP_FIT, MARKUP_ZOOM_IN, MARKUP_ZOOM_OUT, MARKUP_PANEL, MARKUP_CONTEXT, MARKUP_SUMMARY, MARKUP_STATUS};
static const wchar_t markup_class[]=L"BWTLMarkupEditor";
static const wchar_t markup_canvas_class[]=L"BWTLMarkupCanvas";

#include "markup_chrome.inc"

static double markup_fit_scale(const RECT *client){
    return fmin((double)client->right/markup_display->width,(double)client->bottom/markup_display->height);
}

static RECT markup_image_rect(HWND canvas){
    RECT client;
    GetClientRect(canvas,&client);
    if(!markup_display||client.right<1||client.bottom<1)return client;
    const double scale=markup_fit_scale(&client)*markup_zoom;
    const LONG w=max(1,(LONG)lround(scale*markup_display->width));
    const LONG h=max(1,(LONG)lround(scale*markup_display->height));
    LONG x=w>client.right?min(0,max(client.right-w,client.right/2-(LONG)lround(scale*markup_focus_x))):(client.right-w)/2;
    LONG y=h>client.bottom?min(0,max(client.bottom-h,client.bottom/2-(LONG)lround(scale*markup_focus_y))):(client.bottom-h)/2;
    return (RECT){x,y,x+w,y+h};
}

static void markup_normalize_focus(HWND canvas){
    if(!markup_display)return;
    RECT client,rect=markup_image_rect(canvas);
    GetClientRect(canvas,&client);
    if(IsRectEmpty(&rect))return;
    markup_focus_x=(client.right*0.5-rect.left)*markup_display->width/(rect.right-rect.left);
    markup_focus_y=(client.bottom*0.5-rect.top)*markup_display->height/(rect.bottom-rect.top);
}

static void markup_fit(void){
    if(!markup_display)return;
    markup_zoom=1;
    markup_focus_x=markup_display->width*0.5;
    markup_focus_y=markup_display->height*0.5;
    InvalidateRect(markup_canvas,NULL,FALSE);
}

static void markup_zoom_at(HWND canvas,POINT point,double multiplier){
    if(!markup_display||markup_dragging||markup_panning)return;
    RECT client,rect=markup_image_rect(canvas);
    GetClientRect(canvas,&client);
    if(IsRectEmpty(&rect)||IsRectEmpty(&client))return;
    if(!PtInRect(&rect,point))point=(POINT){client.right/2,client.bottom/2};
    markup_zoom=fmin(6.0,fmax(1.0,markup_zoom*multiplier));
    const double scale=markup_fit_scale(&client)*markup_zoom;
    markup_focus_x=(point.x-rect.left)*(double)markup_display->width/(rect.right-rect.left)+(client.right*0.5-point.x)/scale;
    markup_focus_y=(point.y-rect.top)*(double)markup_display->height/(rect.bottom-rect.top)+(client.bottom*0.5-point.y)/scale;
    markup_normalize_focus(canvas);
    InvalidateRect(canvas,NULL,FALSE);
}

static void markup_mark_feedback(void){
    if(!markup_display||markup_waiting||markup_retake)return;
    wchar_t text[1500]=L"",input[256];
    GetWindowTextW(markup_input,input,_countof(input));
    if(!markup_count)wcscpy_s(text,_countof(text),L"拖动标注要查看或修改的位置。");
    for(int i=0;i<markup_count;i++){
        wchar_t description[150];
        markup_describe_mark(markup_display,&markup_marks[i],input,markup_count==1,description,_countof(description));
        const size_t used=wcslen(text);
        swprintf_s(text+used,_countof(text)-used,L"%ls%d  ·  %ls",i?L"\r\n":L"",i+1,description);
    }
    SetWindowTextW(markup_summary,text);
}

static void markup_draw_note(HDC dc,const RECT *image){
    wchar_t note[256];
    if(markup_count!=1||markup_dragging||image->right-image->left<100||
       image->bottom-image->top<48||!GetWindowTextW(markup_input,note,_countof(note)))return;
    const MarkupMark *mark=&markup_marks[0];
    const double px=mark->tool==MARKUP_ARROW?mark->x2:fmax(mark->x1,mark->x2);
    const double py=mark->tool==MARKUP_ARROW?mark->y2:fmin(mark->y1,mark->y2);
    const int anchor_x=image->left+(int)(px*(image->right-image->left)/markup_display->width);
    const int anchor_y=image->top+(int)(py*(image->bottom-image->top)/markup_display->height);
    HGDIOBJ font=SelectObject(dc,native_font);
    SIZE size={0};
    GetTextExtentPoint32W(dc,note,(int)wcslen(note),&size);
    const int width=min(size.cx+16,min(265,image->right-image->left-12));
    const int height=size.cy+8;
    const int left=max(image->left+6,min(anchor_x+9,image->right-width-6));
    const int top=max(image->top+4,min(anchor_y-height-6,image->bottom-height-4));
    RECT label={left,top,left+width,top+height};
    HBRUSH fill=CreateSolidBrush(RGB(17,51,69));
    if(fill){FillRect(dc,&label,fill);DeleteObject(fill);}
    InflateRect(&label,-8,0);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(255,255,255));
    DrawTextW(dc,note,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,font);
}

static void markup_pointer(HWND canvas,LPARAM position,double *x,double *y){
    RECT image=markup_image_rect(canvas);
    if(IsRectEmpty(&image))return;
    LONG px=GET_X_LPARAM(position),py=GET_Y_LPARAM(position);
    *x=fmin(markup_display->width,fmax(0,(double)(px-image.left)*markup_display->width/(image.right-image.left)));
    *y=fmin(markup_display->height,fmax(0,(double)(py-image.top)*markup_display->height/(image.bottom-image.top)));
}



static void markup_draw_marks(HDC dc,const RECT *bounds,int width,int height,const MarkupMark *marks,int count){
    int saved=SaveDC(dc);
    if(!saved)return;
    const double sx=(double)(bounds->right-bounds->left)/width;
    const double sy=(double)(bounds->bottom-bounds->top)/height;
    const int badge=markup_px(20);
    RECT clip;
    GetClipBox(dc,&clip);
    SelectObject(dc,native_font);
    SetBkMode(dc,TRANSPARENT);
    for(int i=0;i<count;i++){
        const MarkupMark *mark=&marks[i];
        int ax=bounds->left+(int)(mark->x1*sx),ay=bounds->top+(int)(mark->y1*sy);
        int bx=bounds->left+(int)(mark->x2*sx),by=bounds->top+(int)(mark->y2*sy);
        if(max(ax,bx)<clip.left||min(ax,bx)>clip.right||max(ay,by)<clip.top||min(ay,by)>clip.bottom)continue;
        const COLORREF color=markup_mark_color(i);
        HPEN pen=CreatePen(PS_SOLID,max(2,markup_px(3)),color);
        if(!pen)continue;
        HGDIOBJ old_pen=SelectObject(dc,pen);
        HGDIOBJ brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        if(mark->tool==MARKUP_ARROW){
            double angle=atan2(by-ay,bx-ax);
            MoveToEx(dc,ax,ay,NULL);
            LineTo(dc,bx,by);
            for(int direction=-1;direction<=1;direction+=2){
                double wing=angle+direction*0.48;
                MoveToEx(dc,bx,by,NULL);
                LineTo(dc,bx-(int)(markup_px(13)*cos(wing)),by-(int)(markup_px(13)*sin(wing)));
            }
        }else if(mark->tool==MARKUP_BOX)
            Rectangle(dc,min(ax,bx),min(ay,by),max(ax,bx),max(ay,by));
        else Ellipse(dc,min(ax,bx),min(ay,by),max(ax,bx),max(ay,by));
        SelectObject(dc,brush);
        SelectObject(dc,old_pen);
        DeleteObject(pen);
        int left=max(clip.left,min(min(ax,bx),clip.right-badge));
        int top=max(clip.top,min(min(ay,by)-badge-markup_px(3),clip.bottom-badge));
        RECT label={left,top,left+badge,top+badge};
        native_theme_round(dc,&label,color,RGB(255,255,255),badge);
        SetTextColor(dc,RGB(255,255,255));
        wchar_t character=(wchar_t)(L'1'+i);
        DrawTextW(dc,&character,1,&label,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);
    }
    RestoreDC(dc,saved);
}


static LRESULT CALLBACK markup_canvas_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT paint;
        HDC target=BeginPaint(window,&paint);
        RECT client,rect=markup_image_rect(window);
        GetClientRect(window,&client);
        HDC buffer=CreateCompatibleDC(target);
        HBITMAP frame=buffer?CreateCompatibleBitmap(target,client.right,client.bottom):NULL;
        HDC dc=frame?buffer:target;
        HGDIOBJ old_frame=frame?SelectObject(buffer,frame):NULL;
        native_theme_fill(dc,&client,RGB(31,48,65));
        if(markup_display){
            HDC source=CreateCompatibleDC(dc);
            if(source){
                HGDIOBJ old=SelectObject(source,markup_display->bitmap);
                SetStretchBltMode(dc,HALFTONE);
                SetBrushOrgEx(dc,0,0,NULL);
                StretchBlt(dc,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,
                    source,0,0,markup_display->width,markup_display->height,SRCCOPY);
                SelectObject(source,old);
                DeleteDC(source);
            }
            MarkupMark visible[MARKUP_MAX_MARKS];
            memcpy(visible,markup_marks,markup_count*sizeof(*visible));
            if(markup_dragging)visible[markup_count]=markup_mark;
            markup_draw_marks(dc,&rect,markup_display->width,markup_display->height,visible,markup_count+markup_dragging);
            markup_draw_note(dc,&rect);
        }
        if(frame){
            BitBlt(target,0,0,client.right,client.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,old_frame);
            DeleteObject(frame);
        }
        if(buffer)DeleteDC(buffer);
        EndPaint(window,&paint);
        return 0;
    }
    case WM_MOUSEWHEEL:{
        POINT point={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        ScreenToClient(window,&point);
        markup_zoom_at(window,point,pow(1.2,GET_WHEEL_DELTA_WPARAM(w)/(double)WHEEL_DELTA));
        return 0;
    }
    case WM_MBUTTONDOWN:
        if(markup_display&&markup_zoom>1&&!markup_dragging){
            markup_normalize_focus(window);
            markup_pan_origin=(POINT){GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            markup_pan_x=markup_focus_x;
            markup_pan_y=markup_focus_y;
            markup_panning=1;
            SetCapture(window);
        }
        return 0;
    case WM_MBUTTONUP:
        if(markup_panning){
            markup_panning=0;
            markup_normalize_focus(window);
            ReleaseCapture();
        }
        return 0;
    case WM_LBUTTONDOWN:{
        if(!markup_display||markup_waiting||markup_retake||markup_panning)return 0;
        if(markup_count==MARKUP_MAX_MARKS){SetWindowTextW(markup_status,L"已到 8 处。可撤销一处或清空标注。");return 0;}
        RECT rect=markup_image_rect(window);
        POINT at={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(!PtInRect(&rect,at))return 0;
        markup_mark=(MarkupMark){.tool=markup_tool};
        markup_pointer(window,l,&markup_mark.x1,&markup_mark.y1);
        markup_mark.x2=markup_mark.x1;
        markup_mark.y2=markup_mark.y1;
        markup_dragging=1;
        SetCapture(window);
        return 0;
    }
    case WM_MOUSEMOVE:case WM_LBUTTONUP:
        if(message==WM_MOUSEMOVE&&markup_panning){
            RECT client;
            GetClientRect(window,&client);
            const double scale=markup_fit_scale(&client)*markup_zoom;
            if(scale>0){
                markup_focus_x=markup_pan_x+(markup_pan_origin.x-GET_X_LPARAM(l))/scale;
                markup_focus_y=markup_pan_y+(markup_pan_origin.y-GET_Y_LPARAM(l))/scale;
                InvalidateRect(window,NULL,FALSE);
            }
            return 0;
        }
        if(markup_dragging){
            markup_pointer(window,l,&markup_mark.x2,&markup_mark.y2);
            if(message==WM_LBUTTONUP){
                markup_dragging=0;
                markup_mark.valid=hypot(markup_mark.x2-markup_mark.x1,markup_mark.y2-markup_mark.y1)>=4;
                if(markup_mark.valid)markup_marks[markup_count++]=markup_mark;
                ReleaseCapture();
                markup_mark_feedback();
            }
            InvalidateRect(window,NULL,FALSE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if(markup_panning){markup_panning=0;markup_normalize_focus(window);}
        if(markup_dragging){markup_dragging=0;InvalidateRect(window,NULL,FALSE);}
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}



static int markup_resume_input(wchar_t *text,size_t capacity){
    return markup_retake&&GetWindowTextW(markup_input,text,(int)capacity)&&
        wcscmp(text,markup_last_submit)&&markup_followup_hint(text);
}

static void markup_recapture(const wchar_t *resume){
    if(markup_waiting||!markup_display)return;
    wcscpy_s(markup_resume_draft,_countof(markup_resume_draft),resume?resume:L"");
    markup_waiting=1;
    EnableWindow(markup_send,FALSE);
    SetWindowTextW(markup_status,L"正在重新截图…");
    native_markup_post(MARKUP_OPEN,0);
}

static void markup_ui_submit(const wchar_t *command){
    if(markup_waiting||!markup_display||!IsWindowEnabled(markup_send))return;
    if(markup_retake){
        wchar_t text[256];
        markup_recapture(!command&&markup_resume_input(text,_countof(text))?text:NULL);
        return;
    }
    if(markup_panel_hidden){
        markup_panel_hidden=0;
        markup_editor_relayout(markup_window);
    }
    MarkupRequest *request=calloc(1,sizeof(*request));
    if(!request){SetWindowTextW(markup_status,L"无法提交标注，请重试。");return;}
    request->ticket=markup_display->ticket;
    request->count=markup_count;
    memcpy(request->marks,markup_marks,markup_count*sizeof(*markup_marks));
    if(command)wcscpy_s(request->text,_countof(request->text),command);
    else if(!GetWindowTextW(markup_input,request->text,_countof(request->text))){
        SetWindowTextW(markup_status,L"说一下你要改哪里、改成什么样。");
        SetFocus(markup_input);
        free(request);
        return;
    }
    markup_waiting=1;
    EnableWindow(markup_send,FALSE);
    if(!command)wcscpy_s(markup_last_submit,_countof(markup_last_submit),request->text);
    SetWindowTextW(markup_status,L"正在处理你的要求…");
    native_markup_post(MARKUP_APPLY,(LPARAM)request);
}

static LRESULT CALLBACK markup_editor_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT paint;
        HDC dc=BeginPaint(window,&paint);
        markup_paint_chrome(window,dc);
        EndPaint(window,&paint);
        return 0;
    }
    case WM_SIZE:
        if(w!=SIZE_MINIMIZED&&markup_window==window)markup_editor_relayout(window);
        return 0;
    case WM_DPICHANGED:{
        markup_dpi=HIWORD(w);
        const RECT *suggested=(const RECT *)l;
        SetWindowPos(window,NULL,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,
            SWP_NOACTIVATE|SWP_NOZORDER);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)l)->ptMinTrackSize=(POINT){markup_px(500),markup_px(365)};
        return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:{
        HWND child=(HWND)l;
        int id=GetDlgCtrlID(child);
        HDC dc=(HDC)w;
        const int edit=child==markup_input||child==markup_status||child==markup_summary;
        SetBkColor(dc,RGB(255,255,255));
        SetBkMode(dc,edit?OPAQUE:TRANSPARENT);
        SetTextColor(dc,id==MARKUP_CONTEXT?RGB(56,88,106):RGB(16,40,57));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_DRAWITEM:{
        const DRAWITEMSTRUCT *item=(const DRAWITEMSTRUCT *)l;
        if(item->CtlType==ODT_BUTTON){
            markup_draw_button(item);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:{
        int id=LOWORD(w);
        if(id==MARKUP_INPUT&&HIWORD(w)==EN_CHANGE){
            if(markup_retake){
                wchar_t text[256];
                SetWindowTextW(markup_send,markup_resume_input(text,_countof(text))?L"继续改图":L"重新截图");
            }
            markup_mark_feedback();
            if(markup_canvas)InvalidateRect(markup_canvas,NULL,FALSE);
            return 0;
        }
        if(id>=MARKUP_TOOL_CIRCLE&&id<=MARKUP_TOOL_BOX){
            markup_tool=(MarkupTool)(id-MARKUP_TOOL_CIRCLE);
            for(int i=0;i<3;i++)InvalidateRect(markup_tools[i],NULL,FALSE);
            return 0;
        }
        if(id==MARKUP_UNDO||id==MARKUP_CLEAR){
            if(!markup_waiting&&!markup_retake){
                if(id==MARKUP_CLEAR)markup_count=0;
                else if(markup_count)markup_count--;
                InvalidateRect(markup_canvas,NULL,FALSE);
                markup_mark_feedback();
            }
            return 0;
        }
        if(id==MARKUP_FIT){markup_fit();return 0;}
        if(id==MARKUP_ZOOM_IN||id==MARKUP_ZOOM_OUT){
            RECT client;
            GetClientRect(markup_canvas,&client);
            POINT center={client.right/2,client.bottom/2};
            markup_zoom_at(markup_canvas,center,id==MARKUP_ZOOM_IN?1.25:0.8);
            return 0;
        }
        if(id==MARKUP_PANEL){
            markup_panel_hidden=!markup_panel_hidden;
            markup_editor_relayout(window);
            return 0;
        }
        if(id==MARKUP_MEASURE){markup_ui_submit(L"测量标注");return 0;}
        if(id==MARKUP_CHECK){markup_ui_submit(L"检查参照");return 0;}
        if(id==MARKUP_RECAPTURE){markup_recapture(NULL);return 0;}
        if(id==MARKUP_SEND){markup_ui_submit(NULL);return 0;}
        if(id==MARKUP_CLOSE){
            if(markup_thinking&&markup_display)native_markup_post(MARKUP_CANCEL,(LPARAM)markup_display->ticket);
            else if(!markup_waiting){
                if(markup_display)native_markup_post(MARKUP_DISMISS,(LPARAM)markup_display->ticket);
                DestroyWindow(window);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:SendMessageW(window,WM_COMMAND,MARKUP_CLOSE,0);return 0;
    case WM_DESTROY:
        markup_image_free(markup_display);
        markup_display=NULL;
        markup_window=markup_canvas=markup_input=markup_status=markup_send=markup_summary=NULL;
        memset(markup_tools,0,sizeof(markup_tools));
        markup_mark=(MarkupMark){0};
        markup_count=markup_dragging=markup_panning=markup_waiting=markup_retake=markup_thinking=markup_panel_hidden=0;
        *markup_last_submit=*markup_resume_draft=0;
        markup_dpi=0;
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}

static LRESULT CALLBACK markup_input_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    const LRESULT result=CallWindowProcW(markup_input_previous,window,message,w,l);
    /* Multiline EDIT does not send EN_CHANGE for programmatic WM_SETTEXT. */
    if(message==WM_SETTEXT&&result&&markup_window)
        SendMessageW(markup_window,WM_COMMAND,MAKEWPARAM(MARKUP_INPUT,EN_CHANGE),(LPARAM)window);
    return result;
}

static HWND markup_child(HWND owner,const wchar_t *type,const wchar_t *label,DWORD style,int id){
    HWND child=CreateWindowW(type,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,owner,
        (HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    if(child)SendMessageW(child,WM_SETFONT,(WPARAM)native_font,TRUE);
    return child;
}

static int markup_ui_open(HWND owner,MarkupImage *image){
    if(markup_waiting&&markup_display&&image->ticket==markup_display->ticket)return 0;
    wchar_t draft[256]=L"";
    const int resume=markup_display&&*markup_resume_draft&&image->assembly==markup_display->assembly&&
        !_wcsicmp(image->name,markup_display->name);
    if(*markup_resume_draft)wcscpy_s(draft,_countof(draft),markup_resume_draft);
    RECT previous={0};
    int panel=markup_panel_hidden;
    if(markup_window){
        GetWindowRect(markup_window,&previous);
        DestroyWindow(markup_window);
    }
    HINSTANCE module=GetModuleHandleW(NULL);
    WNDCLASSW editor={.lpfnWndProc=markup_editor_proc,.hInstance=module,
        .hCursor=LoadCursor(NULL,IDC_ARROW),.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH),.lpszClassName=markup_class};
    WNDCLASSW canvas={.lpfnWndProc=markup_canvas_proc,.hInstance=module,
        .hCursor=LoadCursor(NULL,IDC_CROSS),.lpszClassName=markup_canvas_class};
    if(!RegisterClassW(&editor)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;
    if(!RegisterClassW(&canvas)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;
    wchar_t context[180],title[185];
    swprintf_s(context,_countof(context),L"%ls · %ls",image->assembly?L"装配体":L"零件",image->name);
    wcscpy_s(title,_countof(title),L"MWEAI · 截图改图");
    MONITORINFO monitor={sizeof(monitor)};
    int width=markup_px(1160),height=markup_px(735),x=CW_USEDEFAULT,y=CW_USEDEFAULT;
    if(!IsRectEmpty(&previous)){
        width=previous.right-previous.left;
        height=previous.bottom-previous.top;
        x=previous.left;
        y=previous.top;
    }else if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)){
        width=min(width,monitor.rcWork.right-monitor.rcWork.left-markup_px(24));
        height=min(height,monitor.rcWork.bottom-monitor.rcWork.top-markup_px(24));
        x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2;
        y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2;
    }
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,markup_class,title,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        x,y,width,height,owner,NULL,module,NULL);
    if(!window)return 0;
    markup_window=window;
    markup_dpi=(int)GetDpiForWindow(window);
    markup_panel_hidden=panel;
    native_theme_caption(window);
    for(int i=0;i<_countof(markup_toolbar);i++){
        const MarkupToolbarButton *button=&markup_toolbar[i];
        HWND control=markup_child(window,L"BUTTON",button->label,BS_OWNERDRAW|WS_TABSTOP,button->id);
        if(!control){DestroyWindow(window);return 0;}
        if(button->id>=MARKUP_TOOL_CIRCLE&&button->id<=MARKUP_TOOL_BOX)
            markup_tools[button->id-MARKUP_TOOL_CIRCLE]=control;
    }
    const DWORD edit_style=ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP;
    HWND scope=markup_child(window,L"STATIC",context,SS_CENTERIMAGE|SS_ENDELLIPSIS|SS_NOPREFIX,MARKUP_CONTEXT);
    HWND panel_heading=markup_child(window,L"STATIC",L"标注与改动",SS_CENTERIMAGE,MARKUP_SUBMIT_LABEL);
    HWND check=markup_child(window,L"BUTTON",L"检查参照",WS_TABSTOP|BS_OWNERDRAW,MARKUP_CHECK);
    markup_canvas=markup_child(window,markup_canvas_class,L"",0,0);
    markup_summary=markup_child(window,L"EDIT",L"",edit_style|ES_READONLY,MARKUP_SUMMARY);
    markup_input=markup_child(window,L"EDIT",L"",edit_style|ES_WANTRETURN,MARKUP_INPUT);
    markup_status=markup_child(window,L"EDIT",L"等待执行",edit_style|ES_READONLY,MARKUP_STATUS);
    markup_send=markup_child(window,L"BUTTON",L"执行改图",WS_TABSTOP|BS_OWNERDRAW,MARKUP_SEND);
    if(!markup_canvas||!markup_input||!markup_status||!markup_send||!markup_summary||!scope||!panel_heading||!check){
        DestroyWindow(window);
        return 0;
    }
    markup_input_previous=(WNDPROC)SetWindowLongPtrW(markup_input,GWLP_WNDPROC,(LONG_PTR)markup_input_proc);
    if(!markup_input_previous){DestroyWindow(window);return 0;}
    if(native_title_font)SendMessageW(panel_heading,WM_SETFONT,(WPARAM)native_title_font,TRUE);
    markup_display=image;
    markup_tool=MARKUP_CIRCLE;
    SendMessageW(markup_input,EM_SETLIMITTEXT,_countof(((MarkupRequest *)0)->text)-1,0);
    SendMessageW(markup_summary,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(markup_px(7),markup_px(7)));
    SendMessageW(markup_input,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(markup_px(7),markup_px(7)));
    SendMessageW(markup_status,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(markup_px(7),markup_px(7)));
    markup_fit();
    markup_mark_feedback();
    markup_editor_relayout(window);
    ShowWindow(window,SW_SHOW);
    if(*draft){
        SetWindowTextW(markup_input,draft);
        if(resume)PostMessageW(window,WM_COMMAND,MARKUP_SEND,0);
        else SetWindowTextW(markup_status,L"已换模型，请重新核对要改的位置。");
    }
    return 1;
}
