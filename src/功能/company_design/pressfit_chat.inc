enum { PRESSFIT_READ=1,PRESSFIT_SAVE,PRESSFIT_PLACE };

typedef struct {
    int action,expected,shaft,seat,nut_style,choose_side,follow_nut,short_form;
    int all_on_face,view_side,nearest_bend,flip_side,face_id,recommend,side_zone,follow_length,reuse_prior,replace,resume,learn,reuse_method,resize_hole;
    int mapped_count,mapped_holes[12];
    int inspect,check_clearance,check_previous,check_openings;
    double edge_min,bend_min,tooling[4];
    wchar_t panel_path[AX_PATH_DEPTH*12+4],example_path[AX_PATH_DEPTH*12+4];
    wchar_t thread[12];
    double hole,interference,bound_hole_mm;
    wchar_t model[MAX_PATH];
} FastenerPressfitRequest;

typedef struct {
    ProMdl assembly;
    ProWVerstamp stamp;
    ProPath directory;
    int window;
    double mm;
} FastenerPressfitTarget;

static struct {
    ProMdl assembly;
    ProMdlName name;
    ProPath directory;
    int window;
} fastener_nut_anchor;

#include "pressfit_natural.inc"

static int fastener_pressfit_scope(const wchar_t **p){
    return assembly_take(p,L"压铆螺母柱")||assembly_take(p,L"压铆螺母")||
        assembly_take(p,L"压铆螺柱")||assembly_take(p,L"压铆螺钉")||
        assembly_take(p,L"压铆螺丝")||assembly_take(p,L"压铆件");
}

static int fastener_pressfit_model(const wchar_t **p,wchar_t model[MAX_PATH]){
    const wchar_t *start=fastener_chat_separator(*p);
    const wchar_t *end=start;
    wchar_t quote=*start==L'「'?L'」':*start==L'“'?L'”':*start==L'"'?L'"':0;
    if(quote){
        start++;
        end=wcschr(start,quote);
        if(!end)return 0;
    }else{
        const wchar_t *alphabet=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-:×*＊";
        end+=wcsspn(end,alphabet);
        const wchar_t *suffix=fastener_chat_separator(end);
        const size_t stem=end-start;
        if((stem==3&&!_wcsnicmp(start,L"BSO",3))||(stem==4&&!_wcsnicmp(start,L"BSOS",4))){
            const wchar_t *spec=suffix;
            if(!wcsncmp(spec,L"3.5",3)||!wcsncmp(spec,L"3_5",3))
                spec=fastener_chat_separator(spec+3);
            if(towupper(*spec)==L'M')end=spec+wcsspn(spec,alphabet);
        }
    }
    size_t len=end-start;
    if(!len||len>=MAX_PATH||wcsncpy_s(model,MAX_PATH,start,len))return 0;
    for(wchar_t *mark=model;*mark;mark++)if(*mark==L'*'||*mark==L'＊')*mark=L'X';
    PressfitSelector selector={0};
    if(!pressfit_selector(model,&selector))return 0;
    *p=end+!!quote;
    return 1;
}

static int fastener_pressfit_id(const wchar_t **p,int *id){
    const wchar_t *start=fastener_chat_separator(*p);
    if(!_wcsnicmp(start,L"SURFACE:",8))start+=8;
    if(*start<L'1'||*start>L'9')return 0;
    wchar_t *end;
    const long number=wcstol(start,&end,10);
    if(number<=0||number>=INT_MAX)return 0;
    *id=(int)number;
    *p=end;
    return 1;
}

static int fastener_pressfit_mm(const wchar_t **p,double *value,int allow_zero){
    const wchar_t *start=fastener_chat_separator(*p);
    if(*start==L'Ø'||*start==L'Φ'||*start==L'φ')start++;
    if(*start<L'0'||*start>L'9')return 0;
    wchar_t *end;
    double number=wcstod(start,&end);
    const wchar_t *suffix=fastener_chat_separator(end);
    if(!_wcsnicmp(suffix,L"mm",2))suffix+=2;
    else assembly_take(&suffix,L"毫米");
    if(!isfinite(number)||number<0||(!number&&!allow_zero)||number>1000)return 0;
    *value=number;
    *p=suffix;
    return 1;
}

#include "pressfit_chat_grammar.inc"

static int fastener_pressfit_known_instance(const PressfitSelector *selector){
    for(size_t i=0;i+1<_countof(pressfit_acceptance_rows);i++){
        if(!_wcsicmp(pressfit_acceptance_rows[i].instance,selector->instance))return 1;
    }
    return 0;
}

static int fastener_pressfit_parse(const wchar_t *text,FastenerPressfitRequest *request){
    const wchar_t *p=fastener_bso_action_start(text);
    if(assembly_take(&p,L"我说的是"))p=fastener_bso_action_start(p);
    if(wcspbrk(p,L"?？\r\n"))return 0;
    FastenerPressfitRequest result={0};
    const wchar_t *start=p;
    if(!pressfit_chat_lead(&p,&result))return 0;
    if(assembly_take(&p,L"的紧固件换成")||assembly_take(&p,L"的压铆件换成")||
       assembly_take(&p,L"更换为")||assembly_take(&p,L"换装")){
        result.action=PRESSFIT_PLACE;
        result.short_form=1;
        result.replace=1;
    }else if(fastener_pressfit_take_install(&p)){result.action=PRESSFIT_PLACE;result.short_form=1;}
    else if(assembly_take(&p,L"读取并另存")||assembly_take(&p,L"另存")||assembly_take(&p,L"我要"))result.action=PRESSFIT_SAVE;
    else if(assembly_take(&p,L"核对")||assembly_take(&p,L"查找")||assembly_take(&p,L"读取"))result.action=PRESSFIT_READ;
    else return 0;
    if(result.action!=PRESSFIT_PLACE&&p!=start&&
       (result.expected||result.panel_path[0]||result.all_on_face||result.view_side||result.nearest_bend))return 0;

    p=fastener_chat_separator(p);
    if(result.action==PRESSFIT_PLACE){
        const wchar_t *quantity=p;
        int count=fastener_bso_count(&quantity);
        if(count&&(assembly_take(&quantity,L"个")||assembly_take(&quantity,L"件")||assembly_take(&quantity,L"颗"))){
            if(result.expected&&result.expected!=count)return 0;
            result.expected=count;
            p=fastener_chat_separator(quantity);
        }
    }
    const int scoped=fastener_pressfit_scope(&p);
    p=fastener_chat_separator(p);
    assembly_take(&p,L"型号");
    p=fastener_chat_separator(p);
    if(result.action==PRESSFIT_PLACE&&scoped&&
       (assembly_take(&p,L"你定")||assembly_take(&p,L"你选")||assembly_take(&p,L"按孔选"))){
        result.recommend=1;
    }else if(result.action==PRESSFIT_PLACE&&result.example_path[0]&&
        (assembly_take(&p,L"相同规格")||assembly_take(&p,L"同样的")||fastener_chat_end(p))){
        result.model[0]=0;
    }else{
        if(!fastener_pressfit_model(&p,result.model))return 0;
        pressfit_chat_family_after(&p,result.model);
    }
    PressfitSelector selector={0};
    if(!result.recommend&&!(result.example_path[0]&&!result.model[0])&&
       !pressfit_selector(result.model,&selector))return 0;
    const int bso=!wcsncmp(selector.instance_key,L"BSO",3);
    if(result.action==PRESSFIT_PLACE&&bso){
        p=fastener_chat_separator(p);
        assembly_take(&p,L"的");
        assembly_take(&p,L"螺母柱");
    }
    if(!scoped&&!bso&&!result.example_path[0]&&(result.action!=PRESSFIT_PLACE||!fastener_pressfit_known_instance(&selector)))return 0;
    if(result.action==PRESSFIT_PLACE&&(!wcscmp(selector.instance_key,L"BSO")||!wcscmp(selector.instance_key,L"BSOS"))){
        if(!fastener_chat_end(p))return 0;
        result.model[0]=0;
        *request=result;
        return 1;
    }

    p=fastener_chat_separator(p);
    if(result.action==PRESSFIT_SAVE){
        assembly_take(&p,L"到");
        if(!assembly_take(&p,L"当前工作目录")&&!assembly_take(&p,L"我的工作目录"))assembly_take(&p,L"工作目录");
        assembly_take(&p,L"里面");
    }
    if(result.action==PRESSFIT_PLACE){
        assembly_take(&p,L"到");
        int count=fastener_bso_hole_count(&p);
        if(count){
            if(result.expected&&result.expected!=count)return 0;
            result.expected=count;
        }else if(!assembly_take(&p,L"选中的孔位")&&!assembly_take(&p,L"选中的孔")&&
            !assembly_take(&p,L"选中的安装面"))assembly_take(&p,L"对应位置");
    }

    int have_interference=0;
    while(!fastener_chat_end(p)){
        p=fastener_chat_separator(p);
        if(assembly_take(&p,L"定位圆柱")||assembly_take(&p,L"定位面")){
            if(result.shaft||!fastener_pressfit_id(&p,&result.shaft))return 0;
        }else if(assembly_take(&p,L"承压面")||assembly_take(&p,L"贴合面")){
            if(result.seat||!fastener_pressfit_id(&p,&result.seat))return 0;
        }else if(result.action==PRESSFIT_PLACE&&
                 (assembly_take(&p,L"孔径不符就改孔")||assembly_take(&p,L"孔径不对就改孔"))){
            result.resize_hole=1;
        }else if(result.action==PRESSFIT_PLACE&&assembly_take(&p,L"孔径")){
            if(result.hole||!fastener_pressfit_mm(&p,&result.hole,0))return 0;
        }else if(result.action==PRESSFIT_PLACE&&result.short_form&&
                 (assembly_take(&p,L"方向你定")||assembly_take(&p,L"方向你决定"))){
            result.choose_side=1;
        }else if(result.action==PRESSFIT_PLACE&&assembly_take(&p,L"允许过盈")){
            if(have_interference||!fastener_pressfit_mm(&p,&result.interference,1))return 0;
            have_interference=1;

        }else if(result.action==PRESSFIT_PLACE&&
                 (assembly_take(&p,L"只核对不安装")||assembly_take(&p,L"先看孔位"))){
            result.inspect=1;
        }else if(result.action==PRESSFIT_PLACE&&
                 (assembly_take(&p,L"并核对干涉")||assembly_take(&p,L"核对装后干涉"))){
            result.check_clearance=1;
        }else if(result.action==PRESSFIT_PLACE&&assembly_take(&p,L"最小边距")){
            if(result.edge_min||!fastener_pressfit_mm(&p,&result.edge_min,0))return 0;
        }else if(result.action==PRESSFIT_PLACE&&assembly_take(&p,L"最小折弯净距")){
            if(result.bend_min||!fastener_pressfit_mm(&p,&result.bend_min,0))return 0;
        }else if(result.action==PRESSFIT_PLACE&&assembly_take(&p,L"模具半径和进给")){
            if(result.tooling[0])return 0;
            for(int i=0;i<4;i++)if(!fastener_pressfit_mm(&p,result.tooling+i,0)||result.tooling[i]<0.01)return 0;
        }else if(result.action!=PRESSFIT_PLACE||pressfit_chat_target(&p,&result)!=1)return 0;
    }
    if(!result.shaft!=!result.seat)return 0;
    if(result.shaft)result.short_form=0;
    if(result.resize_hole&&(result.replace||result.inspect||result.all_on_face||result.expected>1))return 0;
    *request=result;
    return 1;
}

static ProError fastener_pressfit_capture(FastenerPressfitTarget *target,PartText *out){
    ProMdlType type;
    ProError error=ProMdlCurrentGet(&target->assembly);
    if(!error)error=ProMdlTypeGet(target->assembly,&type);
    if(error||type!=PRO_MDL_ASSEMBLY){
        part_text_add(out,L"请先打开要安装压铆件的装配，并在其中选定安装面。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    error=ProMdlVerstampGet(target->assembly,&target->stamp);
    if(!error)error=ProWindowCurrentGet(&target->window);
    if(!error)error=ProDirectoryCurrentGet(target->directory);
    if(!error)error=qa_mm_per_unit(target->assembly,&target->mm);
    return error;
}

static ProError fastener_pressfit_bound(const FastenerPressfitTarget *target,PartText *out){
    ProMdl active=NULL;
    ProPath directory;
    ProWVerstamp stamp=NULL;
    int window;
    int matches=!ProMdlCurrentGet(&active)&&active==target->assembly&&
        !ProWindowCurrentGet(&window)&&window==target->window&&
        !ProDirectoryCurrentGet(directory)&&!_wcsicmp(directory,target->directory)&&
        !ProMdlVerstampGet(active,&stamp)&&target->stamp&&stamp&&ProVerstampEqual(target->stamp,stamp);
    if(stamp)ProVerstampFree(&stamp);
    if(matches)return PRO_TK_NO_ERROR;
    part_text_add(out,L"当前装配、窗口、模型版本或工作目录发生变化。请核对当前目标和孔位。\n");
    return PRO_TK_BAD_CONTEXT;
}

#include "pressfit_chat_context.inc"

static double pressfit_chat_panel_thickness(ProMdl model){
    ProDimension dimension;
    ProMdlsubtype subtype;
    double value,mm;
    if(!model||ProMdlSubtypeGet(model,&subtype)||subtype!=PROMDLSTYPE_PART_SHEETMETAL||
       ProSmtPartThicknessGet((ProPart)model,&dimension)||ProDimensionValueGet(&dimension,&value)||
       qa_mm_per_unit(model,&mm)||!isfinite(value)||value<=0.0)return 0.0;
    return value*mm;
}

static ProError pressfit_chat_measured_selection(const FastenerPressfitTarget *target,
        FastenerPressfitRequest *request,double *hole,double *sheet,PartText *out){
    AxComponent panel={0};
    int face=0,holes[12],count=0;
    ProError error=fastener_pressfit_selection_workbench(target->assembly,1,target->mm,&panel,&face,holes,&count);
    if(request&&request->all_on_face){
        if(error||!face){
            part_text_add(out,L"请在当前装配中选中要安装的板面。\n");
            return error?error:PRO_TK_BAD_CONTEXT;
        }
        if((request->face_id&&request->face_id!=face)||request->view_side||request->nearest_bend||
           request->flip_side||request->side_zone){
            part_text_add(out,L"整面自动选型以实际选中的安装面为准，请直接选中希望装配的一侧。\n");
            return PRO_TK_BAD_CONTEXT;
        }
        if(request->panel_path[0]){
            AxComponent given={0};
            error=ax_hw_occurrence(target->assembly,request->panel_path,target->mm,&given);
            if(error||!ax_same_occurrence(&panel,&given))return error?error:PRO_TK_BAD_CONTEXT;
        }
        BsoFaces surfaces={.model=panel.model,.mm=target->mm,.all_holes=1};
        error=fastener_bso_model_faces(&surfaces);
        if(error)return error;
        int verified_count=0;
        *hole=fastener_pressfit_uniform_bores(&panel,face,surfaces.holes,surfaces.hole_count,
            request->expected,target->mm,&verified_count);
        if(*hole<=0.0){
            part_text_add(out,L"所选板面未找到数量吻合、孔径一致的圆孔组。已读到 %d 个同径孔；不同孔径请分组选。\n",
                verified_count);
            return PRO_TK_BAD_CONTEXT;
        }
        request->expected=verified_count;
    }else if(error||!count){
        part_text_add(out,L"请选一下要装的实际圆孔，MWEAI 将读取底孔和板厚后选型。\n");
        return error?error:PRO_TK_E_NOT_FOUND;
    }
    for(int i=0;i<count&&!(request&&request->all_on_face);i++){
        AxGeometry geometry;
        ProVector ends[2];
        wchar_t token[32];
        int bore;
        fastener_bso_surface(holes[i],token);
        error=ax_hw_cylinder(&panel,token,&geometry,ends,&bore);
        const double diameter=!error?2.0*geometry.radius*target->mm:0.0;
        if(error||!bore||diameter<=0.0||(*hole&&fabs(*hole-diameter)>0.001)){
            part_text_add(out,L"选中的孔径不一致，或含有无法核实的圆柱。请按同一孔径分别选型。\n");
            return error?error:PRO_TK_BAD_CONTEXT;
        }
        *hole=diameter;
    }
    *sheet=pressfit_chat_panel_thickness(panel.model);
    if(!*sheet){
        part_text_add(out,L"零件的原生钣金板厚无法核对。可先用明确型号查询安装条件。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_chat_resize_hole(FastenerPressfitTarget *target,
    FastenerPressfitRequest *request,double desired,PartText *out);

static ProError fastener_pressfit_execute(FastenerPressfitRequest request,const wchar_t *text,PartText *reply){
    PartText out=*reply;
    if(request.action==PRESSFIT_PLACE&&!request.model[0]&&!request.nut_style&&!request.recommend&&
       !request.reuse_prior&&!request.resume&&!request.learn&&!request.check_previous&&!request.check_openings&&!request.example_path[0]){
        part_text_add(&out,L"请告诉我螺纹和长度，例如「装配BSO M3×8」。我会从公司库调用对应的具体实例。");
        *reply=out;
        return PRO_TK_BAD_INPUTS;
    }

    wchar_t library[MAX_PATH]=L"",saved[MAX_PATH]=L"";
    FastenerPressfitTarget target={0};
    PressfitCheckpoint checkpoint={0};
    PressfitInstalledExample sample={0};
    ProMdl source=NULL,project=NULL;
    BsoFitting fitting={0};
    BsoSite sites[12];
    int count=0;
    double mm=0.0;
    ProError error=PRO_TK_NO_ERROR;
    const wchar_t *stage=L"定位当前装配";
    if(request.action==PRESSFIT_PLACE)error=fastener_pressfit_capture(&target,&out);
    if(!error&&request.check_openings){
        error=fastener_pressfit_openings(target.assembly,target.mm,&out);
        if(target.stamp)ProVerstampFree(&target.stamp);
        *reply=out;
        return error;
    }
    if(!error&&request.check_previous){
        error=fastener_pressfit_audit_last(target.assembly,&out);
        if(target.stamp)ProVerstampFree(&target.stamp);
        *reply=out;
        return error;
    }
    if(!error&&request.learn){
        error=pressfit_method_learn(target.assembly,&out);
        if(target.stamp)ProVerstampFree(&target.stamp);
        *reply=out;
        return error;
    }
    if(!error&&request.resume){
        stage=L"复核当前装配的压铆任务记录";
        error=pressfit_checkpoint_load(target.assembly,&checkpoint,&out);
        if(!error){
            wcscpy_s(request.model,_countof(request.model),checkpoint.selector);
            request.shaft=checkpoint.shaft;
            request.seat=checkpoint.seat;
            request.replace=checkpoint.replace;
            request.interference=checkpoint.interference;
        }
    }
    if(!error&&request.reuse_prior){
        stage=L"核对上一条任务的型号和装配";
        error=pressfit_chat_use_prior(&request,&target,&out);
    }
    if(!error&&request.example_path[0]){
        stage=L"读取示范组件的原生安装约束";
        error=fastener_pressfit_installed_example(target.assembly,request.example_path,target.mm,&sample,&out);
        if(!error&&!request.model[0]){
            const wchar_t *selector=sample.selector;
            if(!selector[0]&&pressfit_last_fit.assembly==target.assembly&&
               target.window==pressfit_last_fit.window&&!_wcsicmp(pressfit_last_fit.directory,target.directory)&&
               !_wcsnicmp(sample.name,pressfit_last_fit.instance,wcslen(pressfit_last_fit.instance)))
                selector=pressfit_last_fit.selector;
            if(!selector[0]){
                part_text_add(&out,L"示范件的同轴和贴合已核对，库中精确型号尚未对应。请同时给出型号和要装的孔。\n");
                error=PRO_TK_E_NOT_FOUND;
            }else wcscpy_s(request.model,_countof(request.model),selector);
        }
        if(!error){
            if(!request.hole)request.hole=sample.hole;
            if(request.hole<=0||fabs(request.hole-sample.hole)>0.001||
               sample.overlap>request.interference+0.001){
                part_text_add(&out,L"目标孔径或授权过盈与示范件实测不符。请按实际底孔和公差处理。\n");
                error=PRO_TK_BAD_CONTEXT;
            }
        }
    }
    if(!error&&request.nut_style){
        ProMdlName active_name;
        error=ProMdlMdlnameGet(target.assembly,active_name);
        if(!error&&request.follow_nut&&(target.assembly!=fastener_nut_anchor.assembly||
           target.window!=fastener_nut_anchor.window||_wcsicmp(target.directory,fastener_nut_anchor.directory)||
           _wcsicmp(active_name,fastener_nut_anchor.name))){
            part_text_add(&out,L"当前装配和上一条压铆任务的目标不同。请在这边明确要装的型号和孔位。\n");
            error=PRO_TK_BAD_CONTEXT;
        }
        if(!error&&!request.follow_nut){
            fastener_nut_anchor.assembly=target.assembly;
            fastener_nut_anchor.window=target.window;
            wcscpy_s(fastener_nut_anchor.directory,_countof(fastener_nut_anchor.directory),target.directory);
            wcscpy_s(fastener_nut_anchor.name,_countof(fastener_nut_anchor.name),active_name);
        }
    }
    if(!error&&request.recommend){
        stage=L"按实测孔径和板厚选规格";
        double hole=0.0,sheet=0.0;
        error=pressfit_chat_measured_selection(&target,&request,&hole,&sheet,&out);
        if(!error){
            const wchar_t *kind=wcsstr(text,L"压铆螺母柱")?L"压铆螺母柱":
                wcsstr(text,L"压铆螺母")?L"压铆螺母":
                wcsstr(text,L"压铆螺柱")?L"压铆螺柱":NULL;
            error=pressfit_acceptance_recommend(hole,sheet,request.thread,kind,request.model,&out);
            if(!error){
                if(request.hole>0.0&&fabs(request.hole-hole)>0.001){
                    part_text_add(&out,L"指令孔径与所选实际底孔不同，尚未装配。\n");
                    error=PRO_TK_BAD_CONTEXT;
                }else{
                    request.hole=hole;
                    request.check_clearance=1;
                }
            }
        }
    }
    if(!error&&request.nut_style){
        stage=L"匹配公司库已验收的压铆螺母";
        error=fastener_pressfit_short_model(&request,&out);
    }
    if(!error){
        stage=L"从压铆件库读取精确型号";
        error=pressfit_read_exact(request.model,&source,library,&out);
    }
    const PressfitAcceptance *acceptance=!error&&request.action!=PRESSFIT_SAVE?
        pressfit_acceptance_lookup(source,library):NULL;
    if(!error&&request.action!=PRESSFIT_SAVE){
        stage=L"核对定位圆柱及承压面";
        if(request.action==PRESSFIT_PLACE){
            mm=target.mm;
            error=fastener_pressfit_bound(&target,&out);
        }else error=qa_mm_per_unit(source,&mm);
        double hole_hint=request.hole;
        if(!error&&request.action==PRESSFIT_PLACE&&!hole_hint)
            hole_hint=request.bound_hole_mm>0.0?request.bound_hole_mm:
                fastener_pressfit_selected_hole(target.assembly,mm);
        const double catalog_hole=acceptance?pressfit_acceptance_nominal(acceptance,library):0.0;
        if(!error&&acceptance&&!request.hole){
            if(catalog_hole>0.0){
                request.hole=catalog_hole;
                hole_hint=request.hole;
            }else if(hole_hint>0.0&&fabs(hole_hint-acceptance->trial_hole_mm)<=0.001)
                request.hole=hole_hint;
        }
        if(!error&&request.nut_style&&(!acceptance||fabs(catalog_hole-request.hole)>0.001)){
            part_text_add(&out,L"本机公司库型号或孔径资料与验收时不一致，当前装配保留。\n");
            error=PRO_TK_BAD_CONTEXT;
        }
        int verified=0;
        if(!error&&acceptance&&!request.shaft){
            wchar_t reason[450]=L"";
            PartText check={reason,_countof(reason),0};
            verified=!fastener_pressfit_fitting_for_hole(source,mm,
                acceptance->shaft,acceptance->seat,hole_hint,&fitting,&check)&&
                fabs(2*fitting.geometry.cylinder.radius*mm-acceptance->pilot_mm)<=0.001;
        }
        if(!error&&request.nut_style&&!verified){
            part_text_add(&out,L"已验收的定位圆柱或承压面无法在当前库件上核对，当前装配保留。\n");
            error=PRO_TK_BAD_CONTEXT;
        }
        if(!error&&!verified)error=fastener_pressfit_fitting_for_hole(source,mm,
            request.shaft,request.seat,hole_hint,&fitting,&out);
        if(!error&&request.example_path[0]&&
           fabs(2*(fitting.geometry.cylinder.radius-sample.tool.geometry.cylinder.radius)*mm)>0.001){
            part_text_add(&out,L"精确库件定位圆柱与示范件实测直径不同。请核对型号。\n");
            error=PRO_TK_BAD_CONTEXT;
        }
        if(verified){
            part_text_add(&out,L"本规格已通过独立 Creo 试装，样件孔径 Ø%.6g mm。\n",acceptance->trial_hole_mm);
            if(catalog_hole>0.0)
                part_text_add(&out,L"公司库登记底孔 Ø%.6g mm。\n",catalog_hole);
        }
        if(!error)part_text_add(&out,L"定位圆柱 SURFACE:%d，承压面 SURFACE:%d，实测定位径 Ø%.6g mm。\n",
            fitting.shaft,fitting.seat,2*fitting.geometry.cylinder.radius*mm);
    }
    if(!error&&request.resize_hole){
        stage=L"按精确库件底孔改已选圆孔";
        const double nominal=acceptance?pressfit_acceptance_nominal(acceptance,library):0.0;
        if(nominal<=0.0||(request.hole>0.0&&fabs(nominal-request.hole)>0.001)){
            part_text_add(&out,L"该规格暂无可确认的公司底孔尺寸，或与指定孔径不同。已保留现有孔。\n");
            error=PRO_TK_BAD_CONTEXT;
        }else{
            request.hole=nominal;
            error=pressfit_chat_resize_hole(&target,&request,nominal,&out);
        }
    }
    if(!error&&request.action==PRESSFIT_PLACE){
        stage=L"核对实际孔位、孔径和贴合侧";
        const int explicit_location=request.panel_path[0]||request.all_on_face||request.view_side||
            request.nearest_bend||request.flip_side||request.side_zone||request.mapped_count||request.example_path[0];
        if(request.short_form&&!request.hole)
            request.hole=acceptance?acceptance->trial_hole_mm:2*fitting.geometry.cylinder.radius*mm;
        if(request.resume){
            count=checkpoint.count;
            error=pressfit_checkpoint_sites(target.assembly,library,&fitting,&checkpoint,sites,&out);
        }else if(request.nut_style||(request.short_form&&(acceptance||explicit_location)))
            error=fastener_pressfit_short_sites(target.assembly,mm,&request,&fitting,sites,&count,&out);
        else error=fastener_pressfit_resolve_sites(target.assembly,mm,request.expected,&fitting,
            request.hole,request.interference,sites,&count,&out);
        if(!error&&count){
            pressfit_acceptance_describe(source,library,2*sites[0].geometry.cylinder.radius*mm,
                pressfit_chat_panel_thickness(sites[0].panel.model),&out);
            if(request.inspect||request.edge_min||request.bend_min||request.tooling[0]){
                stage=L"检查孔型、边界和模具净空";
                const double *tooling=request.tooling[0]?request.tooling:NULL;
                for(int i=0;!error&&i<count;i++)
                    error=fastener_pressfit_site_preflight(&sites[i],mm,request.edge_min,request.bend_min,tooling,&out);
            }
            if(!error&&request.reuse_method&&!pressfit_method_match(request.model,library,&fitting,sites,count,mm,&out)){
                part_text_add(&out,L"当前孔径、孔深或库件版本没有匹配已保存的压铆方法。\n");
                error=PRO_TK_BAD_CONTEXT;
            }
        }else if(error&&source&&!request.bound_hole_mm){
            double hole=0.0,sheet=0.0;
            wchar_t brief[300]=L"";
            PartText checked={brief,_countof(brief),0};
            if(!pressfit_chat_measured_selection(&target,NULL,&hole,&sheet,&checked)&&request.hole>0&&
               fabs(hole-request.hole)>0.001){
                wchar_t alternative[MAX_PATH]=L"";
                (void)pressfit_acceptance_recommend(hole,sheet,request.thread,NULL,alternative,&out);
            }
        }
    }
    if(!error&&request.inspect){
        error=fastener_pressfit_batch_preflight(target.assembly,source,&fitting,sites,count,mm,&out);
        if(!error){
            fastener_pressfit_highlight(target.assembly,sites,count,&out);
            part_text_add(&out,L"已核对 %d 个孔及占位状态，本次只检查。\n",count);
        }
        if(target.stamp)ProVerstampFree(&target.stamp);
        *reply=out;
        return error;
    }
    if(!error&&request.action!=PRESSFIT_READ){
        stage=L"另存确切库件到当前工作目录";
        error=pressfit_library_project(source,library,&project,saved,&out);
        if(!error)part_text_add(&out,L"已核实工作目录中的库件文件 %ls。\n",saved);
    }
    if(!error&&request.action==PRESSFIT_PLACE){
        stage=L"核对将要装入的本地零件及装配目标";
        error=fastener_pressfit_bound(&target,&out);
        if(!error)error=fastener_pressfit_fitting(project,mm,fitting.shaft,fitting.seat,&fitting,&out);
        if(!error&&!request.resume){
            stage=L"登记装配孔位与续做记录";
            error=pressfit_checkpoint_start(target.assembly,request.model,library,&fitting,sites,count,mm,
                request.interference,request.replace,&checkpoint,&out);
        }
        if(!error){
            stage=L"施加并回读同轴和端面约束";
            PressfitBatchResult results;
            FastenerSavedView *project_guard=pressfit_project_placement_begin(project,saved);
            error=fastener_pressfit_place_batch(target.assembly,project,&fitting,sites,count,mm,
                request.interference,request.replace,&checkpoint,&results,&out);
            if(!ax_recovery_uncertain)pressfit_project_placement_end(source,project,saved,project_guard);
            if(!error&&request.check_clearance){
                stage=L"核对装后实体干涉和周边距离";
                error=fastener_pressfit_batch_audit(target.assembly,sites,&results,mm,request.interference,&out);
            }
        }
    }
    if(!error&&request.action==PRESSFIT_READ)part_text_add(&out,L"库件来源 %ls。\n",library);
    if(!error&&request.action==PRESSFIT_PLACE&&source)pressfit_chat_remember(&target,request.model,source);
    if(error)part_text_add(&out,L"\n卡在「%ls」这一步（Creo %d）。",stage,error);
    if(target.stamp)ProVerstampFree(&target.stamp);
    pressfit_checkpoint_close(&checkpoint);
    *reply=out;
    return error;
}

#include "pressfit_chat_batch.inc"
