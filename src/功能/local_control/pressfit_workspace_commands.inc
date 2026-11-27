/* Chat and the local client share this strict dispatcher. A recognized,
   incomplete command stays here and never falls through to CAD generation. */
typedef enum {
    PW_NONE,PW_HELP,PW_PANELS,PW_LIST,PW_FIT,PW_REVISION,PW_SELECTED,PW_MARKUP,PW_DIAMETER,
    PW_AUDIT,PW_FILL,PW_RESUME,PW_EDIT,PW_DIFF,PW_LEARN,PW_REUSE,PW_MIRROR,
    PW_RECOMMEND,PW_ALTERNATIVE,PW_STACK,PW_DIRECT_SCREW,PW_PDF,PW_BOM,PW_IMAGE
} PwAction;

typedef struct {
    PwAction action;
    const wchar_t *argument;
    int panel,side,preview;
} PwCommand;

static int pw_token(const wchar_t **p,wchar_t *value,size_t capacity){
    const wchar_t *start=sh_plate_spacing(*p),*end=start;
    while(*end&&!iswspace(*end)&&!wcschr(L"；;，,。",*end))end++;
    if(end==start||wcsncpy_s(value,capacity,start,end-start))return 0;
    *p=sh_plate_spacing(end);
    return 1;
}

static int pw_panel_id(const wchar_t **input,int *id){
    const wchar_t *p=sh_plate_spacing(*input);
    if(_wcsnicmp(p,L"ID:",3))return 0;
    p+=3;
    if(*p<L'1'||*p>L'9')return 0;
    wchar_t *end;
    const long value=wcstol(p,&end,10);
    if(value<1||value>=INT_MAX)return 0;
    *id=(int)value;
    *input=sh_plate_spacing(end);
    return 1;
}

static PwCommand pw_command(const wchar_t *text){
    PwCommand command={0};
    const wchar_t *p=grouped_pressfit_start(text);
    const int qualified=sh_plate_take(&p,L"板件");
    if(qualified&&!pw_panel_id(&p,&command.panel))return command;
    if(sh_plate_take(&p,L"反面")||sh_plate_take(&p,L"从反面"))command.side=-1;
    else if(sh_plate_take(&p,L"安装面")){
        if(_wcsnicmp(p,L"SURFACE:",8))return command;
        const wchar_t *end=p;
        if(!fastener_pressfit_id(&end,&command.side))return command;
        p=sh_plate_spacing(end);
    }
    command.preview=sh_plate_take(&p,L"预检");
    const wchar_t *start=p;
    const struct {const wchar_t *phrase;PwAction action;} phrases[]={
        {L"压铆分组帮助",PW_HELP},{L"列出压铆板件",PW_PANELS},
        {L"给选中的孔分组",PW_SELECTED},{L"给截图孔分组",PW_MARKUP},
        {L"按孔径分组",PW_DIAMETER},{L"检查压铆分组",PW_AUDIT},
        {L"补装压铆分组",PW_FILL},{L"恢复分组装配",PW_RESUME},
        {L"编辑压铆组",PW_EDIT},{L"检查改板影响",PW_DIFF},
        {L"保存压铆工艺",PW_LEARN},{L"复用压铆工艺",PW_REUSE},{L"镜像压铆组",PW_MIRROR},
        {L"推荐压铆件",PW_RECOMMEND},{L"替代压铆规格",PW_ALTERNATIVE},{L"顺序装配",PW_STACK},{L"螺钉旋入柱孔",PW_DIRECT_SCREW},
        {L"核对PDF孔位表",PW_PDF},{L"导出压铆材料表",PW_BOM},{L"标注压铆检查截图",PW_IMAGE}
    };
    for(int i=0;i<_countof(phrases);i++){
        p=start;
        if(!sh_plate_take(&p,phrases[i].phrase))continue;
        panel_flow_delimiter(&p,L"：:｜|");
        command.action=phrases[i].action;
        command.argument=p;
        return command;
    }
    if(!(qualified||command.side||command.preview))return command;
    GroupPressfitPlan plan;
    GroupPressfitRevision revision;
    if(grouped_pressfit_list_parse(start))command.action=PW_LIST;
    else if(grouped_pressfit_install_parse(start,&plan))command.action=PW_FIT;
    else if(grouped_pressfit_revision_parse(start,&revision))command.action=PW_REVISION;
    command.argument=start;
    return command;
}

static ProError pw_selected_fit(ProMdl assembly,const PwCommand *command,PartText *out){
    const wchar_t *p=command->argument;
    int save=0;
    double diameter=0;
    GroupPressfitGroup except={0};
    if(command->action==PW_DIAMETER){
        panel_flow_delimiter(&p,L"ØøΦφ⌀");
        if(!panel_flow_decimal(&p,&diameter)||diameter<=0)return PRO_TK_BAD_INPUTS;
        if(sh_plate_take(&p,L"排除")){
            unsigned char seen[GROUP_PRESSFIT_MAX_NUMBER+1]={0};
            if(!grouped_pressfit_hole_list(&p,&except,seen))return PRO_TK_BAD_INPUTS;
        }
        if(!panel_flow_delimiter(&p,L"；;：:"))return PRO_TK_BAD_INPUTS;
    }
    GroupPressfitPlan plan={.panel=command->panel,.side=command->side,.preview=command->preview,.group_count=1};
    GroupPressfitGroup *target=&plan.groups[0];
    if(!grouped_pressfit_name(&p,target->name)||
       !(sh_plate_take(&p,L"装配")||sh_plate_take(&p,L"安装")||sh_plate_take(&p,L"装"))||
       !grouped_pressfit_spec(&p,target->spec,0)||!panel_flow_save_tail(p,&save))return PRO_TK_BAD_INPUTS;
    plan.save=save;
    if(command->action==PW_DIAMETER){
        GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
        if(!geometry)return PRO_TK_GENERAL_ERROR;
        ProError error=group_pressfit_geometry_side(assembly,plan.panel,plan.side,geometry,out);
        if(!error)error=group_pressfit_holediam_select(geometry,diameter,except.holes,except.hole_count,
            target->holes,&target->hole_count,out);
        if(!error)plan.panel=geometry->panel_id;
        free(geometry);
        if(error)return error;
    }else{
        PressfitWorkspaceSelection selection={0};
        ProError error=pressfit_workspace_selected(assembly,command->action==PW_MARKUP,command->side,&selection,out);
        if(error)return error;
        if(plan.panel&&plan.panel!=selection.panel_id){
            part_text_add(out,L"所选圆孔在板件 ID:%d，指令指定了 ID:%d。\n",selection.panel_id,plan.panel);
            return PRO_TK_BAD_CONTEXT;
        }
        plan.panel=selection.panel_id;
        plan.side=selection.side;
        target->hole_count=selection.count;
        memcpy(target->holes,selection.numbers,(size_t)selection.count*sizeof(selection.numbers[0]));
    }
    return grouped_pressfit_install(assembly,&plan,out);
}

static ProError pw_edit_command(ProMdl assembly,const PwCommand *command,PartText *out){
    static const struct {const wchar_t *verb;PressfitWorkspaceEdit edit;} operations[]={
        {L"改名",PW_RENAME},{L"合并",PW_MERGE},{L"拆分",PW_SPLIT},{L"移出",PW_DROP},{L"加入",PW_ADD}
    };
    const wchar_t *p=command->argument;
    const wchar_t *start=p;
    PressfitWorkspaceEdit edit=PW_RENAME;
    int matched=0,save=0;
    for(int i=0;i<_countof(operations);i++){
        p=start;
        if(!sh_plate_take(&p,operations[i].verb))continue;
        edit=operations[i].edit;
        matched=1;
        break;
    }
    wchar_t group[24];
    GroupPressfitGroup other={0};
    if(!matched||!grouped_pressfit_name(&p,group))return PRO_TK_BAD_INPUTS;
    if(edit>=PW_SPLIT){
        unsigned char seen[GROUP_PRESSFIT_MAX_NUMBER+1]={0};
        if(!grouped_pressfit_hole_list(&p,&other,seen))return PRO_TK_BAD_INPUTS;
    }
    if(edit==PW_RENAME||edit==PW_MERGE||edit==PW_SPLIT){
        sh_plate_take(&p,L"到");
        if(!grouped_pressfit_name(&p,other.name))return PRO_TK_BAD_INPUTS;
    }
    if(!panel_flow_save_tail(p,&save))return PRO_TK_BAD_INPUTS;
    return pw_edit(assembly,command->panel,edit,group,&other,command->side,save,out);
}

static ProError pw_methods_command(ProMdl assembly,const PwCommand *command,PartText *out){
    const wchar_t *p=command->argument;
    wchar_t name[24],other[24];
    int save=0;
    if(!grouped_pressfit_name(&p,name))return PRO_TK_BAD_INPUTS;
    if(command->action==PW_LEARN){
        if(!grouped_pressfit_name(&p,other)||!panel_flow_save_tail(p,&save))return PRO_TK_BAD_INPUTS;
        return pw_store_method(assembly,command->panel,name,other,out);
    }
    if(command->action==PW_REUSE){
        GroupPressfitGroup target={0};
        unsigned char seen[GROUP_PRESSFIT_MAX_NUMBER+1]={0};
        if(!grouped_pressfit_name(&p,target.name)||!grouped_pressfit_hole_list(&p,&target,seen)||
           !panel_flow_save_tail(p,&save))return PRO_TK_BAD_INPUTS;
        return pw_apply_method(assembly,command->panel,name,&target,save,out);
    }
    int target=0,axis;
    sh_plate_take(&p,L"到");
    if(!pw_panel_id(&p,&target)||!grouped_pressfit_name(&p,other))return PRO_TK_BAD_INPUTS;
    const wchar_t *xyz=wcschr(L"XYZ",towupper(*p));
    if(!*p||!xyz)return PRO_TK_BAD_INPUTS;
    axis=towupper(*p)-L'X';
    p=sh_plate_spacing(p+1);
    if(!panel_flow_delimiter(&p,L"=＝"))return PRO_TK_BAD_INPUTS;
    wchar_t *end=NULL;
    const double measure=wcstod(p,&end);
    if(end==p||!isfinite(measure)||!panel_flow_save_tail(sh_plate_spacing(end),&save))return PRO_TK_BAD_INPUTS;
    return pw_mirror(assembly,command->panel,name,target,other,command->side,axis,measure,save,out);
}

static ProError pw_recommend_command(ProMdl assembly,const PwCommand *command,PartText *out){
    const wchar_t *p=command->argument;
    int hole;
    sh_plate_take(&p,L"第");
    if(!grouped_pressfit_hole_number(&p,&hole))return PRO_TK_BAD_INPUTS;
    sh_plate_take(&p,L"号孔");
    wchar_t thread[24]=L"",kind[24]=L"",spec[64]=L"";
    if(command->action==PW_ALTERNATIVE){
        if(!grouped_pressfit_spec(&p,spec,0))return PRO_TK_BAD_INPUTS;
    }else{
        if(towupper(*p)==L'M'&&!pw_token(&p,thread,_countof(thread)))return PRO_TK_BAD_INPUTS;
        if(*p&&!wcschr(L"。！!",*p)&&!pw_token(&p,kind,_countof(kind)))return PRO_TK_BAD_INPUTS;
    }
    int save=0;
    if(!panel_flow_save_tail(p,&save)||save)return PRO_TK_BAD_INPUTS;
    return workspace_pressfit_recommend(assembly,command->panel,hole,thread,kind,spec,out);
}

static ProError pw_stack_command(ProMdl assembly,const PwCommand *command,PartText *out){
    const wchar_t *first=command->argument;
    const wchar_t *bar=wcspbrk(first,L"|｜");
    const wchar_t *second=bar?wcspbrk(bar+1,L"|｜"):NULL;
    wchar_t plan_text[1800],electronics[2200];
    GroupPressfitPlan plan;
    if(!bar||!second||wcspbrk(second+1,L"|｜")||
       wcsncpy_s(plan_text,_countof(plan_text),first,bar-first)||
       wcsncpy_s(electronics,_countof(electronics),bar+1,second-bar-1)||
       grouped_pressfit_install_parse(plan_text,&plan)!=1)return PRO_TK_BAD_INPUTS;
    plan.panel=command->panel;
    plan.side=command->side;
    plan.preview=command->preview;
    return workspace_pressfit_stack(assembly,&plan,sh_plate_spacing(electronics),sh_plate_spacing(second+1),out);
}

static ProError pw_delivery_command(ProMdl assembly,const PwCommand *command,PartText *out){
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    PressfitDeliveryRow *rows=calloc(GROUP_PRESSFIT_MAX_NUMBER,sizeof(*rows));
    if(!geometry||!rows){free(geometry);free(rows);return PRO_TK_GENERAL_ERROR;}
    GroupedPressfitRecord record={0};
    ProError error=grouped_pressfit_open(assembly,command->panel,&record);
    if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    if(!error)error=group_pressfit_geometry(assembly,record.panel,geometry,out);
    ProMdlName panel;
    if(!error)error=ProMdlMdlnameGet(geometry->panel.model,panel);
    for(int i=0;!error&&i<geometry->count;i++){
        const GroupPressfitHole *hole=&geometry->holes[i];
        PressfitDeliveryRow *row=&rows[i];
        row->panel=panel;
        row->panel_id=geometry->panel_id;
        row->number=i+1;
        row->bore_id=hole->hole_id;
        row->face_id=hole->face_id;
        row->diameter=hole->diameter;
        row->depth=hole->depth;
        for(int j=0;j<3;j++){
            row->center[j]=hole->center[j];
            row->world[j]=hole->contact[j]/geometry->mm;
        }
    }
    for(int i=0;!error&&i<record.count;i++){
        const GroupedPressfitGroup *group=&record.groups[i];
        GroupedPressfitView view={0};
        wchar_t detail[1200]=L"";
        PartText report={detail,_countof(detail),0};
        const ProError checked=grouped_pressfit_read_view(assembly,geometry,group,2,&view,&report);
        for(int j=0;j<group->count;j++){
            const GroupPressfitHole *hole=&group->sites[j].hole;
            const int number=pw_hole_number(geometry,hole);
            if(!number){
                part_text_add(out,L"%ls有已变动的孔，应先检查改板影响。该孔编号无法确认。\n",group->name);
                continue;
            }
            PressfitDeliveryRow *row=&rows[number-1];
            row->group=group->name;
            row->spec=group->pending[0]?group->pending:group->spec;
            row->component_id=view.ids[j];
            row->status=checked||group->pending[0]?3:
                !view.ids[j]?2:view.ids[j]!=group->sites[j].id?3:1;
            row->bore_id=hole->hole_id;
            row->face_id=hole->face_id;
            for(int k=0;k<3;k++)row->world[k]=hole->contact[k]/geometry->mm;
        }
    }
    if(!error&&command->action==PW_PDF){
        wchar_t path[MAX_PATH];
        error=pressfit_delivery_pdf_attachment(path,out);
        if(!error)error=pressfit_delivery_pdf_compare(path,rows,geometry->count,out);
    }else if(!error)error=pressfit_delivery_export(assembly,rows,geometry->count,command->action==PW_IMAGE,out);
    pressfit_checkpoint_close(&record.scope);
    free(rows);
    free(geometry);
    return error;
}

static const wchar_t pw_help[]=
    L"压铆分组可指定「板件 ID:42」及「反面」。先用「列出压铆板件」「查看可装配孔位」核对。\n"
    L"选孔可说「给选中的孔分组：左组 装 BSO M3×6」，也可在截图窗口圈孔后用「给截图孔分组」。\n"
    L"其他操作包括「按孔径分组」「预检 分组装配」「检查压铆分组」「编辑压铆组」「补装压铆分组」「恢复分组装配」「检查改板影响」。\n"
    L"可用「推荐压铆件」「替代压铆规格」「顺序装配」「螺钉旋入柱孔」「保存压铆工艺」「复用压铆工艺」「镜像压铆组」「核对PDF孔位表」「导出压铆材料表」「标注压铆检查截图」。完整语法见本机《BWTL 压铆工作流程》。";

static ProError pw_execute(ProMdl assembly,const PwCommand *command,PartText *out){
    if(command->action==PW_HELP){part_text_add(out,L"%ls",pw_help);return PRO_TK_NO_ERROR;}
    if(command->preview&&command->action!=PW_FIT&&command->action!=PW_SELECTED&&
       command->action!=PW_MARKUP&&command->action!=PW_DIAMETER){
        part_text_add(out,L"「预检」可用于分组装配、直接选孔、截图选孔和按孔径分组。本次未执行改动。\n");
        return PRO_TK_BAD_INPUTS;
    }
    switch(command->action){
        case PW_PANELS:return pressfit_workspace_panels(assembly,out);
        case PW_LIST:return grouped_pressfit_list_for(assembly,command->panel,command->side,out);
        case PW_FIT:{
            GroupPressfitPlan plan;
            if(grouped_pressfit_install_parse(command->argument,&plan)!=1)return PRO_TK_BAD_INPUTS;
            plan.panel=command->panel;plan.side=command->side;plan.preview=command->preview;
            return grouped_pressfit_install(assembly,&plan,out);
        }
        case PW_REVISION:{
            GroupPressfitRevision revision;
            if(grouped_pressfit_revision_parse(command->argument,&revision)!=1||command->preview||command->side)
                return PRO_TK_BAD_INPUTS;
            revision.panel=command->panel;
            return grouped_pressfit_revise(assembly,&revision,out);
        }
        case PW_SELECTED:case PW_MARKUP:case PW_DIAMETER:return pw_selected_fit(assembly,command,out);
        case PW_AUDIT:case PW_FILL:case PW_RESUME:{
            wchar_t name[24]=L"";
            const wchar_t *p=command->argument;
            grouped_pressfit_name(&p,name);
            int save=0;
            if(!panel_flow_save_tail(p,&save)||(command->action==PW_AUDIT&&save))return PRO_TK_BAD_INPUTS;
            const wchar_t *given=name[0]?name:NULL;
            if(command->action==PW_AUDIT)return pw_audit(assembly,command->panel,given,out);
            return pw_recover(assembly,command->panel,given,command->action==PW_FILL,save,out);
        }
        case PW_EDIT:return pw_edit_command(assembly,command,out);
        case PW_DIFF:return pw_board_diff(assembly,command->panel,out);
        case PW_LEARN:case PW_REUSE:case PW_MIRROR:return pw_methods_command(assembly,command,out);
        case PW_RECOMMEND:case PW_ALTERNATIVE:return pw_recommend_command(assembly,command,out);
        case PW_STACK:return pw_stack_command(assembly,command,out);
        case PW_DIRECT_SCREW:return workspace_pressfit_direct(assembly,command->argument,command->panel,out);
        case PW_PDF:case PW_BOM:case PW_IMAGE:return pw_delivery_command(assembly,command,out);
        default:return PRO_TK_BAD_INPUTS;
    }
}