#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static ProMdl model;
static int features[6];
static MarkupHole native_holes[6];
static FILE *receipt;
static ULONGLONG deadline;
static int queued,phase;


static int check_diameter(int feature,double expected){
    ProFeature f;
    MarkupDriver driver={.diameter=expected};
    return !ProFeatureInit((ProSolid)model,feature,&f)&&
        !ProFeatureDimensionVisit(&f,markup_driver,NULL,&driver)&&driver.count==1;
}

static void done(int pass){
    KillTimer(native_backend_window,9008);
    fprintf(receipt,"PASS=%d\n",pass);
    fclose(receipt);
    ProEngineerEnd();
}

static void CALLBACK test_timer(HWND window,UINT message,UINT_PTR timer,DWORD now){
    if(!queued)queued=ProMacroLoad(L"~ Command " L"\x60" L"BWTLMarkupCheck" L"\x60;")==PRO_TK_NO_ERROR;
}

static void mark_at(const MarkupImage *image,double px,double py,int tool){
    RECT r=markup_image_rect(markup_canvas);
    int x=r.left+(int)lround(px*(r.right-r.left)/image->width);
    int y=r.top+(int)lround(py*(r.bottom-r.top)/image->height);
    int end_x=tool==MARKUP_TOOL_ARROW?x:x+12;
    int end_y=tool==MARKUP_TOOL_ARROW?y:y+12;
    SendMessageW(markup_window,WM_COMMAND,tool,0);
    SendMessageW(markup_canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x-12,y-12));
    SendMessageW(markup_canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end_x,end_y));
    SendMessageW(markup_canvas,WM_LBUTTONUP,0,MAKELPARAM(end_x,end_y));
}

static void export_bitmap(const MarkupImage *image){
    DIBSECTION dib;
    if(GetObjectW(image->bitmap,sizeof(dib),&dib)!=sizeof(dib)||!dib.dsBm.bmBits)return;
    FILE *out=fopen("creo-full-window.bmp","wb");
    if(!out)return;
    DWORD bytes=dib.dsBm.bmWidthBytes*image->height;
    BITMAPFILEHEADER header={0x4d42,sizeof(header)+sizeof(dib.dsBmih)+bytes,0,0,sizeof(header)+sizeof(dib.dsBmih)};
    fwrite(&header,sizeof(header),1,out);
    fwrite(&dib.dsBmih,sizeof(dib.dsBmih),1,out);
    fwrite(dib.dsBm.bmBits,bytes,1,out);
    fclose(out);
}

static RECT child_bounds(HWND control){
    RECT bounds={0};
    GetWindowRect(control,&bounds);
    MapWindowPoints(HWND_DESKTOP,markup_window,(POINT *)&bounds,2);
    return bounds;
}

static int inside(const RECT *client,const RECT *child){
    return child->left>=0&&child->top>=0&&child->right<=client->right&&child->bottom<=client->bottom&&
        child->right>child->left&&child->bottom>child->top;
}

static int check(uiCmdCmdId id,uiCmdValue *value,void *data){
    queued=0;
    if(GetTickCount64()>deadline){fprintf(receipt,"TIMEOUT_PHASE=%d\n",phase);done(0);return 0;}
    if(phase==1&&markup_window&&IsWindowVisible(markup_window)&&markup_capture.image){
        MarkupImage *image=markup_capture.image;
        RECT full;
        HWND creo=markup_creo_window();
        int complete=creo&&GetWindowRect(creo,&full)&&image->width==full.right-full.left&&image->height==full.bottom-full.top;
        fprintf(receipt,"FULL_WINDOW=%d WIDTH=%d HEIGHT=%d HOLES=%d\n",complete,image->width,image->height,image->count);
        if(!complete||image->count!=6){done(0);return 0;}
        export_bitmap(image);
        const SIZE sizes[]={{1210,690},{520,365},{890,620}};
        int resized=1,grouped=1;
        for(int i=0;i<_countof(sizes);i++){
            if(!MoveWindow(markup_window,0,0,MulDiv(sizes[i].cx,native_dpi,96),MulDiv(sizes[i].cy,native_dpi,96),TRUE)){
                done(0);return 0;
            }
            RECT client,overlap;
            GetClientRect(markup_window,&client);
            RECT canvas=child_bounds(markup_canvas),input=child_bounds(markup_input);
            RECT send=child_bounds(markup_send),status=child_bounds(markup_status);
            RECT context=child_bounds(GetDlgItem(markup_window,MARKUP_CONTEXT));
            RECT check=child_bounds(GetDlgItem(markup_window,MARKUP_CHECK));
            resized=resized&&inside(&client,&canvas)&&inside(&client,&input)&&inside(&client,&send)&&inside(&client,&status);
            grouped=grouped&&context.bottom<canvas.top&&!IntersectRect(&overlap,&canvas,&input)&&
                !IntersectRect(&overlap,&canvas,&status)&&!IntersectRect(&overlap,&check,&send)&&send.top>=input.bottom&&
                (i==1?canvas.bottom<input.top:canvas.right<input.left);
        }
        RECT normal=markup_image_rect(markup_canvas),canvas_before=child_bounds(markup_canvas);
        SendMessageW(markup_window,WM_COMMAND,MARKUP_ZOOM_IN,0);
        RECT expanded=markup_image_rect(markup_canvas);
        SendMessageW(markup_window,WM_COMMAND,MARKUP_FIT,0);
        RECT reset=markup_image_rect(markup_canvas);
        int zoom=expanded.right-expanded.left>normal.right-normal.left&&EqualRect(&normal,&reset);
        SendMessageW(markup_window,WM_COMMAND,MARKUP_PANEL,0);
        RECT collapsed=child_bounds(markup_canvas);
        int panel=collapsed.right>canvas_before.right&&!IsWindowVisible(markup_input);
        SendMessageW(markup_window,WM_COMMAND,MARKUP_PANEL,0);
        panel=panel&&IsWindowVisible(markup_input);
        fprintf(receipt,"RESIZED=%d GROUPED=%d ZOOM_FIT=%d COLLAPSIBLE_PANEL=%d\n",resized,grouped,zoom,panel);
        if(!resized||!grouped||!zoom||!panel){done(0);return 0;}
        mark_at(image,image->holes[0].x,image->holes[0].y,MARKUP_TOOL_CIRCLE);
        SetWindowTextW(markup_input,L"这个孔改 Ø5");
        wchar_t feedback[256];
        GetWindowTextW(markup_summary,feedback,_countof(feedback));
        int circle=markup_count==1&&wcsstr(feedback,L"Ø4 → Ø5 mm")&&check_diameter(image->holes[0].feature,4);
        SendMessageW(markup_window,WM_COMMAND,MARKUP_UNDO,0);
        mark_at(image,image->holes[0].x,image->holes[0].y,MARKUP_TOOL_BOX);
        GetWindowTextW(markup_summary,feedback,_countof(feedback));
        int box=markup_count==1&&wcsstr(feedback,L"Ø4 → Ø5 mm");
        SendMessageW(markup_window,WM_COMMAND,MARKUP_CLEAR,0);
        fprintf(receipt,"CIRCLE_PREVIEW=%d BOX_PREVIEW=%d CLEAR=%d\n",circle,box,markup_count==0);
        if(!circle||!box||markup_count){done(0);return 0;}
        for(int i=0;i<5;i++){
            for(int j=0;j<image->count;j++)if(image->holes[j].feature==features[i])
                mark_at(image,image->holes[j].x,image->holes[j].y,MARKUP_TOOL_ARROW);
        }
        fprintf(receipt,"MARKS=%d\n",markup_count);
        if(markup_count!=5){done(0);return 0;}
        SetWindowTextW(markup_input,L"所有标注的孔径改到8mm");
        phase=2;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==2&&!markup_waiting&&markup_retake){
        int pass=check_diameter(features[5],4);
        for(int i=0;i<5;i++)pass=pass&&check_diameter(features[i],8);
        wchar_t status[1000];
        GetWindowTextW(markup_status,status,_countof(status));
        pass=pass&&wcsstr(status,L"已修改 5 处")&&wcsstr(status,L"Ø4 → 8 mm");
        fprintf(receipt,"FIVE_WRITES_AND_STATUS=%d\n",pass);
        if(!pass){done(0);return 0;}
        phase=3;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==3&&markup_window&&IsWindowVisible(markup_window)&&markup_display&&markup_capture.image&&!markup_retake&&markup_display->ticket==markup_capture.image->ticket){
        MarkupImage *image=markup_capture.image;
        mark_at(image,image->holes[0].x,image->holes[0].y,MARKUP_TOOL_ARROW);
        mark_at(image,110,110,MARKUP_TOOL_ARROW);
        if(markup_count!=2){fprintf(receipt,"AFTER_RECAPTURE_MARKS=%d\n",markup_count);done(0);return 0;}
        SetWindowTextW(markup_input,L"1号 Ø7；2号 Ø9");
        phase=4;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==4&&!markup_waiting){
        wchar_t status[1000];
        GetWindowTextW(markup_status,status,_countof(status));
        if(!wcsstr(status,L"2 号标注"))return 0;
        int pass=check_diameter(features[5],4);
        for(int i=0;i<5;i++)pass=pass&&check_diameter(features[i],8);
        fprintf(receipt,"FAILED_TARGET_VISIBLE_NO_WRITES=%d\n",pass);
        if(!pass){done(0);return 0;}
        SendMessageW(markup_window,WM_COMMAND,MARKUP_CLEAR,0);
        MarkupImage *image=markup_capture.image;
        for(int i=0;i<image->count;i++)if(image->holes[i].feature==features[5])
            mark_at(image,image->holes[i].x,image->holes[i].y,MARKUP_TOOL_CIRCLE);
        SetWindowTextW(markup_input,L"这个孔改 Ø5");
        GetWindowTextW(markup_summary,status,_countof(status));
        int located=markup_count==1&&wcsstr(status,L"Ø4 → Ø5 mm")!=NULL;
        fprintf(receipt,"SINGLE_CIRCLE_TARGET=%d\n",located);
        if(!located){done(0);return 0;}
        phase=5;
        SendMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        if(!markup_waiting){fprintf(receipt,"SINGLE_CIRCLE_SUBMITTED=0\n");done(0);}
        return 0;
    }

    if(phase==5&&!markup_waiting){
        wchar_t status[1000];
        GetWindowTextW(markup_status,status,_countof(status));
        if(wcsstr(status,L"正在处理你的要求"))return 0;
        int pass=markup_retake&&check_diameter(features[5],5)&&wcsstr(status,L"Ø4 → 5 mm");
        for(int i=0;i<5;i++)pass=pass&&check_diameter(features[i],8);
        fprintf(receipt,"SINGLE_CIRCLE_EDIT_AND_READBACK=%d\n",pass);
        if(!pass){done(0);return 0;}
        SetWindowTextW(markup_input,L"刚才这个孔再大 1 mm");
        GetWindowTextW(markup_send,status,_countof(status));
        int continues=!wcscmp(status,L"继续改图");
        fprintf(receipt,"CONTINUE_BUTTON=%d\n",continues);
        if(!continues){done(0);return 0;}
        phase=51;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==51&&!markup_waiting&&markup_retake&&check_diameter(features[5],6)){
        wchar_t status[1000];
        GetWindowTextW(markup_status,status,_countof(status));
        int pass=wcsstr(status,L"Ø5 → 6 mm")!=NULL&&!markup_ai.process&&markup_count==0;
        for(int i=0;i<5;i++)pass=pass&&check_diameter(features[i],8);
        fprintf(receipt,"FOLLOWUP_RETAKES_AND_WRITES_WITHOUT_REMARK=%d\n",pass);
        if(!pass){done(0);return 0;}
        phase=6;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==6&&markup_window&&IsWindowVisible(markup_window)&&markup_display&&markup_capture.image&&!markup_retake&&markup_display->ticket==markup_capture.image->ticket){
        MarkupImage *image=markup_capture.image;
        const MarkupHole *row[3]={0};
        for(int i=0;i<image->count;i++)for(int j=0;j<3;j++)
            if(image->holes[i].feature==features[j])row[j]=&image->holes[i];
        if(!row[0]||!row[1]||!row[2]){done(0);return 0;}
        double low_x=HUGE_VAL,high_x=-HUGE_VAL,low_y=HUGE_VAL,high_y=-HUGE_VAL;
        for(int i=0;i<3;i++){
            low_x=fmin(low_x,row[i]->x);high_x=fmax(high_x,row[i]->x);
            low_y=fmin(low_y,row[i]->y);high_y=fmax(high_y,row[i]->y);
        }
        const RECT r=markup_image_rect(markup_canvas);
        int left=r.left+(int)lround((low_x-12)*(r.right-r.left)/image->width);
        int right=r.left+(int)lround((high_x+12)*(r.right-r.left)/image->width);
        int top=r.top+(int)lround((low_y-12)*(r.bottom-r.top)/image->height);
        int bottom=r.top+(int)lround((high_y+12)*(r.bottom-r.top)/image->height);
        SendMessageW(markup_window,WM_COMMAND,MARKUP_TOOL_BOX,0);
        SendMessageW(markup_canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(left,top));
        SendMessageW(markup_canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(right,bottom));
        SendMessageW(markup_canvas,WM_LBUTTONUP,0,MAKELPARAM(right,bottom));
        mark_at(image,row[0]->x,row[0]->y,MARKUP_TOOL_ARROW);
        if(markup_count!=2){done(0);return 0;}
        for(int i=0;i<6;i++){
            int ordinal=0;
            while(ordinal<image->count&&image->holes[ordinal].feature!=features[i])ordinal++;
            if(ordinal==image->count){done(0);return 0;}
            native_holes[i]=image->holes[ordinal];
        }
        SetWindowTextW(markup_input,L"把圈出的四个孔都改成 Ø6");
        phase=7;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==7&&!markup_waiting){
        wchar_t status[1000];
        GetWindowTextW(markup_status,status,_countof(status));
        if(!wcsstr(status,L"实际核对到 3 个"))return 0;
        int pass=markup_count==2&&!markup_retake&&!markup_ai.process;
        for(int i=0;i<6;i++)pass=pass&&!markup_bore_read(model,&native_holes[i],i==5?6:8);
        fprintf(receipt,"MISMATCHED_QUANTITY_KEPT_BOTH_MARKS_NO_WRITE=%d\n",pass);
        if(!pass){done(0);return 0;}
        SetWindowTextW(markup_input,L"把圈出的三个孔都改成 Ø6");
        phase=8;
        PostMessageW(markup_window,WM_COMMAND,MARKUP_SEND,0);
        return 0;
    }
    if(phase==8&&!markup_waiting){
        wchar_t status[1000];
        GetWindowTextW(markup_status,status,_countof(status));
        if(wcsstr(status,L"正在处理你的要求"))return 0;
        int pass=markup_count==2&&markup_retake&&!markup_ai.process&&wcsstr(status,L"已修改 3 处");
        for(int i=0;i<6;i++){
            double diameter=i<3?6:i==5?6:8;
            int check=!markup_bore_read(model,&native_holes[i],diameter)&&check_diameter(features[i],diameter);
            fprintf(receipt,"SITE_%d_NATIVE_FEATURE_%d_SURFACE_%d_EXPECTED_%.1f=%d\n",
                i+1,features[i],native_holes[i].surface,diameter,check);
            pass=pass&&check;
        }
        fprintf(receipt,"NATURAL_GROUP_BOX_PLUS_OVERLAPPING_ARROW=%d\n",pass);
        done(pass);
    }

    return 0;
}

static int open_markup_test(uiCmdCmdId id,uiCmdValue *value,void *data){
    int win;
    if(!ProWindowCurrentGet(&win))ProWindowBrowserSizeSet(win,0);
    HWND owner=GetAncestor(GetActiveWindow(),GA_ROOT);
    int opened=native_ui_open_threaded(owner);
    wchar_t label[30]=L"";
    if(opened)GetWindowTextW(native_top_buttons[4],label,_countof(label));
    int menu=opened&&!wcscmp(label,L"截图改图")&&IsWindowEnabled(native_top_buttons[4]);
    fprintf(receipt,"MENU=%d\n",menu);
    if(!menu||!SetTimer(native_backend_window,9008,250,test_timer)){done(0);return 0;}
    phase=1;
    deadline=GetTickCount64()+45000;
    PostMessageW(native_window,WM_COMMAND,CHAT_RESERVED_3,0);
    return 0;
}

static int prepare_markup_test(uiCmdCmdId id,uiCmdValue *value,void *data){
    receipt=fopen("screenshot-markup.log","w");
    if(!receipt){ProEngineerEnd();return 0;}
    setvbuf(receipt,NULL,_IONBF,0);
    Fixture stock;ProMdlName name;
    model_background_mode=1;
    ProError error=fixture(&model,name,&stock);
    wchar_t command[512],label[16];
    for(int i=0;!error&&i<6;i++){
        swprintf_s(label,_countof(label),L"H%d",i+1);
        swprintf_s(command,_countof(command),L"CREO_OP HOLE %ls %ls SURFACE:%d SURFACE:%d %d SURFACE:%d %d 4 THROUGH 0",
            name,label,stock.top,stock.x,16+24*(i%3),stock.y,16+24*(i/3));
        error=expansion_build(model,command,receipt);
        ProModelitem item;
        if(!error)error=ProModelitemByNameInit(model,PRO_FEATURE,label,&item);
        if(!error)features[i]=item.id;
    }
    if(!error)error=dv_display_new(model);
    if(!error)error=ProMacroLoad(L"~ Command " L"\x60" L"BWTLMarkupOpenTest" L"\x60;");
    if(error)done(0);
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLMarkupPrepare",prepare_markup_test,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLMarkupOpenTest",open_markup_test,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLMarkupCheck",check,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command " L"\x60" L"BWTLMarkupPrepare" L"\x60;");
}