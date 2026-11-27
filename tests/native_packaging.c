#define user_initialize packaging_production_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>
static FILE *report;
static void replay_reference_foam(void){
    ProMdl reference=NULL;
    AxScan *scan=NULL;
    ProError status=ProMdlnameRetrieve(L"BC00000V00",PRO_MDLFILE_ASSEMBLY,&reference);
    if(!status)status=ax_scan(reference,0,&scan);
    fwprintf(report,L"foam_reference=%d\n",status);
    if(!status)for(int j=0;j<scan->count;j++){
        AxOccurrence *item=&scan->occurrences[j];
        if(!item->model||_wcsnicmp(item->name,L"BCPAOMIAN",9))continue;
        ProIdTable ids={0};
        memcpy(ids,item->ids,item->depth*sizeof(ids[0]));
        ProAsmcomppath path;
        ProMatrix pose;
        ProOutline limits;
        ProError error=ProAsmcomppathInit((ProSolid)reference,ids,item->depth,&path);
        if(!error)error=markup_path_pose(&path,PRO_B_TRUE,pose);
        if(!error)error=or_outline(item->model,limits);
        if(error){fwprintf(report,L"foam_pose=%ls status=%d\n",item->name,error);continue;}
        fwprintf(report,L"foam_pose=%ls origin=(%.6f, %.6f, %.6f) model_size=(%.6f, %.6f, %.6f)\n",item->name,
            pose[3][0],pose[3][1],pose[3][2],limits[1][0]-limits[0][0],
            limits[1][1]-limits[0][1],limits[1][2]-limits[0][2]);
    }
    free(scan);
    const wchar_t *names[]={L"BCPAOMIAN1",L"BCPAOMIAN2"};
    for(int i=0;i<_countof(names);i++){
        ProMdl source=NULL,recreated=NULL;
        int steps=0;
        wchar_t detail[4096]=L"";
        PartText out={detail,_countof(detail),0};
        ProError error=ProMdlnameRetrieve((wchar_t *)names[i],PRO_MDLFILE_PART,&source);
        if(!error){
            ProExtRefInfo *groups=NULL;
            int count=0;
            ProError refs=ProSolidExternParentsGet((ProSolid)source,PRO_ALL_EXT_REF_TYPES,&groups,&count);
            fwprintf(report,L"foam_external=%ls query=%d groups=%d\n",names[i],refs,count);
            if(!refs)for(int j=0;j<count;j++)for(int k=0;k<groups[j].n_refs;k++){
                ProExtFeatRef ref=groups[j].ext_refs[k];
                ProFeature feature={0};
                ProModelitem item={0};
                ProRefState state=0;
                ProAsmcomppath from={0},to={0};
                ProExtRefOwnFeatGet(ref,&feature);
                ProExtRefStateGet(ref,&state);
                ProError item_result=ProExtRefModelitemGet(ref,&item);
                ProError path_result=ProExtRefAsmcompsGet(ref,&from,&to);
                ProMdlName owner=L"";
                if(!item_result&&item.owner)ProMdlMdlnameGet(item.owner,owner);
                fwprintf(report,L"feature=%d state=%d item=%d/%d owner=%ls item_status=%d path=%d/%d/%d\n",
                    feature.id,state,item.type,item.id,owner,item_result,path_result,from.table_num,to.table_num);
            }
            if(groups)ProExtRefInfoFree(&groups);
            fflush(report);
        }
        int old_mode=model_background_mode;
        model_background_mode=1;
        if(!error)error=imitation_replay_new(source,PROMDLSTYPE_PART_SOLID,&recreated,&steps,&out);
        ImitationShapeMetrics metrics;
        if(!error)error=imitation_shape_read(source,&metrics);
        if(!error)error=imitation_shape_verify_mode(source,recreated,&metrics,0,&out);
        model_background_mode=old_mode;
        fwprintf(report,L"foam_replay=%ls status=%d steps=%d\n%ls\n",names[i],error,steps,detail);
        fflush(report);
    }
}
static void measure_foam_faces(ProMdl model){
    ProOutline bounds;
    double units;
    if(or_outline(model,bounds)||qa_mm_per_unit(model,&units))return;
    const int normal=packaging_axes[2],x=packaging_axes[0],y=packaging_axes[1];
    const double standoff=10/units;
    for(int side=0;side<2;side++){
        int hit_count=0,missing=0,ambiguous=0,failed=0;
        double low=HUGE_VAL,high=0;
        for(int row=0;row<15;row++)for(int col=0;col<23;col++){
            ProRay ray={0};
            ray.start_point[x]=bounds[0][x]+(col+.5)/23*(bounds[1][x]-bounds[0][x]);
            ray.start_point[y]=bounds[0][y]+(row+.5)/15*(bounds[1][y]-bounds[0][y]);
            ray.start_point[normal]=bounds[side][normal]+(side?standoff:-standoff);
            ray.dir_vector[normal]=side?-1:1;
            MarkupAssemblyHit hit={0};
            ProError error=markup_assembly_hit(model,&ray,&hit);
            if(error==PRO_TK_E_NOT_FOUND){missing++;continue;}
            if(error==PRO_TK_E_FOUND){ambiguous++;continue;}
            if(error){failed++;continue;}
            double offset=(hit.depth-standoff)*units;
            low=fmin(low,offset);high=fmax(high,offset);hit_count++;
        }
        fwprintf(report,L"foam_side=%d axis=%d hits=%d misses=%d ambiguous=%d failed=%d depth_mm=%.6f..%.6f\n",
            side,normal,hit_count,missing,ambiguous,failed,hit_count?low:0,high);
    }
}
static int clash_test_pixels(int window,const wchar_t *name){
    ProPath file;
    double extent[2][3];
    if(!GetFullPathNameW(name,_countof(file),file,NULL)||ProWindowPixelOutlineGet(window,extent))return -1;
    double width=extent[1][0]-extent[0][0],height=extent[1][1]-extent[0][1];
    return ProRasterFileWrite(window,PRORASTERDEPTH_24,width/100,height/100,
        PRORASTERDPI_100,PRORASTERTYPE_TIFF,file);
}
static int dfm_test_screen(void){
    RECT r;
    if(!GetWindowRect(native_window,&r))return 0;
    int width=r.right-r.left,height=r.bottom-r.top;
    BITMAPINFO info={0};
    info.bmiHeader=(BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER),width,-height,1,32};
    HWND handle=native_window;
    HDC source=GetDC(handle),target=CreateCompatibleDC(source);
    void *bits=NULL;
    HBITMAP frame=CreateDIBSection(source,&info,DIB_RGB_COLORS,&bits,NULL,0);
    int ok=0;
    if(frame){
        HGDIOBJ old=SelectObject(target,frame);
        if(PrintWindow(handle,target,2)){
            GdiFlush();
            FILE *out=_wfopen(L"dfm-preview.bmp",L"wb");
            DWORD bytes=width*height*4;
            BITMAPFILEHEADER file={0x4d42,sizeof(file)+sizeof(info.bmiHeader)+bytes,0,0,sizeof(file)+sizeof(info.bmiHeader)};
            if(out){fwrite(&file,sizeof(file),1,out);fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,out);ok=fwrite(bits,bytes,1,out)==1;fclose(out);}
        }
        SelectObject(target,old);DeleteObject(frame);
    }
    DeleteDC(target);ReleaseDC(handle,source);
    return ok;
}
static int run_fixture(uiCmdCmdId command,uiCmdValue *value,void *data){
    const char *stage="source";
    GetCurrentDirectoryW(_countof(runtime_data),runtime_data);
    GetFullPathNameW(L"..\\..",_countof(runtime_root),runtime_root,NULL);
    ProMdlName name;
    ModelPlan plan={0};
    const int real_product=GetEnvironmentVariableW(L"MWEAI_PACKAGING_REAL_PRODUCT",name,_countof(name))!=0;
    plan.a=120;plan.b=70;plan.height=230;plan.ready=1;
    model_background_mode=1;
    ProError error=real_product?PRO_TK_NO_ERROR:model_build_internal(&plan,name,&stage,NULL,NULL,NULL);
    model_background_mode=0;
    ProMdl source=NULL;
    ProPath real_directory=L"";
    if(real_product&&GetEnvironmentVariableW(L"MWEAI_CLASH_FIXTURE_DIR",real_directory,_countof(real_directory)))
        error=ProDirectoryChange(real_directory);
    if(!error)error=real_product?ProMdlnameRetrieve(name,PRO_MDLFILE_ASSEMBLY,&source):ProMdlnameInit(name,PRO_MDL_PART,&source);
    if(!error&&!real_product)error=ProMdlSave(source);
    int window;
    if(!error)error=ProObjectwindowMdlnameCreate(name,real_product?PRO_ASSEMBLY:PRO_PART,&window);
    if(!error)error=ProWindowCurrentSet(window);
    if(!error)error=ProMdlDisplay(source);
    if(!error)error=ProWindowActivate(window);
    if(!error){
        native_ui_create(NULL,1);
        if(real_product&&GetEnvironmentVariableW(L"MWEAI_PRICE_FIXTURE",NULL,0)){
            native_backend_window=CreateWindowW(L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
            SendMessageW(native_top_buttons[CHAT_TOP_COLUMNS+2],BM_CLICK,0,0);
            int opened=pk_page==5&&price_panel&&packaging_visible();
            SendMessageW(price_buttons[0],BM_CLICK,0,0);
            MSG job={0};
            if(PeekMessageW(&job,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)&&job.wParam==PRICE_READ){
                packaging_pending=(int)job.wParam;packaging_command(0,NULL,NULL);
            }
            wchar_t model_width[40];GetWindowTextW(price_fields[COST_W],model_width,_countof(model_width));
            const wchar_t *fields[]={L"55寸户外广告机",L"测试工程师",L"880",L"230",L"2200",L"131",L"",L"2"};
            for(int i=0;i<COST_FIELD_COUNT;i++)if(i!=COST_MATERIAL)SetWindowTextW(price_fields[i],fields[i]);
            SendMessageW(price_fields[COST_MATERIAL],CB_SETCURSEL,4,0);
            SendMessageW(price_buttons[1],BM_CLICK,0,0);
            wchar_t cost[6000],note[256];GetWindowTextW(price_output,cost,_countof(cost));
            GetWindowTextW(price_note,note,_countof(note));
            int totals=wcsstr(cost,L"2638.15")&&wcsstr(cost,L"5276.3");
            ProPath before_folder,after_folder;
            int fallback_ok=price_resolve_folder(before_folder);
            int export_enabled=IsWindowEnabled(price_buttons[3]);
            SendMessageW(price_buttons[3],BM_CLICK,0,0);
            int export_job=PeekMessageW(&job,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)&&job.wParam==PRICE_EXPORT;
            if(export_job){packaging_pending=(int)job.wParam;packaging_command(0,NULL,NULL);}
            SendMessageW(price_buttons[4],BM_CLICK,0,0);
            int folder_job=PeekMessageW(&job,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)&&job.wParam==PRICE_OPEN_FOLDER;
            BOOL folder_ok=price_resolve_folder(after_folder)&&!_wcsicmp(after_folder,price_last_export_directory)&&!_wcsicmp(after_folder,before_folder);
            fwprintf(report,L"price_folder_fallback=%d button=%d last_export=%d\n",fallback_ok,folder_job,folder_ok);
            wchar_t export_note[PRO_PATH_SIZE+48];
            GetWindowTextW(price_note,export_note,_countof(export_note));
            SetWindowTextW(price_fields[COST_QTY],L"3");
            int stale_disabled=!IsWindowEnabled(price_buttons[3]);
            SetWindowTextW(price_fields[COST_QTY],L"2");
            fwprintf(report,L"price_export_enabled=%d export_job=%d stale_disabled=%d %ls\n",export_enabled,export_job,stale_disabled,export_note);
            SendMessageW(price_spray,BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(price_buttons[1],BM_CLICK,0,0);
            wchar_t dry[6000];GetWindowTextW(price_output,dry,_countof(dry));
            int dry_ok=wcsstr(dry,L"本单不计喷涂费用")!=NULL;
            SendMessageW(price_buttons[2],BM_CLICK,0,0);
            fwprintf(report,L"price_open=%d width=%ls totals=%d no_spray=%d back=%d %ls\n%ls\n",
                opened,model_width,totals,dry_ok,!packaging_visible(),note,cost);
            DestroyWindow(native_window);DestroyWindow(native_backend_window);
            fclose(report);ProEngineerEnd();return 0;
        }
        if(real_product&&GetEnvironmentVariableW(L"MWEAI_DFM_FIXTURE",NULL,0)){
            native_backend_window=CreateWindowW(L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
            SendMessageW(native_top_buttons[CHAT_TOP_COLUMNS+1],BM_CLICK,0,0);
            int opened=pk_page==4&&dfm_panel&&packaging_visible();
            MSG job={0};
            SendMessageW(dfm_buttons[0],BM_CLICK,0,0);
            int scan=PeekMessageW(&job,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)&&job.wParam==DFM_SCAN;
            if(scan){packaging_pending=(int)job.wParam;packaging_command(0,NULL,NULL);}
            fwprintf(report,L"dfm_preview=%d\n",dfm_test_screen());
            int rows=ListView_GetItemCount(dfm_results),bends=0,located=0;
            for(int i=0;i<dfm_row_count;i++)if(!wcscmp(dfm_rows[i].code,L"B01")&&dfm_rows[i].feature>0){
                bends++;
                if(!located){
                    dfm_selected=i;
                    located=!dfm_locate(source,window);
                }
            }
            SendMessageW(dfm_group_buttons[2],BM_CLICK,0,0);
            int folded=ListView_GetItemCount(dfm_results);
            SendMessageW(dfm_buttons[1],BM_CLICK,0,0);
            int export_job=PeekMessageW(&job,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)&&job.wParam==DFM_EXPORT;
            if(export_job){packaging_pending=(int)job.wParam;packaging_command(0,NULL,NULL);}
            wchar_t note[PRO_PATH_SIZE+100];
            GetWindowTextW(dfm_status,note,_countof(note));
            SendMessageW(dfm_buttons[2],BM_CLICK,0,0);
            fwprintf(report,L"dfm_open=%d scan=%d parts=%d rows=%d bends=%d filtered=%d located=%d export=%d back=%d\n%ls\n",
                opened,scan,dfm_part_count,rows,bends,folded,located,export_job,!packaging_visible(),note);
            DestroyWindow(native_window);DestroyWindow(native_backend_window);
            fclose(report);ProEngineerEnd();return 0;
        }
        fwprintf(report,L"clash_registration=%d\n",ProCmdCmdIdFind("CodexClashAnalyze",NULL));
        const wchar_t *labels[]={L"工程图",L"干涉分析",L"钣金DFM"};
        const int slots[]={CHAT_TOP_COLUMNS-1,CHAT_TOP_COLUMNS,CHAT_TOP_COLUMNS+1};
        int named_and_disabled=1;
        for(int i=0;i<_countof(slots);i++){
            wchar_t title[32];
            HWND button=native_top_buttons[slots[i]];
            if(!GetWindowTextW(button,title,_countof(title))||wcscmp(title,labels[i])||!!IsWindowEnabled(button)!=(i!=0))
                named_and_disabled=0;
        }
        fwprintf(report,L"top_placeholders=%d\n",named_and_disabled);
        native_backend_window=CreateWindowW(L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
        int was_chat=!packaging_visible()&&!packaging_window;
        SendMessageW(native_top_buttons[CHAT_TOP_COLUMNS],BM_CLICK,0,0);
        MSG queued={0};
        int dispatched=native_backend_window&&PeekMessageW(&queued,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)
            &&queued.wParam==CLASH_SCAN;
        fwprintf(report,L"clash_from_chat=%d\n",named_and_disabled&&was_chat&&dispatched&&packaging_visible()&&pk_page==3);
        if(dispatched){packaging_pending=CLASH_SCAN;packaging_command(0,NULL,NULL);}
        if(real_product&&real_directory[0]){
            ProMdl cur=NULL,win_model=NULL;
            int cur_win=-1;
            int w_status=ProWindowCurrentGet(&cur_win);
            int m_status=ProMdlCurrentGet(&cur);
            int wm_status=ProWindowMdlGet(cur_win,&win_model);
            fwprintf(report,L"clash_fixture_context=%d %d %d expected=%d active=%d same=%d/%d\n",w_status,m_status,wm_status,window,cur_win,source==cur,source==win_model);
            clash_execute(CLASH_SCAN);
            if(!_wcsicmp(name,L"asm0001")&&clash_result.count){
                ShowWindow(native_window,SW_HIDE);
                ProWindowActivate(window);
                clash_test_pixels(window,L"clash-scan.tif");
                clash_clear_red();
                Sleep(130);
                int before=clash_test_pixels(window,L"clash-before.tif");
                clash_row=0;
                clash_execute(CLASH_PAIR);
                Sleep(130);
                AxInterferenceView view={.window=window};
                for(int axis=0;axis<4;axis++)view.from_root[axis][axis]=1;
                ProPoint3d corners[8];
                ProError box=ax_interference_box(&clash_result,&view,0,corners);
                double box_volume=0;
                if(!box)box_volume=(corners[7][0]-corners[0][0])*(corners[7][1]-corners[0][1])*(corners[7][2]-corners[0][2]);
                fwprintf(report,L"clash_box=%d %.12f exact=%.12f overlay=%d\n",box,box_volume,clash_result.pairs[0].volume,clash_box_visible);
                int after=clash_test_pixels(window,L"clash-after.tif");
                fwprintf(report,L"clash_raster_before=%d after=%d count=%d\n",before,after,clash_red_count);
                DestroyWindow(native_window);
                DestroyWindow(native_backend_window);
                fclose(report);
                ProEngineerEnd();
                return 0;
            }
            wchar_t note[240];
            GetWindowTextW(clash_status,note,_countof(note));
            fwprintf(report,L"clash_real=%d red=%d %ls\n",ListView_GetItemCount(clash_rows),clash_red_count,note);
            if(clash_result.count){
                if(!_wcsicmp(name,L"m26x1100000v00")){
                    int parents[AX_MAX_OCCURRENCES],count61=0,internal=0,crossing=0,outside=0;
                    for(int i=0;i<clash_result.scan->count;i++){
                        const AxOccurrence *item=&clash_result.scan->occurrences[i];
                        if(item->depth==1&&!_wcsicmp(item->name,L"M26X1161001V00"))parents[count61++]=item->ids[0];
                    }
                    for(int i=0;i<clash_result.count;i++){
                        int endpoints=0;
                        for(int side=0;side<2;side++){
                            const AxOccurrence *item=&clash_result.scan->occurrences[clash_result.pairs[i].index[side]];
                            for(int j=0;j<count61;j++)if(item->ids[0]==parents[j]){endpoints++;break;}
                        }
                        if(endpoints==2)internal++;
                        else if(endpoints==1)crossing++;
                        else outside++;
                    }
                    fwprintf(report,L"clash_61_scope occurrences=%d internal=%d crossing=%d outside=%d\n",count61,internal,crossing,outside);
                }
                const AxInterferencePair *pair=&clash_result.pairs[0];
                fwprintf(report,L"clash_real_volume=%.3f\n",pair->volume*clash_mm*clash_mm*clash_mm);
                SendMessageW(clash_panel,WM_COMMAND,CLASH_NONE,0);
                fwprintf(report,L"clash_real_filtered=%d\n",ListView_GetItemCount(clash_rows));
                SendMessageW(clash_panel,WM_COMMAND,CLASH_ALL,0);
                clash_row=0;
                clash_execute(CLASH_PAIR);
                ProSelection *selected=NULL;int count=0;
                if(!ProSelbufferSelectionsGet(&selected)&&selected){
                    ProArraySizeGet((ProArray)selected,&count);
                    ProSelectionarrayFree(selected);
                }
                GetWindowTextW(clash_status,note,_countof(note));
                fwprintf(report,L"clash_real_selected=%d highlighted=%d %ls\n",count,(clash_highlights[0]!=NULL)+(clash_highlights[1]!=NULL),note);
                clash_execute(CLASH_ISOLATE);
                GetWindowTextW(clash_status,note,_countof(note));
                fwprintf(report,L"clash_real_isolated=%d %ls\n",clash_has_isolation,note);
                ProMdl other=NULL,active_model=NULL;
                int other_window=-1,current_window=-1;
                ProError changed=ProMdlnameRetrieve(L"m26x1161001v00",PRO_MDLFILE_ASSEMBLY,&other);
                if(!changed)changed=ProObjectwindowMdlnameCreate(L"m26x1161001v00",PRO_ASSEMBLY,&other_window);
                if(!changed)changed=ProWindowCurrentSet(other_window);
                if(!changed)changed=ProMdlDisplay(other);
                if(!changed)changed=ProWindowActivate(other_window);
                if(!changed){
                    SendMessageW(clash_buttons[0],BM_CLICK,0,0);
                    if(PeekMessageW(&queued,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)){
                        packaging_pending=(int)queued.wParam;packaging_command(0,NULL,NULL);
                    }
                    ProWindowCurrentGet(&current_window);
                    ProWindowMdlGet(current_window,&active_model);
                }
                GetWindowTextW(clash_status,note,_countof(note));
                fwprintf(report,L"clash_rescan_switched=%d old_isolation_restored=%d error=%d %ls\n",
                    !changed&&active_model==other&&other==clash_result.root,!clash_has_isolation,changed,note);
                if(!changed)changed=ProWindowCurrentSet(window);
                if(!changed)changed=ProWindowActivate(window);
                if(!changed)clash_execute(CLASH_SCAN);
                clash_row=0;
                clash_execute(CLASH_ISOLATE);
                int was_isolated=clash_has_isolation;
                SendMessageW(native_window,WM_CLOSE,0,0);
                int closed=!IsWindowVisible(native_window);
                if(PeekMessageW(&queued,native_backend_window,PACKAGING_REQUEST,PACKAGING_REQUEST,PM_REMOVE)){
                    packaging_pending=(int)queued.wParam;packaging_command(0,NULL,NULL);
                }
                fwprintf(report,L"clash_close=%d previously_isolated=%d restored=%d\n",closed,was_isolated,!clash_has_isolation);
                clash_execute(CLASH_BACK);
                fwprintf(report,L"clash_real_back=%d\n",!packaging_visible());
            }
            DestroyWindow(native_window);
            DestroyWindow(native_backend_window);
            fclose(report);
            ProEngineerEnd();
            return 0;
        }
        DestroyWindow(native_backend_window);
        native_backend_window=NULL;
        packaging_open();
        SendMessageW(packaging_window,WM_COMMAND,PK_LIBRARY,0);
        RECT client={0,0,900,680};
        HDC layout=CreateCompatibleDC(NULL);
        pk_config_paint(layout,client);
        const int single=pk_config_count;
        RECT combo=pk_config_cards[4].hit;
        SendMessageW(packaging_window,WM_LBUTTONUP,0,MAKELPARAM((combo.left+combo.right)/2,(combo.top+combo.bottom)/2));
        pk_config_paint(layout,client);
        DeleteDC(layout);
        if(!pk_config_atlas||pk_config_choice[0]!=4||pk_config_count!=single+7||pk_config_opening_rows()!=2)return 1;
        fwprintf(report,L"configuration_cards=pass\n");fflush(report);
        if(pk_page!=0||(GetWindowLongPtrW(pk_visual[3],GWL_STYLE)&WS_VISIBLE))return 1;
        SendMessageW(packaging_window,WM_COMMAND,PK_PREVIEW,0);
        if(pk_page!=1||!(GetWindowLongPtrW(pk_visual[3],GWL_STYLE)&WS_VISIBLE))return 1;
        SendMessageW(packaging_window,WM_COMMAND,MAKEWPARAM(PK_AXIS,CBN_SELCHANGE),0);
        if(packaging_pending||packaging_ready)return 1;
        fwprintf(report,L"config_tabs=pass\n");fflush(report);
        if(real_product){
            SendMessageW(pk_checks[PK_MAILER],BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(pk_checks[PK_OUTER],BM_SETCHECK,BST_CHECKED,0);
        }
        SendMessageW(packaging_axis,CB_SETCURSEL,1,0);
        if(!packaging_window||!packaging_preview(source)||(!real_product&&fabs(packaging_size[2]-70)>0.01)){
            wchar_t status[512]=L"";
            GetWindowTextW(packaging_result,status,_countof(status));
            fwprintf(report,L"preview=%ls\n",status);error=PRO_TK_GENERAL_ERROR;
        }
    }
    if(!error&&real_product){
        measure_foam_faces(source);
        replay_reference_foam();
    }
    wchar_t reply[640]=L"";
    if(!error)error=packaging_build(source,reply,_countof(reply));
    fwprintf(report,L"result=%d source=%hs\n%ls\n",error,stage,reply);
    if(!error){
        RECT frame;GetClientRect(packaging_window,&frame);
        HDC buffer=CreateCompatibleDC(NULL);
        packaging_paint(buffer,frame);
        DeleteDC(buffer);
        POINT point={(pk_view_area.left+pk_view_area.right)/2,(pk_view_area.top+pk_view_area.bottom)/2};
        POINT screen=point;ClientToScreen(packaging_window,&screen);
        SendMessageW(packaging_window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(screen.x,screen.y));
        const int zoomed=pk_zoom>1;
        SendMessageW(packaging_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        SendMessageW(packaging_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x+35,point.y+25));
        SendMessageW(packaging_window,WM_LBUTTONUP,0,MAKELPARAM(point.x+35,point.y+25));
        const LONG mx=InterlockedExchange(&pk_orbit_x,0),my=InterlockedExchange(&pk_orbit_y,0);
        fwprintf(report,L"viewport_input zoom=%d drag=(%ld,%ld)\n",zoomed,mx,my);
        fwprintf(report,L"rotation=%d\n",pk_rotate_view(packaging_last,mx*0.008,my*0.008));
        fwprintf(report,L"exploded=%d\n",pk_assembly_view(packaging_last,5));
        fwprintf(report,L"section=%d\n",pk_assembly_view(packaging_last,6));
        fwprintf(report,L"assembled=%d\n",pk_assembly_view(packaging_last,7));
        fwprintf(report,L"preview_bitmap=%d x %d\n",pk_cad_w,pk_cad_h);
        int id=0;
        AcquireSRWLockShared(&pk_cad_lock);
        if(pk_view_count)id=pk_view_items[0].id;
        ReleaseSRWLockShared(&pk_cad_lock);
        ProError hide=id?pk_toggle_component(packaging_last,id):PRO_TK_E_NOT_FOUND;
        ProError show=hide?hide:pk_toggle_component(packaging_last,id);
        fwprintf(report,L"component_hide=%d component_show=%d\\n",hide,show);
        ProError drawing=packaging_drawing(reply,_countof(reply));
        fwprintf(report,L"drawing=%d\n%ls\n",drawing,reply);
        ProMdl active_drawing=NULL,reopened=NULL;
        double original=0,edited=0;
        ProError changed=drawing;
        if(!changed)changed=ProMdlCurrentGet(&active_drawing);
        if(!changed)changed=ProDrawingScaleGet((ProDrawing)active_drawing,(ProSolid)packaging_last,1,&original);
        if(!changed)changed=pk_drawing_read_current();
        if(!changed&&(!pk_drawing_image||pk_drawing_width<100))changed=PRO_TK_INCOMPLETE;
        if(!changed)changed=pk_drawing_edit(10,1/(original*0.8),reply,_countof(reply));
        if(!changed)changed=pk_drawing_edit(11,0,reply,_countof(reply));
        fwprintf(report,L"cad_panel=%d raster=%d x %d\n%ls\n",changed,pk_drawing_width,pk_drawing_height,reply);
        if(!changed)changed=packaging_drawing(reply,_countof(reply));
        if(!changed)changed=ProMdlCurrentGet(&reopened);
        if(!changed)changed=ProDrawingScaleGet((ProDrawing)reopened,(ProSolid)packaging_last,1,&edited);
        if(!changed&&(reopened!=active_drawing||fabs(edited-original*0.8)>1e-8))changed=PRO_TK_INCOMPLETE;
        fwprintf(report,L"cad_edit=%d original=%.5f retained=%.5f\n",changed,original,edited);
        int added_sheet=0;
        if(!changed)changed=ProDrawingSheetCopy((ProDrawing)active_drawing,1,&added_sheet);
        if(!changed)changed=pk_drawing_read_current();
        if(!changed&&(added_sheet!=2||pk_sheets!=2))changed=PRO_TK_INCOMPLETE;
        if(!changed)changed=pk_drawing_edit(13,0,reply,_countof(reply));
        if(!changed&&(pk_sheet!=2||!pk_drawing_image))changed=PRO_TK_INCOMPLETE;
        if(!changed)changed=pk_drawing_edit(12,0,reply,_countof(reply));
        if(!changed&&(pk_sheet!=1||!pk_drawing_image))changed=PRO_TK_INCOMPLETE;
        fwprintf(report,L"cad_pagination=%d sheets=%d\n",changed,pk_sheets);
        if(!changed){
            swprintf_s(pk_save_folder,_countof(pk_save_folder),L"%ls\\CAD-%lu",packaging_output,GetCurrentProcessId());
            if(!CreateDirectoryW(pk_save_folder,NULL))changed=PRO_TK_CANT_OPEN;
        }
        if(!changed)changed=pk_drawing_edit(11,0,reply,_countof(reply));
        fwprintf(report,L"cad_destination=%d\n%ls\n",changed,reply);
        pk_page=2;
        SendMessageW(packaging_window,WM_COMMAND,PK_DWG_BACK,0);
        fwprintf(report,L"cad_back=%d\n",pk_page==1);
    }
    if(!error&&!real_product){
        SendMessageW(pk_checks[PK_MAILER],BM_SETCHECK,BST_UNCHECKED,0);
        SendMessageW(pk_checks[PK_OUTER],BM_SETCHECK,BST_CHECKED,0);
        ProMdl other=NULL;
        model_background_mode=1;
        ProError second=model_build_internal(&plan,name,&stage,NULL,NULL,NULL);
        model_background_mode=0;
        if(!second)second=ProMdlnameInit(name,PRO_MDL_PART,&other);
        if(!second)second=ProMdlSave(other);
        if(!second)second=ProObjectwindowMdlnameCreate(name,real_product?PRO_ASSEMBLY:PRO_PART,&window);
        if(!second)second=ProWindowCurrentSet(window);
        if(!second)second=ProMdlDisplay(other);
        if(!second)second=packaging_preview(other)?packaging_build(other,reply,_countof(reply)):PRO_TK_GENERAL_ERROR;
        AxScan *scan=NULL;
        if(!second)second=ax_scan(packaging_last,1,&scan);
        if(!second&&(!scan||scan->count!=4||pk_last_mask!=11))second=PRO_TK_INCOMPLETE;
        free(scan);
        fwprintf(report,L"outer_only=%d\n%ls\n",second,reply);
        if(!second){
            ProMdlMdlnameGet(packaging_last,name);
            second=ProObjectwindowMdlnameCreate(name,PRO_ASSEMBLY,&window);
            if(!second)second=ProWindowCurrentSet(window);
            if(!second)second=ProMdlDisplay(packaging_last);
            ProMatrix offset;
            AxScan *source_scan=NULL;
            ProAsmcomppath source_path;
            if(!second)second=ax_scan(packaging_last,1,&source_scan);
            if(!second)second=ax_interference_occurrence_path(packaging_last,&source_scan->occurrences[0],&source_path);
            if(!second)second=ProAsmcomppathTrfGet(&source_path,PRO_B_TRUE,offset);
            free(source_scan);
            if(!second)for(int axis=0;axis<3;axis++)offset[3][axis]+=1;
            ProAsmcomp added;
            if(!second)second=ProAsmcompAssemble((ProAssembly)packaging_last,(ProSolid)source,offset,&added);
            if(!second)second=ProSolidRegenerate((ProSolid)packaging_last,PRO_REGEN_NO_FLAGS);
            if(!second)second=ProMdlSave(packaging_last);
            if(!second){
                AxScan *verify=NULL;
                ProError scan_error=ax_scan(packaging_last,1,&verify);
                for(int i=0;!scan_error&&i<verify->count;i++){
                    const AxOccurrence *item=&verify->occurrences[i];
                    ProAsmcomppath path;
                    ProMatrix pose;
                    if(!ax_interference_occurrence_path(packaging_last,item,&path)&&!ProAsmcomppathTrfGet(&path,PRO_B_TRUE,pose))
                        fwprintf(report,L"clash_occ=%ls id=%d status=%d origin=(%.3f,%.3f,%.3f)\n",item->name,item->ids[0],item->state,pose[3][0],pose[3][1],pose[3][2]);
                }
                free(verify);
                pk_page=3;packaging_layout();
                clash_execute(CLASH_SCAN);
                wchar_t clash_note[240];
                GetWindowTextW(clash_status,clash_note,_countof(clash_note));
                fwprintf(report,L"clash_scan_status=%ls cached=%d\n",clash_note,clash_result.count);
                int total=ListView_GetItemCount(clash_rows);
                SendMessageW(clash_panel,WM_COMMAND,CLASH_NONE,0);
                int filtered=ListView_GetItemCount(clash_rows);
                SendMessageW(clash_panel,WM_COMMAND,CLASH_ALL,0);
                clash_row=0;
                clash_execute(CLASH_PAIR);
                ProSelection *selected=NULL;
                int selected_count=0;
                if(!ProSelbufferSelectionsGet(&selected)&&selected){
                    ProArraySizeGet((ProArray)selected,&selected_count);
                    ProSelectionarrayFree(selected);
                }
                clash_execute(CLASH_ISOLATE);
                GetWindowTextW(clash_status,clash_note,_countof(clash_note));
                fwprintf(report,L"clash_isolate_status=%ls\n",clash_note);
                int isolated=clash_has_isolation;
                clash_execute(CLASH_RESTORE);
                fwprintf(report,L"clash_embedded points=%d filtered=%d selected=%d isolated=%d restored=%d\n",
                    total,filtered,selected_count,isolated,!clash_has_isolation);
                clash_execute(CLASH_BACK);
                fwprintf(report,L"clash_return=%d\n",!packaging_visible());
            }
            fwprintf(report,L"clash_fixture=%d\n",second);
        }
    }
    if(native_window)DestroyWindow(native_window);
    fclose(report);
    ProEngineerEnd();
    return 0;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t message[80]){
    uiCmdCmdId command;
    _wfopen_s(&report,L"packaging-result.txt",L"w, ccs=UTF-8");
    if(!report)return PRO_TK_CANT_WRITE;
    fwprintf(report,L"loaded\n");fflush(report);
    ProError error=ProCmdActionAdd("MWEAIPackagingFixture",run_fixture,uiProe2ndImmediate,access_command,PRO_B_TRUE,PRO_B_TRUE,&command);
    if(!error)error=ProMacroLoad(L"~ Command \x0060" L"MWEAIPackagingFixture\x0060;");
    return error;
}
