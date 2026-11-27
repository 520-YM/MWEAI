#ifndef BWTL_NATIVE_DELIVERY_BUNDLE
#define BWTL_NATIVE_DELIVERY_BUNDLE
#include <ProWindows.h>

typedef struct {
    ProMdl root;
    ProMdlName name;
    wchar_t stamp[256],breadth[96];
    int assembly,views,sheets,flat_files,native_files,template_warnings,settings_notice,generated_views;
    ProPath template_path,drawing_leaf;
} CompanyDelivery;

typedef struct { ProMdl model; double area; int id,ambiguous; } CompanyDeliveryFace;

static ProError bwtl_delivery_face_visit(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    CompanyDeliveryFace *face=data;
    ProSrftype kind;
    ProSmtSurfType side;
    if(ProSurfaceTypeGet(surface,&kind)||kind!=PRO_SRF_PLANE||
       ProSmtSurfaceTypeGet((ProPart)face->model,surface,&side)||side!=PRO_SMT_SURF_FACE)
        return PRO_TK_NO_ERROR;
    double area;
    int id;
    ProError error=ProSurfaceAreaEval(surface,&area);
    if(!error)error=ProSurfaceIdGet(surface,&id);
    if(error)return error;
    if(!face->id||area>face->area+1e-7*fmax(face->area,1.0)){
        face->id=id;
        face->area=area;
        face->ambiguous=0;
    }else if(part_close(area,face->area))face->ambiguous=1;
    return PRO_TK_NO_ERROR;
}

static ProError bwtl_delivery_flat(ProMdl sheet,const DeliveryOutput *delivery,const wchar_t *base,
    const wchar_t *fixed,FabDxf *flat,int *surface){
    wchar_t token[40];
    *surface=0;
    if(fixed&&*fixed){
        if(wcsncmp(fixed,L"SURFACE:",8)||!part_integer(fixed+8,surface)||*surface<=0)return PRO_TK_BAD_INPUTS;
        wcscpy_s(token,_countof(token),fixed);
    }else{
        CompanyDeliveryFace face={.model=sheet};
        ProError error=ProSolidSurfaceVisit((ProSolid)sheet,bwtl_delivery_face_visit,NULL,&face);
        if(error)return error;
        if(!face.id)return PRO_TK_E_NOT_FOUND;
        if(face.ambiguous)return PRO_TK_E_AMBIGUOUS;
        *surface=face.id;
        swprintf_s(token,_countof(token),L"SURFACE:%d",face.id);
    }
    ProPath exported;
    ProError error=fab_flat_read(sheet,delivery->stage,base,token,flat,exported);
    if(!error&&(!flat->outline||flat->count<1))error=PRO_TK_INCOMPLETE;
    return error;
}

/* For both unversioned PTC templates and the conventional versioned Creo files. */
static ProError bwtl_delivery_revision(const wchar_t *base,ProPath path){
    ProPath pattern;
    if(swprintf_s(pattern,_countof(pattern),L"%ls.*",base)<0)return PRO_TK_BAD_INPUTS;
    WIN32_FIND_DATAW entry;
    HANDLE find=FindFirstFileW(pattern,&entry);
    int latest=0;
    if(find!=INVALID_HANDLE_VALUE){
        do{
            const wchar_t *dot=wcsrchr(entry.cFileName,L'.');
            int revision=0;
            if(dot&&part_integer(dot+1,&revision)&&revision>latest&&
               !(entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
               (entry.nFileSizeHigh||entry.nFileSizeLow))latest=revision;
        }while(FindNextFileW(find,&entry));
        FindClose(find);
    }
    if(latest)return swprintf_s(path,PRO_PATH_SIZE,L"%ls.%d",base,latest)<0?PRO_TK_BAD_INPUTS:PRO_TK_NO_ERROR;
    if(!dv_file_nonempty(base))return PRO_TK_E_NOT_FOUND;
    return wcscpy_s(path,PRO_PATH_SIZE,base)?PRO_TK_BAD_INPUTS:PRO_TK_NO_ERROR;
}

static ProError bwtl_delivery_template(const CompanyDelivery *package,ProPath picked,int fallback){
    ProPath configured;
    if(!fallback&&!ProConfigoptionGet(L"template_drawing",configured)&&configured[0]){
        wchar_t *kind=wcsstr(configured,L"-part-");
        if(package->assembly&&kind)memcpy(kind+1,L"assy",4*sizeof(wchar_t));
        if(!package->assembly||kind)return bwtl_delivery_revision(configured,picked);
    }
    wchar_t base[PRO_PATH_SIZE],root[MAX_PATH];
    if(!GetEnvironmentVariableW(L"PRO_DIRECTORY",root,_countof(root))){
        DWORD used=GetModuleFileNameW(NULL,root,_countof(root));
        if(!used||used>=_countof(root))return PRO_TK_E_NOT_FOUND;
        wchar_t *slash=wcsrchr(root,L'\\');
        if(!slash)return PRO_TK_E_NOT_FOUND;
        *slash=0;
    }
    static const wchar_t *const suffixes[]={L"\\templates\\a3_drawing.drw",L"\\..\\..\\templates\\a3_drawing.drw",
        L"\\..\\..\\..\\Common Files\\templates\\a3_drawing.drw"};
    for(size_t i=0;i<_countof(suffixes);i++){
        ProPath combined;
        if(swprintf_s(combined,_countof(combined),L"%ls%ls",root,suffixes[i])<0||
           !GetFullPathNameW(combined,_countof(base),base,NULL))continue;
        if(!bwtl_delivery_revision(base,picked))return PRO_TK_NO_ERROR;
    }
    return PRO_TK_E_NOT_FOUND;
}

static const wchar_t *bwtl_delivery_leaf(const wchar_t *path){
    const wchar_t *name=wcsrchr(path,L'\\');
    return name?name+1:path;
}

static ProError bwtl_delivery_count_views(ProDrawing drawing,ProMdl model,int *attached){
    *attached=0;
    ProView *views=NULL;
    ProError error=ProDrawingViewsCollect(drawing,&views);
    if(error==PRO_TK_E_NOT_FOUND)return PRO_TK_NO_ERROR;
    int count=0;
    if(!error)error=ProArraySizeGet((ProArray)views,&count);
    for(int i=0;!error&&i<count;i++){
        ProSolid actual=NULL;
        if(!ProDrawingViewSolidGet(drawing,views[i],&actual)&&actual==(ProSolid)model)(*attached)++;
    }
    if(views)ProArrayFree((ProArray *)&views);
    return error;
}

static ProError bwtl_delivery_view_size(ProDrawing drawing,ProMdl model,double *width,double *height,double *scale){
    ProPlotPaperSize sheet;
    ProOutline bounds;
    ProUnititem drawing_units,mm;
    ProUnitConversion conversion;
    double model_mm=0;
    ProError error=ProDrawingSheetSizeGet(drawing,1,&sheet,width,height);
    if(!error)error=ProDrawingSheetUnitsGet(drawing,1,&drawing_units);
    if(!error)error=ProUnitInit(model,L"mm",&mm);
    if(!error)error=ProUnitConversionCalculate(&drawing_units,&mm,&conversion);
    if(!error)error=qa_mm_per_unit(model,&model_mm);
    if(!error)error=ProSolidOutlineGet((ProSolid)model,bounds);
    if(error)return error;
    const double span=fmax(bounds[1][0]-bounds[0][0],
        fmax(bounds[1][1]-bounds[0][1],bounds[1][2]-bounds[0][2]))*model_mm;
    if(!isfinite(span)||span<=0||conversion.scale<=0||conversion.offset!=0)return PRO_TK_BAD_CONTEXT;
    const double available=fmin(*width,*height)*conversion.scale*0.28;
    const double target=available/span;
    const double conventional[]={1,0.5,0.25,0.2,0.1,0.05,0.02,0.01};
    *scale=target;
    for(size_t i=0;i<_countof(conventional);i++)if(conventional[i]<=target){
        *scale=conventional[i];
        break;
    }
    return PRO_TK_NO_ERROR;
}

static ProError bwtl_delivery_add_views(ProDrawing drawing,ProMdl model,int *attached){
    double width,height,scale;
    ProError error=bwtl_delivery_view_size(drawing,model,&width,&height,&scale);
    if(error)return error;
    ProName size;
    ProMatrix screen_to_sheet={{0}};
    error=ProDrawingSheetTrfGet(drawing,1,size,screen_to_sheet);
    const double determinant=screen_to_sheet[0][0]*screen_to_sheet[1][1]-
        screen_to_sheet[0][1]*screen_to_sheet[1][0];
    if(!error&&(!isfinite(determinant)||fabs(determinant)<1e-12))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProDrawingSolidAdd(drawing,(ProSolid)model);
    if(error==PRO_TK_E_IN_USE)error=PRO_TK_NO_ERROR;
    ProMatrix axes[]={
        {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}},
        {{1,0,0,0},{0,0,-1,0},{0,1,0,0},{0,0,0,1}},
        {{0,0,-1,0},{0,1,0,0},{1,0,0,0},{0,0,0,1}}
    };
    const double locations[][2]={{0.24,0.67},{0.24,0.35},{0.55,0.67}};
    for(int i=0;!error&&i<3;i++){
        /* Creo supplies the logical screen-to-sheet transform. The view
           constructor takes a logical screen point on that same sheet. */
        const double x=width*locations[i][0]-screen_to_sheet[3][0];
        const double y=height*locations[i][1]-screen_to_sheet[3][1];
        ProPoint3d screen={
            (x*screen_to_sheet[1][1]-y*screen_to_sheet[1][0])/determinant,
            (y*screen_to_sheet[0][0]-x*screen_to_sheet[0][1])/determinant,
            0};
        ProView view;
        if(!isfinite(screen[0])||!isfinite(screen[1]))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ProDrawingGeneralviewCreate(drawing,(ProSolid)model,1,PRO_B_FALSE,screen,scale,axes[i],&view);
        ProPoint3d bounds[2];
        if(!error)error=ProDrawingViewOutlineGet(drawing,view,bounds);
        for(int corner=0;!error&&corner<4;corner++){
            ProPoint3d logical={bounds[corner&1][0],bounds[(corner>>1)&1][1],0},paper;
            error=ProPntTrfEval(logical,screen_to_sheet,paper);
            if(!error&&(!isfinite(paper[0])||!isfinite(paper[1])||
               paper[0]<0||paper[0]>width||paper[1]<0||paper[1]>height))error=PRO_TK_OUT_OF_RANGE;
        }
    }
    if(!error)error=bwtl_delivery_count_views(drawing,model,attached);
    return error;
}

static ProError bwtl_delivery_include_formats(ProDrawing drawing,int sheets,const DeliveryOutput *work){
    ProPath folder;
    ProError error=ProConfigoptionGet(L"pro_format_dir",folder);
    if(error)return error;
    for(int i=1;i<=sheets;i++){
        ProName format;
        error=ProDrawingFormatGet(drawing,i,format);
        if(error==PRO_TK_NOT_EXIST||error==PRO_TK_E_NOT_FOUND)continue;
        if(error)return error;
        wchar_t *extension=wcsrchr(format,L'.');
        if(extension&&!_wcsicmp(extension,L".frm"))*extension=0;
        if(!dv_safe_leaf(format))return PRO_TK_BAD_CONTEXT;
        ProPath requested,original,target;
        if(swprintf_s(requested,_countof(requested),L"%ls\\%ls.frm",folder,format)<0)return PRO_TK_BAD_INPUTS;
        error=bwtl_delivery_revision(requested,original);
        if(!error)error=fab_leaf_path(work->stage,bwtl_delivery_leaf(original),L"",target);
        if(!error&&GetFileAttributesW(target)==INVALID_FILE_ATTRIBUTES)error=smy_copy_checked(original,target);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

static ProError bwtl_delivery_clean_intermediates(const DeliveryOutput *work,int *settings_notice){
    ProPath pattern,path;
    if(swprintf_s(pattern,_countof(pattern),L"%ls\\bwtl_flat_*.prt.*",work->stage)<0)
        return PRO_TK_BAD_INPUTS;
    WIN32_FIND_DATAW entry;
    HANDLE find=FindFirstFileW(pattern,&entry);
    ProError error=PRO_TK_NO_ERROR;
    if(find!=INVALID_HANDLE_VALUE){
        do{
            const wchar_t *end=wcsrchr(entry.cFileName,L'.');
            int version=0;
            if(!end||!part_integer(end+1,&version)||version<1||
               entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||
               fab_leaf_path(work->stage,entry.cFileName,L"",path)||!DeleteFileW(path)){
                error=PRO_TK_CANT_WRITE;
                break;
            }
        }while(FindNextFileW(find,&entry));
        FindClose(find);
    }
    if(error)return error;
    error=fab_leaf_path(work->stage,L"datafile_errors.txt",L"",path);
    if(error)return error;
    *settings_notice=dv_file_nonempty(path);
    if(*settings_notice&&!DeleteFileW(path))return PRO_TK_CANT_WRITE;
    return PRO_TK_NO_ERROR;
}


static ProError bwtl_delivery_create_drawing(CompanyDelivery *package,ProMdl model,const wchar_t *name,
    ProDrawing *drawing,PartText *out){
    ProMdlnameShortdata solid={0};
    ProMdl template_model=NULL;
    ProMdlName template_name,created;
    ProError error=ProMdlMdlnameGet(model,solid.name);
    if(error)return error;
    wcscpy_s(solid.type,_countof(solid.type),package->assembly?L"asm":L"prt");
    wcscpy_s(created,_countof(created),name);
    error=bwtl_delivery_template(package,package->template_path,0);
    if(!error)error=ProMdlFiletypeLoad(package->template_path,PRO_MDLFILE_DRAWING,PRO_B_FALSE,&template_model);
    int fallback=0;
    if(error){
        fallback=1;
        error=bwtl_delivery_template(package,package->template_path,1);
        if(!error)error=ProMdlFiletypeLoad(package->template_path,PRO_MDLFILE_DRAWING,PRO_B_FALSE,&template_model);
    }
    trace("bundle drawing template native load",error);
    if(error)return error;
    error=ProMdlMdlnameGet(template_model,template_name);
    if(error)return error;
    ProDwgcreateErrs reports=NULL;
    error=ProDrawingFromTemplateCreate(created,template_name,&solid,0,drawing,&reports);
    if(reports){
        ProDwgcreateErrsCountGet(reports,&package->template_warnings);
        ProDwgcreateErrsFree(&reports);
    }
    trace("bundle drawing template native create",error);
    if(error==PRO_TK_DWGCREATE_ERRORS&&*drawing)error=PRO_TK_NO_ERROR;
    if(error||!*drawing)return error?error:PRO_TK_INCOMPLETE;
    error=bwtl_delivery_count_views(*drawing,model,&package->views);
    if(!error)error=ProDrawingSheetsCount(*drawing,&package->sheets);
    if(error||package->sheets<1)return error?error:PRO_TK_INCOMPLETE;
    wcscpy_s(package->breadth,_countof(package->breadth),fallback?L"Creo 通用 A3 模板":L"本机配置的 A3 工程图模板");
    return PRO_TK_NO_ERROR;
}

static ProError bwtl_delivery_drawing(CompanyDelivery *package,ProMdl model,const DeliveryOutput *work,PartText *out){
    wchar_t name[PRO_NAME_SIZE];
    ProPath file,base,found;
    if(swprintf_s(name,_countof(name),L"%.22ls_DLV",package->name)<0)return PRO_TK_BAD_INPUTS;
    ProMdl existing=NULL;
    if(!ProMdlnameInit(name,PRO_MDL_DRAWING,&existing))return PRO_TK_E_FOUND;
    int original_window=-1,window=-1;
    ProError error=ProWindowCurrentGet(&original_window);
    ProDrawing drawing=NULL;
    if(!error)error=bwtl_delivery_create_drawing(package,model,name,&drawing,out);
    if(!error)error=ProObjectwindowMdlnameCreate(name,PRO_DRAWING,&window);
    if(!error)error=ProWindowCurrentSet(window);
    if(!error)error=ProMdlDisplay((ProMdl)drawing);
    trace("bundle drawing native display",error);
    if(!error&&package->views==0){
        error=bwtl_delivery_add_views(drawing,model,&package->views);
        if(!error)package->generated_views=1;
        trace("bundle drawing three associated native views",error);
    }
    if(!error&&package->views<3)error=PRO_TK_INCOMPLETE;
    if(!error)error=bwtl_delivery_include_formats(drawing,package->sheets,work);
    trace("bundle drawing associated formats",error);
    if(!error)error=ProMdlSave((ProMdl)drawing);
    if(!error)error=fab_leaf_path(work->stage,name,L".drw",base);
    if(!error)error=bwtl_delivery_revision(base,found);
    if(!error)wcscpy_s(package->drawing_leaf,_countof(package->drawing_leaf),bwtl_delivery_leaf(found));
    if(!error)error=fab_leaf_path(work->stage,L"drawing",L".pdf",file);
    if(!error)error=company_drawing_pdf_write((ProMdl)drawing,file);
    trace("bundle drawing PDF export",error);
    if(!error&&!company_drawing_pdf_valid(file))error=PRO_TK_INCOMPLETE;
    if(window>=0){
        ProError restore=ProWindowCurrentSet(original_window);
        ProError closed=ProWindowDelete(window);
        if(!error&&(restore||closed))error=restore?restore:closed;
    }
    if(drawing){
        ProError erased=ProMdlErase((ProMdl)drawing);
        if(!error&&erased)error=erased;
    }
    return error;
}

static ProError bwtl_delivery_manufacturing(ProMdl sheet,const DeliveryOutput *work,const wchar_t *base,
    const wchar_t *fixed,FILE *file,int qty,int *found,PartText *out){
    AxBomModel item={.model=sheet,.type=PRO_MDL_PART,.qty=qty};
    ProError error=ProMdlMdlnameGet(sheet,item.name);
    if(error)return error;
    ax_bom_read(&item);
    FabBends bends={.model=sheet};
    error=fab_thickness(sheet,&bends.mm,&bends.thickness);
    if(!error)error=ProSolidSurfaceVisit((ProSolid)sheet,fab_bend_visit,NULL,&bends);
    FabDxf *flat=calloc(1,sizeof(*flat));
    if(!flat)return PRO_TK_OUT_OF_MEMORY;
    int face=0;
    if(!error)error=bwtl_delivery_flat(sheet,work,base,fixed,flat,&face);
    if(error==PRO_TK_E_AMBIGUOUS)part_text_add(out,L"%ls 的展开固定面有并列选项。请指定固定面 SURFACE:编号。\n",item.name);
    if(!error&&flat->bends!=bends.count){
        part_text_add(out,L"%ls 的展开折弯线数 %d 与模型读回 %d 不一致。\n",item.name,flat->bends,bends.count);
        error=PRO_TK_INCOMPLETE;
    }
    if(!error){
        wchar_t amount[32],t[32],w[32],h[32],lines[32],holes[32],bend_count[32],ref[32];
        swprintf_s(amount,_countof(amount),L"%d",qty);
        swprintf_s(t,_countof(t),L"%.8g",bends.thickness);
        swprintf_s(w,_countof(w),L"%.8g",flat->high[0]-flat->low[0]);
        swprintf_s(h,_countof(h),L"%.8g",flat->high[1]-flat->low[1]);
        swprintf_s(lines,_countof(lines),L"%d",flat->outline);
        swprintf_s(holes,_countof(holes),L"%d",flat->holes);
        swprintf_s(bend_count,_countof(bend_count),L"%d",bends.count);
        swprintf_s(ref,_countof(ref),L"SURFACE:%d",face);
        const wchar_t *row[]={item.name,amount,item.value[AB_DRAWING],item.value[AB_REV],
            ax_bom_known(item.value[AB_MATERIAL]),t,w,h,lines,holes,bend_count,ref};
        if(!ax_bom_csv(file,row,_countof(row)))error=PRO_TK_CANT_WRITE;
    }
    free(flat);
    if(!error)(*found)++;
    return error;
}

static ProError bwtl_delivery_report(CompanyDelivery *package,const DeliveryOutput *work,ProMdl model,
    const AxBomData *data,const wchar_t *fixed,PartText *out){
    FILE *process=NULL;
    ProError error=fab_csv_open(work->stage,L"manufacturing.csv",&process);
    if(!error&&fwprintf(process,L"模型,数量,图号,版本,材料,板厚_mm,展开X_mm,展开Y_mm,轮廓图元,孔槽图元,折弯数,固定面\n")<0)
        error=PRO_TK_CANT_WRITE;
    if(!error&&!package->assembly)error=bwtl_delivery_manufacturing(model,work,L"flat",fixed,process,1,
        &package->flat_files,out);
    for(int i=1;!error&&package->assembly&&i<data->count;i++){
        const AxBomModel *item=&data->models[i];
        if(item->type!=PRO_MDL_PART)continue;
        ProMdlsubtype subtype;
        error=ProMdlSubtypeGet(item->model,&subtype);
        if(error||subtype!=PROMDLSTYPE_PART_SHEETMETAL)continue;
        wchar_t flat_name[PRO_NAME_SIZE+12];
        if(swprintf_s(flat_name,_countof(flat_name),L"flat-%ls",item->name)<0){error=PRO_TK_BAD_INPUTS;break;}
        error=bwtl_delivery_manufacturing(item->model,work,flat_name,NULL,process,item->qty,&package->flat_files,out);
    }
    return fab_close(process,error);
}

static ProError bwtl_delivery_bom_part(ProMdl part,const DeliveryOutput *work){
    AxBomModel model={.model=part,.type=PRO_MDL_PART,.qty=1};
    FILE *file=NULL;
    ProError error=ProMdlMdlnameGet(part,model.name);
    if(!error)error=ax_bom_open(work->stage,L"bom.csv",&file);
    if(!error){
        ax_bom_read(&model);
        const wchar_t *row[]={model.name,L"1",model.value[AB_CODE],model.value[AB_DRAWING],model.value[AB_REV],
            ax_bom_known(model.value[AB_MATERIAL]),model.value[AB_SPEC]};
        if(fwprintf(file,L"模型,数量,料号,图号,版本,材料,规格\n")<0||!ax_bom_csv(file,row,_countof(row)))error=PRO_TK_CANT_WRITE;
    }
    return fab_close(file,error);
}

static ProError bwtl_delivery_manifest(const CompanyDelivery *package,const DeliveryOutput *work){
    FILE *file=NULL;
    ProError error=fab_csv_open(work->stage,L"manifest.tsv",&file);
    ProPath pattern,path;
    int count=0;
    if(error)return error;
    if(fwprintf(file,L"BWTL DELIVERY v1\nMODEL\t%ls\t%ls\nTEMPLATE\t%ls\t%ls\nVIEWS\t%d\tSHEETS\t%d\t模板诊断\t%d\t设置警告\t%d\t自动创建视图\t%d\n"
        L"目录文件\t字节\tSHA256\n",package->name,package->stamp,package->breadth,
        bwtl_delivery_leaf(package->template_path),package->views,package->sheets,package->template_warnings,package->settings_notice,package->generated_views)<0||
       swprintf_s(pattern,_countof(pattern),L"%ls\\*",work->stage)<0)error=PRO_TK_CANT_WRITE;
    WIN32_FIND_DATAW entry;
    HANDLE find=error?INVALID_HANDLE_VALUE:FindFirstFileW(pattern,&entry);
    if(find==INVALID_HANDLE_VALUE&&!error)error=PRO_TK_CANT_OPEN;
    if(find!=INVALID_HANDLE_VALUE){
        do{
            const wchar_t *leaf=entry.cFileName;
            if(!wcscmp(leaf,L".")||!wcscmp(leaf,L"..")||!wcscmp(leaf,L"manifest.tsv"))continue;
            wchar_t hash[65];
            ULONGLONG bytes;
            if(entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||
               !smy_package_leaf(leaf)||fab_leaf_path(work->stage,leaf,L"",path)||
               !dv_sha256(path,hash,&bytes)||fwprintf(file,L"%ls\t%llu\t%ls\n",leaf,bytes,hash)<0){
                error=PRO_TK_INCOMPLETE;
                break;
            }
            count++;
        }while(FindNextFileW(find,&entry));
        if(!error&&GetLastError()!=ERROR_NO_MORE_FILES)error=PRO_TK_CANT_OPEN;
        FindClose(find);
    }
    if(!error&&count<package->native_files+package->flat_files+4)error=PRO_TK_INCOMPLETE;
    return fab_close(file,error);
}

static ProError bwtl_delivery_bundle(ProMdl model,const wchar_t *folder,const wchar_t *fixed,PartText *out){
    CompanyDelivery package={.root=model};
    DeliveryOutput work={0};
    ProMdlType type;
    ProMdlsubtype subtype;
    AxBomData bom={0};
    PartCommand command={0};
    ProMdl original=NULL,drawing_model=model,part_copy=NULL;
    ProPath native_leaf,step,saved;
    wchar_t stamp[256];
    int written=0,missing=0;
    const wchar_t *phase=L"核对当前可交付的模型";
    ProError error=ProMdlCurrentGet(&original);
    if(!error&&(original!=model||!model||!storage_prepare_output()||dv_recovery_uncertain))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlTypeGet(model,&type);
    if(!error)error=ProMdlSubtypeGet(model,&subtype);
    if(!error)error=ProMdlMdlnameGet(model,package.name);
    if(!error)error=smy_stamp(model,package.stamp);
    if(!error)error=part_health(model);
    package.assembly=!error&&type==PRO_MDL_ASSEMBLY;
    if(!error&&!package.assembly&&(type!=PRO_MDL_PART||subtype!=PROMDLSTYPE_PART_SHEETMETAL))error=PRO_TK_INVALID_TYPE;
    if(!error&&package.assembly&&(fixed&&*fixed))error=PRO_TK_BAD_INPUTS;
    if(!error&&package.assembly)error=ax_master(model);
    if(!error&&package.assembly)error=ax_bom_collect(model,&bom,0);
    if(!error&&!package.assembly)error=dv_no_external_dependencies(model);
    if(!error&&!package.assembly)error=qa_single_body(model,NULL);
    if(error)goto done;

    phase=L"准备工作目录中的独立交付子目录";
    if((error=dv_stage_begin(folder,&work))||(error=dv_stage_enter(&work)))goto done;
    phase=L"写入并核对原生模型及依赖文件";
    if(package.assembly){
        error=ax_pack_dependencies(&bom,work.stage,&package.native_files,&missing,out);
        if(!error&&missing){
            part_text_add(out,L"装配或 %d 个依赖文件未保存或来源待核对。请保存需要交付的装配和组件之后再生成。\n",missing);
            error=PRO_TK_INCOMPLETE;
        }
    }else{
        error=dv_native_copy(model,&work,native_leaf);
        if(!error)error=fab_leaf_path(work.stage,native_leaf,L"",saved);
        if(!error)error=ProMdlFiletypeLoad(saved,PRO_MDLFILE_PART,PRO_B_FALSE,&part_copy);
        if(!error){drawing_model=part_copy;package.native_files=1;}
    }
    if(error)goto done;

    phase=L"生成 BOM 和制造数据";
    if(package.assembly){
        error=ax_bom_csv_output(AX_B_TREE,&bom,NULL,&command,work.stage,&written);
        if(!error)error=ax_bom_csv_output(AX_B_BUY,&bom,NULL,&command,work.stage,&written);
    }else error=bwtl_delivery_bom_part(model,&work);
    if(!error)error=bwtl_delivery_report(&package,&work,model,&bom,fixed,out);
    if(error)goto done;
    phase=L"导出原生 STEP 几何";
    error=dv_export_file(drawing_model,work.stage,PRO_INTF_EXPORT_STEP,step);
    if(error)goto done;
    phase=L"创建关联工程图和 PDF";
    error=bwtl_delivery_drawing(&package,drawing_model,&work,out);
    if(error)goto done;
    phase=L"核对模型版本和交付清单";
    error=smy_stamp(model,stamp);
    if(!error&&wcscmp(stamp,package.stamp))error=PRO_TK_BAD_CONTEXT;
    if(!error&&(ProMdlCurrentGet(&original)||original!=model))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=bwtl_delivery_clean_intermediates(&work,&package.settings_notice);
    if(!error)error=bwtl_delivery_manifest(&package,&work);
done:
    if(part_copy){
        ProError erase=ProMdlErase(part_copy);
        if(!error&&erase)error=erase;
    }
    ax_bom_clear(&bom);
    error=dv_stage_finish(&work,error,out);
    if(error)part_text_add(out,L"交付包未生成，卡在「%ls」（Creo %d）。\n",phase,error);
    else part_text_add(out,L"交付包已写入 %ls。\n%ls创建 %d 个模型关联视图，已输出原生工程图、PDF、%d 个原生模型文件、STEP、BOM、制造表和 %d 份展开 DXF。\n"
        L"每份文件已记录字节数及 SHA-256。工程图供校对，标题栏、尺寸公差和签审需按公司要求核定。制造表采用实测数据。%ls",work.target,
        package.breadth,package.views,package.native_files,package.flat_files,
        package.settings_notice?L"本机 Creo 忽略了工程图设置中的过时选项，已在清单记录。":L"");
    return error;
}

static int bwtl_delivery_chat(const wchar_t *text){
    if(smart_analysis_request(text))return 0;
    const wchar_t *p=model_request_action_start(text);
    if(!assembly_take(&p,L"给当前模型生成交付包")&&!assembly_take(&p,L"给当前零件生成交付包")&&
       !assembly_take(&p,L"给当前装配生成交付包")&&!assembly_take(&p,L"生成交付包"))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    wchar_t directory[PRO_NAME_SIZE]=L"",face[40]=L"";
    p+=wcsspn(p,L" ，,");
    if(p[wcsspn(p,L" \t。.!！")]&&wcsncmp(p,L"固定面",3)){
        assembly_take(&p,L"包名");
        p+=wcsspn(p,L" ：:");
        size_t chars=wcscspn(p,L" \t，,。！!");
        if(!chars||chars>=_countof(directory))return 0;
        wcsncpy_s(directory,_countof(directory),p,chars);
        p+=chars;
    }
    p+=wcsspn(p,L" \t，,");
    if(assembly_take(&p,L"固定面")){
        p+=wcsspn(p,L" ：:");
        size_t chars=wcscspn(p,L" \t，,。！!");
        if(!chars||chars>=_countof(face))return 0;
        wcsncpy_s(face,_countof(face),p,chars);
        p+=chars;
    }
    p+=wcsspn(p,L" \t。.!！");
    if(*p)return 0;
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    ProMdl model=NULL;
    wchar_t reply[4500]=L"";
    PartText out={reply,_countof(reply),0};
    ProError error=ProMdlCurrentGet(&model);
    if(!error&&!directory[0]){
        ProMdlName model_name;
        error=ProMdlMdlnameGet(model,model_name);
        if(!error&&swprintf_s(directory,_countof(directory),L"%.41ls_delivery",model_name)<0)error=PRO_TK_BAD_INPUTS;
    }
    if(!error)error=bwtl_delivery_bundle(model,directory,face,&out);
    if(error&&!reply[0])part_text_add(&out,L"请打开要交付的钣金零件或已保存的装配，并切换到数据盘项目目录（Creo %d）。",error);
    smart_record_tool(reply);
    smart_reply(reply);
    return 1;
}

#endif
