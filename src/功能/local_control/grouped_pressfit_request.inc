/* Shared bounded grammar for the BWTL chat and local control client. */
enum {
    GROUP_PRESSFIT_MAX_GROUPS=8,
    GROUP_PRESSFIT_MAX_HOLES=12,
    GROUP_PRESSFIT_TOTAL_HOLES=GROUP_PRESSFIT_MAX_GROUPS*GROUP_PRESSFIT_MAX_HOLES,
    GROUP_PRESSFIT_MAX_NUMBER=128
};

typedef struct {
    wchar_t name[24],spec[64];
    int hole_count,holes[GROUP_PRESSFIT_MAX_HOLES];
} GroupPressfitGroup;

typedef struct {
    GroupPressfitGroup groups[GROUP_PRESSFIT_MAX_GROUPS];
    int group_count,save,panel,side,preview;
} GroupPressfitPlan;

typedef struct {
    wchar_t name[24],spec[64];
    int length,save,panel;
} GroupPressfitRevision;

static const wchar_t grouped_pressfit_usage[]=
    L"先发「查看可装配孔位」核对孔号，再发「按孔位分组装配：左组 1、4、6号孔 装 BSO M3×6；右组 2、3、5号孔 装 BSO M3×8」。"
    L"每个孔号只分给一组。需要写入当前项目时，在最后加「，保存到当前项目」。";

static const wchar_t grouped_pressfit_revision_usage[]=
    L"请指明已装配的组名与新规格，例如「右组改成8长，保存到当前项目」或「把右组改成 BSO M3×6」。";

static const wchar_t *grouped_pressfit_start(const wchar_t *input){
    const wchar_t *p=sh_plate_spacing(input?input:L"");
    sh_plate_take(&p,L"请");
    sh_plate_take(&p,L"帮我");
    return p;
}

static int grouped_pressfit_list_parse(const wchar_t *input){
    const wchar_t *p=grouped_pressfit_start(input);
    if(!(sh_plate_take(&p,L"查看可装配孔位")||sh_plate_take(&p,L"列出可装配孔位")||
         sh_plate_take(&p,L"查看压铆孔位")))return 0;
    panel_flow_delimiter(&p,L"。！!");
    return !*p;
}

static int grouped_pressfit_name_char(wchar_t c){
    return (c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')||
        (c>=0x4e00&&c<=0x9fff)||c==L'_'||c==L'-';
}

static int grouped_pressfit_name(const wchar_t **text,wchar_t name[24]){
    const wchar_t *start=sh_plate_spacing(*text),*p=start;
    while(*p!=L'组'&&grouped_pressfit_name_char(*p)&&p-start<23)p++;
    if(*p!=L'组'||p==start||p-start>=23||wcsncpy_s(name,24,start,p-start+1))return 0;
    *text=sh_plate_spacing(p+1);
    return 1;
}

static int grouped_pressfit_spec(const wchar_t **text,wchar_t spec[64],int implicit_bso){
    const int exact=workspace_pressfit_exact_spec(text,spec);
    if(exact)return exact>0;
    const wchar_t *p=*text;
    wchar_t raw[MAX_PATH];
    PressfitSelector selector={0};
    if(implicit_bso&&towupper(*p)==L'M'){
        const size_t length=wcsspn(p,L"Mm0123456789.xX×*＊");
        if(length<4||length>=_countof(raw)-3)return 0;
        wcscpy_s(raw,_countof(raw),L"BSO");
        if(wcsncpy_s(raw+3,_countof(raw)-3,p,length))return 0;
        p+=length;
    }else if(!fastener_pressfit_model(&p,raw))return 0;

    size_t used=0;
    for(const wchar_t *at=raw;*at;at++){
        if(iswspace(*at))continue;
        if(used+1>=64)return 0;
        spec[used++]=wcschr(L"×*＊",*at)?L'X':towupper(*at);
    }
    spec[used]=0;
    if(!pressfit_selector(spec,&selector)||!selector.generic[0]||!selector.bso_length)return 0;
    *text=sh_plate_spacing(p);
    return 1;
}

static int grouped_pressfit_hole_number(const wchar_t **text,int *id){
    const wchar_t *p=*text;
    if(*p<L'1'||*p>L'9')return 0;
    wchar_t *end=NULL;
    const long number=wcstol(p,&end,10);
    if(number>GROUP_PRESSFIT_MAX_NUMBER)return 0;
    *id=(int)number;
    *text=sh_plate_spacing(end);
    return 1;
}

static int grouped_pressfit_hole_list(const wchar_t **text,GroupPressfitGroup *group,unsigned char *seen){
    const wchar_t *p=*text;
    const int bracket=panel_flow_delimiter(&p,L"（(");
    for(;;){
        int id;
        sh_plate_take(&p,L"第");
        if(group->hole_count>=GROUP_PRESSFIT_MAX_HOLES||!grouped_pressfit_hole_number(&p,&id)||seen[id])return 0;
        group->holes[group->hole_count++]=id;
        seen[id]=1;
        if(!sh_plate_take(&p,L"号孔"))sh_plate_take(&p,L"号");
        const wchar_t *next=p;
        if(!panel_flow_delimiter(&next,L"、,，/+＋"))break;
        const wchar_t *number=next;
        sh_plate_take(&number,L"第");
        if(*number<L'1'||*number>L'9')break;
        p=next;
    }
    sh_plate_take(&p,L"孔");
    if(bracket&&!panel_flow_delimiter(&p,L"）)"))return 0;
    *text=p;
    return 1;
}

/* 0 leaves ordinary chat alone. A recognized malformed write returns -1 and
   can never fall through to another model-changing request handler. */
static int grouped_pressfit_install_parse(const wchar_t *input,GroupPressfitPlan *plan){
    *plan=(GroupPressfitPlan){0};
    const wchar_t *p=grouped_pressfit_start(input);
    if(!(sh_plate_take(&p,L"按孔位分组装配")||sh_plate_take(&p,L"按孔号分组装配")||
         sh_plate_take(&p,L"分组装配压铆件")||sh_plate_take(&p,L"按孔位分组安装")||sh_plate_take(&p,L"分组装配")))return 0;
    if(wcsnlen(input,2048)>=2048||wcspbrk(input,L"\r\n?？"))return -1;
    panel_flow_delimiter(&p,L"：:");
    unsigned char seen[GROUP_PRESSFIT_MAX_NUMBER+1]={0};
    do{
        if(plan->group_count>=GROUP_PRESSFIT_MAX_GROUPS)return -1;
        GroupPressfitGroup *group=&plan->groups[plan->group_count];
        if(!grouped_pressfit_name(&p,group->name))return -1;
        for(int i=0;i<plan->group_count;i++)if(!_wcsicmp(plan->groups[i].name,group->name))return -1;
        panel_flow_delimiter(&p,L"：:");
        if(!grouped_pressfit_hole_list(&p,group,seen))return -1;
        sh_plate_take(&p,L"各");
        if(!(sh_plate_take(&p,L"装配")||sh_plate_take(&p,L"安装")||sh_plate_take(&p,L"装")))return -1;
        if(!sh_plate_take(&p,L"一颗")&&!sh_plate_take(&p,L"一个"))sh_plate_take(&p,L"一件");
        if(!grouped_pressfit_spec(&p,group->spec,0))return -1;
        plan->group_count++;
        int save=0;
        if(panel_flow_save_tail(p,&save)){
            plan->save=save;
            return 1;
        }
    }while(panel_flow_delimiter(&p,L"；;，,"));
    return -1;
}

static int grouped_pressfit_revision_parse(const wchar_t *input,GroupPressfitRevision *revision){
    *revision=(GroupPressfitRevision){0};
    const wchar_t *p=grouped_pressfit_start(input);
    sh_plate_take(&p,L"只");
    if(!sh_plate_take(&p,L"把"))sh_plate_take(&p,L"将");
    if(!grouped_pressfit_name(&p,revision->name))return 0;
    if(!sh_plate_take(&p,L"的紧固件")&&!sh_plate_take(&p,L"的压铆件"))
        sh_plate_take(&p,L"的");
    const int by_length=sh_plate_take(&p,L"长度");
    if(!(sh_plate_take(&p,L"更换为")||sh_plate_take(&p,L"改成")||sh_plate_take(&p,L"换成")||
         sh_plate_take(&p,L"改为")||sh_plate_take(&p,L"换装")))return 0;

    const wchar_t *start=p;
    double length;
    if(panel_flow_decimal(&p,&length)&&(sh_plate_take(&p,L"长")||by_length)){
        if(length<1||length>=1000||length!=(int)length)return -1;
        revision->length=(int)length;
    }else{
        if(wcsncmp(start,L"型号",2)&&_wcsnicmp(start,L"BSO",3)&&(towupper(*start)!=L'M'||start[1]<L'0'||start[1]>L'9'))return 0;
        p=start;
        if(by_length||!grouped_pressfit_spec(&p,revision->spec,1))return -1;
    }
    if(wcsnlen(input,256)>=256||wcspbrk(input,L"\r\n?？"))return -1;
    return panel_flow_save_tail(p,&revision->save)?1:-1;
}
