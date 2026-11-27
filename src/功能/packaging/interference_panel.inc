/* Native MWEAI page. Creo calls run on the same backend as packaging commands. */
enum { CLASH_SCAN=14, CLASH_PAIR, CLASH_A, CLASH_B, CLASH_CLEAR, CLASH_ISOLATE,
       CLASH_RESTORE, CLASH_BACK, CLASH_TO_PACKAGING, CLASH_CLOSE, CLASH_TO_DFM, CLASH_TO_PRICE, CLASH_ACTION_LAST=CLASH_TO_PRICE };
enum { CLASH_UPDATE=WM_APP+306, CLASH_SEARCH=701, CLASH_PARTS, CLASH_ROWS,
       CLASH_ALL, CLASH_NONE, CLASH_BUTTON=720 };

typedef struct { const wchar_t *name; int count, checked; } ClashPart;
static HWND clash_panel,clash_title,clash_filter,clash_search,clash_parts,clash_rows,clash_all,clash_none,clash_status;
static HWND clash_buttons[8];
static AxInterferenceCache clash_result;
static ClashPart clash_groups[AX_MAX_OCCURRENCES];
static int clash_group_for[AX_MAX_OCCURRENCES],clash_group_count,clash_updating;
static int clash_row=-1,clash_side,clash_selected_row=-1,clash_window=-1;
static LONG clash_pending,clash_cleanup_requested;
static int clash_red_window=-1,clash_red_count,clash_box_visible;
enum { CLASH_BOX_DISPLAY=0x42574342 };
static ULONGLONG clash_queued_at;
static ProSimprep clash_previous,clash_isolated;
static ProSelection clash_highlights[2];
static int clash_has_isolation;
static double clash_mm;
static HBRUSH clash_brush;

static void packaging_switch(int show);
static void packaging_layout(void);

static ProError clash_restore(void){
    if(!clash_has_isolation)return PRO_TK_NO_ERROR;
    ProMdl owner=NULL;
    if(ProMdlnameInit(clash_result.name,PRO_MDL_ASSEMBLY,&owner)||owner!=clash_result.root){
        clash_has_isolation=0;
        return PRO_TK_NO_ERROR;
    }
    int current=-1,owner_window=-1;
    ProWindowCurrentGet(&current);
    ProError error=ProMdlWindowGet(owner,&owner_window);
    if(!error&&owner_window!=current)error=ProWindowCurrentSet(owner_window);
    if(!error)error=ProSimprepActivate((ProSolid)owner,&clash_previous);
    if(!error)error=ProSimprepDelete(&clash_isolated);
    if(!error){clash_has_isolation=0;(void)ProWindowRepaint(owner_window);}
    if(owner_window>=0&&owner_window!=current&&current>=0)(void)ProWindowCurrentSet(current);
    return error;
}

static void clash_clear_red(void){
    if(clash_box_visible){ProDisplist3dDelete(CLASH_BOX_DISPLAY);clash_box_visible=0;}
    if(clash_red_window<0)return;
    ProMdl shown=NULL;
    if(!ProWindowMdlGet(clash_red_window,&shown)&&shown==clash_result.root)
        (void)ProWindowRepaint(clash_red_window);
    clash_red_window=-1;
    clash_red_count=0;
}

static ProError clash_isolate(ProMdl model,int row){
    ProError error=clash_restore();
    if(!error)error=ProSimprepActiveGet((ProSolid)model,&clash_previous);
    static LONG sequence;
    ProName name;
    swprintf_s(name,_countof(name),L"MWEAI_CLASH_%08lX_%04lX",GetCurrentProcessId(),InterlockedIncrement(&sequence)&0xffff);
    ProSimprepdata *data=NULL;
    if(!error)error=ProSimprepdataAlloc(name,PRO_B_FALSE,PRO_SIMPREP_EXCLUDE,&data);
    for(int side=0;!error&&side<2;side++){
        if(clash_selected_row==row&&clash_side&&clash_side!=side+1)continue;
        const AxOccurrence *occurrence=&clash_result.scan->occurrences[clash_result.pairs[row].index[side]];
        ProSimprepitem item={0};
        ProSimprepAction action={0};
        ProIdTable ids={0};
        memcpy(ids,occurrence->ids,(size_t)occurrence->depth*sizeof(int));
        action.type=PRO_SIMPREP_INCLUDE;
        error=ProSimprepdataitemInit(ids,occurrence->depth,PRO_VALUE_UNUSED,&action,&item);
        if(!error)error=ProSimprepdataitemAdd(data,&item);
    }
    if(!error){
        error=ProSimprepCreate((ProSolid)model,data,&clash_isolated);
        clash_has_isolation=!error;
    }
    if(data)ProSimprepdataFree(&data);
    if(!error)error=ProSimprepActivate((ProSolid)model,&clash_isolated);
    if(!error)error=ax_view_repaint(model);
    if(error&&clash_has_isolation)(void)clash_restore();
    return error;
}

static void clash_clear_highlights(void){
    for(int i=0;i<_countof(clash_highlights);i++)if(clash_highlights[i]){
        ProSelectionUnhighlight(clash_highlights[i]);
        ProSelectionFree(&clash_highlights[i]);
    }
}

static ProError clash_occurrence_selection(int row,int side,ProSelection *selection){
    const AxOccurrence *part=&clash_result.scan->occurrences[clash_result.pairs[row].index[side]];
    ProAsmcomppath path;
    ProModelitem item;
    ProError error=ax_interference_occurrence_path(clash_result.root,part,&path);
    if(!error)error=ProMdlToModelitem(part->model,&item);
    return error?error:ProSelectionAlloc(&path,&item,selection);
}

static ProError clash_draw_box_lines(void *data,void *a,void *b,void *c,void *d,void *e){
    (void)a;(void)b;(void)c;(void)d;(void)e;
    ProPoint3d *corners=data;
    ProColor old,red={0};
    red.method=PRO_COLOR_METHOD_RGB;
    red.value.map=(ProColormap){1.0,0.12,0.10};
    ProError error=ProGraphicsColorModify(&red,&old);
    if(error&&error!=PRO_TK_NO_CHANGE)return error;
    const int faces[][4]={{0,1,3,2},{4,5,7,6},{0,1,5,4},{2,3,7,6},{0,2,6,4},{1,3,7,5}};
    for(int face=0;face<_countof(faces);face++)for(int i=0;i<=56;i++){
        double t=(double)i/56;
        ProPoint3d start,end;
        for(int axis=0;axis<3;axis++){
            start[axis]=(1-t)*corners[faces[face][0]][axis]+t*corners[faces[face][1]][axis];
            end[axis]=(1-t)*corners[faces[face][3]][axis]+t*corners[faces[face][2]][axis];
        }
        ProGraphicsPenPosition(start);
        ProGraphicsLineDraw(end);
    }
    (void)ProGraphicsColorModify(&old,NULL);
    return PRO_TK_NO_ERROR;
}

/* The solid intersection is contained in this box. Draw a filled-style
   marker only when its volume agrees with the independently computed solid. */
static ProError clash_draw_verified_box(int row,int window){
    AxInterferenceView view={.window=window};
    for(int i=0;i<4;i++)view.from_root[i][i]=1;
    ProPoint3d corners[8];
    ProError error=ax_interference_box(&clash_result,&view,row,corners);
    if(error)return error;
    double box=1;
    for(int axis=0;axis<3;axis++)box*=corners[7][axis]-corners[0][axis];
    const double measured=clash_result.pairs[row].volume;
    if(!isfinite(measured)||measured<=0||fabs(box-measured)>measured*1e-6)
        return PRO_TK_E_NOT_FOUND;
    if(clash_box_visible)ProDisplist3dDelete(CLASH_BOX_DISPLAY);
    error=ProDisplist3dCreate(CLASH_BOX_DISPLAY,clash_draw_box_lines,corners,NULL,NULL,NULL,NULL,NULL);
    if(error){clash_box_visible=0;return error;}
    clash_box_visible=1;
    ProMatrix identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    error=ProDisplist3dDisplay(CLASH_BOX_DISPLAY,identity);
    if(!error)error=ProWindowRepaint(window);
    if(error){ProDisplist3dDelete(CLASH_BOX_DISPLAY);clash_box_visible=0;}
    return error;
}
static ProError clash_draw_pair(int row,int window){
    ProSelection selection[2]={NULL,NULL};
    ProInterferenceData data=NULL;
    ProError error=clash_occurrence_selection(row,0,&selection[0]);
    if(!error)error=clash_occurrence_selection(row,1,&selection[1]);
    if(!error)error=ProFitInterferenceCompute(selection[0],selection[1],PRO_B_FALSE,PRO_B_FALSE,&data);
    if(!error)error=data?ProFitInterferencevolumeDisplay(data,PRO_COLOR_ERROR):PRO_TK_E_NOT_FOUND;
    if(data)ProInterferenceDataFree(data);
    for(int i=0;i<2;i++)if(selection[i])ProSelectionFree(&selection[i]);
    if(!clash_draw_verified_box(row,window))error=PRO_TK_NO_ERROR;
    if(!error){clash_red_window=window;clash_red_count=1;}
    return error;
}
static ProError clash_select(int row,int side,int *highlighted){
    clash_clear_highlights();
    ProError error=ProSelbufferClear();
    *highlighted=error==PRO_TK_CANT_ACCESS||error==PRO_TK_CANT_MODIFY;
    if(*highlighted||error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    for(int i=0;!error&&i<2;i++){
        if(side&&side!=i+1)continue;
        const AxOccurrence *occurrence=&clash_result.scan->occurrences[clash_result.pairs[row].index[i]];
        ProAsmcomppath path;
        ProSelection selection=NULL;
        ProModelitem item;
        error=ax_interference_occurrence_path(clash_result.root,occurrence,&path);
        if(!error)error=ProMdlToModelitem(occurrence->model,&item);
        if(!error&&!*highlighted)error=ProSelectionAlloc(&path,&item,&selection);
        if(!error&&!*highlighted)error=ProSelbufferSelectionAdd(selection);
        if(selection)ProSelectionFree(&selection);
        if(error==PRO_TK_CANT_ACCESS||error==PRO_TK_CANT_MODIFY){*highlighted=1;error=PRO_TK_NO_ERROR;}
        if(!error&&*highlighted){
            error=ProSelectionAlloc(&path,&item,&clash_highlights[i]);
            if(!error)error=ProSelectionHighlight(clash_highlights[i],PRO_COLOR_HIGHLITE);
        }
    }
    if(error){(void)ProSelbufferClear();clash_clear_highlights();}
    else{clash_side=side;clash_selected_row=row;}
    return error;
}
static int __cdecl clash_sort_pairs(const void *a,const void *b){
    const AxInterferencePair *left=a,*right=b;
    const double lv=isfinite(left->volume)?left->volume:-1;
    const double rv=isfinite(right->volume)?right->volume:-1;
    if(lv!=rv)return lv>rv?-1:1;
    if(left->index[0]!=right->index[0])return left->index[0]-right->index[0];
    return left->index[1]-right->index[1];
}

static void clash_execute(int action){
    ProError error=PRO_TK_NO_ERROR;
    const wchar_t *message=L"";
    int window=-1;
    ProMdl model=NULL;
    ProMdlType type;
    const int assembly=!ProWindowCurrentGet(&window)&&!ProWindowMdlGet(window,&model)&&
        model&&!ProMdlTypeGet(model,&type)&&type==PRO_MDL_ASSEMBLY;
    const int bound=assembly&&model==clash_result.root&&window==clash_window;
    const int row=clash_row;
    InterlockedExchange(&clash_pending,2);
    if(action==CLASH_SCAN){
        if(clash_has_isolation)error=clash_restore();
        if(!error){
            clash_clear_highlights();
            clash_clear_red();
            ax_interference_result_free(&clash_result);
            clash_window=-1;
            clash_side=0;
            if(!assembly)message=L"请先在 Creo 激活要检查的装配，再点击「重新分析」。";
            else{
                ProPath directory;
                error=ProDirectoryCurrentGet(directory);
                if(!error)error=qa_mm_per_unit(model,&clash_mm);
                if(!error)error=ax_interference_compute_mode(model,directory,&clash_result,0,&clash_red_count);
                if(clash_red_count)clash_red_window=window;
                if(!error)error=ax_interference_capture(&clash_result);
                if(error){clash_clear_red();ax_interference_result_free(&clash_result);}
                else{
                    if(clash_result.count>1)qsort(clash_result.pairs,clash_result.count,sizeof(*clash_result.pairs),clash_sort_pairs);
                    clash_window=window;
                    if(clash_result.count==1&&!clash_draw_verified_box(0,window)){
                        clash_red_window=window;clash_red_count=1;
                    }
                    message=!clash_result.count?L"未发现精确实体干涉。":clash_red_count==clash_result.count?L"红色区域为实体重叠。":L"部分干涉区域未能在 Creo 中标红。";
                }
            }
        }
    }else if(action>=CLASH_BACK||action==CLASH_RESTORE||action==CLASH_CLEAR){
        error=clash_restore();
        if(!error){clash_clear_highlights();clash_clear_red();}
        if(!error&&action==CLASH_CLEAR){
            clash_clear_highlights();
            error=ProSelbufferClear();
            if(error==PRO_TK_CANT_ACCESS||error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
            clash_side=0;
        }
        if(!error)message=action==CLASH_CLEAR?L"已清除选择并恢复显示。":L"已恢复原显示。";
    }else if(!bound){
        error=PRO_TK_BAD_CONTEXT;
        message=L"活动装配已切换，请点击「重新分析」。";
    }else if(row<0||row>=clash_result.count){
        error=PRO_TK_E_NOT_FOUND;
        message=L"请先选择一条干涉记录。";
    }else{
        ProPath directory;
        error=clash_restore();
        if(!error)error=ProDirectoryCurrentGet(directory);
        if(!error)error=ax_interference_validate(&clash_result,directory);
        if(!error)clash_clear_red();
        if(!error&&action==CLASH_ISOLATE)error=clash_isolate(model,row);
        int highlighted=0;
        if(!error&&action!=CLASH_ISOLATE)error=clash_select(row,action==CLASH_PAIR?0:action==CLASH_A?1:2,&highlighted);
        if(!error){
            const ProError red=clash_draw_pair(row,window);
            message=red?L"已定位实例，Creo 未能显示该干涉体积。":L"已定位实例，红色区域为实体重叠。";
        }
    }
    if(InterlockedExchange(&clash_cleanup_requested,0)){
        (void)clash_restore();
        clash_clear_highlights();
        clash_clear_red();
    }
    wchar_t status[180];
    if(error&&!message[0]){
        swprintf_s(status,_countof(status),L"操作未完成（Creo %d）。请确认当前装配处于主表示，必要时重新分析。",error);
        message=status;
    }
    if(clash_panel)SendMessageW(clash_panel,CLASH_UPDATE,action>=CLASH_BACK&&!error?action:action==CLASH_SCAN?CLASH_SCAN:0,(LPARAM)message);
    InterlockedExchange(&clash_pending,0);
}

static void clash_queue(int action){
    if(!native_backend_window){SetWindowTextW(clash_status,L"Creo 命令通道未连接。");return;}
    if(native_busy){SetWindowTextW(clash_status,L"请等当前任务完成。");return;}
    if(InterlockedCompareExchange(&clash_pending,1,0))return;
    if(action!=CLASH_SCAN&&action<CLASH_BACK){
        int selected=ListView_GetNextItem(clash_rows,-1,LVNI_SELECTED);
        LVITEMW item={0};
        item.mask=LVIF_PARAM;item.iItem=selected;
        clash_row=selected>=0&&SendMessageW(clash_rows,LVM_GETITEMW,0,(LPARAM)&item)?(int)item.lParam:-1;
    }
    clash_queued_at=GetTickCount64();
    SetTimer(clash_panel,1,350,NULL);
    SetWindowTextW(clash_status,L"正在等待 Creo…");
    if(!PostMessageW(native_backend_window,PACKAGING_REQUEST,action,0)){
        InterlockedExchange(&clash_pending,0);
        SetWindowTextW(clash_status,L"无法向 Creo 提交操作。");
    }
}

static void clash_cell(HWND list,int row,int column,const wchar_t *text){
    LVITEMW item={0};item.iSubItem=column;item.pszText=(wchar_t *)text;
    SendMessageW(list,LVM_SETITEMTEXTW,row,(LPARAM)&item);
}

static int clash_list_add(HWND list,int row,const wchar_t *text,int key){
    LVITEMW item={0};
    item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=row;item.pszText=(wchar_t *)text;item.lParam=key;
    return (int)SendMessageW(list,LVM_INSERTITEMW,0,(LPARAM)&item);
}

static void clash_fill_rows(void){
    ListView_DeleteAllItems(clash_rows);
    int visible=0,unknown=0;
    for(int i=0;i<clash_result.count;i++){
        const AxInterferencePair *pair=&clash_result.pairs[i];
        if(!clash_groups[clash_group_for[pair->index[0]]].checked||!clash_groups[clash_group_for[pair->index[1]]].checked)continue;
        wchar_t number[32],volume[64];
        swprintf_s(number,_countof(number),L"%d",i+1);
        int row=clash_list_add(clash_rows,visible++,number,i);
        for(int side=0;side<2;side++){
            const AxOccurrence *part=&clash_result.scan->occurrences[pair->index[side]];
            wchar_t path[AX_PATH_DEPTH*12+4]=L"";
            ax_path_label(part->ids,part->depth,path,_countof(path));
            clash_cell(clash_rows,row,1+side*2,part->name);
            clash_cell(clash_rows,row,2+side*2,path[0]?path+3:path);
        }
        if(isfinite(pair->volume))swprintf_s(volume,_countof(volume),L"%.3f",pair->volume*clash_mm*clash_mm*clash_mm);
        else {wcscpy_s(volume,_countof(volume),L"未核实");unknown++;}
        clash_cell(clash_rows,row,5,volume);
    }
    if(visible)ListView_SetItemState(clash_rows,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    wchar_t heading[256];
    swprintf_s(heading,_countof(heading),L"干涉分析结果   %ls   显示 %d / %d 对%ls",clash_result.name,visible,clash_result.count,
               unknown?L"（含未核实体积）":L"");
    SetWindowTextW(clash_title,heading);
}

static int clash_contains(const wchar_t *name,const wchar_t *filter){
    size_t n=wcslen(filter);
    for(;*name;name++)if(!_wcsnicmp(name,filter,n))return 1;
    return !n;
}

static void clash_fill_parts(void){
    wchar_t text[128],number[24];
    GetWindowTextW(clash_search,text,_countof(text));
    clash_updating=1;
    ListView_DeleteAllItems(clash_parts);
    for(int i=0;i<clash_group_count;i++){
        if(!clash_contains(clash_groups[i].name,text))continue;
        int row=clash_list_add(clash_parts,ListView_GetItemCount(clash_parts),clash_groups[i].name,i);
        swprintf_s(number,_countof(number),L"%d",clash_groups[i].count);
        clash_cell(clash_parts,row,1,number);
        ListView_SetCheckState(clash_parts,row,clash_groups[i].checked);
    }
    clash_updating=0;
}

static int __cdecl clash_sort_parts(const void *a,const void *b){
    const ClashPart *left=a,*right=b;
    return left->count!=right->count?right->count-left->count:_wcsicmp(left->name,right->name);
}

static void clash_rebuild(void){
    clash_group_count=0;
    for(int i=0;i<clash_result.count;i++){
        const AxInterferencePair *pair=&clash_result.pairs[i];
        int first=-1;
        for(int side=0;side<2;side++){
            const wchar_t *name=clash_result.scan->occurrences[pair->index[side]].name;
            int group=0;
            while(group<clash_group_count&&_wcsicmp(clash_groups[group].name,name))group++;
            if(group==clash_group_count)clash_groups[clash_group_count++]=(ClashPart){name,0,1};
            if(group!=first)clash_groups[group].count++;
            first=group;
        }
    }
    qsort(clash_groups,clash_group_count,sizeof(*clash_groups),clash_sort_parts);
    for(int i=0;i<clash_result.count;i++)for(int side=0;side<2;side++){
        int index=clash_result.pairs[i].index[side];
        for(int group=0;group<clash_group_count;group++)
            if(!_wcsicmp(clash_result.scan->occurrences[index].name,clash_groups[group].name)){
                clash_group_for[index]=group;break;
            }
    }
    clash_fill_parts();
    clash_fill_rows();
}

static HWND clash_control(HWND parent,const wchar_t *kind,const wchar_t *text,DWORD style,int id){
    HWND child=CreateWindowW(kind,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,
        (HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(child,WM_SETFONT,(WPARAM)native_font,TRUE);
    return child;
}

static void clash_columns(HWND table,const wchar_t *const *labels,int count){
    for(int i=0;i<count;i++){
        LVCOLUMNW col={0};
        col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(wchar_t *)labels[i];col.cx=90;
        SendMessageW(table,LVM_INSERTCOLUMNW,i,(LPARAM)&col);
    }
}

static void clash_layout(int width,int height){
    const int left=max(166,width/4),right=left+22,table=max(100,width-right-10);
    const int footer=width<840?69:35, bottom=height-footer-34;
    MoveWindow(clash_title,12,13,max(80,width-223),24,TRUE);
    MoveWindow(clash_buttons[0],width-211,8,101,29,TRUE);
    MoveWindow(clash_buttons[1],width-104,8,94,29,TRUE);
    MoveWindow(clash_filter,12,51,left,20,TRUE);
    MoveWindow(clash_search,12,74,left,25,TRUE);
    MoveWindow(clash_parts,12,104,left,max(50,bottom-104),TRUE);
    MoveWindow(clash_rows,right,51,table,max(50,bottom-51),TRUE);
    ListView_SetColumnWidth(clash_parts,0,left-74);
    ListView_SetColumnWidth(clash_parts,1,68);
    int file=max(124,(table-340)/2);
    const int widths[]={49,file,89,file,89,104};
    for(int i=0;i<_countof(widths);i++)ListView_SetColumnWidth(clash_rows,i,widths[i]);
    int half=(left-5)/2;
    MoveWindow(clash_all,12,bottom+6,half,28,TRUE);
    MoveWindow(clash_none,17+half,bottom+6,half,28,TRUE);
    int columns=width<840?3:6,button=max(65,(table-5*(columns-1))/columns);
    for(int i=0;i<6;i++)MoveWindow(clash_buttons[i+2],right+(i%columns)*(button+5),
        bottom+6+(i/columns)*32,button,28,TRUE);
    MoveWindow(clash_status,12,height-24,width-24,20,TRUE);
}

static LRESULT CALLBACK clash_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&common);
        clash_brush=CreateSolidBrush(CHAT_CANVAS_COLOR);
        clash_title=clash_control(window,L"STATIC",L"干涉分析结果",SS_ENDELLIPSIS,0);
        clash_filter=clash_control(window,L"STATIC",L"零件筛选",0,0);
        clash_search=clash_control(window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,CLASH_SEARCH);
        SendMessageW(clash_search,EM_SETCUEBANNER,FALSE,(LPARAM)L"搜索零件名称");
        clash_parts=clash_control(window,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,CLASH_PARTS);
        clash_rows=clash_control(window,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,CLASH_ROWS);
        ListView_SetExtendedListViewStyle(clash_parts,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        ListView_SetExtendedListViewStyle(clash_rows,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        const wchar_t *part_labels[]={L"零件名称",L"涉及对数"};
        const wchar_t *row_labels[]={L"序号",L"零件 A",L"实例路径 A",L"零件 B",L"实例路径 B",L"重叠体积 mm³"};
        clash_columns(clash_parts,part_labels,_countof(part_labels));
        clash_columns(clash_rows,row_labels,_countof(row_labels));
        clash_status=clash_control(window,L"STATIC",L"",SS_ENDELLIPSIS,0);
        clash_all=clash_control(window,L"BUTTON",L"全部勾选",BS_OWNERDRAW|WS_TABSTOP,CLASH_ALL);
        clash_none=clash_control(window,L"BUTTON",L"全部取消",BS_OWNERDRAW|WS_TABSTOP,CLASH_NONE);
        const wchar_t *labels[]={L"重新分析",L"返回聊天",L"选中这一对",L"选中零件 A",L"选中零件 B",L"清除选择",L"仅显示选中零件",L"恢复显示"};
        for(int i=0;i<_countof(labels);i++)
            clash_buttons[i]=clash_control(window,L"BUTTON",labels[i],BS_OWNERDRAW|WS_TABSTOP,CLASH_BUTTON+i);
        return 0;
    }
    case WM_SIZE:clash_layout(LOWORD(l),HIWORD(l));return 0;
    case WM_ERASEBKGND:{ RECT r;GetClientRect(window,&r);FillRect((HDC)w,&r,clash_brush);return 1; }
    case WM_CTLCOLORSTATIC:SetBkColor((HDC)w,CHAT_CANVAS_COLOR);return (LRESULT)clash_brush;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT button=*(DRAWITEMSTRUCT *)l;
        if(button.CtlID==CLASH_BUTTON)button.CtlID=CHAT_SEND;
        native_theme_button(&button);return TRUE;
    }
    case WM_TIMER:
        if(w==1){
            LONG pending=InterlockedCompareExchange(&clash_pending,0,0);
            if(!pending)KillTimer(window,1);
            else if(pending==1&&GetTickCount64()-clash_queued_at>6000&&
                    InterlockedCompareExchange(&clash_pending,0,1)==1){
                KillTimer(window,1);
                SetWindowTextW(clash_status,L"Creo 尚未接受命令。请退出当前 Creo 命令后重试。");
            }
        }
        return 0;
    case CLASH_UPDATE:
        if(w==CLASH_SCAN)clash_rebuild();
        SetWindowTextW(clash_status,(const wchar_t *)l);
        if(w==CLASH_BACK)packaging_switch(0);
        if(w==CLASH_TO_PACKAGING)packaging_open();
        if(w==CLASH_TO_DFM)pk_request_dfm();
        if(w==CLASH_TO_PRICE)pk_request_price();

        return 0;
    case WM_COMMAND:{
        int id=LOWORD(w);
        if(InterlockedCompareExchange(&clash_pending,0,0))return 0;
        if(id==CLASH_SEARCH&&HIWORD(w)==EN_CHANGE)clash_fill_parts();
        if(id==CLASH_ALL||id==CLASH_NONE){
            for(int i=0;i<clash_group_count;i++)clash_groups[i].checked=id==CLASH_ALL;
            clash_fill_parts();clash_fill_rows();
        }
        if(id>=CLASH_BUTTON&&id<CLASH_BUTTON+8){
            const int actions[]={CLASH_SCAN,CLASH_BACK,CLASH_PAIR,CLASH_A,CLASH_B,CLASH_CLEAR,CLASH_ISOLATE,CLASH_RESTORE};
            clash_queue(actions[id-CLASH_BUTTON]);
        }
        return 0;
    }
    case WM_NOTIFY:{
        const NMHDR *hdr=(const NMHDR *)l;
        if(InterlockedCompareExchange(&clash_pending,0,0))return 0;
        if(hdr->idFrom==CLASH_PARTS&&hdr->code==LVN_ITEMCHANGED&&!clash_updating){
            const NMLISTVIEW *change=(const NMLISTVIEW *)l;
            if(change->iItem>=0&&((change->uNewState^change->uOldState)&LVIS_STATEIMAGEMASK)){
                int group=(int)change->lParam;
                if(group>=0&&group<clash_group_count){
                    clash_groups[group].checked=ListView_GetCheckState(clash_parts,change->iItem)!=0;
                    clash_fill_rows();
                }
            }
        }
        if(hdr->idFrom==CLASH_ROWS&&hdr->code==NM_DBLCLK){
            const NMITEMACTIVATE *hit=(const NMITEMACTIVATE *)l;
            if(hit->iItem>=0)clash_queue(hit->iSubItem==1?CLASH_A:hit->iSubItem==3?CLASH_B:CLASH_PAIR);
        }
        return 0;
    }
    case WM_DESTROY:
        clash_panel=NULL;
        DeleteObject(clash_brush);clash_brush=NULL;
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}

static void clash_open(HWND owner){
    if(clash_panel)return;
    const wchar_t *name=L"MweaiInterferencePanel";
    WNDCLASSW cls={0};
    cls.lpfnWndProc=clash_proc;cls.hInstance=GetModuleHandleW(NULL);cls.lpszClassName=name;
    cls.hCursor=LoadCursorW(NULL,IDC_ARROW);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    clash_panel=CreateWindowW(name,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        0,0,0,0,owner,NULL,cls.hInstance,NULL);
}
