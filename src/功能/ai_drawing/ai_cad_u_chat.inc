/* One deterministic CAD route using the same company workflow as blank parts. */
static const wchar_t cad_u_feature[]=L"BWTL_U_PROFILE";

typedef struct {
    ModelingPartIntent template;
    double length,width,height,thickness,radius;
    int default_radius,learned;
    wchar_t recipe[MAX_PATH];
} CadURequest;

static ProError cad_u_learn_recipe(ProMdl source,PartText *out);
static int cad_u_find_recipe(double *gauge,double *radius,wchar_t path[MAX_PATH]);

static CadURequest cad_u_pending;
static struct {
    ProMdl model;
    ProMdlName name;
    ProPath directory;
} cad_u_recent;

static int cad_u_number(const wchar_t **input,double *value){
    const wchar_t *p=*input;
    while(iswspace(*p))p++;
    if(*p<L'0'||*p>L'9')return 0;
    wchar_t *end;
    double number=wcstod(p,&end);
    if(!isfinite(number)||number<=0||number>10000)return 0;
    p=end;
    while(iswspace(*p))p++;
    if(!_wcsnicmp(p,L"mm",2))p+=2;
    else assembly_take(&p,L"毫米");
    *input=p;
    *value=number;
    return 1;
}

static int cad_u_dimensions(const wchar_t **input,CadURequest *spec){
    const wchar_t *p=*input;
    double *axes[]={&spec->length,&spec->width,&spec->height};
    for(int i=0;i<_countof(axes);i++){
        if(!cad_u_number(&p,axes[i]))return 0;
        if(i==2)break;
        while(iswspace(*p))p++;
        if(!*p||!wcschr(L"×xX*＊-－",*p))return 0;
        p++;
    }
    *input=p;
    return 1;
}

static int cad_u_parse(const wchar_t *text,CadURequest *spec){
    if(smart_readonly_override(text)||wcspbrk(text,L"?？\r\n"))return 0;
    const wchar_t *p=model_request_action_start(text);
    if(assembly_take(&p,L"按公司流程")||assembly_take(&p,L"按公司标准"))
        p=model_request_action_start(modeling_part_skip(p));
    spec->learned=assembly_take(&p,L"按学到的做法")||assembly_take(&p,L"按已学做法")||
        assembly_take(&p,L"按学过的做法");
    p=model_request_action_start(modeling_part_skip(p));
    int single=assembly_take(&p,L"单独");
    p=model_request_action_start(modeling_part_skip(p));
    if(!(assembly_take(&p,L"新建")||assembly_take(&p,L"创建")||assembly_take(&p,L"画")||
         assembly_take(&p,L"做")||assembly_take(&p,L"绘制")))return 0;
    p=modeling_part_skip(p);
    if(!assembly_take(&p,L"一个")&&!assembly_take(&p,L"一件"))assembly_take(&p,L"个");
    p=modeling_part_skip(p);
    if(!assembly_take(&p,L"钣金件"))assembly_take(&p,L"钣金");
    p=modeling_part_skip(p);
    if(*p!=L'U'&&*p!=L'u'&&*p!=L'Ｕ'&&*p!=L'ｕ')return 0;
    p=modeling_part_skip(p+1);
    if(!(assembly_take(&p,L"形")||assembly_take(&p,L"型")))return 0;
    p=modeling_part_skip(p);
    if(!assembly_take(&p,L"加强筋"))return 0;
    p=modeling_part_skip(p);
    if(!assembly_take(&p,L"外形尺寸")&&!assembly_take(&p,L"尺寸"))assembly_take(&p,L"外形");
    p=modeling_part_skip(p);
    if(!cad_u_dimensions(&p,spec))return 0;
    p=modeling_part_skip(p);
    if(!(assembly_take(&p,L"板厚")||assembly_take(&p,L"厚度")||assembly_take(&p,L"料厚")))return 0;
    p=modeling_part_skip(p);
    if(!cad_u_number(&p,&spec->thickness))return 0;
    p=modeling_part_skip(p);
    if(assembly_take(&p,L"内折弯半径")||assembly_take(&p,L"内半径")||
        assembly_take(&p,L"内R")||assembly_take(&p,L"内r")){
        p=modeling_part_skip(p);
        if(!cad_u_number(&p,&spec->radius))return 0;
        p=modeling_part_skip(p);
    }else{
        spec->radius=spec->thickness;
        spec->default_radius=1;
    }
    /* The same native naming/authorization parser validates optional clauses. */
    wchar_t naming[240];
    if(swprintf_s(naming,_countof(naming),L"%ls创建一个钣金零件，%ls",
        single?L"单独":L"",p)<0||!modeling_part_parse(naming,&spec->template))return 0;
    if(!single&&spec->template.scope==MODELING_PART_NEW)spec->template.require_company=1;
    return 1;
}

static void cad_u_record(ProMdl part,const wchar_t *action,ProError error,const wchar_t *text){
    ProMdl window=NULL;
    ProMdlName name;
    ProPath directory;
    int window_id;
    if(ProWindowCurrentGet(&window_id)||ProWindowMdlGet(window_id,&window)||part!=window||
       ProMdlMdlnameGet(part,name)||ProDirectoryCurrentGet(directory))return;
    chat_task_record_bound(part,window,name,directory,action,error,text,sh_recovery_uncertain,smart_steps+1);
    if(!error){
        cad_u_recent.model=part;
        wcscpy_s(cad_u_recent.name,_countof(cad_u_recent.name),name);
        wcscpy_s(cad_u_recent.directory,_countof(cad_u_recent.directory),directory);
    }
}

static void cad_u_warn_flanges(void){
    PartCommand operation={.verb=PART_FOUNDATION,.count=1};
    wcscpy_s(operation.arg[0],_countof(operation.arg[0]),L"SMT_FLANGE");
    bwtl_warn_traceback(&operation);
}

static ProError cad_u_make(ProMdl part,ProMdl assembly,PartText *out){
    cad_u_warn_flanges();
    const CadURequest *spec=&cad_u_pending;
    ProError error=bwtl_u_reinforcement(part,cad_u_feature,spec->length,spec->width,spec->height,
        spec->thickness,spec->radius,out);
    if(!error&&spec->recipe[0])
        part_text_add(out,L"\n已读取建模方法并用新尺寸重建。\n%ls\n",spec->recipe);
    if(!error&&spec->default_radius)
        part_text_add(out,L"\n本次按板厚取内折弯半径 %.8g mm。",spec->radius);
    cad_u_record(part,L"创建 U 形加强筋并读回外形",error,out->text);
    return error;
}

static int cad_u_height_request(const wchar_t *text,double *height){
    if(smart_readonly_override(text)||wcspbrk(text,L"?？\r\n"))return 0;
    const wchar_t *p=model_request_action_start(text);
    assembly_take(&p,L"把");
    (void)(assembly_take(&p,L"刚才那个U形加强筋的")||assembly_take(&p,L"加强筋的")||
        assembly_take(&p,L"这个U形加强筋的")||assembly_take(&p,L"当前U形加强筋的"));
    if(!(assembly_take(&p,L"外高")||assembly_take(&p,L"总高"))||
       !(assembly_take(&p,L"改成")||assembly_take(&p,L"改为")||
         assembly_take(&p,L"改到")||assembly_take(&p,L"调整到")))return 0;
    return cad_u_number(&p,height)&&!*modeling_part_skip(p);
}

static int cad_u_learn_request(const wchar_t *text){
    if(smart_readonly_override(text)||wcspbrk(text,L"?？\r\n"))return 0;
    const wchar_t *p=model_request_action_start(text);
    if(!assembly_take(&p,L"学习")||!assembly_take(&p,L"当前"))return 0;
    p=modeling_part_skip(p);
    if(!*p||!wcschr(L"UuＵｕ",*p))return 0;
    p=modeling_part_skip(p+1);
    if((!assembly_take(&p,L"形")&&!assembly_take(&p,L"型"))||!assembly_take(&p,L"加强筋"))return 0;
    assembly_take(&p,L"的");
    if(!assembly_take(&p,L"建模方法"))assembly_take(&p,L"做法");
    return !*modeling_part_skip(p);
}

static int cad_u_use_recipe(CadURequest *spec){
    double gauge,radius;
    if(!cad_u_find_recipe(&gauge,&radius,spec->recipe))return 0;
    if(spec->default_radius&&fabs(radius-gauge)>0.002){
        spec->radius=radius;
        spec->default_radius=0;
    }
    return 1;
}

static int cad_u_chat(const wchar_t *text){
    if(cad_u_learn_request(text)){
        if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
        smart_begin(text);smart_local_plan=1;
        append_card(L"你",text);save_turn(L"用户",text);native_prompt_set(L"");
        ProMdl model=NULL,owner=NULL;
        int window=0;
        wchar_t report[1800]=L"";
        PartText out={report,_countof(report),0};
        ProError error=ProMdlCurrentGet(&model);
        if(!error)error=ProWindowCurrentGet(&window);
        if(!error)error=ProWindowMdlGet(window,&owner);
        if(!error)error=model&&model==owner?cad_u_learn_recipe(model,&out):PRO_TK_BAD_CONTEXT;
        if(error==PRO_TK_CANT_WRITE)
            part_text_add(&out,L"已读到样件的做法，但无法写入当前实例的数据盘。请检查 E 盘的建模方法库。");
        else if(error)
            part_text_add(&out,L"建模方法未保存（Creo %d）。请打开带矩形首壁、两道同向全长 90° 折弯的钣金加强筋。"
                L"该做法要求两道折弯的高度和内 R 一致。",error);
        smart_reply(report);
        return 1;
    }
    CadURequest spec={0};
    if(cad_u_parse(text,&spec)){
        if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
        if(spec.learned&&!cad_u_use_recipe(&spec)){
            smart_begin(text);smart_local_plan=1;
            append_card(L"你",text);save_turn(L"用户",text);native_prompt_set(L"");
            smart_reply(L"建模方法库还没有可调用的 U 形加强筋做法。打开样件后发送「学习当前 U 形加强筋的做法」。");
            return 1;
        }
        if(spec.width<=2*(spec.radius+spec.thickness)||spec.height<=spec.radius+spec.thickness){
            smart_begin(text);smart_local_plan=1;
            append_card(L"你",text);save_turn(L"用户",text);native_prompt_set(L"");
            smart_reply(L"这些尺寸无法形成两侧直壁。外宽要大于两倍「板厚＋内折弯半径」，外高要大于「板厚＋内折弯半径」。");
            return 1;
        }
        cad_u_pending=spec;
        return default_part_begin_request(text,&spec.template,cad_u_make);
    }
    double height;
    if(!cad_u_height_request(text,&height))return 0;
    ProMdl model=NULL;
    ProFeature feature;
    const int has_model=!ProMdlCurrentGet(&model)&&!ux_named_feature(model,cad_u_feature,&feature);
    const int refers_prior=wcsstr(text,L"刚才")!=NULL;
    if(!has_model&&!(refers_prior&&cad_u_recent.model))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    smart_begin(text);smart_local_plan=1;
    append_card(L"你",text);save_turn(L"用户",text);native_prompt_set(L"");
    wchar_t receipt[4000]=L"";
    PartText out={receipt,_countof(receipt),0};
    int window;
    ProMdl owner=NULL;
    ProPath directory;
    ProMdlName name;
    ProError error=has_model?ProWindowCurrentGet(&window):PRO_TK_E_NOT_FOUND;
    if(!error)error=ProWindowMdlGet(window,&owner);
    if(!error)error=ProDirectoryCurrentGet(directory);
    if(!error)error=ProMdlMdlnameGet(model,name);
    if(refers_prior&&cad_u_recent.model&&(error||model!=cad_u_recent.model||
       _wcsicmp(name,cad_u_recent.name)||_wcsicmp(directory,cad_u_recent.directory))){
        part_text_add(&out,L"刚才完成的 U 形加强筋是 %ls.PRT。请先切到该钣金件的窗口和工作目录。",cad_u_recent.name);
        smart_reply(receipt);
        return 1;
    }
    if(!error&&owner!=model)error=PRO_TK_BAD_CONTEXT;
    if(!error){
        cad_u_warn_flanges();
        error=bwtl_u_reinforcement_resize_height(model,cad_u_feature,height,&out);
    }
    if(error&&!receipt[0])
        part_text_add(&out,L"调整 U 形加强筋外高未完成（Creo %d）。请核对当前零件与实际特征定义。",error);
    cad_u_record(model,L"修改 U 形加强筋外高并读回外形",error,receipt);
    smart_steps=1;
    smart_progress=!error;
    smart_failures=!!error;
    smart_record_tool(receipt);
    smart_reply(receipt);
    return 1;
}
