/* Outside-dimension U reinforcement on native full-length edge walls.
   Case U's published stock-wall route preserves separately editable bends. */
static ProError bwtl_u_reinforcement_measure(ProMdl sheet,double outside[3],double *gauge){
    ProOutline bounds;
    double mm=0;
    ProError error=fab_thickness(sheet,&mm,gauge);
    if(!error)error=or_outline(sheet,bounds);
    if(error)return error;
    outside[0]=(bounds[1][0]-bounds[0][0])*mm;
    outside[1]=(bounds[1][1]-bounds[0][1])*mm;
    outside[2]=(bounds[1][2]-bounds[0][2])*mm;
    return PRO_TK_NO_ERROR;
}

static int bwtl_u_dimensions_same(const double a[3],const double b[3]){
    for(int i=0;i<3;i++)if(!isfinite(a[i])||fabs(a[i]-b[i])>0.002)return 0;
    return 1;
}

typedef struct {
    ProFeature feature;
    ProElement original,edited;
    double radius;
} BwtlUWall;

static ProError bwtl_u_walls(ProMdl sheet,const wchar_t *key,double scale,BwtlUWall walls[2]){
    for(int i=0;i<2;i++){
        ProName name;
        if(swprintf_s(name,PRO_NAME_SIZE,L"%ls_W%d",key,i+1)<0)return PRO_TK_BAD_INPUTS;
        ProElement angle,radius;
        double actual_angle;
        ProError error=cm_feature_find(sheet,name,&walls[i].feature);
        if(!error)error=ProFeatureElemtreeExtract(&walls[i].feature,NULL,PRO_FEAT_EXTRACT_NO_OPTS,&walls[i].original);
        if(!error)error=cm_bend_elements(&walls[i].feature,walls[i].original,&angle,&radius);
        if(!error)error=ProElementDoubleGet(angle,NULL,&actual_angle);
        if(!error)error=ProElementDoubleGet(radius,NULL,&walls[i].radius);
        if(error)return error;
        if(fabs(actual_angle-90)>0.002||walls[i].radius<=0)return PRO_TK_BAD_CONTEXT;
        error=cm_bend_verified(&walls[i].feature,90,walls[i].radius,scale);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

static void bwtl_u_walls_free(BwtlUWall walls[2]){
    for(int i=0;i<2;i++){
        if(walls[i].edited)ProFeatureElemtreeFree(&walls[i].feature,walls[i].edited);
        if(walls[i].original)ProFeatureElemtreeFree(&walls[i].feature,walls[i].original);
    }
}

static ProError bwtl_u_reinforcement(ProMdl sheet,const wchar_t *feature,double length,double outside_width,
        double outside_height,double thickness,double inside_radius,PartText *out){
    if(!sheet||!feature||!out||!isfinite(length)||!isfinite(outside_width)||!isfinite(outside_height)||
       !isfinite(thickness)||!isfinite(inside_radius)||length<=0||thickness<=0||inside_radius<=0||
       outside_width<=2*(inside_radius+thickness)||outside_height<=inside_radius+thickness)
        return PRO_TK_BAD_INPUTS;
    if(sh_recovery_uncertain||cm_recovery_uncertain)return PRO_TK_BAD_CONTEXT;
    ProMdlsubtype subtype;
    ProError error=ProMdlSubtypeGet(sheet,&subtype);
    if(error||subtype!=PROMDLSTYPE_PART_SHEETMETAL)return error?error:PRO_TK_BAD_CONTEXT;
    FdTxn tx={0};
    BwtlUWall walls[2]={0};
    ProMdlName model;
    PartCommand command;
    wchar_t protocol[512],rollback[512],native_receipt[400]=L"";
    PartText native={native_receipt,_countof(native_receipt),0};
    double actual[3]={0},gauge=0;
    double expected[]={length,outside_width,outside_height};
    int executed=0;
    const wchar_t *stage=L"核对空钣金模板";
#define U_DO(label,e) do{stage=L##label;error=(e);trace("U native " #e,error);if(error)goto done;}while(0)
    U_DO("核对空钣金模板",template_empty((ProSolid)sheet));
    U_DO("记录原有钣金特征",sh_begin(&tx,sheet,feature,PRO_FEAT_WALL));
    U_DO("读取零件名",ProMdlMdlnameGet(sheet,model));
    if(swprintf_s(protocol,_countof(protocol),
        L"CREO_OP CASE_U %ls %ls PLANE:FRONT PLANE:RIGHT %.12g %.12g %.12g %.12g %.12g POS",
        model,feature,length,outside_width-2*thickness,thickness,outside_height,inside_radius)<0||
       part_command_parse(protocol,&command)!=1||
       cabinet_plan_valid(command.count,(wchar_t (*)[PART_TOKEN_MAX])command.arg)!=1){
        stage=L"核对钣金加强筋尺寸";
        error=PRO_TK_BAD_INPUTS;
        goto done;
    }
    executed=1;
    U_DO("创建首壁和两道全长折弯",cm_u(&tx,&command,&native));
    U_DO("核对模型重生成",part_regenerate(sheet));
    U_DO("读回实际外形和板厚",bwtl_u_reinforcement_measure(sheet,actual,&gauge));
    if(!bwtl_u_dimensions_same(actual,expected)||fabs(gauge-thickness)>0.002){
        part_text_add(out,L"\n实测外形 %.8g×%.8g×%.8g mm、板厚 %.8g mm。",actual[0],actual[1],actual[2],gauge);
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
    U_DO("复核两道原生90度折弯",bwtl_u_walls(sheet,feature,tx.scale,walls));
    for(int i=0;i<2;i++)if(fabs(walls[i].radius/tx.scale-inside_radius)>0.002){
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
done:
    bwtl_u_walls_free(walls);
    if(error&&executed){
        tx.stage="U reinforcement verification";
        am_finish(&tx,error,0,rollback,_countof(rollback));
        if(!tx.rollback_ok)sh_recovery_uncertain=1;
    }else fd_cleanup(&tx,0);
    if(error)part_text_add(out,L"\nU 形加强筋未完成。停在「%ls」（Creo %d）。%ls",
        stage,error,executed&&!tx.rollback_ok?L"请核对 Creo 内的回退状态。":L"原有特征保留。");
    else part_text_add(out,L"\n外形实测 %.8g × %.8g × %.8g mm，板厚 %.8g mm，两道 90° 折弯内 R%.8g mm。两侧高度都可编辑。未保存。",
        actual[0],actual[1],actual[2],gauge,inside_radius);
    return error;
#undef U_DO
}

/* The stock flat-wall section has one driving leg dimension. Use its native
   type and value, and compare to the actual exterior before modifying it. */
static ProError bwtl_u_height_edit(BwtlUWall *wall,double from,double to){
    ProSection profile=NULL;
    ProBoolean intent=PRO_B_FALSE;
    ProWSecerror diagnostics=NULL;
    int *ids=NULL,count=0,height_id=-1;
    double start,end;
    ProError error=ProFeatureElemtreeExtract(&wall->feature,NULL,PRO_FEAT_EXTRACT_NO_OPTS,&wall->edited);
    if(!error)error=fd_section_from_tree(wall->edited,&profile);
    if(!error)error=cm_wall_offsets(profile,&start,&end);
    if(!error&&(fabs(start)>1e-6||fabs(end)>1e-6))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProSecdimIdsGet(profile,&ids,&count);
    for(int i=0;!error&&i<count;i++){
        ProSecdimType type;
        double value;
        error=ProSecdimTypeGet(profile,ids[i],&type);
        if(error||type!=PRO_TK_DIM_LINE)continue;
        error=ProSecdimValueGet(profile,ids[i],&value);
        if(!error&&fabs(value-from)<1e-6){
            if(height_id>=0)error=PRO_TK_BAD_CONTEXT;
            else height_id=ids[i];
        }
    }
    if(ids)ProArrayFree((ProArray *)&ids);
    if(error)return error;
    if(height_id<0)return PRO_TK_BAD_CONTEXT;
    error=ProSectionIntentManagerModeGet(profile,&intent);
    if(!error&&intent)error=ProSectionIntentManagerModeSet(profile,PRO_B_FALSE);
    if(!error)error=ProSecdimValueSet(profile,height_id,to);
    if(!error)error=ProSecerrorAlloc(&diagnostics);
    if(!error)error=ProSectionRegenerate(profile,&diagnostics);
    if(diagnostics)ProSecerrorFree(&diagnostics);
    return error;
}

static ProError bwtl_u_reinforcement_resize_height(ProMdl sheet,const wchar_t *feature,double outside_height,
        PartText *out){
    if(!sheet||!feature||!out||!isfinite(outside_height)||outside_height<=0)return PRO_TK_BAD_INPUTS;
    if(sh_recovery_uncertain||cm_recovery_uncertain)return PRO_TK_BAD_CONTEXT;
    BwtlUWall walls[2]={0};
    double before[3]={0},after[3]={0},gauge=0,final_gauge=0,mm=0;
    int attempted=0,restored=1;
    const wchar_t *stage=L"核对当前 U 形加强筋";
    ProError error=bwtl_u_reinforcement_measure(sheet,before,&gauge);
#define U_EDIT(label,e) do{stage=L##label;error=(e);trace("U edit " #e,error);if(error)goto done;}while(0)
    if(error)goto done;
    U_EDIT("读取模型单位",qa_mm_per_unit(sheet,&mm));
    U_EDIT("核对现有的两道折弯",bwtl_u_walls(sheet,feature,1/mm,walls));
    if(outside_height<=gauge+fmax(walls[0].radius,walls[1].radius)*mm){
        stage=L"目标外高不足以容纳板厚及内折弯半径";
        error=PRO_TK_BAD_INPUTS;
        goto done;
    }
    if(fabs(before[2]-outside_height)<=0.002){
        part_text_add(out,L"当前加强筋外高已为 %.8g mm。",before[2]);
        goto done;
    }
    for(int i=0;i<2;i++)
        U_EDIT("求解两侧原生高度尺寸",bwtl_u_height_edit(&walls[i],before[2]/mm,outside_height/mm));
    for(int i=0;i<2;i++){
        attempted=i+1;
        U_EDIT("重生成原有折弯特征",cm_apply_tree(&walls[i].feature,walls[i].edited));
        U_EDIT("复核折弯角度和半径",cm_bend_verified(&walls[i].feature,90,walls[i].radius,1/mm));
    }
    U_EDIT("读回修改后的外形及板厚",bwtl_u_reinforcement_measure(sheet,after,&final_gauge));
    double expected[]={before[0],before[1],outside_height};
    if(!bwtl_u_dimensions_same(after,expected)||fabs(final_gauge-gauge)>0.002){
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
    part_text_add(out,L"已将 U 形加强筋外高从 %.8g 改为 %.8g mm。外长 %.8g mm、外宽 %.8g mm、板厚 %.8g mm，两道折弯通过核对。未保存。",
        before[2],after[2],after[0],after[1],final_gauge);
done:
    if(error&&attempted){
        for(int i=attempted-1;i>=0;i--)
            if(cm_apply_tree(&walls[i].feature,walls[i].original))restored=0;
        if(bwtl_u_reinforcement_measure(sheet,after,&final_gauge)||!bwtl_u_dimensions_same(after,before)||
           fabs(final_gauge-gauge)>0.002)restored=0;
        if(!restored)sh_recovery_uncertain=1;
    }
    bwtl_u_walls_free(walls);
    if(error)part_text_add(out,L"加强筋外高未修改。停在「%ls」（Creo %d）。%ls",
        stage,error,restored?L"":L"请核对 Creo 内的回退状态。");
    return error;
#undef U_EDIT
}
