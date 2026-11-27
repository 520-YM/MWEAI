/* The same bounded grammar handles local BWTL chat and the Codex client.
   Positions are in the plate sketch, measured from its lower-left corner. */
typedef struct {
    ProMdlName drawing;
    double length,width,thickness,hole,x,y;
    wchar_t spec[64],center_spec[64];
    double corner_x,corner_y,center_spacing;
    int save,six;
} PanelPressfitPlan;

static const wchar_t panel_flow_usage[]=
    L"请给出完整图号和两位版本，例如「按图号 M26A9881001V00 建一个 80×60×1.5 的钣金件，中心打 Ø5.4 通孔，装一颗 BSO M3×8，保存到当前项目」。"
    L"孔也可写成「在 X=20、Y=30 处打 Ø5.4 通孔」，坐标从板的左下角起算。省略最后的保存指示时，装配留在当前 Creo 会话。"
    L"六孔可用「打6个 Ø5.4 通孔，四角横向边距15、纵向边距15，装 BSO M3×8，中间两孔孔距40，装 BSO M3×6」。";

static int panel_flow_delimiter(const wchar_t **text,const wchar_t *allowed){
    const wchar_t *p=sh_plate_spacing(*text);
    if(!*p||!wcschr(allowed,*p))return 0;
    *text=sh_plate_spacing(p+1);
    return 1;
}

static int panel_flow_decimal(const wchar_t **text,double *number){
    const wchar_t *start=sh_plate_spacing(*text);
    const wchar_t *p=start+wcsspn(start,L"0123456789");
    if(p==start)return 0;
    if(*p==L'.'){
        const wchar_t *digits=++p;
        p+=wcsspn(digits,L"0123456789");
        if(p==digits)return 0;
    }
    wchar_t token[24];
    if(p-start>=_countof(token)||wcsncpy_s(token,_countof(token),start,p-start)||!part_number(token,number))return 0;
    p=sh_plate_spacing(p);
    if(!_wcsnicmp(p,L"mm",2))p=sh_plate_spacing(p+2);
    else sh_plate_take(&p,L"毫米");
    *text=p;
    return 1;
}

static int panel_flow_drawing(const wchar_t **text,ProMdlName drawing){
    const wchar_t *p=sh_plate_spacing(*text);
    panel_flow_delimiter(&p,L"：:");
    const size_t letters=wcsspn(p,L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");
    if(letters<=3||letters>=PRO_NAME_SIZE||wcsncpy_s(drawing,PRO_NAME_SIZE,p,letters)||
       !company_pair_revision(drawing+letters-3)||!model_assembly_name_valid(drawing)||!dv_safe_leaf(drawing))return 0;
    *text=sh_plate_spacing(p+letters);
    panel_flow_delimiter(text,L"，,：:");
    return 1;
}

static int panel_flow_hole(const wchar_t **text,PanelPressfitPlan *plan){
    const wchar_t *p=*text;
    sh_plate_take(&p,L"在");
    if(sh_plate_take(&p,L"板中心")||sh_plate_take(&p,L"中心")){
        plan->x=plan->length/2.0;
        plan->y=plan->width/2.0;
    }else if(panel_flow_delimiter(&p,L"Xx")&&panel_flow_delimiter(&p,L"=＝")&&panel_flow_decimal(&p,&plan->x)&&
             panel_flow_delimiter(&p,L"，,、")&&panel_flow_delimiter(&p,L"Yy")&&panel_flow_delimiter(&p,L"=＝")&&
             panel_flow_decimal(&p,&plan->y)){
        sh_plate_take(&p,L"处");
    }else return 0;

    if(!(sh_plate_take(&p,L"打")||sh_plate_take(&p,L"开")))return 0;
    sh_plate_take(&p,L"一个");
    if(!panel_flow_delimiter(&p,L"ØøΦφ⌀")&&!sh_plate_take(&p,L"直径"))return 0;
    if(!panel_flow_decimal(&p,&plan->hole)||!sh_plate_take(&p,L"通孔"))return 0;
    const double radius=plan->hole/2.0;
    if(radius<=0||plan->x<=radius||plan->y<=radius||
       plan->x+radius>=plan->length||plan->y+radius>=plan->width)return 0;
    *text=p;
    return 1;
}

static int panel_flow_specification(const wchar_t **text,wchar_t spec[64]){
    const wchar_t *p=*text;
    sh_plate_take(&p,L"各");
    if(!(sh_plate_take(&p,L"装配")||sh_plate_take(&p,L"安装")||sh_plate_take(&p,L"装")))return 0;
    if(!sh_plate_take(&p,L"一颗")&&!sh_plate_take(&p,L"一个"))sh_plate_take(&p,L"一件");

    wchar_t model[MAX_PATH];
    PressfitSelector selector={0};
    if(!fastener_pressfit_model(&p,model)||!pressfit_selector(model,&selector)||
       !selector.bso_length||!selector.generic[0])return 0;

    /* Keep the BSO shorthand used to resolve the exact company family.
       Remove spaces for the native press-fit request's single model token. */
    size_t used=0;
    for(const wchar_t *at=model;*at;at++){
        if(iswspace(*at))continue;
        if(used+1>=64)return 0;
        spec[used++]=*at==L'×'?L'X':towupper(*at);
    }
    spec[used]=0;
    *text=sh_plate_spacing(p);
    return used>0;
}

static int panel_flow_save_tail(const wchar_t *p,int *save){
    if(panel_flow_delimiter(&p,L"，,；;")){
        if(sh_plate_take(&p,L"保存到当前项目工作目录")||sh_plate_take(&p,L"保存到当前项目目录")||
           sh_plate_take(&p,L"保存到当前项目")||sh_plate_take(&p,L"保存到当前工作目录"))*save=1;
        else if(!sh_plate_take(&p,L"先不保存")&&!sh_plate_take(&p,L"暂不保存")&&!sh_plate_take(&p,L"不保存"))return 0;
    }
    panel_flow_delimiter(&p,L"。！!");
    return !*p;
}

/* Stable slot order is lower-left, lower-right, upper-left, upper-right,
   middle-left, middle-right. Surface IDs are obtained from actual bores. */
static int panel_six_layout(const PanelPressfitPlan *plan,double centers[6][2]){
    const double radius=plan->hole/2.0;
    const int count=plan->six?6:1;
    if(!isfinite(radius)||radius<=0||!isfinite(plan->length)||!isfinite(plan->width)||
       !isfinite(plan->thickness)||plan->thickness<=0)return 0;
    if(plan->six){
        if(!(plan->corner_x<plan->length/2&&plan->corner_y<plan->width/2&&plan->center_spacing>0))return 0;
        const double left=plan->corner_x,right=plan->length-left;
        const double lower=plan->corner_y,upper=plan->width-lower;
        const double cx=plan->length/2.0,cy=plan->width/2.0,offset=plan->center_spacing/2.0;
        const double values[6][2]={{left,lower},{right,lower},{left,upper},{right,upper},{cx-offset,cy},{cx+offset,cy}};
        memcpy(centers,values,sizeof(values));
    }else{
        centers[0][0]=plan->x;
        centers[0][1]=plan->y;
    }
    for(int i=0;i<count;i++){
        const double x=centers[i][0],y=centers[i][1];
        if(!(x>radius&&x+radius<plan->length&&y>radius&&y+radius<plan->width))return 0;
        for(int j=0;j<i;j++){
            const double dx=x-centers[j][0],dy=y-centers[j][1];
            if(dx*dx+dy*dy<=plan->hole*plan->hole)return 0;
        }
    }
    return count;
}

static int panel_flow_six_holes(const wchar_t **text,PanelPressfitPlan *plan){
    const wchar_t *p=*text;
    if(!(sh_plate_take(&p,L"打")||sh_plate_take(&p,L"开"))||
       !(sh_plate_take(&p,L"六个")||(sh_plate_take(&p,L"6")&&sh_plate_take(&p,L"个"))))return 0;
    if((!panel_flow_delimiter(&p,L"ØøΦφ⌀")&&!sh_plate_take(&p,L"直径"))||
       !panel_flow_decimal(&p,&plan->hole)||!sh_plate_take(&p,L"通孔")||
       !panel_flow_delimiter(&p,L"，,；;")||!sh_plate_take(&p,L"四角")||
       !sh_plate_take(&p,L"横向边距")||!panel_flow_decimal(&p,&plan->corner_x)||
       !panel_flow_delimiter(&p,L"、，,")||!sh_plate_take(&p,L"纵向边距")||!panel_flow_decimal(&p,&plan->corner_y)||
       !panel_flow_delimiter(&p,L"，,；;")||!panel_flow_specification(&p,plan->spec)||
       !panel_flow_delimiter(&p,L"，,；;")||!sh_plate_take(&p,L"中间两孔")||
       !sh_plate_take(&p,L"孔距")||!panel_flow_decimal(&p,&plan->center_spacing)||
       !panel_flow_delimiter(&p,L"，,；;")||!panel_flow_specification(&p,plan->center_spec))return -1;

    double positions[6][2];
    plan->six=1;
    if(panel_six_layout(plan,positions)!=6)return -1;
    *text=p;
    return 1;
}

/* Zero means another request. A malformed request that clearly identifies
   this workflow must be refused as a unit before any native write. */
static int panel_flow_parse(const wchar_t *text,PanelPressfitPlan *plan){
    memset(plan,0,sizeof(*plan));
    if(!text)return 0;
    const wchar_t *p=sh_plate_spacing(text);
    sh_plate_take(&p,L"请");
    const int direct=sh_plate_take(&p,L"钣金压铆一体");
    if(direct){
        panel_flow_delimiter(&p,L"：:");
    }else{
        if(!(sh_plate_take(&p,L"按这个图号")||sh_plate_take(&p,L"按该图号")||sh_plate_take(&p,L"按图号"))||
           !wcsstr(p,L"钣金")||!wcsstr(p,L"孔")||!wcsstr(p,L"装"))return 0;
    }

    if(wcsnlen(text,512)>=512||wcspbrk(text,L"\r\n?？")||!panel_flow_drawing(&p,plan->drawing))return -1;

    static const wchar_t *const create[]={L"新建",L"创建",L"建立",L"建",L"做",L"画"};
    static const wchar_t *const amount[]={L"一个",L"一块",L"一件",L"一张"};
    static const wchar_t *const plate[]={L"矩形钣金平板",L"钣金平板",L"钣金零件",L"钣金件",L"钣金板"};
    ShPlateSize size;
    if(!sh_plate_take_one(&p,create,_countof(create)))return -1;
    sh_plate_take_one(&p,amount,_countof(amount));
    if(!sh_plate_size(&p,&size))return -1;
    sh_plate_take(&p,L"的");
    if(!sh_plate_take_one(&p,plate,_countof(plate))||!panel_flow_delimiter(&p,L"，,；;"))return -1;
    plan->length=size.length;
    plan->width=size.width;
    plan->thickness=size.thickness;
    const int six=panel_flow_six_holes(&p,plan);
    if(six<0||(!six&&(!panel_flow_hole(&p,plan)||!panel_flow_delimiter(&p,L"，,；;")||
       !panel_flow_specification(&p,plan->spec))))return -1;
    return panel_flow_save_tail(p,&plan->save)?1:-1;
}

static int panel_flow_revision_parse(const wchar_t *text,int *length,int *save){
    *save=0;
    *length=0;
    if(!text)return 0;
    const wchar_t *p=sh_plate_spacing(text);
    sh_plate_take(&p,L"请");
    if(!(sh_plate_take(&p,L"中间两个")||sh_plate_take(&p,L"中间两颗")||
         sh_plate_take(&p,L"中间两孔")))return 0;
    if(!(sh_plate_take(&p,L"改成")||sh_plate_take(&p,L"换成")||sh_plate_take(&p,L"改为")))return 0;
    double size;
    const wchar_t *number=p;
    const int by_length=panel_flow_decimal(&p,&size)&&sh_plate_take(&p,L"长");
    if(!by_length&&_wcsnicmp(number,L"BSO",3))return 0;
    if(wcsnlen(text,200)>=200||wcspbrk(text,L"\r\n?？"))return -1;
    if(by_length){
        if(size<1||size>100||size!=(int)size)return -1;
        *length=(int)size;
    }else {
        p=number;
        wchar_t model[MAX_PATH];
        PressfitSelector spec={0};
        if(!fastener_pressfit_model(&p,model)||!pressfit_selector(model,&spec)||
           !spec.bso_length||!spec.generic[0]||!wcsstr(spec.generic,L"M3"))return -1;
        *length=spec.bso_length;
    }
    return panel_flow_save_tail(p,save)?1:-1;
}
