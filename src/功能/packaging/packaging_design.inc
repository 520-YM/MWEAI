#include <shlobj.h>

/* Prototype: rectangular shipping case and flat end pads in product XYZ. */
static HWND packaging_window, packaging_fields[3], packaging_result, packaging_axis;
static int packaging_axes[3];
static ProMdl packaging_source,packaging_last;
static ProMdlName packaging_last_name;
static ProPath packaging_output;
static ProOutline packaging_bounds;
static ProPath packaging_directory;
static double packaging_input[3], packaging_size[3], packaging_mm;
static int packaging_ready, packaging_pending;
#define PACKAGING_REQUEST (WM_APP+188)
#include "interference_panel.inc"
#include "dfm_panel.inc"
#include "price_panel.inc"


enum { PK_LOWER, PK_UPPER, PK_MAILER, PK_OUTER, PK_ITEM_COUNT };
static const wchar_t *const pk_names[]={L"下泡棉",L"上泡棉",L"飞机盒",L"重力纸箱"};
static HWND pk_checks[PK_ITEM_COUNT],pk_tree;
typedef struct {double size[3],origin[3],wall;int enabled;} PkItem;
static PkItem pk_plan[PK_ITEM_COUNT];
static unsigned pk_last_mask;
static double pk_product_origin[3],pk_external[3];

static int pk_make_plan(const double product[3],const double input[3],unsigned mask){
    const double padding=input[1],gap=input[2];
    double box[3]={product[0]+2*gap,product[1]+2*gap,
        product[2]+padding*((mask&1)!=0)+padding*((mask&2)!=0)};
    memset(pk_plan,0,sizeof(pk_plan));
    for(int i=0;i<PK_ITEM_COUNT;i++)pk_plan[i].enabled=(mask&(1u<<i))!=0;
    for(int i=0;i<2;i++){
        pk_plan[i].size[0]=box[0];pk_plan[i].size[1]=box[1];pk_plan[i].size[2]=padding;
    }
    pk_product_origin[0]=gap;pk_product_origin[1]=gap;
    pk_product_origin[2]=pk_plan[PK_LOWER].enabled?padding:0;
    pk_plan[PK_UPPER].origin[2]=pk_product_origin[2]+product[2];
    for(int i=PK_MAILER;i<=PK_OUTER;i++)if(pk_plan[i].enabled){
        const double wall=i==PK_MAILER?2.0:input[0];
        const double room=i==PK_OUTER&&pk_plan[PK_MAILER].enabled?gap:0;
        pk_plan[i].wall=wall;
        for(int axis=0;axis<3;axis++){
            double shift=wall+room;
            for(int j=0;j<i;j++)if(pk_plan[j].enabled)pk_plan[j].origin[axis]+=shift;
            pk_product_origin[axis]+=shift;
            box[axis]+=2*shift;
            pk_plan[i].size[axis]=box[axis];
        }
    }
    memcpy(pk_external,box,sizeof(box));
    return mask!=0;
}

static unsigned pk_selected(void){
    unsigned bits=0;
    for(int i=0;i<PK_ITEM_COUNT;i++)
        if(SendMessageW(pk_checks[i],BM_GETCHECK,0,0)==BST_CHECKED)bits|=1u<<i;
    return bits;
}

static void pk_show_tree(unsigned bits){
    wchar_t text[256]=L"包装子装配";
    for(int i=0;i<PK_ITEM_COUNT;i++)if(bits&(1u<<i)){
        wcscat_s(text,_countof(text),L"\r\n    ");
        wcscat_s(text,_countof(text),pk_names[i]);
    }
    SetWindowTextW(pk_tree,text);
}


static HBITMAP pk_cad_image,pk_drawing_image;
static int pk_drawing_width,pk_drawing_height;
static ProMdl pk_drawing_model,pk_drawing_assembly;
static HWND pk_drawing_scale,pk_drawing_buttons[8];
static int pk_sheet=1,pk_sheets;
static HWND pk_sheet_label,pk_save_label;
static ProPath pk_save_folder,pk_last_saved_folder;
static int pk_cad_w,pk_cad_h;
static LONG pk_orbit_x,pk_orbit_y,pk_visibility_id;
static int pk_dragging;
static POINT pk_drag_point;
static double pk_zoom=1.0;
static RECT pk_view_area;
typedef struct { int id,hidden; wchar_t name[128]; } PkViewItem;
static PkViewItem pk_view_items[64];
static int pk_view_count;
static ProMdl pk_view_model;
static SRWLOCK pk_cad_lock=SRWLOCK_INIT;
static HBITMAP pk_window_bitmap(ProMdl model,int *width,int *height){
    HBITMAP bitmap=NULL;
    int window;
    *width=0;*height=0;
    double outline[2][3];
    ProPath path;
    if(model&&storage_directory_on_drive(runtime_data,L'E')&&!ProMdlWindowGet(model,&window)&&
        !ProWindowPixelOutlineGet(window,outline)&&
        swprintf_s(path,_countof(path),L"%ls\\pack-view-%lu.tif",runtime_data,GetCurrentProcessId())>0){
        double w=outline[1][0]-outline[0][0],h=outline[1][1]-outline[0][1];
        if(w>80&&h>80){
            double factor=fmin(1,1200/fmax(w,h))/100.0;
            if(!ProRasterFileWrite(window,PRORASTERDEPTH_24,w*factor,h*factor,
                PRORASTERDPI_100,PRORASTERTYPE_TIFF,path))
                bitmap=markup_read_raster(path,width,height);
            DeleteFileW(path);
        }
    }
    return bitmap;
}

static HBITMAP pk_render_drawing(ProMdl drawing,int *width,int *height){
    static LONG sequence;
    ProPath pdf,png;
    wchar_t system[MAX_PATH],shell[MAX_PATH],command[1536];
    if(!drawing||!storage_directory_on_drive(runtime_data,L'E')||
       !GetSystemDirectoryW(system,_countof(system))||
       swprintf_s(shell,_countof(shell),L"%ls\\WindowsPowerShell\\v1.0\\powershell.exe",system)<0)return NULL;
    LONG serial=InterlockedIncrement(&sequence);
    if(swprintf_s(pdf,_countof(pdf),L"%ls\\pack-dwg-%lu-%ld.pdf",runtime_data,GetCurrentProcessId(),serial)<0||
       swprintf_s(png,_countof(png),L"%ls\\pack-dwg-%lu-%ld.png",runtime_data,GetCurrentProcessId(),serial)<0||
       swprintf_s(command,_countof(command),L"\"%ls\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\\scripts\\Render-MWEAIPackagingDrawing.ps1\" \"%ls\" \"%ls\" %d",
           shell,runtime_root,pdf,png,pk_sheet-1)<0)return NULL;
    HBITMAP bitmap=NULL;
    if(!company_drawing_pdf_write(drawing,pdf)){
        STARTUPINFOW startup={sizeof(startup)};
        PROCESS_INFORMATION proc={0};
        if(CreateProcessW(shell,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,runtime_data,&startup,&proc)){
            DWORD code;
            if(WaitForSingleObject(proc.hProcess,8000)==WAIT_OBJECT_0&&GetExitCodeProcess(proc.hProcess,&code)&&code==0)
                bitmap=markup_read_raster(png,width,height);
            else if(GetExitCodeProcess(proc.hProcess,&code)&&code==STILL_ACTIVE){
                TerminateProcess(proc.hProcess,1);
                WaitForSingleObject(proc.hProcess,1000);
            }
            CloseHandle(proc.hThread);
            CloseHandle(proc.hProcess);
        }
    }
    DeleteFileW(pdf);DeleteFileW(png);
    return bitmap;
}
static void pk_drawing_capture(ProMdl model){
    int width,height;
    HBITMAP bitmap=model?pk_render_drawing(model,&width,&height):NULL;
    if(!bitmap)width=height=0;
    AcquireSRWLockExclusive(&pk_cad_lock);
    if(pk_drawing_image)DeleteObject(pk_drawing_image);
    pk_drawing_image=bitmap;pk_drawing_width=width;pk_drawing_height=height;
    ReleaseSRWLockExclusive(&pk_cad_lock);
}
static void pk_capture(ProMdl model){
    int width,height;
    HBITMAP bitmap=pk_window_bitmap(model,&width,&height);
    PkViewItem items[64]={0};
    int count=0;
    ProMdlType type;
    AxScan *scan=NULL;
    if(model&&!ProMdlTypeGet(model,&type)&&type==PRO_MDL_ASSEMBLY&&!ax_scan(model,0,&scan)){
        for(int i=0;i<scan->count&&count<_countof(items);i++){
            const AxOccurrence *entry=&scan->occurrences[i];
            ProFeature feature;
            ProBoolean hidden=PRO_B_FALSE;
            if(entry->depth!=1||entry->state!=PRO_FEAT_ACTIVE||
               ax_component_feature(model,entry->ids[0],&feature)||ProModelitemIsHidden(&feature,&hidden))continue;
            items[count].id=feature.id;
            items[count].hidden=hidden==PRO_B_TRUE;
            wcsncpy_s(items[count].name,_countof(items[count].name),entry->name,_TRUNCATE);
            count++;
        }
    }
    free(scan);
    AcquireSRWLockExclusive(&pk_cad_lock);
    if(pk_cad_image)DeleteObject(pk_cad_image);
    pk_cad_image=bitmap;pk_cad_w=width;pk_cad_h=height;
    pk_view_model=model;pk_view_count=count;
    memcpy(pk_view_items,items,count*sizeof(items[0]));
    ReleaseSRWLockExclusive(&pk_cad_lock);
}

static int packaging_preview(ProMdl source){
    ProOutline bounds;
    ProPath directory;
    double scale, input[3];
    wchar_t text[32], *end, result[720];
    packaging_ready=0;
    InvalidateRect(packaging_window,NULL,TRUE);
    for(int i=0;i<3;i++){
        GetWindowTextW(packaging_fields[i],text,_countof(text));
        input[i]=wcstod(text,&end);
        if(end==text||*end||!isfinite(input[i])||input[i]<0||input[i]>300||(i<2&&input[i]<1)){
            SetWindowTextW(packaging_result,L"请输入有效的板厚、泡棉厚度和余量（毫米）。");
            return 0;
        }
    }
    if(!source||qa_mm_per_unit(source,&scale)||or_outline(source,bounds)||
       ProDirectoryCurrentGet(directory)||!storage_directory_on_drive(directory,L'E')){
        SetWindowTextW(packaging_result,L"请打开有实体的零件或装配，并选择 E 盘项目工作目录。");
        return 0;
    }
    int axis=(int)SendMessageW(packaging_axis,CB_GETCURSEL,0,0);
    if(axis<0||axis>2)return 0;
    for(int i=0;i<3;i++){
        packaging_axes[i]=(axis+i+1)%3;
        int source_axis=packaging_axes[i];
        packaging_size[i]=(bounds[1][source_axis]-bounds[0][source_axis])*scale;
        if(!isfinite(packaging_size[i])||packaging_size[i]<=0||packaging_size[i]>10000){
            SetWindowTextW(packaging_result,L"产品外形尺寸无效或超出初版范围。");
            return 0;
        }
    }
    unsigned bits=pk_selected();
    if(!pk_make_plan(packaging_size,input,bits)){
        SetWindowTextW(packaging_result,L"请至少勾选一种包装件。");
        return 0;
    }
    swprintf_s(result,_countof(result),
        L"产品外形\n%.1f × %.1f × %.1f mm\n\n装箱外形\n%.1f × %.1f × %.1f mm\n\n支撑面：待确认\n屏幕避空：待核对\n实体干涉：建模后检查",
        packaging_size[0],packaging_size[1],packaging_size[2],
        pk_external[0],pk_external[1],pk_external[2]);
    pk_show_tree(bits);
    SetWindowTextW(packaging_result,result);
    packaging_source=source;
    packaging_mm=scale;
    memcpy(packaging_bounds,bounds,sizeof(bounds));
    memcpy(packaging_input,input,sizeof(input));
    wcscpy_s(packaging_directory,_countof(packaging_directory),directory);
    pk_capture(source);
    packaging_ready=1;
    InvalidateRect(packaging_window,NULL,FALSE);
    return 1;
}

static ProError packaging_place(ProMdl assembly,ProMdl model,const double xyz[3],double mm,ProAsmcomp *placed){
    ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    wchar_t text[512]=L"";
    PartText report={text,_countof(text),0};
    for(int i=0;i<3;i++)pose[3][i]=xyz[i]/mm;
    return ax_place_model(assembly,model,pose,AX_PLACE_FIXED,mm,placed,&report);
}

static ProError packaging_source_clean(ProMdl source){
    ProBoolean dirty;
    ProMdlType type;
    ProError error=ProMdlTypeGet(source,&type);
    if(!error)error=ProMdlModificationVerify(source,&dirty);
    if(error||dirty)return error?error:PRO_TK_BAD_CONTEXT;
    if(type!=PRO_MDL_ASSEMBLY)return PRO_TK_NO_ERROR;
    AxScan *scan=NULL;
    error=ax_scan(source,1,&scan);
    if(!error){
        for(int i=0;!error&&i<scan->count;i++){
            ProMdl model=scan->occurrences[i].model;
            if(model){
                error=ProMdlModificationVerify(model,&dirty);
                if(!error&&dirty)error=PRO_TK_BAD_CONTEXT;
            }
        }
    }
    free(scan);
    return error;
}


static ProError pk_count_surface(ProSurface surface,ProError status,ProAppData data){
    (void)surface;
    if(!status)++*(int *)data;
    return status;
}

static void pk_check_fit(ProMdl assembly,int product_id,const ProAsmcomp packing[PK_ITEM_COUNT],double mm,
    wchar_t *summary,size_t capacity,FILE *details){
    AxScan *scan=NULL;
    ProError error=ax_scan(assembly,0,&scan);
    ProSelection targets[PK_ITEM_COUNT]={0};
    int pairs=0;for(int i=0;i<PK_ITEM_COUNT;i++)pairs+=packing[i].id!=0;
    int candidates=0,checked=0,unknown=0,no_solid=0,collisions[2]={0};
    double volume_by_group[2]={0};
    for(int i=0;!error&&i<PK_ITEM_COUNT;i++){
        if(!packing[i].id)continue;
        AxComponent component={0};
        wchar_t path[40];
        swprintf_s(path,_countof(path),L"ID:%d",packing[i].id);
        error=ax_component_resolve(assembly,path,&component);
        if(!error)error=ax_part_selection(&component,&targets[i]);
    }
    if(!error)for(int i=0;i<scan->count;i++){
        const AxOccurrence *entry=&scan->occurrences[i];
        if(entry->ids[0]!=product_id||entry->state!=PRO_FEAT_ACTIVE||entry->type!=PRO_MDL_PART)continue;
        candidates++;
        if(candidates>64){unknown+=pairs;continue;}
        ProSelection product=NULL;
        AxComponent component={0};
        wchar_t path[256];
        ProError pick=PRO_TK_BAD_INPUTS;
        if(ax_path_label(entry->ids,entry->depth,path,_countof(path))){
            ProIdTable ids={0};
            memcpy(ids,entry->ids,entry->depth*sizeof(ids[0]));
            pick=ProAsmcomppathInit((ProSolid)assembly,ids,entry->depth,&component.path);
            if(!pick)pick=ProAsmcomppathMdlGet(&component.path,&component.model);
            if(!pick&&component.model!=entry->model)pick=PRO_TK_BAD_CONTEXT;
        }
        if(!pick){
            ProSolidBody *bodies=NULL;
            int count=-1;
            ProError body_error=ProSolidBodiesCollect((ProSolid)component.model,&bodies);
            if(!body_error&&bodies)ProArraySizeGet((ProArray)bodies,&count);
            int surfaces=0;
            if(!body_error&&count>=0){
                for(int j=0;j<count;j++){
                    ProError visit=ProSolidBodySurfaceVisit(&bodies[j],pk_count_surface,&surfaces);
                    if(visit&&visit!=PRO_TK_E_NOT_FOUND){body_error=visit;break;}
                }
            }
            if(bodies)ProArrayFree((ProArray *)&bodies);
            ProMassProperty measured={0};
            ProError mass_error=qa_geometry_mass(component.model,&measured);
            if(!body_error&&count>=0&&surfaces==0){
                no_solid+=pairs;
                if(details)fwprintf(details,L"%ls | 原生主体没有实体面，略过 %d 对体积干涉检查\n",path,pairs);
                continue;
            }
            ProModelitem item;
            pick=part_health(component.model);
            if(!pick)pick=ProMdlToModelitem(component.model,&item);
            if(!pick)pick=ProSelectionAlloc(&component.path,&item,&product);
            if(pick&&details)
                fwprintf(details,L"%ls | 实体数 %d (Creo %d)，体积 %.9g (Creo %d)\n",path,count,body_error,measured.volume,mass_error);
        }
        if(pick){
            if(details){
                ProMdlName model_name=L"";
                if(entry->model)ProMdlMdlnameGet(entry->model,model_name);
                fwprintf(details,L"%ls | %ls | 无法提取组件选取 | Creo %d\n",path,model_name,pick);
            }
            unknown+=pairs;continue;
        }
        for(int j=0;j<PK_ITEM_COUNT;j++){
            if(!targets[j])continue;
            double volume;
            ProError measure=ax_interference_volume(product,targets[j],&volume);
            if(measure){
                if(details)fwprintf(details,L"%ls | %ls | Creo %d\n",path,pk_names[j],measure);
                unknown++;continue;
            }
            checked++;
            volume*=mm*mm*mm;
            if(volume>0.01){collisions[j<PK_MAILER]++;volume_by_group[j<PK_MAILER]+=volume;}
        }
        ProSelectionFree(&product);
    }
    for(int i=0;i<PK_ITEM_COUNT;i++)if(targets[i])ProSelectionFree(&targets[i]);
    if(error||!candidates||(scan&&(scan->limited||scan->unavailable)))unknown++;
    free(scan);
    swprintf_s(summary,capacity,L"实体干涉检查：已核对 %d 对，未核实 %d 项，无实体 %d 对。\n产品与外箱 %d 对（%.3g mm³），产品与泡棉 %d 对（%.3g mm³）。\n仅计大于 0.01 mm³ 的实体交叠，泡棉承重需按样件核验。",
        checked,unknown,no_solid,collisions[0],volume_by_group[0],collisions[1],volume_by_group[1]);
}

typedef struct { double x,y,z,w,d; } PkPocket;
static ProError pk_pocket_profile(ProSection section,const ProMatrix frame,double scale,void *data){
    PkPocket *p=data;
    ImitationGeometry geometry={0};
    ImitationSketch s={&geometry,IMITATION_WINDOW,0,1,p->z,section,frame,scale};
    const ImitationPoint pts[]={{p->x,p->y},{p->x+p->w,p->y},{p->x+p->w,p->y+p->d},{p->x,p->y+p->d}};
    return imitation_polygon(&s,pts,_countof(pts));
}
static ProError pk_cut(ProMdl model,ProMdlName name,ProOutline bounds,
    double w,double d,double z,double depth,PartText *out){
    const wchar_t *basis=imitation_template_plane(model,2),*orient=imitation_template_plane(model,0);
    if(!basis||!orient)return PRO_TK_E_NOT_FOUND;
    ProError error=imitation_offset(model,name,out,L"POCKET_START",basis,2,z);
    double frame[4][4];
    PkPocket pocket={(bounds[0][0]+bounds[1][0]-w)/2,(bounds[0][1]+bounds[1][1]-d)/2,z,w,d};
    if(!error)error=smx_custom_sketch(model,L"POCKET_PROFILE",L"PLANE:POCKET_START",orient,pk_pocket_profile,&pocket,frame);
    if(!error)error=imitation_operation(model,out,0,L"CREO_OP SOLID_EXTRUDE %ls POCKET POCKET_PROFILE CUT BLIND %.12g %ls",
        name,depth,frame[2][2]>0?L"NEG":L"POS");
    return error;
}
static ProError packaging_build(ProMdl source,wchar_t *reply,size_t capacity){
    ProMdl assembly=NULL, parts[PK_ITEM_COUNT]={0};
    ProMdlName root, name;
    ProPath directory;
    ProOutline bounds;
    SYSTEMTIME now;
    const wchar_t *stage=L"创建包装模型";
    const char *native_stage=NULL;
    int saved=0,window;
    ProError error=PRO_TK_BAD_CONTEXT;
    if(!packaging_ready||!source||source!=packaging_source||
       ProDirectoryCurrentGet(directory)||_wcsicmp(directory,packaging_directory)||
       !storage_directory_on_drive(directory,L'E')||or_outline(source,bounds)||
       memcmp(bounds,packaging_bounds,sizeof(bounds))){
        wcscpy_s(reply,capacity,L"当前模型或工作目录已变化，请重新预览。");
        return error;
    }
    error=packaging_source_clean(source);
    if(error){
        wcscpy_s(reply,capacity,L"无法核实原产品及组件的保存状态。请保存源模型后重新预览。");
        return error;
    }
    unsigned bits=pk_selected();
    if(!pk_make_plan(packaging_size,packaging_input,bits))return PRO_TK_BAD_INPUTS;
    double source_at[3];
    for(int i=0;i<3;i++)source_at[i]=pk_product_origin[i]-bounds[0][packaging_axes[i]]*packaging_mm;
    GetLocalTime(&now);
    swprintf_s(root,_countof(root),L"PK_%02u%02u%02u_%02u%02u%02u_%03u",now.wYear%100,now.wMonth,now.wDay,
        now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
    const int old_background=model_background_mode;
    model_background_mode=1;
    error=model_create_empty_named(3,root,name,&native_stage);
    if(!error)error=ProMdlnameInit(root,PRO_MDL_ASSEMBLY,&assembly);
    double mm=0;
    if(!error)error=qa_mm_per_unit(assembly,&mm);
    if(!error&&!part_close(mm,packaging_mm)){stage=L"核对装配和产品单位";error=PRO_TK_BAD_CONTEXT;}
    for(int i=0;!error&&i<PK_ITEM_COUNT;i++){
        if(!pk_plan[i].enabled)continue;
        static const wchar_t *suffix[]={L"LOWER",L"UPPER",L"MAIL",L"OUTER"};
        swprintf_s(name,_countof(name),L"%ls_%ls",root,suffix[i]);
        wchar_t part_name[PRO_NAME_SIZE];
        error=model_create_empty_named(1,name,part_name,&native_stage);
        if(!error)error=ProMdlnameInit(part_name,PRO_MDL_PART,&parts[i]);
        ModelPlan plan={0};
        plan.a=pk_plan[i].size[0];plan.b=pk_plan[i].size[1];plan.height=pk_plan[i].size[2];plan.ready=1;
        int restored;
        if(!error)error=model_build_internal(&plan,part_name,&native_stage,parts[i],&restored,NULL);
        if(!error){
            ProOutline measured;
            stage=L"核对新件外形";
            error=or_outline(parts[i],measured);
            for(int axis=0;!error&&axis<3;axis++)
                if(fabs((measured[1][axis]-measured[0][axis])*mm-pk_plan[i].size[axis])>0.01)error=PRO_TK_GENERAL_ERROR;
            if(!error&&pk_plan[i].wall>0){
                const double t=pk_plan[i].wall;
                wchar_t detail[512]=L"";
                PartText report={detail,_countof(detail),0};
                stage=L"创建独立纸箱空腔";
                if(fabs(mm-1.0)>0.000001)error=PRO_TK_UNSUPPORTED;
                if(!error)error=pk_cut(parts[i],part_name,measured,plan.a-2*t,plan.b-2*t,
                    measured[0][2]+t,plan.height-2*t,&report);
                if(!error){
                    ProMassProperty mass;
                    stage=L"核对纸箱内腔与板厚";
                    error=ProSolidMassPropertyWithDensityGet((ProSolid)parts[i],NULL,PRO_MP_DENS_USE_ALWAYS,1.0,&mass);
                    const double expected=plan.a*plan.b*plan.height-(plan.a-2*t)*(plan.b-2*t)*(plan.height-2*t);
                    if(!error&&fabs(mass.volume-expected)>max(0.01,expected*0.0001))error=PRO_TK_INCOMPLETE;
                }
            }
        }
    }
    model_background_mode=old_background;
    ProAsmcomp product={0},packing[PK_ITEM_COUNT]={0};
    wchar_t fit[480]=L"";
        if(!error){
        stage=L"按选定装箱方向装入产品";
        ProMatrix product_pose={{0}};
        wchar_t detail[512]=L"";
        PartText report={detail,_countof(detail),0};
        product_pose[3][3]=1;
        for(int i=0;i<3;i++){
            product_pose[packaging_axes[i]][i]=1;
            product_pose[3][i]=source_at[i]/mm;
        }
        error=ax_place_model(assembly,source,product_pose,AX_PLACE_FIXED,mm,&product,&report);
    }
    for(int i=0;!error&&i<PK_ITEM_COUNT;i++){
        ProOutline measured;
        if(!pk_plan[i].enabled)continue;
        ProMdl part=parts[i];
        stage=L"装配外箱和泡棉";
        error=or_outline(part,measured);
        double xyz[3];
        if(!error){
            for(int axis=0;axis<3;axis++)xyz[axis]=pk_plan[i].origin[axis]-measured[0][axis]*mm;
            error=packaging_place(assembly,part,xyz,mm,&packing[i]);
        }
    }
    if(!error){
        ProOutline measured;
        
        stage=L"核对包装总装外形";
        error=or_outline(assembly,measured);
        for(int i=0;!error&&i<3;i++)
            if(fabs((measured[1][i]-measured[0][i])*mm-pk_external[i])>0.03)error=PRO_TK_GENERAL_ERROR;
    }

    ProPath output=L"", assembly_file=L"";
    if(!error){
        stage=L"创建独立包装交付目录";
        if(swprintf_s(output,_countof(output),L"%ls%ls%ls",directory,directory[wcslen(directory)-1]==L'\\'?L"":L"\\",root)<0||
           !CreateDirectoryW(output,NULL))error=PRO_TK_CANT_WRITE;
    }
    if(!error){
        ProPath diagnostic_path;
        FILE *details=NULL;
        if(swprintf_s(diagnostic_path,_countof(diagnostic_path),L"%ls\\包装接触明细.txt",output)>0)
            _wfopen_s(&details,diagnostic_path,L"w, ccs=UTF-8");
        pk_check_fit(assembly,product.id,packing,mm,fit,_countof(fit),details);
        if(details){fwprintf(details,L"%ls\n",fit);fclose(details);}
        stage=L"保存包装总装及依赖";
        error=ProMdlnameBackup(assembly,output);
    }
    for(int i=0;!error&&i<=PK_ITEM_COUNT;i++){
        if(i<PK_ITEM_COUNT&&!pk_plan[i].enabled)continue;
        ProMdlName saved_name;
        ProPath pattern;
        ProMdl model=i==4?assembly:parts[i];
        stage=L"核对磁盘上的包装模型";
        error=ProMdlMdlnameGet(model,saved_name);
        if(error)break;
        if(swprintf_s(pattern,_countof(pattern),L"%ls\\%ls.%ls*",output,saved_name,i==4?L"asm":L"prt")<0){
            error=PRO_TK_BAD_INPUTS;break;
        }
        WIN32_FIND_DATAW info;
        HANDLE file=FindFirstFileW(pattern,&info);
        if(file==INVALID_HANDLE_VALUE){error=PRO_TK_CANT_OPEN;break;}
        FindClose(file);
        if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY||!(info.nFileSizeHigh||info.nFileSizeLow)){
            error=PRO_TK_CANT_OPEN;break;
        }
        if(i==4)wcscpy_s(assembly_file,_countof(assembly_file),info.cFileName);
        saved++;
    }
    ProError display_error=0;
    if(!error){
        stage=L"打开包装总装";
        error=ProObjectwindowMdlnameCreate(root,PRO_ASSEMBLY,&window);
        if(!error)error=ProWindowCurrentSet(window);
        if(!error)error=ProMdlDisplay(assembly);
        if(!error)error=ProWindowActivate(window);
        display_error=error;
        if(!error)pk_capture(assembly);
        error=0;
    }
    if(error)swprintf_s(reply,capacity,L"包装未完整生成。阶段：%ls，Creo %d。\n已保存 %d 个新模型。",stage,error,saved);
    else swprintf_s(reply,capacity,L"已保存包装总装及勾选的可编辑包装零件，并核对总装外形。%ls\n%ls\\%ls\n%ls",display_error?L"Creo 未确认自动切换窗口，请打开已保存的总装。":L"",output,assembly_file,fit);
    if(!error){
        pk_drawing_model=NULL;pk_drawing_assembly=NULL;pk_sheet=1;pk_sheets=0;
        pk_save_folder[0]=pk_last_saved_folder[0]=0;pk_drawing_capture(NULL);
        packaging_last=assembly;
        pk_last_mask=bits;
        wcscpy_s(packaging_last_name,_countof(packaging_last_name),root);
        wcscpy_s(packaging_output,_countof(packaging_output),output);
    }
    packaging_ready=0;
    return error;
}


static int pk_saved_drawing(const wchar_t *folder,const wchar_t *name,ProPath result){
    ProPath pattern;
    result[0]=0;
    if(swprintf_s(pattern,_countof(pattern),L"%ls\\%ls.drw.*",folder,name)<0)return 0;
    WIN32_FIND_DATAW file;
    HANDLE find=FindFirstFileW(pattern,&file);
    if(find==INVALID_HANDLE_VALUE)return 0;
    unsigned long best=0;
    do{
        if((file.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!(file.nFileSizeHigh||file.nFileSizeLow))continue;
        const wchar_t *dot=wcsrchr(file.cFileName,L'.');
        wchar_t *end=NULL;
        unsigned long number=dot?wcstoul(dot+1,&end,10):0;
        if(number<=best||!end||*end)continue;
        if(swprintf_s(result,PRO_PATH_SIZE,L"%ls\\%ls",folder,file.cFileName)>0)best=number;
    }while(FindNextFileW(find,&file));
    FindClose(find);
    return best!=0;
}

static ProError packaging_drawing(wchar_t *reply,size_t capacity){
    if(!packaging_last||!packaging_output[0]){
        wcscpy_s(reply,capacity,L"请先生成包装模型，再打开 CAD 图纸。");
        return PRO_TK_BAD_CONTEXT;
    }
    ProMdl model=NULL,drawing=NULL;
    ProError error=ProMdlnameInit(packaging_last_name,PRO_MDL_ASSEMBLY,&model);
    if(error||model!=packaging_last||!storage_directory_on_drive(packaging_output,L'E')){
        wcscpy_s(reply,capacity,L"无法核实上次包装总装，请重新生成。");
        return PRO_TK_BAD_CONTEXT;
    }
    ProMdlName name;
    ProPath saved,pdf;
    CompanyDelivery package={0};
    wchar_t detail[400]=L"";
    PartText out={detail,_countof(detail),0};
    int old_window=-1,window=-1,created=0;
    package.assembly=1;
    swprintf_s(name,_countof(name),L"%ls_DWG",packaging_last_name);
    swprintf_s(pdf,_countof(pdf),L"%ls\\%ls.pdf",packaging_output,name);
    const int exists=pk_saved_drawing(packaging_output,name,saved);
    const wchar_t *stage=L"读取 CAD 图纸";
    error=ProMdlnameInit(name,PRO_MDL_DRAWING,&drawing);
    if(error==PRO_TK_E_NOT_FOUND){
        if(exists)error=ProMdlFiletypeLoad(saved,PRO_MDLFILE_DRAWING,PRO_B_FALSE,&drawing);
        else{
            stage=L"新建 CAD 图纸";
            ProDrawing native_drawing=NULL;
            ProWindowCurrentGet(&old_window);
            error=bwtl_delivery_create_drawing(&package,model,name,&native_drawing,&out);
            drawing=(ProMdl)native_drawing;created=!error;
        }
    }
    if(!error){
        stage=L"打开 Creo 图纸窗口";
        if(ProMdlWindowGet(drawing,&window))
            error=ProObjectwindowMdlnameCreate(name,PRO_DRAWING,&window);
        if(!error)error=ProWindowCurrentSet(window);
        if(!error)error=ProMdlDisplay(drawing);
    }
    if(!error&&!exists){
        stage=L"保存可编辑的 Creo 工程图";
        if(created&&package.views<3)error=bwtl_delivery_add_views((ProDrawing)drawing,model,&package.views);
        if(!error&&created&&package.views<3)error=PRO_TK_INCOMPLETE;
        if(!error)error=ProMdlnameBackup(drawing,packaging_output);
        if(!error&&!pk_saved_drawing(packaging_output,name,saved))error=PRO_TK_CANT_OPEN;
    }
    if(!error){
        stage=L"激活 Creo 工程图";
        error=ProWindowActivate(window);
        ProMdl current=NULL;
        if(!error)error=ProMdlCurrentGet(&current);
        if(!error&&current!=drawing)error=PRO_TK_BAD_CONTEXT;
    }
    if(error){
        if(old_window>=0)ProWindowCurrentSet(old_window);
        swprintf_s(reply,capacity,L"CAD 图纸未打开。阶段：%ls，Creo %d。",stage,error);
    }else{
        if(created&&!company_drawing_pdf_valid(pdf))company_drawing_pdf_write(drawing,pdf);
        swprintf_s(reply,capacity,L"已在 Creo 打开可编辑的 CAD 图纸。\n%ls",saved);
    }
    return error;
}

static ProError pk_rotate_view(ProMdl model,double yaw,double pitch){
    int window;
    ProMatrix current, rotated;
    ProError error;
    if(!model)return PRO_TK_BAD_CONTEXT;
    if((error=ProMdlWindowGet(model,&window))||
       (error=ProViewMatrixGet(model,NULL,current)))return error;
    memcpy(rotated,current,sizeof(rotated));
    const double c=cos(yaw),s=sin(yaw),cp=cos(pitch),sp=sin(pitch);
    for(int i=0;i<3;i++){
        rotated[i][0]=c*current[i][0]-s*current[i][2];
        const double z=s*current[i][0]+c*current[i][2];
        rotated[i][1]=cp*current[i][1]-sp*z;
        rotated[i][2]=sp*current[i][1]+cp*z;
        double norm=hypot(hypot(rotated[i][0],rotated[i][1]),rotated[i][2]);
        if(!isfinite(norm)||norm<1e-12)return PRO_TK_GENERAL_ERROR;
        for(int j=0;j<3;j++)rotated[i][j]/=norm;
        rotated[i][3]=0;
        rotated[3][i]=0;
    }
    rotated[3][3]=1;
    if((error=ProViewMatrixSet(model,NULL,rotated))||
       (error=ProWindowRepaint(window)))return error;
    pk_capture(model);
    InvalidateRect(packaging_window,NULL,FALSE);
    return PRO_TK_NO_ERROR;
}
static ProError pk_assembly_view(ProMdl model,int action){
    if(!model||model!=packaging_last)return PRO_TK_BAD_CONTEXT;
    ProError error=0;
    if(action==5)error=ax_view_explosion_display(model,1);
    else if(action==7){
        AxSectionFind section={L"MWE_PACK_SECTION",{0},0};
        error=ProSolidXsecVisit((ProSolid)model,ax_view_section_find,&section);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
        if(!error&&section.found)error=ProXsecDelete(&section.value);
        if(!error)error=ax_view_explosion_display(model,0);
    }
    else{
        error=ax_view_explosion_display(model,0);
        PartCommand command={0};
        wchar_t report[200]=L"";
        PartText text={report,_countof(report),0};
        ProOutline outline;
        if(!error)error=or_outline(model,outline);
        if(!error){
            command.count=5;
            wcscpy_s(command.arg[2],_countof(command.arg[2]),L"MWE_PACK_SECTION");
            wcscpy_s(command.arg[3],_countof(command.arg[3]),L"PLANE:ASM_FRONT");
            swprintf_s(command.arg[4],_countof(command.arg[4]),L"%.12g",
                (outline[0][2]+outline[1][2])*0.5*packaging_mm);
            error=ax_view_section(model,&command,packaging_mm,&text);
            if(error==PRO_TK_E_FOUND){
                command.count=4;
                error=ax_view_section(model,&command,packaging_mm,&text);
            }
        }
    }
    if(!error){
        pk_capture(model);
        InvalidateRect(packaging_window,NULL,FALSE);
    }
    return error;
}
static ProError pk_toggle_component(ProMdl model,int id){
    ProFeature item;
    ProBoolean hidden,after;
    ProError error=ax_component_feature(model,id,&item);
    if(!error)error=ProModelitemIsHidden(&item,&hidden);
    if(!error)error=hidden?ProModelitemUnhide(&item):ProModelitemHide(&item);
    if(!error)error=ProModelitemIsHidden(&item,&after);
    if(!error&&after==hidden)error=PRO_TK_INCOMPLETE;
    if(!error)error=ax_view_repaint(model);
    if(!error){pk_capture(model);InvalidateRect(packaging_window,NULL,FALSE);}
    return error;
}
static const wchar_t *pk_destination(void){
    return pk_save_folder[0]?pk_save_folder:packaging_output;
}

static void pk_drawing_labels(void){
    wchar_t page[40],path[PRO_PATH_SIZE+24];
    swprintf_s(page,_countof(page),L"%d / %d",pk_sheets?pk_sheet:0,pk_sheets);
    swprintf_s(path,_countof(path),L"保存路径  %ls",pk_destination()[0]?pk_destination():L"生成包装后使用交付目录");
    SetWindowTextW(pk_sheet_label,page);
    SetWindowTextW(pk_save_label,path);
    EnableWindow(pk_drawing_buttons[5],pk_sheet>1);
    EnableWindow(pk_drawing_buttons[6],pk_sheet<pk_sheets);
}

static ProError pk_drawing_read_current(void){
    ProMdl current=NULL;
    double scale;
    ProError error=ProMdlCurrentGet(&current);
    if(!error)error=ProDrawingSheetsCount((ProDrawing)current,&pk_sheets);
    if(!error&&pk_sheets<1)error=PRO_TK_INCOMPLETE;
    if(!error)pk_sheet=max(1,min(pk_sheet,pk_sheets));
    if(!error)error=ProDrawingScaleGet((ProDrawing)current,(ProSolid)packaging_last,pk_sheet,&scale);
    if(!error&&(!isfinite(scale)||scale<=0))error=PRO_TK_INCOMPLETE;
    if(!error){
        wchar_t value[40];
        swprintf_s(value,_countof(value),L"%.9g",1.0/scale);
        SetWindowTextW(pk_drawing_scale,value);
        pk_drawing_model=current;
        pk_drawing_assembly=packaging_last;
        pk_drawing_capture(current);
        pk_drawing_labels();
        InvalidateRect(packaging_window,NULL,FALSE);
    }
    return error;
}

static ProError pk_drawing_edit(int action,double denominator,wchar_t *reply,size_t capacity){
    if(!pk_drawing_model||pk_drawing_assembly!=packaging_last)return PRO_TK_BAD_CONTEXT;
    int window=-1,old_window=-1;
    ProMdl current=NULL;
    ProError error=ProWindowCurrentGet(&old_window);
    if(!error)error=ProMdlWindowGet(pk_drawing_model,&window);
    if(!error)error=ProWindowCurrentSet(window);
    if(!error)error=ProMdlCurrentGet(&current);
    if(!error&&current!=pk_drawing_model)error=PRO_TK_BAD_CONTEXT;
    if(!error&&action==10){
        if(!isfinite(denominator)||denominator<.01||denominator>1000)error=PRO_TK_BAD_INPUTS;
        else{
            error=ProDrawingScaleSet((ProDrawing)current,(ProSolid)packaging_last,pk_sheet,1/denominator);
            double checked=0;
            if(!error)error=ProDrawingScaleGet((ProDrawing)current,(ProSolid)packaging_last,pk_sheet,&checked);
            if(!error&&fabs(checked-1/denominator)>1e-9)error=PRO_TK_INCOMPLETE;
            if(!error)error=ProWindowRepaint(window);
            if(!error)pk_drawing_capture(current);
            if(!error)swprintf_s(reply,capacity,L"第 %d 张图的模型比例已改为 1:%.9g，尚未保存。",pk_sheet,denominator);
        }
    }else if(!error&&(action==12||action==13)){
        int previous=pk_sheet;
        pk_sheet=max(1,min(pk_sheets,pk_sheet+(action==12?-1:1)));
        error=pk_drawing_read_current();
        if(error){pk_sheet=previous;pk_drawing_labels();}
        else swprintf_s(reply,capacity,L"第 %d / %d 张图纸",pk_sheet,pk_sheets);
    }else if(!error){
        ProMdlName name;
        ProPath before,after,destination;
        wcscpy_s(destination,_countof(destination),pk_destination());
        error=ProMdlMdlnameGet(current,name);
        if(!error&&!storage_directory_on_drive(destination,L'E'))error=PRO_TK_BAD_CONTEXT;
        const wchar_t *origin=pk_last_saved_folder[0]?pk_last_saved_folder:packaging_output;
        int same=!_wcsicmp(destination,origin);
        if(!error&&!same&&pk_saved_drawing(destination,name,before))error=PRO_TK_E_FOUND;
        if(!error)error=same?ProMdlSave(current):ProMdlnameBackup(current,destination);
        if(!error&&!pk_saved_drawing(destination,name,after))error=PRO_TK_INCOMPLETE;
        if(!error){
            wcscpy_s(pk_last_saved_folder,_countof(pk_last_saved_folder),destination);
            swprintf_s(reply,capacity,L"已保存工程图。\n%ls",after);
        }
    }
    if(old_window>=0&&old_window!=window)ProWindowCurrentSet(old_window);
    InvalidateRect(packaging_window,NULL,FALSE);
    return error;
}
static int packaging_command(uiCmdCmdId id,uiCmdValue *value,void *data){
    (void)id;(void)value;(void)data;
    const int action=packaging_pending;
    packaging_pending=0;
    if(!packaging_window||native_busy||!action)return 0;
    ProMdl source=NULL;
    ProMdlCurrentGet(&source);
    if(action==1)packaging_preview(source==packaging_last&&packaging_source?packaging_source:source);
    else if(action==4){
        AcquireSRWLockShared(&pk_cad_lock);
        if(source!=pk_view_model)source=NULL;
        ReleaseSRWLockShared(&pk_cad_lock);
        const LONG dx=InterlockedExchange(&pk_orbit_x,0),dy=InterlockedExchange(&pk_orbit_y,0);
        ProError error=(dx||dy)?pk_rotate_view(source,dx*0.008,dy*0.008):PRO_TK_NO_ERROR;
        if(error)SetWindowTextW(packaging_result,L"请先打开并预览本次包装的产品或总装。");
    }
    else if(action==8){
        if(source&&(source==packaging_last||source==packaging_source)&&!ax_view_repaint(source)){
            pk_capture(source);InvalidateRect(packaging_window,NULL,FALSE);
        }else SetWindowTextW(packaging_result,L"请先在 Creo 打开本次产品或包装总装。");
    }
    else if(action==9){
        int id=InterlockedExchange(&pk_visibility_id,0);
        AcquireSRWLockShared(&pk_cad_lock);
        int expected=source&&source==pk_view_model;
        ReleaseSRWLockShared(&pk_cad_lock);
        if(!expected||!id||pk_toggle_component(source,id))
            SetWindowTextW(packaging_result,L"未能切换所选组件的显示状态。");
    }
    else if(action>=10&&action<=13){
        wchar_t reply[640]=L"",number[40],*end;
        GetWindowTextW(pk_drawing_scale,number,_countof(number));
        double denominator=wcstod(number,&end);
        ProError error=pk_drawing_edit(action,end==number||*end?0:denominator,reply,_countof(reply));
        if(error)swprintf_s(reply,_countof(reply),L"图纸操作未完成（Creo %d）。请先在本页加载图纸，并输入有效比例。",error);
        SetWindowTextW(packaging_result,reply);
    }
    else if(action>=CLASH_SCAN&&action<=CLASH_ACTION_LAST)clash_execute(action);
    else if(action>=DFM_SCAN&&action<=DFM_EXPORT)dfm_execute(action);
    else if(action==PRICE_READ)price_read_model();
    else if(action==PRICE_EXPORT)price_export();
    else if(action==PRICE_OPEN_FOLDER)price_open_folder();
    else if(action>=5){
        if(pk_assembly_view(source,action))
            SetWindowTextW(packaging_result,L"请先生成并打开本次包装总装，再切换预览。");
    }
    else{
        wchar_t reply[640];
        if(action==3){
            int previous=-1;
            ProWindowCurrentGet(&previous);
            if(!packaging_drawing(reply,_countof(reply))){
                ProError error=pk_drawing_read_current();
                if(error)swprintf_s(reply,_countof(reply),L"图纸已生成，但比例读取失败（Creo %d）。",error);
            }
            if(previous>=0)ProWindowCurrentSet(previous);
        }
        else packaging_build(source==packaging_last&&packaging_source?packaging_source:source,reply,_countof(reply));
        SetWindowTextW(packaging_result,reply);
        append_card(L"包装设计",reply);
    }
    return 0;
}


enum { PK_REFRESH_PACK=140, PK_REFRESH_MODEL, PK_BACK, PK_NEXT, PK_DWG_LOAD, PK_DWG_SCALE, PK_DWG_SAVE, PK_DWG_INPUT, PK_DWG_FOLDER,PK_DWG_OPEN,PK_DWG_PREV,PK_DWG_NEXT,PK_DWG_BACK, PK_LIBRARY=101, PK_PREVIEW, PK_LIST, PK_BUILD, PK_DRAWING, PK_CHAT, PK_FIRST_BOX=120, PK_AXIS=130 };
static HWND pk_menu[6],pk_boxes[6],pk_labels[5],pk_preview_button;
static int pk_page,pk_dimension_view;
static HWND pk_visual[6],pk_preview_actions[5],pk_deferred_fields[2];


static HBRUSH pk_background;
static const wchar_t *pk_types[]={L"普通瓦楞纸箱",L"重型瓦楞纸箱",L"天地盖",L"飞机盒",L"木箱",L"托盘纸箱"};

static HWND packaging_control(HWND owner,const wchar_t *kind,const wchar_t *label,DWORD style,UINT id){
    HWND control=CreateWindowW(kind,label,WS_CHILD|style,0,0,0,0,
        owner,(HMENU)(UINT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(control,WM_SETFONT,(WPARAM)native_font,TRUE);
    return control;
}

#include "packaging_config_ui.inc"

static void packaging_arrange(void){
    RECT rc;
    if(!packaging_window)return;
    GetClientRect(packaging_window,&rc);
    const int w=rc.right,h=rc.bottom;
    if(w<100||h<160)return;
    HWND active_panel=pk_page==3?clash_panel:pk_page==4?dfm_panel:pk_page==5?price_panel:NULL;
    if(active_panel){
        for(HWND child=GetWindow(packaging_window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
            if(child!=active_panel)ShowWindow(child,SW_HIDE);
        SetWindowPos(active_panel,HWND_TOP,0,0,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        return;
    }
    if(clash_panel)ShowWindow(clash_panel,SW_HIDE);
    if(dfm_panel)ShowWindow(dfm_panel,SW_HIDE);
    if(price_panel)ShowWindow(price_panel,SW_HIDE);
    const int tab=(w-14)/4,left=min(w*3/8,max(225,w/4));
    for(int i=0,j=0;i<6;i++){
        ShowWindow(pk_menu[i],(i==2||i==3)?SW_HIDE:SW_SHOWNA);
        if(i!=2&&i!=3)MoveWindow(pk_menu[i],7+j++*tab,7,tab-5,30,TRUE);
    }
    MoveWindow(pk_config_scrollbar,w-18,44,17,h-84,TRUE);
    ShowWindow(pk_config_scrollbar,pk_page==0?SW_SHOW:SW_HIDE);
    if(pk_page==0)pk_config_scroll_to(packaging_window,pk_config_scroll);
    int visible=pk_page==1?SW_SHOW:SW_HIDE;
    int field_left=12,field_right=12+(left-16)/2,fw=max(25,(left-32)/2);
    int stride=min(47,max(32,(h-115)/9));
    MoveWindow(packaging_axis,field_left,100,fw,150,TRUE);
    ShowWindow(packaging_axis,visible);
    int cols[]={field_right,field_left,field_right};
    int rows[]={0,1,2};
    for(int i=0;i<3;i++){
        int y=100+rows[i]*stride;
        MoveWindow(pk_labels[i+1],cols[i],y-20,fw,19,TRUE);
        MoveWindow(packaging_fields[i],cols[i],y,fw,22,TRUE);
        ShowWindow(pk_labels[i+1],visible);
        ShowWindow(packaging_fields[i],visible);
    }
    for(int i=0;i<2;i++){
        MoveWindow(pk_deferred_fields[i],i?field_left:field_right,100+(i+1)*stride,fw,22,TRUE);
        ShowWindow(pk_deferred_fields[i],visible);
    }
    int action_y=max(225,h-119);
    MoveWindow(pk_preview_button,field_left,action_y,fw,28,TRUE);
    ShowWindow(pk_preview_button,visible);
    for(int i=0;i<5;i++)ShowWindow(pk_preview_actions[i],visible);
    MoveWindow(pk_preview_actions[0],field_right,action_y,fw,28,TRUE);
    MoveWindow(pk_preview_actions[1],field_left,action_y+32,fw,28,TRUE);
    MoveWindow(pk_preview_actions[2],field_right,action_y+32,fw,28,TRUE);
    MoveWindow(pk_preview_actions[3],12,h-36,fw,28,TRUE);
    MoveWindow(pk_preview_actions[4],w-fw-12,h-36,fw,28,TRUE);
    int j=0;
    for(int i=0;i<_countof(pk_visual);i++){
        const int shown=i==3||i==5;
        if(shown)MoveWindow(pk_visual[i],left+12+j++*70,85,65,27,TRUE);
        ShowWindow(pk_visual[i],shown?visible:SW_HIDE);
    }
    for(int i=0;i<PK_ITEM_COUNT;i++)ShowWindow(pk_checks[i],SW_HIDE);
    ShowWindow(pk_tree,SW_HIDE);
    ShowWindow(packaging_result,pk_page==0?SW_HIDE:SW_SHOW);
    for(int i=0;i<8;i++){
        ShowWindow(pk_drawing_buttons[i],pk_page==2?SW_SHOW:SW_HIDE);
    }
    MoveWindow(pk_drawing_buttons[0],12,53,90,28,TRUE);
    MoveWindow(pk_drawing_buttons[1],211,53,87,28,TRUE);
    MoveWindow(pk_drawing_buttons[2],303,53,87,28,TRUE);
    const int folder_y=w>=675?53:87,folder_x=w>=675?399:12;
    MoveWindow(pk_drawing_buttons[3],folder_x,folder_y,119,28,TRUE);
    MoveWindow(pk_drawing_buttons[4],folder_x+125,folder_y,134,28,TRUE);
    MoveWindow(pk_drawing_buttons[5],16,h-92,67,27,TRUE);
    MoveWindow(pk_sheet_label,88,h-88,60,20,TRUE);
    MoveWindow(pk_drawing_buttons[6],152,h-92,67,27,TRUE);
    MoveWindow(pk_drawing_buttons[7],w-102,h-92,86,27,TRUE);
    MoveWindow(pk_save_label,16,h-59,w-32,21,TRUE);
    ShowWindow(pk_sheet_label,pk_page==2?SW_SHOW:SW_HIDE);
    ShowWindow(pk_save_label,pk_page==2?SW_SHOW:SW_HIDE);
    MoveWindow(pk_drawing_scale,150,55,54,23,TRUE);
    ShowWindow(pk_drawing_scale,pk_page==2?SW_SHOW:SW_HIDE);
    if(pk_page==2){MoveWindow(packaging_result,16,h-34,w-32,29,TRUE);pk_drawing_labels();}
    else MoveWindow(packaging_result,left+12,max(116,h-77),max(30,w-left-24),34,TRUE);
    RedrawWindow(packaging_window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}

static void packaging_switch(int show){
    HWND chat_controls[]={native_history,native_prompt,native_preview,native_buttons[0],
        native_buttons[1],native_buttons[2],native_stop_button,native_attach_button,native_clear_button};
    if(!packaging_window)return;
    if(show){
        for(int i=0;i<_countof(chat_controls);i++)ShowWindow(chat_controls[i],SW_HIDE);
        ShowWindow(packaging_window,SW_SHOWNA);
        RedrawWindow(packaging_window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }else{
        ShowWindow(packaging_window,SW_HIDE);
        for(int i=0;i<_countof(chat_controls);i++)
            if(chat_controls[i]!=native_preview)ShowWindow(chat_controls[i],SW_SHOWNA);
        native_layout();
        RedrawWindow(native_window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
}

static void packaging_layout(void){
    if(!packaging_window||!native_window)return;
    RECT r;GetClientRect(native_window,&r);
    int y=MulDiv(124,native_dpi,96);
    SetWindowPos(packaging_window,HWND_TOP,8,y,r.right-16,r.bottom-y-8,SWP_NOACTIVATE|SWP_NOCOPYBITS);
    packaging_arrange();
    if(packaging_visible())RedrawWindow(packaging_window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
}

static void pk_box_icon(HDC dc,RECT rect,int type){
    int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2-9;
    int a=min((rect.right-rect.left)/4,55),b=min((rect.bottom-rect.top)/5,28);
    POINT top[]={{x-a,y-b},{x,y-2*b},{x+a,y-b},{x,y}};
    POINT left[]={{x-a,y-b},{x,y},{x,y+b},{x-a,y}};
    POINT right[]={{x,y},{x+a,y-b},{x+a,y},{x,y+b}};
    const COLORREF colors[]={RGB(224,180,115),RGB(184,134,76),RGB(208,160,98)};
    POINT *points[]={top,left,right};
    for(int i=0;i<3;i++){
        HBRUSH fill=CreateSolidBrush(colors[i]);
        HGDIOBJ old=SelectObject(dc,fill);
        Polygon(dc,points[i],4);
        SelectObject(dc,old);
        DeleteObject(fill);
    }
    if(type==2||type==3){
        MoveToEx(dc,x-a,y-b*2,NULL);LineTo(dc,x,y-b);
        LineTo(dc,x+a,y-b*2);
    }
    if(type==5)Rectangle(dc,x-a,y+b+2,x+a,y+b+7);
}


static void pk_face(HDC dc,const POINT points[4],COLORREF color){
    HBRUSH brush=CreateSolidBrush(color);
    if(!brush)return;
    HGDIOBJ old=SelectObject(dc,brush);
    Polygon(dc,points,4);
    SelectObject(dc,old);
    DeleteObject(brush);
}

static void pk_prism(HDC dc,int x,int y,int width,int height,int dx,int dy,const COLORREF color[3],int open){
    const POINT front[]={{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
    const POINT side[]={{x+width,y},{x+width+dx,y-dy},{x+width+dx,y+height-dy},{x+width,y+height}};
    const POINT top[]={{x,y},{x+dx,y-dy},{x+width+dx,y-dy},{x+width,y}};
    pk_face(dc,side,color[1]);
    pk_face(dc,front,color[0]);
    pk_face(dc,top,color[2]);
    if(open){
        const POINT back_flap[]={{x+dx,y-dy},{x+width+dx,y-dy},{x+width+dx+dy,y-3*dy},{x+dx+dy,y-3*dy}};
        const POINT front_flap[]={{x,y},{x+width,y},{x+width-dy,y+dy},{x-dy,y+dy}};
        pk_face(dc,back_flap,color[0]);pk_face(dc,front_flap,color[0]);
    }
}

static void pk_structure(HDC dc,RECT area){
    double x=packaging_size[0],y=packaging_size[1],z=packaging_size[2];
    const double foam=packaging_input[1];
    const int width=area.right-area.left,height=area.bottom-area.top;
    if(width<98||height<104)return;
    const double box_x=pk_external[0],box_y=pk_external[1],box_z=pk_external[2];
    /* Keep model axes consistent with the actual builder: pad thickness is along Z. */
    const double scale=min((width/2.0-15)/(box_x+0.46*box_y),(height-55)/(box_z+0.4*box_y+2*foam));
    if(!isfinite(scale)||scale<=0)return;
    const int product_x=area.left+width/4-(int)((x+0.46*y)*scale/2);
    const int carton_x=area.left+3*width/4-(int)((box_x+0.46*box_y)*scale/2);
    const int product_w=max(2,(int)(x*scale)),product_h=max(2,(int)(z*scale));
    const int depth=max(2,(int)(y*scale*0.46)),rise=max(2,(int)(y*scale*0.23));
    const int pad=max(3,(int)(foam*scale)),space=8;
    const int center=area.top+(height-22)/2;
    const int top=center-product_h/2;
    const COLORREF body[]={RGB(43,64,76),RGB(27,44,55),RGB(91,115,127)};
    const COLORREF pad_tones[]={RGB(238,243,246),RGB(198,210,218),RGB(255,255,255)};
    const COLORREF paper[]={RGB(209,159,99),RGB(175,122,65),RGB(106,72,38)};
    if(pk_plan[PK_UPPER].enabled)pk_prism(dc,product_x,top-pad-space,product_w,pad,depth,rise,pad_tones,0);
    pk_prism(dc,product_x,top,product_w,product_h,depth,rise,body,0);
    if(pk_plan[PK_LOWER].enabled)pk_prism(dc,product_x,top+product_h+space,product_w,pad,depth,rise,pad_tones,0);
    const int box_h=max(2,(int)(box_z*scale));
    if(pk_plan[PK_MAILER].enabled||pk_plan[PK_OUTER].enabled)pk_prism(dc,carton_x,center-box_h/2,max(2,(int)(box_x*scale)),box_h,
        max(2,(int)(box_y*scale*0.46)),max(2,(int)(box_y*scale*0.23)),paper,1);
    RECT labels={area.left,area.bottom-20,area.left+width/2,area.bottom};
    DrawTextW(dc,L"产品与上下泡棉",-1,&labels,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    labels.left=labels.right;labels.right=area.right;
    DrawTextW(dc,pk_plan[PK_OUTER].enabled?L"重力纸箱":pk_plan[PK_MAILER].enabled?L"飞机盒":L"",-1,&labels,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}

static void pk_reference_draw(HDC dc,RECT rect,HBITMAP image,int image_w,int image_h){
    if(!image||rect.right<=rect.left||rect.bottom<=rect.top)return;
    double scale=min((double)(rect.right-rect.left)/image_w,(double)(rect.bottom-rect.top)/image_h);
    scale*=pk_zoom;
    int width=max(1,(int)(image_w*scale)),height=max(1,(int)(image_h*scale));
    int x=(rect.left+rect.right-width)/2,y=(rect.top+rect.bottom-height)/2;
    HDC source=CreateCompatibleDC(dc);
    if(!source)return;
    HGDIOBJ old=SelectObject(source,image);
    int mode=SetStretchBltMode(dc,HALFTONE);
    int saved=SaveDC(dc);
    if(saved){
        IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
        StretchBlt(dc,x,y,width,height,source,0,0,image_w,image_h,SRCCOPY);
        RestoreDC(dc,saved);
    }
    SetStretchBltMode(dc,mode);
    SelectObject(source,old);
    DeleteDC(source);
}

static void packaging_paint(HDC dc,RECT rect){
    native_theme_fill(dc,&rect,CHAT_CANVAS_COLOR);
    RECT toolbar={0,0,rect.right,43};
    native_theme_fill(dc,&toolbar,RGB(221,232,237));
    SetRectEmpty(&pk_view_area);
    if(pk_page==0){pk_config_paint(dc,rect);return;}
    if(pk_page==2){
        RECT page={9,rect.right>=675?92:126,rect.right-9,rect.bottom-62},label={110,53,150,81};
        HGDIOBJ font=SelectObject(dc,native_font);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,CHAT_TEXT_COLOR);
        DrawTextW(dc,L"比例 1:",-1,&label,DT_SINGLELINE|DT_VCENTER);
        if(page.bottom>page.top){
            native_theme_round(dc,&page,RGB(223,231,237),CHAT_FRAME_COLOR,6);
            AcquireSRWLockShared(&pk_cad_lock);
            page.bottom-=36;
            if(pk_drawing_image)pk_reference_draw(dc,page,pk_drawing_image,pk_drawing_width,pk_drawing_height);
            else DrawTextW(dc,L"生成包装模型后，在这里加载工程图。",-1,&page,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
            ReleaseSRWLockShared(&pk_cad_lock);
        }
        SelectObject(dc,font);
        return;
    }
    const int left=min(rect.right*3/8,max(225,rect.right/4));
    const int stride=min(47,max(32,(rect.bottom-115)/9));
    RECT head={9,48,left-6,76}, box={left+5,116,rect.right-7,rect.bottom-80};
    native_theme_round(dc,&head,RGB(49,67,86),RGB(49,67,86),5);
    if(box.bottom>box.top)native_theme_round(dc,&box,RGB(223,231,237),CHAT_FRAME_COLOR,6);
    HGDIOBJ old=SelectObject(dc,native_font);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(255,255,255));head.left+=8;
    DrawTextW(dc,L"包装参数",-1,&head,DT_SINGLELINE|DT_VCENTER);
    SetTextColor(dc,CHAT_TEXT_COLOR);
    RECT label={12,80,left-8,100};
    DrawTextW(dc,L"装箱方向",-1,&label,DT_SINGLELINE);
    label=(RECT){12+(left-16)/2,80+stride,left-8,100+stride};
    DrawTextW(dc,L"包入深度",-1,&label,DT_SINGLELINE);
    label=(RECT){12,80+2*stride,left-8,100+2*stride};
    DrawTextW(dc,L"单边配合间隙",-1,&label,DT_SINGLELINE);
    wchar_t summary[220];
    pk_config_summary(summary,_countof(summary));
    RECT config={left+6,47,rect.right-7,79};
    native_theme_round(dc,&config,RGB(222,233,240),CHAT_FRAME_COLOR,5);config.left+=7;
    DrawTextW(dc,summary,-1,&config,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    int y=104+3*stride;
    RECT dims={12,y,left-6,y+21};
    DrawTextW(dc,L"计算尺寸（mm）",-1,&dims,DT_SINGLELINE);
    const wchar_t *labels[]={L"产品外形",L"泡棉凹腔",L"装箱外包络",L"纸箱内尺寸",L"纸箱外尺寸"};
    const PkItem *carton=pk_plan[PK_OUTER].enabled?&pk_plan[PK_OUTER]:pk_plan[PK_MAILER].enabled?&pk_plan[PK_MAILER]:NULL;
    for(int i=0;i<5&&y+43<max(225,rect.bottom-119);i++){
        y+=19;
        RECT line={12,y,left-7,y+20};
        double size[3]={0};
        int known=packaging_ready&&(i==0||i==2||(i>=3&&carton));
        if(known)for(int j=0;j<3;j++)size[j]=i==0?packaging_size[j]:i==2?(pk_plan[PK_MAILER].enabled&&pk_plan[PK_OUTER].enabled?pk_plan[PK_MAILER].size[j]:pk_external[j]-(carton?2*carton->wall:0)):
            carton->size[j]-(i==3?2*carton->wall:0);
        wchar_t value[100];
        if(known)swprintf_s(value,_countof(value),L"%ls  %.1f × %.1f × %.1f",labels[i],size[0],size[1],size[2]);
        else swprintf_s(value,_countof(value),L"%ls  —",labels[i]);
        DrawTextW(dc,value,-1,&line,DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if(box.bottom>box.top){
        pk_view_area=box;
        InflateRect(&box,-3,-3);
        AcquireSRWLockShared(&pk_cad_lock);
        if(pk_cad_image)pk_reference_draw(dc,box,pk_cad_image,pk_cad_w,pk_cad_h);
        else DrawTextW(dc,L"点击「预览包装」读取当前模型",-1,&box,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        ReleaseSRWLockShared(&pk_cad_lock);
    }
    SelectObject(dc,old);
}

static void pk_queue(HWND window,int action){
    if(native_busy||(action==2&&!packaging_ready)){
        SetWindowTextW(packaging_result,native_busy?L"请等当前任务完成。":L"请先预览，再生成模型。");
        return;
    }
    if(!PostMessageW(native_backend_window,PACKAGING_REQUEST,action,0))
        SetWindowTextW(packaging_result,L"无法向 Creo 提交包装操作。");
}

static LRESULT CALLBACK packaging_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        pk_background=CreateSolidBrush(CHAT_CANVAS_COLOR);
        pk_drawing_scale=packaging_control(window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,PK_DWG_INPUT);
        pk_drawing_buttons[0]=packaging_control(window,L"BUTTON",L"加载／刷新",WS_TABSTOP|BS_OWNERDRAW,PK_DWG_LOAD);
        pk_drawing_buttons[1]=packaging_control(window,L"BUTTON",L"应用比例",WS_TABSTOP|BS_OWNERDRAW,PK_DWG_SCALE);
        pk_drawing_buttons[2]=packaging_control(window,L"BUTTON",L"保存图纸",WS_TABSTOP|BS_OWNERDRAW,PK_DWG_SAVE);
        const wchar_t *labels2[]={L"选择保存路径",L"打开保存文件夹",L"上一页",L"下一页",L"上一步"};
        const int ids2[]={PK_DWG_FOLDER,PK_DWG_OPEN,PK_DWG_PREV,PK_DWG_NEXT,PK_DWG_BACK};
        for(int i=0;i<5;i++)pk_drawing_buttons[i+3]=packaging_control(window,L"BUTTON",labels2[i],WS_TABSTOP|BS_OWNERDRAW,ids2[i]);
        pk_sheet_label=packaging_control(window,L"STATIC",L"",SS_CENTER,0);
        pk_save_label=packaging_control(window,L"STATIC",L"",SS_PATHELLIPSIS,0);
        pk_config_scrollbar=packaging_control(window,L"SCROLLBAR",L"",SBS_VERT,131);
        wchar_t atlas_path[MAX_PATH];
        int atlas_width,atlas_height;
        if(swprintf_s(atlas_path,_countof(atlas_path),L"%ls\\resources\\packaging\\config-cards.png",runtime_root)>0)
            pk_config_atlas=markup_read_raster(atlas_path,&atlas_width,&atlas_height);

        pk_dimension_view=0;
        pk_visual[0]=packaging_control(window,L"BUTTON",L"Creo 视图",BS_OWNERDRAW|WS_TABSTOP,20);
        pk_visual[1]=packaging_control(window,L"BUTTON",L"尺寸示意",BS_OWNERDRAW|WS_TABSTOP,21);
        pk_visual[3]=packaging_control(window,L"BUTTON",L"爆炸",BS_OWNERDRAW|WS_TABSTOP,23);
        pk_visual[4]=packaging_control(window,L"BUTTON",L"剖切",BS_OWNERDRAW|WS_TABSTOP,24);
        pk_visual[5]=packaging_control(window,L"BUTTON",L"合拢",BS_OWNERDRAW|WS_TABSTOP,25);

        const wchar_t *menu[]={L"包装配置",L"装箱预览",L"包装清单",L"生成模型",L"CAD图纸",L"返回聊天"};
        for(int i=0;i<6;i++)pk_menu[i]=packaging_control(window,L"BUTTON",menu[i],WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,PK_LIBRARY+i);
        for(int i=0;i<6;i++)pk_boxes[i]=packaging_control(window,L"BUTTON",pk_types[i],WS_TABSTOP|BS_OWNERDRAW,PK_FIRST_BOX+i);


        packaging_axis=packaging_control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,PK_AXIS);
        const wchar_t *axes[]={L"沿 X 轴装箱",L"沿 Y 轴装箱",L"沿 Z 轴装箱"};
        for(int i=0;i<3;i++)SendMessageW(packaging_axis,CB_ADDSTRING,0,(LPARAM)axes[i]);
        SendMessageW(packaging_axis,CB_SETCURSEL,2,0);
        const wchar_t *labels[]={L"纸板厚度（mm）",L"端部底垫（mm）",L"装箱间隙（mm）"};
        const wchar_t *values[]={L"5",L"40",L"20"};
        for(int i=0;i<3;i++){
            pk_labels[i+1]=packaging_control(window,L"STATIC",labels[i],0,0);
            packaging_fields[i]=packaging_control(window,L"EDIT",values[i],WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,10+i);
        }
        pk_deferred_fields[0]=packaging_control(window,L"EDIT",L"待接入",WS_BORDER|ES_READONLY,156);
        pk_deferred_fields[1]=packaging_control(window,L"EDIT",L"待接入",WS_BORDER|ES_READONLY,157);
        const wchar_t *action_labels[]={L"刷新包装",L"生成模型",L"刷新模型",L"上一步",L"下一步"};
        const int action_ids[]={PK_REFRESH_PACK,PK_BUILD,PK_REFRESH_MODEL,PK_BACK,PK_NEXT};
        for(int i=0;i<5;i++)pk_preview_actions[i]=packaging_control(window,L"BUTTON",action_labels[i],WS_TABSTOP|BS_OWNERDRAW,action_ids[i]);
        pk_preview_button=packaging_control(window,L"BUTTON",L"预览包装",WS_TABSTOP|BS_OWNERDRAW,1);
        for(int i=0;i<PK_ITEM_COUNT;i++){
            pk_checks[i]=packaging_control(window,L"BUTTON",pk_names[i],BS_AUTOCHECKBOX|WS_TABSTOP,30+i);
            SendMessageW(pk_checks[i],BM_SETCHECK,i==PK_OUTER?BST_UNCHECKED:BST_CHECKED,0);
        }
        pk_tree=packaging_control(window,L"STATIC",L"包装子装配",SS_LEFT,0);
        packaging_result=packaging_control(window,L"STATIC",L"选择包装类型。",WS_VISIBLE|SS_LEFT,0);
        return 0;
    }
    case WM_SIZE:packaging_arrange();return 0;
    case WM_LBUTTONDOWN:{
        POINT point={(short)LOWORD(l),(short)HIWORD(l)};
        if(pk_page==1&&PtInRect(&pk_view_area,point)){
            pk_dragging=1;pk_drag_point=point;SetFocus(window);SetCapture(window);
            SetTimer(window,61,90,NULL);return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if(pk_dragging){
            POINT point={(short)LOWORD(l),(short)HIWORD(l)};
            InterlockedExchangeAdd(&pk_orbit_x,point.x-pk_drag_point.x);
            InterlockedExchangeAdd(&pk_orbit_y,point.y-pk_drag_point.y);
            pk_drag_point=point;
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        pk_dragging=0;return 0;
    case WM_TIMER:
        if(w==61){
            if(pk_orbit_x||pk_orbit_y)pk_queue(window,4);
            else if(!pk_dragging)KillTimer(window,61);
            return 0;
        }
        break;
    case WM_RBUTTONUP:{
        POINT point={(short)LOWORD(l),(short)HIWORD(l)};
        if(pk_page!=1||!PtInRect(&pk_view_area,point))break;
        HMENU menu=CreatePopupMenu();
        if(!menu)return 0;
        int ids[64],count=0;
        AcquireSRWLockShared(&pk_cad_lock);
        for(int i=0;i<pk_view_count;i++){
            wchar_t label[170];
            swprintf_s(label,_countof(label),L"%ls %ls  [%d]",pk_view_items[i].hidden?L"显示":L"隐藏",pk_view_items[i].name,pk_view_items[i].id);
            if(AppendMenuW(menu,MF_STRING,count+1,label))ids[count++]=pk_view_items[i].id;
        }
        ReleaseSRWLockShared(&pk_cad_lock);
        if(!count)AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"打开装配后可选择部件");
        AppendMenuW(menu,MF_SEPARATOR,0,NULL);
        AppendMenuW(menu,MF_STRING,100,L"恢复预览比例");
        ClientToScreen(window,&point);
        int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,NULL);
        DestroyMenu(menu);
        if(choice==100){pk_zoom=1;InvalidateRect(window,&pk_view_area,FALSE);}
        else if(choice>0&&choice<=count){InterlockedExchange(&pk_visibility_id,ids[choice-1]);pk_queue(window,9);}
        return 0;
    }
    case WM_MOUSEWHEEL:{
        POINT point={(short)LOWORD(l),(short)HIWORD(l)};
        ScreenToClient(window,&point);
        if(pk_page==1&&PtInRect(&pk_view_area,point)){
            pk_zoom=fmax(0.35,fmin(5,pk_zoom*pow(1.15,(short)HIWORD(w)/(double)WHEEL_DELTA)));
            InvalidateRect(window,&pk_view_area,FALSE);return 0;
        }
        if(pk_page==0){pk_config_scroll_to(window,pk_config_scroll-(short)HIWORD(w)*70/WHEEL_DELTA);return 0;}
        break;
    }
    case WM_VSCROLL:
        if((HWND)l==pk_config_scrollbar){
            SCROLLINFO info={sizeof(info),SIF_PAGE|SIF_TRACKPOS};
            GetScrollInfo(pk_config_scrollbar,SB_CTL,&info);
            int offset=pk_config_scroll;
            switch(LOWORD(w)){
            case SB_LINEUP:offset-=35;break;
            case SB_LINEDOWN:offset+=35;break;
            case SB_PAGEUP:offset-=info.nPage;break;
            case SB_PAGEDOWN:offset+=info.nPage;break;
            case SB_TOP:offset=0;break;
            case SB_BOTTOM:offset=pk_config_limit;break;
            case SB_THUMBTRACK:case SB_THUMBPOSITION:offset=info.nTrackPos;break;
            }
            pk_config_scroll_to(window,offset);return 0;
        }
        break;
    case WM_LBUTTONUP:
        if(pk_dragging){pk_dragging=0;ReleaseCapture();return 0;}
        if(pk_page==0){
            POINT point={(short)LOWORD(l),(short)HIWORD(l)};
            RECT r;GetClientRect(window,&r);
            if(PtInRect(&pk_config_next,point)){
                SendMessageW(window,WM_COMMAND,PK_PREVIEW,0);return 0;
            }
            if(point.y>=43&&point.y<r.bottom-40)
                for(int i=0;i<pk_config_count;i++){
                    const PkConfigCard *card=&pk_config_cards[i];
                    if(card->image<0||!PtInRect(&card->hit,point))continue;
                    pk_config_choice[card->group]=card->value;
                    pk_config_scroll_to(window,pk_config_scroll);
                    break;
                }
            return 0;
        }
        break;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w,CHAT_CANVAS_COLOR);
        SetBkMode((HDC)w,OPAQUE);
        SetTextColor((HDC)w,CHAT_TEXT_COLOR);
        return (LRESULT)(pk_background?pk_background:GetStockObject(WHITE_BRUSH));
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *item=(DRAWITEMSTRUCT *)l;
        UINT id=item->CtlID;
        if(id>=PK_FIRST_BOX&&id<PK_FIRST_BOX+6){
            int type=id-PK_FIRST_BOX;
            RECT r=item->rcItem,label=r;
            native_theme_fill(item->hDC,&r,CHAT_CANVAS_COLOR);
            native_theme_round(item->hDC,&r,RGB(235,241,245),type?CHAT_FRAME_COLOR:RGB(15,127,115),7);
            pk_box_icon(item->hDC,r,type);
            label.top=label.bottom-33;
            SetBkMode(item->hDC,TRANSPARENT);
            HGDIOBJ old=SelectObject(item->hDC,native_font);
            SetTextColor(item->hDC,CHAT_TEXT_COLOR);
            DrawTextW(item->hDC,pk_types[type],-1,&label,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            SelectObject(item->hDC,old);
            return TRUE;
        }
        DRAWITEMSTRUCT styled=*item;
        if((pk_page==2&&id==PK_DRAWING)|| (pk_page!=2&&id==PK_LIBRARY+pk_page)||id==PK_BUILD||id==20+pk_dimension_view)styled.CtlID=CHAT_SEND;
        native_theme_button(&styled);
        return TRUE;
    }
    case WM_COMMAND:{
        UINT id=LOWORD(w);
        if(id==PK_BACK)id=PK_LIBRARY;
        if(id==PK_DWG_BACK)id=PK_PREVIEW;
        if(id==PK_DWG_FOLDER){
            BROWSEINFOW browse={0};
            browse.hwndOwner=window;browse.lpszTitle=L"选择 E 盘上的 CAD 图纸保存文件夹";browse.ulFlags=BIF_RETURNONLYFSDIRS;
            PIDLIST_ABSOLUTE selection=SHBrowseForFolderW(&browse);
            if(selection){
                wchar_t path[MAX_PATH];
                if(SHGetPathFromIDListW(selection,path)&&storage_directory_on_drive(path,L'E')){
                    wcscpy_s(pk_save_folder,_countof(pk_save_folder),path);pk_drawing_labels();
                }else SetWindowTextW(packaging_result,L"请选择 E 盘上的有效文件夹。");
                CoTaskMemFree(selection);
            }
            return 0;
        }
        if(id==PK_DWG_OPEN){
            const wchar_t *folder=pk_destination();
            if(!folder[0]||!storage_directory_on_drive(folder,L'E')||
               (INT_PTR)ShellExecuteW(window,L"open",folder,NULL,NULL,SW_SHOWNORMAL)<=32)
                SetWindowTextW(packaging_result,L"请先选定有效的保存文件夹。");
            return 0;
        }
        if(id==PK_DWG_PREV||id==PK_DWG_NEXT){pk_queue(window,id==PK_DWG_PREV?12:13);return 0;}
        if(id==PK_NEXT)id=PK_DRAWING;
        if(id==PK_DRAWING){pk_page=2;packaging_arrange();if(packaging_last)pk_queue(window,3);return 0;}
        if(id>=PK_DWG_LOAD&&id<=PK_DWG_SAVE){pk_queue(window,id==PK_DWG_LOAD?3:id==PK_DWG_SCALE?10:11);return 0;}
        if(id==PK_REFRESH_PACK||id==PK_REFRESH_MODEL){pk_queue(window,id==PK_REFRESH_PACK?1:8);return 0;}
        if(id>=30&&id<30+PK_ITEM_COUNT){
            packaging_ready=0;pk_show_tree(pk_selected());
            SetWindowTextW(packaging_result,L"包装配置已变更，请重新预览。");
            InvalidateRect(window,NULL,TRUE);return 0;
        }
        if(id>=23&&id<=25){pk_dimension_view=0;pk_queue(window,id-18);return 0;}
        if(id==20||id==21){pk_dimension_view=id-20;RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);return 0;}
        if(packaging_window&&((id>=10&&id<=12&&HIWORD(w)==EN_CHANGE)||(id==PK_AXIS&&HIWORD(w)==CBN_SELCHANGE))){
            packaging_ready=0;
            SetWindowTextW(packaging_result,L"参数已修改，请重新预览。");
            InvalidateRect(window,NULL,TRUE);
        }
        if(id==PK_LIBRARY||id==PK_PREVIEW){
            pk_page=id-PK_LIBRARY;
            if(pk_page==1&&!packaging_ready)SetWindowTextW(packaging_result,L"点击预览包装后显示装箱尺寸。");
            if(pk_page==0)SetWindowTextW(packaging_result,L"配置尺寸和组件后，点击「预览包装」核对。");
            packaging_arrange();
        }
        if(id==PK_CHAT)packaging_switch(0);
        if(id==PK_FIRST_BOX){pk_page=1;packaging_arrange();SetWindowTextW(packaging_result,L"等待读取当前模型。");}
        if(id>PK_FIRST_BOX&&id<PK_FIRST_BOX+6){
            wchar_t note[100];swprintf_s(note,_countof(note),L"%ls的 Creo 建模尚未验收。",pk_types[id-PK_FIRST_BOX]);
            SetWindowTextW(packaging_result,note);
        }
        if(id==1||id==PK_BUILD||id==PK_DRAWING){
            pk_page=1;packaging_arrange();
            pk_queue(window,id==1?1:id==PK_BUILD?2:3);
        }
        return 0;
    }
    case WM_PAINT:{
        PAINTSTRUCT paint;RECT r;
        HDC dc=BeginPaint(window,&paint);GetClientRect(window,&r);
        HDC frame=CreateCompatibleDC(dc);
        HBITMAP bitmap=frame?CreateCompatibleBitmap(dc,r.right,r.bottom):NULL;
        if(bitmap){
            HGDIOBJ old=SelectObject(frame,bitmap);
            packaging_paint(frame,r);
            BitBlt(dc,0,0,r.right,r.bottom,frame,0,0,SRCCOPY);
            SelectObject(frame,old);
            DeleteObject(bitmap);
        }else packaging_paint(dc,r);
        if(frame)DeleteDC(frame);
        EndPaint(window,&paint);return 0;
    }
    case WM_DESTROY:pk_drawing_capture(NULL);pk_drawing_model=NULL;pk_drawing_assembly=NULL;KillTimer(window,61);pk_dragging=0;pk_zoom=1;pk_orbit_x=pk_orbit_y=pk_visibility_id=0;if(pk_config_atlas)DeleteObject(pk_config_atlas);pk_config_atlas=NULL;pk_capture(NULL);if(pk_background)DeleteObject(pk_background);pk_background=NULL;packaging_window=NULL;packaging_ready=0;return 0;
    }
    return DefWindowProcW(window,message,w,l);
}

static void packaging_create(void){
    if(!native_window)return;
    if(!packaging_window){
        const wchar_t *name=L"MweaiPackagingPanel";
        HINSTANCE module=GetModuleHandleW(NULL);
        WNDCLASSW cls={0};
        cls.style=CS_HREDRAW|CS_VREDRAW;cls.lpfnWndProc=packaging_proc;cls.hInstance=module;cls.lpszClassName=name;
        cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        packaging_window=CreateWindowW(name,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,0,0,native_window,NULL,module,NULL);
        pk_page=0;pk_show_tree(pk_selected());
    }
}

static void packaging_open(void){
    packaging_create();
    if(packaging_window){
        if(pk_page==3&&clash_has_isolation){clash_queue(CLASH_TO_PACKAGING);return;}
        if(pk_page>=3)pk_page=0;
        packaging_layout();
        packaging_switch(1);
    }
}

static void pk_request_interference(void){
    if(native_busy){
        append_card(L"干涉分析",L"请等当前任务完成。");
        return;
    }
    packaging_create();
    if(!packaging_window)return;
    clash_open(packaging_window);
    if(!clash_panel)return;
    pk_page=3;
    packaging_layout();
    packaging_switch(1);
    clash_queue(CLASH_SCAN);
}
static void pk_request_dfm(void){
    if(pk_page==3&&(clash_has_isolation||clash_red_count)){clash_queue(CLASH_TO_DFM);return;}
    packaging_create();
    if(!packaging_window)return;
    dfm_open(packaging_window);
    if(!dfm_panel)return;
    pk_page=4;
    packaging_layout();
    packaging_switch(1);
}

static void pk_request_price(void){
    if(pk_page==3&&(clash_has_isolation||clash_red_count)){clash_queue(CLASH_TO_PRICE);return;}
    packaging_create();
    if(!packaging_window)return;
    price_open(packaging_window);
    if(!price_panel)return;
    pk_page=5;
    packaging_layout();
    packaging_switch(1);
}

static int pk_price_visible(void){
    return pk_page==5&&packaging_visible();
}
static int pk_dfm_visible(void){
    return pk_page==4&&packaging_visible();
}
static int pk_interference_visible(void){
    return pk_page==3&&packaging_visible();
}

static int pk_interference_before_close(void){
    if(!pk_interference_visible())return 0;
    InterlockedExchange(&clash_cleanup_requested,1);
    ShowWindow(native_window,SW_HIDE);
    clash_queue(CLASH_CLOSE);
    return 0;
}
static int packaging_visible(void){
    return packaging_window&&(GetWindowLongPtrW(packaging_window,GWL_STYLE)&WS_VISIBLE)!=0;
}
