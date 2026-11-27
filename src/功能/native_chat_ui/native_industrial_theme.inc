#include <dwmapi.h>

#include <commctrl.h>
#pragma comment(lib,"comctl32.lib")

static void pk_request_interference(void);
static void pk_request_dfm(void);
static void pk_request_price(void);
static int pk_price_visible(void);
static int pk_dfm_visible(void);
static int pk_interference_visible(void);
static int pk_interference_before_close(void);
static void native_theme_button(const DRAWITEMSTRUCT *item);


static void background_menu(HWND window,HWND button){
    HMENU menu=CreatePopupMenu();
    if(!menu)return;
    AppendMenuW(menu,MF_STRING,1,L"55 寸立式 → 49 寸缩窄版");
    AppendMenuW(menu,MF_STRING,2,L"查看后台任务");
    AppendMenuW(menu,MF_STRING,3,L"打开已验收结果");
    RECT bounds;
    GetWindowRect(button,&bounds);
    UINT action=(UINT)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,bounds.left,bounds.bottom,0,window,NULL);
    DestroyMenu(menu);
    if(!action)return;
    wchar_t *reference=NULL;
    if(action==1){
        wchar_t path[MAX_PATH]=L"";
        OPENFILENAMEW picker={.lStructSize=sizeof(picker),.hwndOwner=window,
            .lpstrFilter=L"49 寸参考屏\0" L"49inch.prt.*\0\0",.lpstrFile=path,.nMaxFile=_countof(path),
            .lpstrTitle=L"选择 E 盘上的 49inch Creo 参考屏",.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR};
        if(!GetOpenFileNameW(&picker))return;
        reference=_wcsdup(path);
        if(!reference)return;
    }
    if(!PostMessageW(native_backend_window,WM_APP+184,action,(LPARAM)reference))free(reference);
}

static LRESULT CALLBACK native_header_tools(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    (void)data;
    HWND interference=native_top_buttons[CHAT_TOP_COLUMNS],dfm=native_top_buttons[CHAT_TOP_COLUMNS+1],price=native_top_buttons[CHAT_TOP_COLUMNS+2];
    HWND background=native_top_buttons[CHAT_TOP_COLUMNS+3];
    if(message==WM_SHOWWINDOW&&w&&interference){
        SetWindowTextW(background,L"后台 Creo");
        SetWindowLongPtrW(background,GWL_STYLE,GetWindowLongPtrW(background,GWL_STYLE)|WS_TABSTOP);
        EnableWindow(background,TRUE);
        SetWindowTextW(native_top_buttons[CHAT_TOP_COLUMNS-1],L"工程图");
        SetWindowTextW(interference,L"干涉分析");
        SetWindowLongPtrW(interference,GWL_STYLE,GetWindowLongPtrW(interference,GWL_STYLE)|WS_TABSTOP);
        EnableWindow(interference,TRUE);
        SetWindowTextW(dfm,L"钣金DFM");
        SetWindowLongPtrW(dfm,GWL_STYLE,GetWindowLongPtrW(dfm,GWL_STYLE)|WS_TABSTOP);
        EnableWindow(dfm,TRUE);
        SetWindowTextW(price,L"钣金价格");
        SetWindowLongPtrW(price,GWL_STYLE,GetWindowLongPtrW(price,GWL_STYLE)|WS_TABSTOP);
        EnableWindow(price,TRUE);
    }
    if(message==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&background&&(HWND)l==background){background_menu(window,background);return 0;}
    if(message==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&interference&&(HWND)l==interference){
        pk_request_interference();
        return 0;
    }
    if(message==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&dfm&&(HWND)l==dfm){
        pk_request_dfm();return 0;
    }
    if(message==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&price&&(HWND)l==price){
        pk_request_price();return 0;
    }
    if(message==WM_DRAWITEM&&((((const DRAWITEMSTRUCT *)l)->hwndItem==interference&&pk_interference_visible())||
       (((const DRAWITEMSTRUCT *)l)->hwndItem==dfm&&pk_dfm_visible())||
       (((const DRAWITEMSTRUCT *)l)->hwndItem==price&&pk_price_visible()))){
        DRAWITEMSTRUCT styled=*(DRAWITEMSTRUCT *)l;
        styled.CtlID=CHAT_SEND;
        native_theme_button(&styled);
        return TRUE;
    }
    if(message==WM_CLOSE&&pk_interference_before_close())return 0;
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,native_header_tools,id);
    return DefSubclassProc(window,message,w,l);
}

static void native_theme_caption(HWND window){
    if(window==native_window)SetWindowSubclass(window,native_header_tools,1,0);

    typedef HRESULT (WINAPI *SetWindowAttribute)(HWND,DWORD,LPCVOID,DWORD);
    HMODULE module=LoadLibraryExW(L"dwmapi.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module)return;
    SetWindowAttribute set=(SetWindowAttribute)GetProcAddress(module,"DwmSetWindowAttribute");
    if(set){
        const COLORREF background=CHAT_HEADER_COLOR,text=CHAT_INPUT_COLOR;
        set(window,DWMWA_CAPTION_COLOR,&background,sizeof(background));
        set(window,DWMWA_TEXT_COLOR,&text,sizeof(text));
    }
    FreeLibrary(module);
}

static void native_theme_fill(HDC dc,const RECT *rect,COLORREF color){
    HBRUSH brush=CreateSolidBrush(color);
    if(brush){FillRect(dc,rect,brush);DeleteObject(brush);}
}

static void native_theme_round(HDC dc,const RECT *rect,COLORREF fill,COLORREF border,int radius){
    HBRUSH brush=CreateSolidBrush(fill);
    HPEN pen=CreatePen(PS_SOLID,1,border);
    if(brush&&pen){
        HGDIOBJ old_brush=SelectObject(dc,brush);
        HGDIOBJ old_pen=SelectObject(dc,pen);
        RoundRect(dc,rect->left,rect->top,rect->right,rect->bottom,radius,radius);
        SelectObject(dc,old_pen);
        SelectObject(dc,old_brush);
    }
    if(pen)DeleteObject(pen);
    if(brush)DeleteObject(brush);
}

static void native_theme_wave(HDC dc,int width,int height,int offset,COLORREF color){
    const int left=width/2;
    POINT curve[]={{width*3/4,height-offset},{width*4/5,offset},{width,offset}};
    HBRUSH brush=CreateSolidBrush(color);
    if(!brush)return;
    HGDIOBJ old_brush=SelectObject(dc,brush);
    BeginPath(dc);
    MoveToEx(dc,left,height,NULL);
    PolyBezierTo(dc,curve,_countof(curve));
    LineTo(dc,width,height);
    CloseFigure(dc);
    EndPath(dc);
    FillPath(dc);
    SelectObject(dc,old_brush);
    DeleteObject(brush);
}

static void native_theme_edit_frame(HDC dc,HWND parent,HWND edit,COLORREF fill){
    RECT rect;
    if(!GetWindowRect(edit,&rect))return;
    MapWindowPoints(HWND_DESKTOP,parent,(POINT *)&rect,2);
    InflateRect(&rect,3,3);
    native_theme_round(dc,&rect,fill,CHAT_FRAME_COLOR,7);
}

static void native_theme_paint(HWND window,HDC dc){
    RECT rect;
    GetClientRect(window,&rect);
    native_theme_fill(dc,&rect,CHAT_CANVAS_COLOR);
    int header=MulDiv(122,native_dpi,96);
    int footer=rect.bottom-MulDiv(49,native_dpi,96);
    RECT top={0,0,rect.right,header};
    RECT rule={0,header-2,rect.right,header};
    RECT bottom={0,footer,rect.right,rect.bottom};
    RECT divider={0,footer,rect.right,footer+1};
    native_theme_fill(dc,&top,CHAT_HEADER_COLOR);
    native_theme_wave(dc,rect.right,header,MulDiv(10,native_dpi,96),RGB(16,82,102));
    native_theme_wave(dc,rect.right,header,MulDiv(35,native_dpi,96),RGB(17,71,91));
    native_theme_fill(dc,&rule,RGB(34,147,140));
    if(packaging_visible())return;
    native_theme_fill(dc,&bottom,CHAT_FOOTER_COLOR);
    native_theme_fill(dc,&divider,RGB(216,225,230));
    native_theme_edit_frame(dc,window,native_history,CHAT_CANVAS_COLOR);
    native_theme_edit_frame(dc,window,native_prompt,CHAT_INPUT_COLOR);
}

static void native_theme_button(const DRAWITEMSTRUCT *item){
    const int top=item->CtlID>=CHAT_FEATURES&&item->CtlID<=CHAT_RESERVED_3;
    const int disabled=(item->itemState&ODS_DISABLED)!=0;
    const int pressed=(item->itemState&ODS_SELECTED)!=0;
    const int send=item->CtlID==CHAT_SEND;
    COLORREF fill,outline,text;
    if(disabled){
        fill=top?RGB(188,204,214):RGB(227,234,238);
        outline=top?RGB(176,195,205):RGB(205,217,224);
        text=RGB(79,103,117);
    }else if(send){
        fill=pressed?RGB(12,85,78):CHAT_ACTION_COLOR;
        outline=fill;
        text=CHAT_INPUT_COLOR;
    }else{
        fill=pressed?RGB(191,214,226):top?RGB(218,232,240):RGB(225,234,239);
        outline=top?RGB(167,190,203):RGB(178,195,205);
        text=CHAT_TEXT_COLOR;
    }
    RECT rect=item->rcItem;
    int saved=SaveDC(item->hDC);
    native_theme_fill(item->hDC,&rect,top?CHAT_HEADER_COLOR:CHAT_FOOTER_COLOR);
    native_theme_round(item->hDC,&rect,fill,outline,MulDiv(8,native_dpi,96));
    SelectObject(item->hDC,native_font);
    SetBkMode(item->hDC,TRANSPARENT);
    SetTextColor(item->hDC,text);
    wchar_t label[40];
    GetWindowTextW(item->hwndItem,label,_countof(label));
    DrawTextW(item->hDC,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if((item->itemState&(ODS_FOCUS|ODS_NOFOCUSRECT))==ODS_FOCUS){
        InflateRect(&rect,-3,-3);
        DrawFocusRect(item->hDC,&rect);
    }
    if(saved)RestoreDC(item->hDC,saved);
}



