enum { DFM_SCAN=30,DFM_LOCATE,DFM_EXPORT };
enum { DFM_UPDATE=WM_APP+316,DFM_FIND=751,DFM_PARTS,DFM_GROUP,DFM_RESULTS,DFM_RUN,DFM_SAVE,DFM_BACK,DFM_GO,DFM_RULE,DFM_STAGE=770,DFM_BADGE=780 };
enum { DFM_BLOCK,DFM_RISK,DFM_PENDING,DFM_PASS };
static const wchar_t *dfm_states[]={L"阻塞",L"风险",L"待确认",L"通过"};
static const COLORREF dfm_status_background[]={RGB(255,219,217),RGB(255,235,195),RGB(232,235,239),RGB(199,239,232)};
static const COLORREF dfm_status_ink[]={RGB(147,29,31),RGB(125,79,12),RGB(72,79,91),RGB(11,100,86)};
static const wchar_t *dfm_groups[]={L"全部工序",L"下料与展开",L"折弯",L"连接与表面",L"装配与检验"};
typedef struct {ProMdl model;ProFamilyMdlName name;int enabled;} DfmPart;
typedef struct {
    int part,group,status,feature;
    const wchar_t *code,*check,*advice;
    wchar_t basis[180];
    wchar_t value[80];
} DfmRow;
static DfmPart dfm_parts[AX_MAX_OCCURRENCES];
static DfmRow dfm_rows[12000];
static int dfm_part_count,dfm_row_count,dfm_limited,dfm_updating,dfm_selected=-1,dfm_window=-1;
static LONG dfm_pending;
static ULONGLONG dfm_queued_at;
static ProMdl dfm_root;
static ProMdlName dfm_name;
static HWND dfm_panel,dfm_title,dfm_filter,dfm_find,dfm_part_list,dfm_group_title,dfm_results;
static HWND dfm_group_buttons[5],dfm_badges[4];
static int dfm_group_index;
static HWND dfm_detail_title,dfm_detail,dfm_status,dfm_buttons[5];
static HBRUSH dfm_brush;
static void dfm_add(int part,int group,int status,int feature,const wchar_t *code,const wchar_t *check,
                    const wchar_t *value,const wchar_t *basis,const wchar_t *advice){
    if(dfm_row_count==_countof(dfm_rows)){dfm_limited=1;return;}
    DfmRow *r=&dfm_rows[dfm_row_count++];
    *r=(DfmRow){.part=part,.group=group,.status=status,.feature=feature,.code=code,.check=check,.advice=advice};
    wcsncpy_s(r->basis,_countof(r->basis),basis,_TRUNCATE);
    wcsncpy_s(r->value,_countof(r->value),value,_TRUNCATE);
}

static ProError dfm_find_flat(ProFeature *feature,ProError status,ProAppData data){
    if(status)return status;
    ProFeattype type;
    ProFeatStatus state;
    if(!ProFeatureTypeGet(feature,&type)&&type==PRO_FEAT_FLAT_PAT&&
       !ProFeatureStatusGet(feature,&state)&&state==PRO_FEAT_ACTIVE)*(int *)data=feature->id;
    return PRO_TK_NO_ERROR;
}

static double dfm_radius_limit,dfm_flange_factor,dfm_hole_factor,dfm_tangent_factor;

static double dfm_read_limit(const wchar_t *file,const wchar_t *key,double fallback){
    wchar_t value[32],*end;
    if(!GetPrivateProfileStringW(L"Screening",key,L"",value,_countof(value),file))return fallback;
    double read=wcstod(value,&end);
    return !*end&&isfinite(read)&&read>0&&read<=100?read:fallback;
}

static double dfm_rule_value(const wchar_t *key,double fallback){
    ProPath file;
    if(swprintf_s(file,_countof(file),L"%ls\\resources\\dfm\\screening.ini",runtime_root)>0)
        fallback=dfm_read_limit(file,key,fallback);
    if(swprintf_s(file,_countof(file),L"%ls\\DFM-screening.ini",runtime_data)>0)
        fallback=dfm_read_limit(file,key,fallback);
    return fallback;
}

static void dfm_load_rules(void){
    dfm_radius_limit=dfm_rule_value(L"PreferredBendRadiusMm",.762);
    dfm_flange_factor=dfm_rule_value(L"FlangeThicknessFactor",4);
    dfm_hole_factor=dfm_rule_value(L"HoleThicknessFactor",1);
    dfm_tangent_factor=dfm_rule_value(L"HoleToTangentThicknessFactor",4);
}

static double dfm_bore_tangent(const DeliveryBores *holes,const SmyFlangeScan *flange,double mm,double t){
    double nearest=HUGE_VAL;
    if(!holes||holes->limited)return nearest;
    for(int i=0;i<holes->count;i++){
        const DeliveryBore *b=&holes->bores[i];
        if(b->classification!=2||fabs((b->high-b->low)*mm-t)>fmax(.05,t*.03))continue;
        ProVector center;cm_point(b->centerline,b->axis,(b->low+b->high)/2,center);
        for(int j=0;j<flange->count;j++){
            if(fabs(ad_dot(b->axis,flange->normal[j]))<.9999)continue;
            ProVector from,direction;
            ad_sub(center,flange->tangent_start[j],from);
            ad_sub(flange->tangent_end[j],flange->tangent_start[j],direction);
            double length=sqrt(ad_dot(direction,direction));
            if(!ad_unit(direction))continue;
            double offset=ad_dot(from,flange->normal[j]),along=ad_dot(from,direction);
            if(fabs(fabs(offset)*mm-t/2)>.05||along+b->radius<0||along-b->radius>length)continue;
            double projected=ad_dot(from,from)-offset*offset-along*along;
            double overflow=fmax(0,fmax(-along,along-length));
            nearest=fmin(nearest,fmax(0,(sqrt(fmax(0,projected)+overflow*overflow)-b->radius)*mm));
        }
    }
    return nearest;
}
static void dfm_check_part(int i){
    ProMdl model=dfm_parts[i].model;
    wchar_t value[80];
    ProError health=part_health(model);
    int bodies=0;
    ProError count_error=sd_body_count(model,&bodies);
    if(!count_error&&bodies==0)health=PRO_TK_INCOMPLETE;
    ProError model_error=health?health:count_error;
    swprintf_s(value,_countof(value),model_error?L"Creo %d":L"已读到 %d 个实体",model_error?model_error:bodies);
    dfm_add(i,1,health||count_error?DFM_BLOCK:DFM_PASS,0,L"M01",L"模型状态",value,L"模型健康且含实体",L"核对失败特征、模型再生状态和实体。");
    double mm=0,thickness=0;
    ProError error=fab_thickness(model,&mm,&thickness);
    if(!error)swprintf_s(value,_countof(value),L"%.4g mm",thickness);
    dfm_add(i,1,error?DFM_PENDING:DFM_PASS,0,L"M02",L"模型名义板厚",error?L"未读取":value,L"Creo 原生单实体钣金厚度读取",L"本项通过指厚度已读取。图纸标称值、板厚公差及局部成形减薄另核。");
    int flat=0;
    ProSolidFeatVisit((ProSolid)model,dfm_find_flat,NULL,&flat);
    dfm_add(i,1,DFM_PENDING,flat,L"M04",L"展开轮廓",flat?L"有活动展平特征":L"未找到活动展平特征",L"需核对展开轮廓自交和重复边",L"在现有展开或 DXF 中复核切割轮廓。");
    DeliveryBores *holes=NULL;
    double min_diameter=HUGE_VAL;
    if(!error){
        OrganizeView view={model,mm};
        if(!org_bores(&view,&holes)&&!holes->limited)
            for(int j=0;j<holes->count;j++)
                if(holes->bores[j].classification==2)min_diameter=fmin(min_diameter,2*holes->bores[j].radius*mm);
    }
    wchar_t basis[180];
    const double hole_limit=thickness*dfm_hole_factor;
    swprintf_s(basis,_countof(basis),L"圆孔参考下限 %.3g mm（%.3g×板厚）",hole_limit,dfm_hole_factor);
    if(isfinite(min_diameter))swprintf_s(value,_countof(value),L"最小 Ø%.3g mm",min_diameter);
    dfm_add(i,1,isfinite(min_diameter)?min_diameter+1e-6<hole_limit?DFM_RISK:DFM_PASS:DFM_PENDING,0,
        L"C01",L"完整直通圆孔",isfinite(min_diameter)?value:L"未识别可判定的完整圆孔",
        hole_limit>0?basis:L"需要先读取板厚",
        L"来源 Protolabs 钣金设计指南。槽宽、异形孔、冲孔及精密孔按实际设备与工艺另核。");
    double nearest_bore=HUGE_VAL;
    int near_feature=0;
    FabBends bends={0};
    error=smy_bends_load(model,&bends);
    if(!error)for(int j=0;j<bends.count;j++){
        const FabBend *bend=&bends.rows[j];
        swprintf_s(value,_countof(value),L"R %.3g mm / %.3g°",bend->radius,bend->angle);
        swprintf_s(basis,_countof(basis),L"常用刀具参考 R ≥ %.3g mm",dfm_radius_limit);
        dfm_add(i,2,bend->radius+1e-6<dfm_radius_limit?DFM_RISK:DFM_PASS,bend->id,L"B01",L"折弯内半径预筛",value,basis,
            L"参考 Protolabs 常用 R0.762 mm 刀具。较小内半径可能采用其他刀具，按材料和轧向复核。");
        SmyFlangeScan flange={.model=model};
        ProVector low,high;
        ProError e=smy_bend_span(model,bend->surface,&flange.bend,flange.axis,low,high);
        if(!e)e=ProSurfaceContourVisit(flange.bend,smy_dfm_contour,NULL,&flange);
        double shortest=HUGE_VAL;
        if(!e&&flange.count>=2)for(int k=0;k<flange.count;k++)shortest=fmin(shortest,flange.heights[k]*bends.mm);
        if(isfinite(shortest))swprintf_s(value,_countof(value),L"%.3g mm",shortest);
        const double flange_limit=bends.thickness*dfm_flange_factor;
        swprintf_s(basis,_countof(basis),L"直段预筛 ≥ %.3g mm（%.3g×板厚）",flange_limit,dfm_flange_factor);
        dfm_add(i,2,isfinite(shortest)?shortest+1e-6<flange_limit?DFM_RISK:DFM_PASS:DFM_PENDING,
            bend->id,L"B02",L"相邻折边直段",isfinite(shortest)?value:L"未可靠识别两侧直段",basis,
            L"参照 Protolabs 通用法兰 4t。实测为折弯相切壁直段包络，选模和连续支承按工厂口径复核。");
        if(!e){
            double distance=dfm_bore_tangent(holes,&flange,bends.mm,bends.thickness);
            if(distance<nearest_bore){nearest_bore=distance;near_feature=bend->id;}
        }
    }
    if(error)dfm_add(i,2,DFM_PENDING,0,L"B01",L"折弯几何",L"部分折弯无法完整读取",L"需人工核对钣金模型",L"检查所属钣金实体及折弯面。");
    const double tangent_limit=thickness*dfm_tangent_factor;
    swprintf_s(basis,_countof(basis),L"圆孔边至切线预筛 ≥ %.3g mm（%.3g×板厚）",tangent_limit,dfm_tangent_factor);
    if(isfinite(nearest_bore))swprintf_s(value,_countof(value),L"最近 %.3g mm",nearest_bore);
    dfm_add(i,2,isfinite(nearest_bore)?nearest_bore+1e-6<tangent_limit?DFM_RISK:DFM_PASS:DFM_PENDING,
        near_feature,L"B03",L"圆孔到折弯切线",isfinite(nearest_bore)?value:L"未识别可配对的同壁直通圆孔",
        tangent_limit>0?basis:L"需要先读取板厚",
        L"以 4t 为可编辑的预筛阈值。仅量测可配对的完整圆孔，槽与异形孔用实际展开图、模具影响区复核。");
    free(holes);
    dfm_add(i,3,DFM_PENDING,0,L"J01",L"压铆规格",L"需要实际紧固件完整料号",
        L"PEM CLS-M4-1 示例：板厚≥1 mm，孔 Ø5.41 +0.08/0，中心距边≥6.9 mm",
        L"示例仅适用于该料号。确认实际型号、基材硬度及压入工装后对照厂商数据。");
    dfm_add(i,3,DFM_PENDING,0,L"F01",L"喷粉与成品配合",L"需要粉末型号、涂层图及配合尺寸",
        L"按选用粉末技术数据表。双面对夹间隙扣除两侧实测膜厚",
        L"AkzoNobel 不同体系膜厚要求不同。核对遮蔽、螺纹、接地和密封位置。");
    dfm_add(i,4,DFM_PENDING,0,L"A03",L"装配公差链",L"需要关键装配尺寸、公差与验收间隙",
        L"按名义间隙扣减最不利制造与膜厚偏差",
        L"从工程图读取定位基准、孔位、成品尺寸与允许偏差后复核。");
}

static void dfm_add_part(ProMdl part){
    ProMdlsubtype subtype;
    if(!part||ProMdlSubtypeGet(part,&subtype)||subtype!=PROMDLSTYPE_PART_SHEETMETAL)return;
    for(int i=0;i<dfm_part_count;i++)if(dfm_parts[i].model==part)return;
    if(dfm_part_count==_countof(dfm_parts)){dfm_limited=1;return;}
    DfmPart *entry=&dfm_parts[dfm_part_count];
    *entry=(DfmPart){.model=part,.enabled=1};
    if(ProMdlMdlnameGet(part,entry->name))return;
    dfm_check_part(dfm_part_count++);
}

static void dfm_check_static_interference(ProMdl model){
    AxInterferenceCache result={0};
    ProPath directory;
    ProError error=ProDirectoryCurrentGet(directory);
    if(!error)error=ax_interference_compute_mode(model,directory,&result,0,NULL);
    int hits[AX_MAX_OCCURRENCES]={0};
    if(!error)for(int i=0;i<result.count;i++){
        ProMdl first=result.scan->occurrences[result.pairs[i].index[0]].model;
        ProMdl second=result.scan->occurrences[result.pairs[i].index[1]].model;
        for(int j=0;j<dfm_part_count;j++)if(first==dfm_parts[j].model||second==dfm_parts[j].model)hits[j]++;
    }
    for(int i=0;i<dfm_part_count;i++){
        wchar_t value[80];
        swprintf_s(value,_countof(value),L"涉及 %d 对已检出的实体交叠",hits[i]);
        const int incomplete=error||(result.scan&&(result.scan->limited||result.scan->unavailable));
        dfm_add(i,4,hits[i]?DFM_RISK:incomplete?DFM_PENDING:DFM_PASS,0,L"A01",L"当前装配实体干涉",
            error?L"Creo 未返回干涉结果":value,L"精确实体干涉筛查，参考目标 0 对",
            L"在干涉分析页复核实例。设计压合、运动状态和公差极限须单独验证。");
    }
    ax_interference_result_free(&result);
}
static ProError dfm_export(wchar_t *output,size_t capacity){
    if(!dfm_row_count)return PRO_TK_E_NOT_FOUND;
    ProPath directory,path;
    ProError error=ProDirectoryCurrentGet(directory);
    if(error||!storage_directory_on_drive(directory,L'E'))return PRO_TK_BAD_CONTEXT;
    static LONG serial;
    SYSTEMTIME now;GetLocalTime(&now);
    if(swprintf_s(path,_countof(path),L"%ls%lsMWEAI-DFM-%04d%02d%02d-%02d%02d%02d-%lu-%ld.csv",
        directory,directory[wcslen(directory)-1]==L'\\'?L"":L"\\",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),InterlockedIncrement(&serial))<0)
        return PRO_TK_BAD_INPUTS;
    FILE *file=_wfopen(path,L"wt, ccs=UTF-8");
    if(!file)return PRO_TK_CANT_WRITE;
    if(fwprintf(file,L"模型,规则版本,状态,规则,零件,特征ID,检查项,实测值,判定依据,建议\n")<0)error=PRO_TK_CANT_WRITE;
    for(int i=0;!error&&i<dfm_row_count;i++){
        const DfmRow *r=&dfm_rows[i];
        wchar_t feature[24];swprintf_s(feature,_countof(feature),L"%d",r->feature);
        const wchar_t *fields[]={dfm_name,L"V0.2",dfm_states[r->status],r->code,dfm_parts[r->part].name,
            feature,r->check,r->value,r->basis,r->advice};
        for(int j=0;j<_countof(fields);j++)if(!dv_csv_text(file,fields[j])||fputwc(j+1==_countof(fields)?L'\n':L',',file)==WEOF){
            error=PRO_TK_CANT_WRITE;break;
        }
    }
    if(fclose(file)&&!error)error=PRO_TK_CANT_WRITE;
    if(error)DeleteFileW(path);
    else swprintf_s(output,capacity,L"已导出报告  %ls",path);
    return error;
}

static ProError dfm_locate(ProMdl current,int window){
    if(dfm_selected<0||dfm_selected>=dfm_row_count)return PRO_TK_E_NOT_FOUND;
    if(current!=dfm_root||window!=dfm_window)return PRO_TK_BAD_CONTEXT;
    const DfmRow *r=&dfm_rows[dfm_selected];
    ProMdl part=NULL;
    ProError error=ProMdlnameInit(dfm_parts[r->part].name,PRO_MDL_PART,&part);
    if(error||part!=dfm_parts[r->part].model)return PRO_TK_BAD_CONTEXT;
    ProMdlType type;
    error=ProMdlTypeGet(current,&type);
    if(error)return error;
    if(type==PRO_MDL_ASSEMBLY){
        AxScan *scan=NULL;ProAsmcomppath path;ProSelection selection=NULL;ProModelitem item;
        error=ax_scan(current,0,&scan);
        int found=0;
        for(int i=0;!error&&i<scan->count;i++){
            if(scan->occurrences[i].model!=part)continue;
            error=ax_interference_occurrence_path(current,&scan->occurrences[i],&path);
            found=1;break;
        }
        if(!error&&!found)error=PRO_TK_E_NOT_FOUND;
        if(!error)error=ProMdlToModelitem(part,&item);
        if(!error)error=ProSelectionAlloc(&path,&item,&selection);
        if(!error)error=ProSelectionHighlight(selection,PRO_COLOR_HIGHLITE);
        if(selection)ProSelectionFree(&selection);
        free(scan);
        return error;
    }
    if(!r->feature)return PRO_TK_E_NOT_FOUND;
    ProFeature feature;ProSelection selection=NULL;
    error=ProFeatureInit((ProSolid)part,r->feature,&feature);
    if(!error)error=ProSelectionAlloc(NULL,&feature,&selection);
    if(!error)error=ProSelectionHighlight(selection,PRO_COLOR_HIGHLITE);
    if(selection)ProSelectionFree(&selection);
    return error;
}

static void dfm_execute(int action){
    wchar_t note[PRO_PATH_SIZE+100]=L"";
    ProMdl model=NULL;int window=-1;
    ProWindowCurrentGet(&window);
    if(window>=0)ProWindowMdlGet(window,&model);
    ProError error=PRO_TK_NO_ERROR;
    InterlockedExchange(&dfm_pending,2);
    if(action==DFM_SCAN){
        dfm_load_rules();
        dfm_row_count=dfm_part_count=dfm_limited=0;dfm_selected=-1;dfm_name[0]=0;dfm_root=model;dfm_window=window;
        ProMdlType type;
        if(model&&!ProMdlTypeGet(model,&type)){
            ProMdlMdlnameGet(model,dfm_name);
            if(type==PRO_MDL_PART)dfm_add_part(model);
            if(type==PRO_MDL_ASSEMBLY){
                AxScan *scan=NULL;
                error=ax_scan(model,0,&scan);
                if(!error){
                    dfm_limited=scan->limited||scan->unavailable;
                    for(int i=0;i<scan->count;i++)if(scan->occurrences[i].type==PRO_MDL_PART)dfm_add_part(scan->occurrences[i].model);
                    if(dfm_part_count)dfm_check_static_interference(model);
                }
                free(scan);
            }
        }
        if(!error)swprintf_s(note,_countof(note),dfm_part_count?L"已检查 %d 个钣金零件%ls。通过表示符合本项预筛口径。":
            L"请在 Creo 激活含原生钣金零件的零件或装配。",dfm_part_count,dfm_limited?L"（部分组件未读全）":L"");
    }else if(action==DFM_LOCATE){
        error=dfm_locate(model,window);
        if(!error)wcscpy_s(note,_countof(note),L"已在 Creo 中定位。装配中定位到首个匹配实例。");
        else if(error==PRO_TK_E_NOT_FOUND)wcscpy_s(note,_countof(note),L"该项暂无可定位的特征或实例。");
    }else{
        error=dfm_export(note,_countof(note));
        if(error==PRO_TK_BAD_CONTEXT)wcscpy_s(note,_countof(note),L"请选择 E 盘上的 Creo 工作目录后再导出。");
    }
    if(error&&!note[0])swprintf_s(note,_countof(note),L"操作未完成（Creo %d）。请核对活动模型和分析结果。",error);
    if(dfm_panel)SendMessageW(dfm_panel,DFM_UPDATE,action,(LPARAM)note);
    InterlockedExchange(&dfm_pending,0);
}

static void dfm_queue(int action){
    if(!native_backend_window||native_busy){SetWindowTextW(dfm_status,L"Creo 正忙或命令通道未连接。");return;}
    if(InterlockedCompareExchange(&dfm_pending,1,0))return;
    dfm_queued_at=GetTickCount64();
    SetTimer(dfm_panel,1,350,NULL);
    SetWindowTextW(dfm_status,L"正在等待 Creo…");
    if(!PostMessageW(native_backend_window,PACKAGING_REQUEST,action,0)){
        InterlockedExchange(&dfm_pending,0);
        SetWindowTextW(dfm_status,L"无法向 Creo 提交操作。");
    }
}

static void dfm_show_detail(void){
    int selected=ListView_GetNextItem(dfm_results,-1,LVNI_SELECTED);
    LVITEMW item={.mask=LVIF_PARAM,.iItem=selected};
    dfm_selected=selected>=0&&SendMessageW(dfm_results,LVM_GETITEMW,0,(LPARAM)&item)?(int)item.lParam/_countof(dfm_states):-1;
    wchar_t title[120]=L"问题详情",detail[430]=L"选择一条检查结果查看测量、依据和处理建议。";
    if(dfm_selected>=0){
        const DfmRow *r=&dfm_rows[dfm_selected];
        swprintf_s(title,_countof(title),L"%ls  %ls",r->code,r->check);
        swprintf_s(detail,_countof(detail),L"实测  %ls    依据  %ls\r\n%ls",r->value,r->basis,r->advice);
    }
    SetWindowTextW(dfm_detail_title,title);
    SetWindowTextW(dfm_detail,detail);
    EnableWindow(dfm_buttons[3],dfm_selected>=0);
    EnableWindow(dfm_buttons[4],dfm_selected>=0);
}

static void dfm_fill_results(void){
    dfm_updating=1;
    ListView_DeleteAllItems(dfm_results);
    int group=dfm_group_index,counts[4]={0},visible=0;
    for(int i=0;i<dfm_row_count;i++){
        const DfmRow *r=&dfm_rows[i];
        if(!dfm_parts[r->part].enabled||(group>0&&group!=r->group))continue;
        counts[r->status]++;
        int row=clash_list_add(dfm_results,visible++,dfm_states[r->status],i*_countof(dfm_states)+r->status);
        const wchar_t *fields[]={r->code,dfm_parts[r->part].name,r->check,r->value,r->basis};
        for(int j=0;j<_countof(fields);j++)clash_cell(dfm_results,row,j+1,fields[j]);
    }
    if(visible)ListView_SetItemState(dfm_results,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    for(int i=0;i<_countof(counts);i++){
        wchar_t badge[32];
        swprintf_s(badge,_countof(badge),L"%ls  %d",dfm_states[i],counts[i]);
        SetWindowTextW(dfm_badges[i],badge);
        InvalidateRect(dfm_badges[i],NULL,TRUE);
    }
    EnableWindow(dfm_buttons[1],dfm_row_count>0);
    dfm_updating=0;
    dfm_show_detail();
}

static void dfm_fill_parts(void){
    dfm_updating=1;
    wchar_t find[128];
    GetWindowTextW(dfm_find,find,_countof(find));
    ListView_DeleteAllItems(dfm_part_list);
    for(int i=0;i<dfm_part_count;i++)if(clash_contains(dfm_parts[i].name,find)){
        int row=clash_list_add(dfm_part_list,ListView_GetItemCount(dfm_part_list),dfm_parts[i].name,i);
        ListView_SetCheckState(dfm_part_list,row,dfm_parts[i].enabled);
    }
    dfm_updating=0;
}
static void dfm_layout(int w,int h){
    int left=max(160,w/5),x=left+19,width=max(160,w-x-8),detail=max(220,h-h/3);
    MoveWindow(dfm_title,10,13,max(90,w-278),21,TRUE);
    for(int i=0;i<3;i++)MoveWindow(dfm_buttons[i],w-269+i*88,8,83,29,TRUE);
    for(int i=0;i<_countof(dfm_badges);i++)MoveWindow(dfm_badges[i],10+i*110,48,103,25,TRUE);
    MoveWindow(dfm_filter,10,84,left,20,TRUE);
    MoveWindow(dfm_find,10,108,left,24,TRUE);
    int groups_y=max(212,detail-95);
    MoveWindow(dfm_part_list,10,138,left,max(70,groups_y-145),TRUE);
    ListView_SetColumnWidth(dfm_part_list,0,left-7);
    MoveWindow(dfm_group_title,10,groups_y,left,21,TRUE);
    for(int i=0;i<_countof(dfm_group_buttons);i++)MoveWindow(dfm_group_buttons[i],10,groups_y+25+i*27,left,25,TRUE);
    MoveWindow(dfm_results,x,84,width,max(75,detail-89),TRUE);
    const int widths[]={63,66,max(108,width/5),max(130,width/5),110,max(135,width/4)};
    for(int i=0;i<_countof(widths);i++)ListView_SetColumnWidth(dfm_results,i,widths[i]);
    MoveWindow(dfm_detail_title,x+4,detail+3,width-10,22,TRUE);
    MoveWindow(dfm_detail,x+4,detail+28,width-10,max(45,h-detail-91),TRUE);
    MoveWindow(dfm_buttons[3],x+4,h-60,100,29,TRUE);
    MoveWindow(dfm_buttons[4],x+111,h-60,86,29,TRUE);
    MoveWindow(dfm_status,10,h-25,w-20,20,TRUE);
}

static void dfm_draw_status_cell(const NMLVCUSTOMDRAW *draw){
    HWND table=draw->nmcd.hdr.hwndFrom,header=ListView_GetHeader(table);
    RECT cell,row;
    if(!header||!Header_GetItemRect(header,0,&cell)||
       !ListView_GetItemRect(table,(int)draw->nmcd.dwItemSpec,&row,LVIR_BOUNDS))return;
    MapWindowPoints(header,table,(POINT *)&cell,2);
    cell.top=row.top;cell.bottom=row.bottom-1;cell.right--;
    if(IsRectEmpty(&cell))return;
    int status=(int)(draw->nmcd.lItemlParam%_countof(dfm_states));
    HDC dc=draw->nmcd.hdc;
    int saved=SaveDC(dc);
    if(!saved)return;
    IntersectClipRect(dc,cell.left,cell.top,cell.right,cell.bottom);
    SetDCBrushColor(dc,dfm_status_background[status]);
    FillRect(dc,&cell,(HBRUSH)GetStockObject(DC_BRUSH));
    SetTextColor(dc,dfm_status_ink[status]);SetBkMode(dc,TRANSPARENT);
    if(native_font)SelectObject(dc,native_font);
    cell.left+=4;
    DrawTextW(dc,dfm_states[status],-1,&cell,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    RestoreDC(dc,saved);
}
static LRESULT CALLBACK dfm_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&common);
        dfm_brush=CreateSolidBrush(CHAT_CANVAS_COLOR);
        dfm_title=clash_control(window,L"STATIC",L"钣金 DFM",SS_ENDELLIPSIS,0);
        for(int i=0;i<_countof(dfm_badges);i++){
            wchar_t badge[32];swprintf_s(badge,_countof(badge),L"%ls  0",dfm_states[i]);
            dfm_badges[i]=clash_control(window,L"STATIC",badge,SS_OWNERDRAW,DFM_BADGE+i);
        }
        dfm_filter=clash_control(window,L"STATIC",L"钣金零件",0,0);
        dfm_find=clash_control(window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,DFM_FIND);
        SendMessageW(dfm_find,EM_SETCUEBANNER,FALSE,(LPARAM)L"搜索零件");
        dfm_part_list=clash_control(window,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|LVS_REPORT,DFM_PARTS);
        dfm_results=clash_control(window,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,DFM_RESULTS);
        ListView_SetExtendedListViewStyle(dfm_part_list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        ListView_SetExtendedListViewStyle(dfm_results,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        const wchar_t *parts[]={L"零件名称"};
        const wchar_t *columns[]={L"状态",L"规则",L"零件",L"检查项",L"实测值",L"判定依据"};
        clash_columns(dfm_part_list,parts,_countof(parts));
        clash_columns(dfm_results,columns,_countof(columns));
        dfm_group_title=clash_control(window,L"STATIC",L"检查工序",0,0);
        dfm_group_index=0;
        for(int i=0;i<_countof(dfm_groups);i++)
            dfm_group_buttons[i]=clash_control(window,L"BUTTON",dfm_groups[i],BS_AUTORADIOBUTTON|WS_TABSTOP|(i?0:WS_GROUP),DFM_STAGE+i);
        CheckRadioButton(window,DFM_STAGE,DFM_STAGE+4,DFM_STAGE);
        dfm_detail_title=clash_control(window,L"STATIC",L"问题详情",0,0);
        dfm_detail=clash_control(window,L"STATIC",L"选择一条检查结果查看测量、依据和处理建议。",0,0);
        dfm_status=clash_control(window,L"STATIC",L"点击「开始分析」读取 Creo 当前活动模型。",SS_PATHELLIPSIS,0);
        const wchar_t *buttons[]={L"开始分析",L"导出报告",L"返回聊天",L"在 Creo 中定位",L"查看规则"};
        for(int i=0;i<_countof(buttons);i++)dfm_buttons[i]=clash_control(window,L"BUTTON",buttons[i],BS_OWNERDRAW|WS_TABSTOP,DFM_RUN+i);
        EnableWindow(dfm_buttons[1],FALSE);EnableWindow(dfm_buttons[3],FALSE);EnableWindow(dfm_buttons[4],FALSE);
        return 0;
    }
    case WM_SIZE:dfm_layout(LOWORD(l),HIWORD(l));return 0;
    case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect((HDC)w,&r,dfm_brush);return 1;}
    case WM_CTLCOLORSTATIC:SetBkColor((HDC)w,CHAT_CANVAS_COLOR);return (LRESULT)dfm_brush;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT button=*(DRAWITEMSTRUCT *)l;
        if(button.CtlID>=DFM_BADGE&&button.CtlID<DFM_BADGE+4){

            int i=button.CtlID-DFM_BADGE;
            HBRUSH fill=CreateSolidBrush(dfm_status_background[i]);
            FillRect(button.hDC,&button.rcItem,fill);DeleteObject(fill);
            wchar_t title[32];GetWindowTextW(button.hwndItem,title,_countof(title));
            SetTextColor(button.hDC,dfm_status_ink[i]);SetBkMode(button.hDC,TRANSPARENT);
            DrawTextW(button.hDC,title,-1,&button.rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
            return TRUE;
        }
        if(button.CtlID==DFM_RUN)button.CtlID=CHAT_SEND;
        native_theme_button(&button);return TRUE;
    }
    case WM_TIMER:
        if(w==1){
            LONG pending=InterlockedCompareExchange(&dfm_pending,0,0);
            if(!pending)KillTimer(window,1);
            else if(pending==1&&GetTickCount64()-dfm_queued_at>6000&&InterlockedCompareExchange(&dfm_pending,0,1)==1){
                KillTimer(window,1);
                SetWindowTextW(dfm_status,L"Creo 尚未接受命令，请退出当前 Creo 操作后重试。");
            }
        }
        return 0;
    case DFM_UPDATE:
        if(w==DFM_SCAN){
            wchar_t title[180];
            swprintf_s(title,_countof(title),L"钣金 DFM   ·   %ls",dfm_name[0]?dfm_name:L"当前 Creo 模型");
            SetWindowTextW(dfm_title,title);
            dfm_fill_parts();dfm_fill_results();InvalidateRect(window,NULL,TRUE);
        }
        SetWindowTextW(dfm_status,(const wchar_t *)l);
        return 0;
    case WM_COMMAND:{
        int id=LOWORD(w);
        if(id==DFM_BACK){packaging_switch(0);return 0;}
        if(InterlockedCompareExchange(&dfm_pending,0,0))return 0;
        if(id==DFM_FIND&&HIWORD(w)==EN_CHANGE)dfm_fill_parts();
        else if(id>=DFM_STAGE&&id<DFM_STAGE+5){dfm_group_index=id-DFM_STAGE;dfm_fill_results();}
        else if(id==DFM_RUN)dfm_queue(DFM_SCAN);
        else if(id==DFM_SAVE)dfm_queue(DFM_EXPORT);
        else if(id==DFM_GO)dfm_queue(DFM_LOCATE);
        else if(id==DFM_RULE){
            if(dfm_selected>=0&&dfm_selected<dfm_row_count){
                const DfmRow *r=&dfm_rows[dfm_selected];
                wchar_t note[420];swprintf_s(note,_countof(note),L"%ls  %ls\r\n\r\n判定依据  %ls\r\n\r\n复核方法  %ls",r->code,r->check,r->basis,r->advice);
                MessageBoxW(window,note,L"钣金 DFM 规则 V0.2",MB_OK|MB_ICONINFORMATION);
            }
        }
        return 0;
    }
    case WM_NOTIFY:{
        const NMHDR *hdr=(const NMHDR *)l;
        if(hdr->idFrom==DFM_RESULTS&&hdr->code==NM_CUSTOMDRAW){
            NMLVCUSTOMDRAW *draw=(NMLVCUSTOMDRAW *)l;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYPOSTPAINT;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT)dfm_draw_status_cell(draw);
            return CDRF_DODEFAULT;
        }
        if(dfm_updating||InterlockedCompareExchange(&dfm_pending,0,0))return 0;
        if(hdr->idFrom==DFM_PARTS&&hdr->code==LVN_ITEMCHANGED){
            const NMLISTVIEW *change=(const NMLISTVIEW *)l;
            int i=(int)change->lParam;
            if(change->iItem>=0&&i>=0&&i<dfm_part_count&&((change->uNewState^change->uOldState)&LVIS_STATEIMAGEMASK)){
                dfm_parts[i].enabled=ListView_GetCheckState(dfm_part_list,change->iItem);
                dfm_fill_results();
            }
        }else if(hdr->idFrom==DFM_RESULTS&&hdr->code==LVN_ITEMCHANGED)dfm_show_detail();
        return 0;
    }
    case WM_DESTROY:
        dfm_panel=NULL;
        DeleteObject(dfm_brush);dfm_brush=NULL;
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}

static void dfm_open(HWND parent){
    if(dfm_panel)return;
    const wchar_t *name=L"MweaiDfmPanel";
    WNDCLASSW cls={0};
    cls.lpfnWndProc=dfm_proc;cls.hInstance=GetModuleHandleW(NULL);cls.lpszClassName=name;
    cls.hCursor=LoadCursor(NULL,IDC_ARROW);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    dfm_panel=CreateWindowW(name,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,0,0,parent,NULL,cls.hInstance,NULL);
}
