static ProError ax_interference_outline(const AxOccurrence *item,AxInterferenceStamp *stamp,Pro3dPnt outline[2]){
    Pro3dPnt local[2];
    ProError error=ProSolidOutlineGet((ProSolid)item->model,local);
    if(error)return error;
    for(int corner=0;corner<8;corner++){
        ProPoint3d point,transformed;
        for(int axis=0;axis<3;axis++)point[axis]=local[(corner>>axis)&1][axis];
        if((error=ProPntTrfEval(point,stamp->pose,transformed)))return error;
        for(int axis=0;axis<3;axis++){
            if(!isfinite(transformed[axis]))return PRO_TK_BAD_CONTEXT;
            outline[0][axis]=corner?fmin(outline[0][axis],transformed[axis]):transformed[axis];
            outline[1][axis]=corner?fmax(outline[1][axis],transformed[axis]):transformed[axis];
        }
    }
    return PRO_TK_NO_ERROR;
}

static ProError ax_interference_box(AxInterferenceCache *cache,AxInterferenceView *view,int row,ProPoint3d corners[8]){
    Pro3dPnt envelope[2][2],bounds[2];
    const AxInterferencePair *pair=&cache->pairs[row];
    for(int side=0;side<2;side++){
        int i=pair->index[side];
        ProError error=ax_interference_outline(&cache->scan->occurrences[i],&cache->stamps[i],envelope[side]);
        if(error)return error;
    }
    for(int axis=0;axis<3;axis++){
        bounds[0][axis]=fmax(envelope[0][0][axis],envelope[1][0][axis]);
        bounds[1][axis]=fmin(envelope[0][1][axis],envelope[1][1][axis]);
        if(bounds[1][axis]<=bounds[0][axis])return PRO_TK_INCOMPLETE;
    }
    for(int i=0;i<8;i++){
        ProPoint3d point;
        for(int axis=0;axis<3;axis++)point[axis]=bounds[(i>>axis)&1][axis];
        ProError error=ProPntTrfEval(point,view->from_root,corners[i]);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

typedef struct {
    ProPoint3d vertices[AX_INTERFERENCE_BOX_LIMIT][8];
    int count;
} AxInterferenceLines;

static ProError ax_interference_draw(void *data,void *a,void *b,void *c,void *d,void *e){
    (void)a;(void)b;(void)c;(void)d;(void)e;
    AxInterferenceLines *lines=data;
    ProColor old,red={0};
    red.method=PRO_COLOR_METHOD_RGB;
    red.value.map=(ProColormap){1.0,0.10,0.10};
    ProError error=ProGraphicsColorModify(NULL,&old);
    if(error&&error!=PRO_TK_NO_CHANGE)return error;
    error=ProGraphicsColorModify(&red,NULL);
    if(error&&error!=PRO_TK_NO_CHANGE)return error;
    for(int i=0;i<lines->count;i++){
        for(int corner=0;corner<8;corner++){
            for(int axis=1;axis<=4;axis*=2){
                if(corner&axis)continue;
                ProGraphicsPenPosition(lines->vertices[i][corner]);
                ProGraphicsLineDraw(lines->vertices[i][corner|axis]);
            }
        }
    }
    error=ProGraphicsColorModify(&old,NULL);
    return error==PRO_TK_NO_CHANGE?PRO_TK_NO_ERROR:error;
}

static ProError ax_interference_display(ProMdl model,AxInterferenceView *view,AxInterferenceLines *lines){
    ProError error=ax_interference_clear_overlay();
    if(!error)error=ProDisplist3dCreate(AX_INTERFERENCE_DISPLAY,ax_interference_draw,lines,NULL,NULL,NULL,NULL,NULL);
    if(error)return error;
    ax_interference_overlay.exists=1;
    ax_interference_overlay.window=view->window;
    ax_interference_overlay.model=model;
    ProMatrix identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    error=ProDisplist3dDisplay(AX_INTERFERENCE_DISPLAY,identity);
    if(!error)error=ProWindowRepaint(view->window);
    if(error)(void)ax_interference_clear_overlay();
    return error;
}

static ProError ax_interference_show(ProMdl model,AxInterferenceCache *cache,AxInterferenceView *view,
                                    const wchar_t *selector,PartText *out){
    const int all=!wcscmp(selector,L"ALL"),prev=!wcscmp(selector,L"PREV"),next=!wcscmp(selector,L"NEXT");
    int row=0,eligible=0,omitted=0;
    if(!all&&!next&&!prev&&(!part_integer(selector,&row)||row<1))return PRO_TK_BAD_INPUTS;
    if(next||prev){
        const int step=prev?-1:1;
        int found=cache->selected;
        if(found<0||found>=cache->count)found=prev?0:cache->count-1;
        for(int i=0;i<cache->count;i++){
            found=(found+step+cache->count)%cache->count;
            if(ax_interference_row_visible(cache,view,found,cache->visible,NULL)){
                row=found+1;
                break;
            }
        }
        if(!row){
            part_text_add(out,L"当前窗口范围内没有符合条件的干涉序号。\n");
            return PRO_TK_E_NOT_FOUND;
        }
    }

    AxInterferenceLines *lines=calloc(1,sizeof(*lines));
    if(!lines)return PRO_TK_OUT_OF_MEMORY;
    ProError error=PRO_TK_NO_ERROR;
    for(int i=0;i<cache->count;i++){
        if((!all&&row!=i+1)||!ax_interference_row_visible(cache,view,i,cache->visible,NULL))continue;
        eligible++;
        if(lines->count==AX_INTERFERENCE_BOX_LIMIT){omitted++;continue;}
        ProError box=ax_interference_box(cache,view,i,lines->vertices[lines->count]);
        if(box){
            if(!all){error=box;break;}
            omitted++;
        }else lines->count++;
    }
    if(!error&&!lines->count){
        error=eligible?PRO_TK_INCOMPLETE:PRO_TK_E_NOT_FOUND;
        part_text_add(out,eligible?L"所选干涉没有可核对的位置框。\n":L"当前窗口范围内没有符合条件的干涉序号。\n");
    }
    if(!error)error=ax_interference_display(model,view,lines);
    if(!error){
        if(!all)cache->selected=row-1;
        if(row)part_text_add(out,L"已定位第 %d 对。",row);
        part_text_add(out,L"已绘制 %d 个红色干涉位置框。红框使用两个实例在总装中的包络交集。\n",lines->count);
        if(omitted)part_text_add(out,L"另有 %d 对未画框。可指定行序号单独复核。\n",omitted);
    }
    free(lines);
    return error;
}
