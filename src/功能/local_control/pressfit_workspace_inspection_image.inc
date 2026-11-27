static HRESULT pressfit_delivery_png(HBITMAP bitmap,const wchar_t *path){
    IWICImagingFactory *factory=NULL;
    IWICBitmap *source=NULL;
    IWICStream *stream=NULL;
    IWICBitmapEncoder *encoder=NULL;
    IWICBitmapFrameEncode *frame=NULL;
    IPropertyBag2 *properties=NULL;
    const HRESULT init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    HRESULT status=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory,(void **)&factory);
    if(SUCCEEDED(status))status=IWICImagingFactory_CreateBitmapFromHBITMAP(factory,bitmap,NULL,WICBitmapIgnoreAlpha,&source);
    if(SUCCEEDED(status))status=IWICImagingFactory_CreateStream(factory,&stream);
    if(SUCCEEDED(status))status=IWICStream_InitializeFromFilename(stream,path,GENERIC_WRITE);
    if(SUCCEEDED(status))status=IWICImagingFactory_CreateEncoder(factory,&GUID_ContainerFormatPng,NULL,&encoder);
    if(SUCCEEDED(status))status=IWICBitmapEncoder_Initialize(encoder,(IStream *)stream,WICBitmapEncoderNoCache);
    if(SUCCEEDED(status))status=IWICBitmapEncoder_CreateNewFrame(encoder,&frame,&properties);
    if(SUCCEEDED(status))status=IWICBitmapFrameEncode_Initialize(frame,properties);
    if(SUCCEEDED(status))status=IWICBitmapFrameEncode_WriteSource(frame,(IWICBitmapSource *)source,NULL);
    if(SUCCEEDED(status))status=IWICBitmapFrameEncode_Commit(frame);
    if(SUCCEEDED(status))status=IWICBitmapEncoder_Commit(encoder);
    if(properties)IPropertyBag2_Release(properties);
    if(frame)IWICBitmapFrameEncode_Release(frame);
    if(encoder)IWICBitmapEncoder_Release(encoder);
    if(stream)IWICStream_Release(stream);
    if(source)IWICBitmap_Release(source);
    if(factory)IWICImagingFactory_Release(factory);
    if(SUCCEEDED(init))CoUninitialize();
    return status;
}

static COLORREF pressfit_delivery_color(int status){
    switch(status){
    case PRESSFIT_DELIVERY_INSTALLED:return RGB(7,116,75);
    case PRESSFIT_DELIVERY_MISSING:return RGB(175,36,30);
    case PRESSFIT_DELIVERY_REVIEW:return RGB(146,79,12);
    default:return RGB(62,79,109);
    }
}

static void pressfit_delivery_fill(HDC dc,const RECT *bounds,COLORREF color){
    HBRUSH brush=CreateSolidBrush(color);
    if(brush){FillRect(dc,bounds,brush);DeleteObject(brush);}
}

static void pressfit_delivery_label(HDC dc,int x,int y,const wchar_t *text,COLORREF color){
    SetTextColor(dc,color);
    TextOutW(dc,x,y,text,(int)wcslen(text));
}

/* Move just the label; its leader always returns to the projected native
   point. Any hidden or overcrowded location remains in the summary table. */
static int pressfit_delivery_label_rect(const RECT *used,int count,int x,int y,int width,
    const RECT *viewport,RECT *label){
    const int dx[]={12,12,-12,-12,12,12,-12,-12,40,-40,40,-40};
    const int dy[]={-12,12,-12,12,-36,36,-36,36,-53,53,53,-53};
    for(int i=0;i<_countof(dx);i++){
        const int left=x+dx[i]-(dx[i]<0?width:0),top=y+dy[i];
        RECT candidate={left,top,left+width,top+22};
        if(candidate.left<viewport->left||candidate.right>viewport->right||
           candidate.top<viewport->top||candidate.bottom>viewport->bottom)continue;
        int overlap=0;
        for(int j=0;j<count;j++){
            RECT intersection;
            if(IntersectRect(&intersection,&candidate,&used[j])){overlap=1;break;}
        }
        if(!overlap){*label=candidate;return 1;}
    }
    return 0;
}

static ProError pressfit_delivery_raster(ProMdl assembly,MarkupProjection *map,MarkupImage *image){
    map->model=assembly;
    map->image=image;
    ProError error=ProWindowCurrentGet(&map->window);
    if(!error)error=ProViewMatrixGet(assembly,NULL,map->view);
    if(!error)error=ProWindowCurrentMatrixGet(map->zoom);
    if(!error)error=ProWindowPixelOutlineGet(map->window,map->viewport);
    if(error)return error;
    const double w=map->viewport[1][0]-map->viewport[0][0],h=map->viewport[1][1]-map->viewport[0][1];
    if(!isfinite(w)||!isfinite(h)||w<100||h<100)return PRO_TK_BAD_CONTEXT;
    const double ratio=fmin(1.0,1800/fmax(w,h));
    wchar_t path[MAX_PATH];
    if(!storage_directory_on_drive(ai_dir,L'E')||
       swprintf_s(path,_countof(path),L"%ls\\pressfit-raster-%lu-%ld.tif",ai_dir,GetCurrentProcessId(),
        InterlockedIncrement(&pressfit_delivery_serial))<0)return PRO_TK_CANT_WRITE;
    error=ProRasterFileWrite(map->window,PRORASTERDEPTH_24,w*ratio/100,h*ratio/100,
        PRORASTERDPI_100,PRORASTERTYPE_TIFF,path);
    if(!error)image->bitmap=markup_read_raster(path,&image->width,&image->height);
    DeleteFileW(path);
    return error?error:image->bitmap?PRO_TK_NO_ERROR:PRO_TK_CANT_OPEN;
}

static int pressfit_delivery_plot(HDC output,MarkupProjection *map,const PressfitDeliveryRow *rows,int count,
    const RECT *viewport,int *hidden){
    RECT labels[PRESSFIT_DELIVERY_MAX];
    int marked=0;
    *hidden=0;
    for(int i=0;i<count;i++){
        const PressfitDeliveryRow *row=&rows[i];
        if(row->status==PRESSFIT_DELIVERY_UNASSIGNED)continue;
        ProPoint3d world={row->world[0],row->world[1],row->world[2]};
        Pro2dPnt pixel;
        if(!markup_project(map,world,pixel)||pixel[0]<0||pixel[1]<0||
           pixel[0]>=map->image->width||pixel[1]>=map->image->height){(*hidden)++;continue;}
        const int x=viewport->left+(int)lround(pixel[0]);
        const int y=viewport->top+(int)lround(pixel[1]);
        wchar_t key[48];
        swprintf_s(key,_countof(key),L"%d/%d",row->panel_id,row->number);
        SIZE extent={0};
        GetTextExtentPoint32W(output,key,(int)wcslen(key),&extent);
        RECT label;
        if(!pressfit_delivery_label_rect(labels,marked,x,y,extent.cx+12,viewport,&label)){
            (*hidden)++;
            continue;
        }
        const COLORREF color=pressfit_delivery_color(row->status);
        HPEN pen=CreatePen(PS_SOLID,2,color);
        if(!pen){(*hidden)++;continue;}
        HGDIOBJ prior_pen=SelectObject(output,pen);
        HGDIOBJ prior_brush=SelectObject(output,GetStockObject(HOLLOW_BRUSH));
        MoveToEx(output,x,y,NULL);
        LineTo(output,label.left<x?label.right:label.left,(label.top+label.bottom)/2);
        Ellipse(output,x-6,y-6,x+7,y+7);
        SelectObject(output,prior_brush);
        SelectObject(output,prior_pen);
        DeleteObject(pen);
        pressfit_delivery_fill(output,&label,color);
        pressfit_delivery_label(output,label.left+6,label.top+2,key,RGB(255,255,255));
        labels[marked++]=label;
    }
    return marked;
}

static ProError pressfit_delivery_image(ProMdl assembly,const PressfitDeliveryRow *rows,int count,
    const wchar_t *directory,PartText *out){
    wchar_t path[MAX_PATH];
    if(!pressfit_delivery_identifier(directory,L"压铆装配检查图.png",path))return PRO_TK_OUT_OF_RANGE;
    MarkupImage image={0};
    MarkupProjection map={0};
    ProError error=pressfit_delivery_raster(assembly,&map,&image);
    if(error)return error;

    const int top=63,bottom=68,margin=14;
    const int width=(int)fmax(560,image.width+margin*2),height=top+image.height+bottom;
    const int left=(width-image.width)/2;
    const RECT viewport={left,top,left+image.width,top+image.height};
    HDC output=CreateCompatibleDC(NULL),source=CreateCompatibleDC(NULL);
    BITMAPINFO info={0};
    info.bmiHeader=(BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER),width,-height,1,32};
    void *bits=NULL;
    HBITMAP canvas=output?CreateDIBSection(output,&info,DIB_RGB_COLORS,&bits,NULL,0):NULL;
    if(!canvas||!source){error=PRO_TK_OUT_OF_MEMORY;goto done;}
    HGDIOBJ old=SelectObject(output,canvas),base=SelectObject(source,image.bitmap);
    RECT page={0,0,width,height};
    pressfit_delivery_fill(output,&page,RGB(245,249,250));
    if(!BitBlt(output,left,top,image.width,image.height,source,0,0,SRCCOPY))error=PRO_TK_CANT_OPEN;
    SelectObject(source,base);
    SetBkMode(output,TRANSPARENT);
    HFONT title=CreateFontW(-22,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    HFONT font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    HGDIOBJ prior_font=title?SelectObject(output,title):GetCurrentObject(output,OBJ_FONT);
    pressfit_delivery_label(output,margin,8,L"压铆装配检查图",RGB(16,42,55));
    if(font)SelectObject(output,font);
    ProMdlName name=L"";
    ProMdlMdlnameGet(assembly,name);
    wchar_t caption[160];
    swprintf_s(caption,_countof(caption),L"%ls · 标记为 板件实例 ID／孔号 · 当前 Creo 视角",name);
    pressfit_delivery_label(output,margin,37,caption,RGB(53,72,85));
    int hidden=0;
    const int marked=pressfit_delivery_plot(output,&map,rows,count,&viewport,&hidden);
    const int footer_y=viewport.bottom+8;
    pressfit_delivery_label(output,margin,footer_y,L"● 已装并核对",pressfit_delivery_color(PRESSFIT_DELIVERY_INSTALLED));
    pressfit_delivery_label(output,margin+128,footer_y,L"● 待补装",pressfit_delivery_color(PRESSFIT_DELIVERY_MISSING));
    pressfit_delivery_label(output,margin+226,footer_y,L"● 待复核",pressfit_delivery_color(PRESSFIT_DELIVERY_REVIEW));
    swprintf_s(caption,_countof(caption),L"图中标注 %d 处；视图外或标记太密 %d 处请查安装孔位表。",marked,hidden);
    pressfit_delivery_label(output,margin,footer_y+25,caption,RGB(53,72,85));
    SelectObject(output,prior_font);
    if(title)DeleteObject(title);
    if(font)DeleteObject(font);
    SelectObject(output,old);
    if(!error&&FAILED(pressfit_delivery_png(canvas,path)))error=PRO_TK_CANT_WRITE;
    if(error)DeleteFileW(path);
    else part_text_add(out,L"已保存 Creo 检查图，标注 %d 处。另有 %d 处请查安装孔位表。\n%ls\n",marked,hidden,path);
done:
    if(canvas)DeleteObject(canvas);
    if(source)DeleteDC(source);
    if(output)DeleteDC(output);
    DeleteObject(image.bitmap);
    return error;
}