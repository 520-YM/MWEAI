/* Delivery rows are snapshots from the same active assembly and native audit. */
typedef struct {
    const wchar_t *panel,*group,*spec;
    int panel_id,number,bore_id,face_id,component_id,status;
    double center[3],world[3],diameter,depth;
} PressfitDeliveryRow;

enum { PRESSFIT_DELIVERY_UNASSIGNED=0,PRESSFIT_DELIVERY_INSTALLED=1,
    PRESSFIT_DELIVERY_MISSING=2,PRESSFIT_DELIVERY_REVIEW=3 };
#define PRESSFIT_DELIVERY_MAX 1024
static LONG pressfit_delivery_serial;

static const wchar_t *pressfit_delivery_text(const wchar_t *value){
    return value?value:L"";
}

static const wchar_t *pressfit_delivery_status(int status){
    static const wchar_t *const labels[]={L"未分组",L"已装并核对",L"待补装",L"待复核"};
    return status>=0&&status<_countof(labels)?labels[status]:labels[PRESSFIT_DELIVERY_REVIEW];
}

static int pressfit_delivery_same_spec(const wchar_t *a,const wchar_t *b){
    PressfitSelector first={0},second={0};
    return pressfit_selector(a,&first)&&pressfit_selector(b,&second)&&
        !_wcsicmp(first.instance_key,second.instance_key)&&
        (!_wcsicmp(first.generic_key,second.generic_key)||!first.generic_key[0]||!second.generic_key[0]);
}

/* The CSV is intended for Excel as well as plain-text tools. Prefix ambiguous
   formula starters so their original group/specification names are data. */
static void pressfit_delivery_cell(FILE *file,const wchar_t *value){
    const wchar_t *s=pressfit_delivery_text(value);
    const wchar_t *start=s;
    while(iswspace(*start))start++;
    fputwc(L'"',file);
    if(*start&&wcschr(L"=+-@",*start))fputwc(L'\'',file);
    for(;*s;s++){
        if(*s==L'"')fputwc(L'"',file);
        fputwc(*s==L'\r'||*s==L'\n'?L' ':*s,file);
    }
    fputwc(L'"',file);
}

static int pressfit_delivery_close(FILE *file){
    const int valid=!ferror(file);
    return fclose(file)==0&&valid;
}

static int pressfit_delivery_identifier(const wchar_t *directory,const wchar_t *suffix,wchar_t path[MAX_PATH]){
    return swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,suffix)>=0;
}

static ProError pressfit_delivery_directory(ProMdl assembly,wchar_t directory[MAX_PATH]){
    ProError error=default_part_active(assembly,-1,NULL);
    ProMdlName name;
    ProPath work;
    wchar_t actual[MAX_PATH];
    if(!error)error=ProMdlMdlnameGet(assembly,name);
    if(!error)error=ProDirectoryCurrentGet(work);
    if(error)return error;
    if(!dv_safe_leaf(name)||!or_real_path(work,1,actual)||
       !storage_directory_on_drive(actual,L'E')||!storage_output_directory(actual))
        return PRO_TK_BAD_CONTEXT;
    SYSTEMTIME now;
    GetLocalTime(&now);
    if(swprintf_s(directory,MAX_PATH,L"%ls\\%ls-压铆交付-%04d%02d%02d-%02d%02d%02d-%lu-%ld",
        actual,name,now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),
        InterlockedIncrement(&pressfit_delivery_serial))<0)return PRO_TK_OUT_OF_RANGE;
    return CreateDirectoryW(directory,NULL)?PRO_TK_NO_ERROR:PRO_TK_CANT_WRITE;
}

static ProError pressfit_delivery_tables(const PressfitDeliveryRow *rows,int count,
    const wchar_t *directory,PartText *out){
    wchar_t hole_path[MAX_PATH],bom_path[MAX_PATH];
    if(!pressfit_delivery_identifier(directory,L"安装孔位表.csv",hole_path)||
       !pressfit_delivery_identifier(directory,L"紧固件BOM.csv",bom_path))return PRO_TK_OUT_OF_RANGE;
    FILE *holes=_wfopen(hole_path,L"wtx, ccs=UTF-8");
    if(!holes)return PRO_TK_CANT_WRITE;
    FILE *bom=_wfopen(bom_path,L"wtx, ccs=UTF-8");
    if(!bom){
        fclose(holes);
        DeleteFileW(hole_path);
        return PRO_TK_CANT_WRITE;
    }
    fwprintf(holes,L"钣金件,板件实例ID,孔号,X(mm),Y(mm),Z(mm),孔径(mm),孔深(mm),孔壁ID,安装面ID,组名,规格,紧固件实例ID,状态\n");
    fwprintf(bom,L"规格,所需数量,已验收数量,待处理数量\n");
    unsigned char seen[PRESSFIT_DELIVERY_MAX]={0};
    int assigned=0,installed=0,bom_lines=0;
    for(int i=0;i<count;i++){
        const PressfitDeliveryRow *row=&rows[i];
        pressfit_delivery_cell(holes,row->panel);
        fwprintf(holes,L",%d,%d,%.8g,%.8g,%.8g,%.8g,%.8g,%d,%d,",row->panel_id,row->number,
            row->center[0],row->center[1],row->center[2],row->diameter,row->depth,row->bore_id,row->face_id);
        pressfit_delivery_cell(holes,row->group);
        fputwc(L',',holes);
        pressfit_delivery_cell(holes,row->spec);
        fputwc(L',',holes);
        if(row->component_id>0)fwprintf(holes,L"%d",row->component_id);
        fputwc(L',',holes);
        pressfit_delivery_cell(holes,pressfit_delivery_status(row->status));
        fputwc(L'\n',holes);
        const wchar_t *spec=pressfit_delivery_text(row->spec);
        if(!*spec)continue;
        assigned++;
        if(row->status==PRESSFIT_DELIVERY_INSTALLED)installed++;
        if(seen[i])continue;
        int needed=0,verified=0;
        for(int j=i;j<count;j++){
            const wchar_t *other=pressfit_delivery_text(rows[j].spec);
            if(!_wcsicmp(spec,other)||(*other&&pressfit_delivery_same_spec(spec,other))){
                seen[j]=1;
                needed++;
                if(rows[j].status==PRESSFIT_DELIVERY_INSTALLED)verified++;
            }
        }
        pressfit_delivery_cell(bom,spec);
        fwprintf(bom,L",%d,%d,%d\n",needed,verified,needed-verified);
        bom_lines++;
    }
    const int holes_ok=pressfit_delivery_close(holes);
    const int bom_ok=pressfit_delivery_close(bom);
    if(!holes_ok||!bom_ok){
        DeleteFileW(hole_path);
        DeleteFileW(bom_path);
        return PRO_TK_CANT_WRITE;
    }
    part_text_add(out,L"已导出 %d 个孔位、%d 种分组规格。分组共 %d 处，已装并核对 %d 处。\n安装孔位表 %ls\n紧固件 BOM %ls\n",
        count,bom_lines,assigned,installed,hole_path,bom_path);
    return PRO_TK_NO_ERROR;
}

#include "pressfit_workspace_inspection_image.inc"
#include "pressfit_workspace_pdf.inc"

static ProError pressfit_delivery_export(ProMdl assembly,const PressfitDeliveryRow *rows,int count,int image,PartText *out){
    if(!assembly||!rows||count<1||count>PRESSFIT_DELIVERY_MAX)return PRO_TK_BAD_INPUTS;
    wchar_t directory[MAX_PATH];
    ProError error=pressfit_delivery_directory(assembly,directory);
    if(!error){
        if(image)error=pressfit_delivery_image(assembly,rows,count,directory,out);
        else error=pressfit_delivery_tables(rows,count,directory,out);
        if(error)RemoveDirectoryW(directory);
    }
    if(error)part_text_add(out,L"压铆交付未完成。请核对当前装配、Creo 视图和可写的 E 盘项目目录（Creo %d）。\n",error);
    return error;
}