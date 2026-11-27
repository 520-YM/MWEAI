static const wchar_t *markup_shared_consent_clause(const wchar_t *text){
    static const wchar_t consent[]=L"允许修改所有同名实例";
    const wchar_t *clause=NULL;
    for(const wchar_t *p=text;*p;p++)if(wcschr(L"，,；;",*p))clause=p;
    if(!clause)return NULL;
    const wchar_t *p=markup_skip_space(clause+1);
    const size_t length=_countof(consent)-1;
    if(wcsncmp(p,consent,length))return NULL;
    for(p+=length;*p&&(iswspace(*p)||wcschr(L"。.!！",*p));p++){}
    return *p?NULL:clause;
}

/* An assembly image identifies an occurrence, while a driving dimension changes
   the model definition. Inspect all occurrences before authorizing that edit. */
static ProError markup_assembly_authorize_child(const MarkupCapture *capture,const MarkupRequest *request,
    const AxComponent *component,wchar_t *reply,size_t capacity){
    ProMdlType type;
    ProError error=ProMdlTypeGet(component->model,&type);
    if(!error&&type!=PRO_MDL_PART)error=PRO_TK_INVALID_TYPE;
    if(!error)error=markup_check_capture(capture);
    if(!error)error=ax_ordinary_direct((ProAsmcomp *)&component->feature);
    if(!error)error=part_health(component->model);
    if(!error)error=part_health(capture->model);
    if(error||ax_recovery_uncertain){
        wcscpy_s(reply,capacity,L"选中组件的模型、装配健康状态或活动实例条件无法核对，本次未写入。");
        return error?error:PRO_TK_BAD_CONTEXT;
    }
    AxScan *scan=NULL;
    error=ax_scan(capture->model,1,&scan);
    if(!error&&(scan->limited||scan->unavailable))error=PRO_TK_INCOMPLETE;
    int uses=0,selected=0;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *row=&scan->occurrences[i];
        if(row->model!=component->model)continue;
        uses++;
        selected|=row->state==PRO_FEAT_ACTIVE&&row->depth==component->path.table_num&&
            !memcmp(row->ids,component->path.comp_id_table,row->depth*sizeof(row->ids[0]));
    }
    free(scan);
    if(error||!selected){
        wcscpy_s(reply,capacity,L"无法核对装配中同一零件的全部实例，本次未写入。");
        return error?error:PRO_TK_BAD_CONTEXT;
    }
    ProMdlName name;
    wchar_t path[AX_PATH_DEPTH*12+4];
    if(ProMdlMdlnameGet(component->model,name)||
       !ax_path_label(component->path.comp_id_table,component->path.table_num,path,_countof(path)))
        return PRO_TK_BAD_CONTEXT;
    if(uses>1&&!markup_shared_consent_clause(request->text)){
        swprintf_s(reply,capacity,L"%ls 对应 %ls，当前装配共有 %d 个实例共用这个零件。"
            L"确认同步改动时，请在原指令后加「允许修改所有同名实例」。",path,name,uses);
        return PRO_TK_BAD_CONTEXT;
    }
    swprintf_s(reply,capacity,L"已核对子件 %ls（%ls），当前装配中涉及 %d 个实例。",
        name,path,uses);
    return PRO_TK_NO_ERROR;
}

static ProError markup_assembly_child(const MarkupCapture *capture,const MarkupRequest *request,const MarkupEdit *edits,
    const int *indices,int count,AxComponent *component,wchar_t *reply,size_t capacity){
    for(int i=0;i<count;i++){
        const MarkupOccurrence *where=edits[i].measure==MARKUP_DIAMETER?
            &capture->image->holes[indices[i]].occurrence:&capture->image->openings[indices[i]].occurrence;
        AxComponent current={0};
        ProError error=markup_assembly_component(capture,where,&current);
        if(error||(i&&!ax_same_occurrence(component,&current))){
            wcscpy_s(reply,capacity,L"本次标注没有绑定到同一子件实例，请按子件分组修改。");
            return error?error:PRO_TK_BAD_CONTEXT;
        }
        *component=current;
    }
    return markup_assembly_authorize_child(capture,request,component,reply,capacity);
}

typedef struct {
    double value;
    int screen_axis,screen_sign,audit_clearance;
} MarkupAssemblyOffset;

static int markup_assembly_offset_end(const wchar_t *end,int *audit){
    static const wchar_t *const checks[]={L"然后检查间隙",L"再检查间隙",L"并检查间隙",L"检查间隙"};
    if(fastener_chat_end(end))return 1;
    while(*end&&(iswspace(*end)||wcschr(L"，,；;",*end)))end++;
    if(!markup_take_phrase(&end,checks,_countof(checks))||!fastener_chat_end(end))return 0;
    *audit=1;
    return 1;
}

static int markup_assembly_offset_request(const wchar_t *text,MarkupAssemblyOffset *request){
    static const wchar_t *const objects[]={L"这个支架",L"这块支架",L"这个零件",L"这个件",L"这个组件",L"这个子装配",
        L"这个子件",L"标注的组件",L"圈中的组件",L"箭头指的组件",L"该组件"};
    static const wchar_t *const actions[]={L"右移",L"左移",L"上移",L"下移"};
    static const wchar_t *const offsets[]={L"定位偏距改到",L"定位偏距改为",L"偏距改到",L"偏距改为",L"偏距设为"};
    const wchar_t *p=model_request_action_start(text);
    if(*p==L'把'||*p==L'将')p=markup_skip_space(p+1);
    markup_take_phrase(&p,objects,_countof(objects));
    if(*p==L'的')p=markup_skip_space(p+1);
    if(markup_take_phrase(&p,offsets,_countof(offsets))){
        wchar_t *tail;
        const double value=wcstod(p,&tail);
        if(p==tail||!isfinite(value)||fabs(value)>10000)return 0;
        const wchar_t *end=markup_skip_space(tail);
        if(!_wcsnicmp(end,L"mm",2)||!wcsncmp(end,L"毫米",2))end+=2;
        int audit=0;
        if(!markup_assembly_offset_end(end,&audit))return 0;
        *request=(MarkupAssemblyOffset){.value=value,.screen_axis=-1,.audit_clearance=audit};
        return 1;
    }
    if(*p==L'向'||*p==L'往')p=markup_skip_space(p+1);
    int direction=0;
    for(;direction<_countof(actions);direction++){
        size_t n=wcslen(actions[direction]);
        if(!wcsncmp(p,actions[direction],n)){p=markup_skip_space(p+n);break;}
    }
    if(direction==_countof(actions))return 0;
    double value;
    int audit=0;
    if(!fastener_pressfit_mm(&p,&value,0)||!markup_assembly_offset_end(p,&audit))return 0;
    *request=(MarkupAssemblyOffset){.value=value,.screen_axis=direction/2,
        .screen_sign=(direction==0||direction==3)?1:-1,.audit_clearance=audit};
    return 1;
}

/* Screen-relative moves require one native signed plane-offset constraint
   whose axis is aligned with the requested view direction. */
static ProError markup_assembly_constraint_axis(ProAsmcompconstraint row,ProMatrix parent_pose,ProVector direction){
    ProSelection reference=NULL;
    ProDatumside side;
    ProGeomitem item;
    AxGeometry geometry;
    ProError error=ProAsmcompconstraintAsmreferenceGet(row,&reference,&side);
    if(!error&&(!reference||ProSelectionVerify(reference)))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProSelectionModelitemGet(reference,&item);
    if(!error)error=ax_geometry(&item,&geometry);
    if(!error&&geometry.type!=AX_GEOM_PLANE)error=PRO_TK_INVALID_TYPE;
    if(!error)error=ax_model_to_root(reference,&geometry);
    if(!error)error=ProVectorTrfEval(geometry.direction,parent_pose,direction);
    return error?error:ad_unit(direction)?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}

static double markup_assembly_screen_sign(const MarkupCapture *capture,const double *direction,int axis){
    ProVector basis0,basis1;
    memcpy(basis0,capture->face_basis[0],sizeof(basis0));
    memcpy(basis1,capture->face_basis[1],sizeof(basis1));
    if(!capture->face_span||!ad_unit(basis0)||!ad_unit(basis1))return 0;
    const double a=ad_dot(direction,basis0),b=ad_dot(direction,basis1);
    const double screen=a*capture->face_pixels[0][axis]+b*capture->face_pixels[1][axis];
    const double along=axis?b:a;
    return fabs(along)>=0.9999&&isfinite(screen)&&fabs(screen)>1e-9?(screen>0?1:-1):0;
}

static ProError markup_assembly_offset_command(ProMdl assembly,int component,int index,double old_value,
    double new_value,wchar_t *reply,size_t capacity){
    ProMdlName name;
    ProError error=ProMdlMdlnameGet(assembly,name);
    if(error)return error;
    wchar_t command[300];
    if(swprintf_s(command,_countof(command),L"CREO_OP ASM_EDIT_OFFSET %ls ID:%d %d %.12g %.12g",
        name,component,index,old_value,new_value)<0)return PRO_TK_BAD_INPUTS;
    PartCommand parsed={0};
    if(part_command_parse(command,&parsed)!=1)return PRO_TK_BAD_INPUTS;
    return ax_execute(assembly,&parsed,reply,capacity);
}

static ProError markup_assembly_move_clearance(AxComponent *moving,AxComponent *reference,double mm,PartText *out,int after){
    ProSelection selection[2]={0};
    ProError error=ax_part_selection(moving,&selection[0]);
    if(!error)error=ax_part_selection(reference,&selection[1]);
    double gap=0,overlap=0;
    if(!error)error=fastener_pressfit_pair_fit(selection[0],selection[1],mm,&gap,&overlap);
    for(int i=0;i<2;i++)if(selection[i])ProSelectionFree(&selection[i]);
    if(!error){
        if(overlap>0)part_text_add(out,L"\n%ls两件实体正体积干涉 %.6g mm³。",after?L"移动后":L"移动前",overlap);
        else part_text_add(out,L"\n%ls两件的最小间隙 %.6g mm。",after?L"移动后":L"移动前",gap);
    }
    return error;
}

static ProError markup_assembly_move(const MarkupCapture *capture,const MarkupRequest *request,
    const MarkupAssemblyOffset *move,PartText *out){
    if(request->count!=(move->audit_clearance?2:1)){
        part_text_add(out,move->audit_clearance?
            L"请依次标注要移动的组件和用来测间隙的组件。":L"请只标注一个要移动的组件。");
        return PRO_TK_BAD_INPUTS;
    }
    MarkupAssemblyPick pick={0};
    ProError error=markup_assembly_pick(capture,&request->marks[0],&pick);
    if(error){part_text_add(out,L"这处标注无法唯一绑定组件，请把箭头指向该组件的实体表面。");return error;}
    AxComponent *component=&pick.component;
    MarkupAssemblyPick compare={0};
    if(move->audit_clearance){
        error=markup_assembly_pick(capture,&request->marks[1],&compare);
        if(error||ax_same_occurrence(component,&compare.component)){
            part_text_add(out,L"请把 2 号标注放在用于比较间隙的另一个组件上。");
            return error?error:PRO_TK_BAD_INPUTS;
        }
    }
    ProMdl parent=capture->model;
    ProMatrix parent_pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    wchar_t path[AX_PATH_DEPTH*12+4];
    if(!ax_path_label(component->path.comp_id_table,component->path.table_num,path,_countof(path)))
        return PRO_TK_BAD_CONTEXT;
    if(component->path.table_num>1){
        wchar_t parent_key[AX_PATH_DEPTH*12+4];
        if(!ax_path_label(component->path.comp_id_table,component->path.table_num-1,parent_key,_countof(parent_key)))
            return PRO_TK_BAD_CONTEXT;
        error=ax_nested_parent(capture->model,parent_key,&parent,out);
        if(error)return error;
        AxComponent parent_component={0};
        error=ax_component_resolve(capture->model,parent_key,&parent_component);
        if(!error)error=ProAsmcomppathTrfGet(&parent_component.path,PRO_B_TRUE,parent_pose);
        if(error)return error;
    }
    ProAsmcomppath root;
    ProAsmcompconstraint *rows=NULL;
    int count=0,selected=-1,matches=0;
    double mm=0,root_mm=0,old_value=0,new_value=0;
    ProVector axis={0};
    error=qa_mm_per_unit(parent,&mm);
    if(!error)error=qa_mm_per_unit(capture->model,&root_mm);
    if(!error)error=ax_root_path(parent,&root);
    if(!error)error=ax_read_constraints(&component->feature,&root,&rows,&count);
    for(int i=0;!error&&i<count;i++){
        ProAsmcompConstrType type;
        error=ProAsmcompconstraintTypeGet(rows[i],&type);
        if(error||!ax_offset_type(type))continue;
        double sign=1;
        ProVector current_axis={0};
        if(move->screen_axis>=0){
            if(type!=PRO_ASM_ALIGN_OFF||markup_assembly_constraint_axis(rows[i],parent_pose,current_axis))continue;
            sign=markup_assembly_screen_sign(capture,current_axis,move->screen_axis);
            if(!sign)continue;
        }
        error=ProAsmcompconstraintOffsetGet(rows[i],&old_value);
        old_value*=mm;
        new_value=move->screen_axis<0?move->value:old_value+move->value*sign*move->screen_sign;
        memcpy(axis,current_axis,sizeof(axis));
        selected=i;
        matches++;
    }
    if(rows)ProAsmcompconstraintArrayFree(rows);
    if(error)return error;
    if(move->audit_clearance&&(error=markup_assembly_move_clearance(component,&compare.component,root_mm,out,0))){
        part_text_add(out,L"移动前无法可靠读取两件间隙，未修改。");
        return error;
    }
    if(matches!=1){
        part_text_add(out,L"%ls 没有唯一能对应所要求方向的已有平面偏距约束（匹配 %d 条）。"
            L"可检查定位参照，或把视图转正后重截。",path,matches);
        return PRO_TK_BAD_CONTEXT;
    }
    if(fabs(old_value-new_value)<=1e-5){
        part_text_add(out,L"%ls 当前偏距已是 %.6g mm。",path,old_value);
        return PRO_TK_NO_ERROR;
    }

    ProMatrix before,expected,after;
    AwAssemblyState other={0};
    error=ProAsmcomppathTrfGet(&component->path,PRO_B_TRUE,before);
    if(!error)error=aw_state_capture(parent,&other);
    if(error)return error;
    memcpy(expected,before,sizeof(expected));
    if(move->screen_axis>=0)
        for(int k=0;k<3;k++)expected[3][k]+=(new_value-old_value)/root_mm*axis[k];
    wchar_t native[750]=L"";
    error=markup_assembly_offset_command(parent,component->feature.id,selected+1,old_value,new_value,native,_countof(native));
    if(error){part_text_add(out,L"%ls",native);aw_state_free(&other);return error;}
    if(parent!=capture->model)error=part_regenerate(capture->model);
    if(!error)error=part_health(capture->model);
    if(!error)error=ProAsmcomppathTrfGet(&component->path,PRO_B_TRUE,after);
    if(!error&&move->screen_axis>=0&&!ax_same_pose(expected,after,root_mm))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=aw_state_verify(parent,&other,component->feature.id,mm,out);
    if(error){
        wchar_t restore[750]=L"";
        ProError restored=markup_assembly_offset_command(parent,component->feature.id,selected+1,new_value,old_value,
            restore,_countof(restore));
        if(!restored&&parent!=capture->model)restored=part_regenerate(capture->model);
        if(!restored)restored=ProAsmcomppathTrfGet(&component->path,PRO_B_TRUE,after);
        if(!restored&&!ax_same_pose(before,after,root_mm))restored=PRO_TK_BAD_CONTEXT;
        if(!restored)restored=aw_state_verify(parent,&other,0,mm,out);
        if(restored)ax_uncertain(out);
        else part_text_add(out,L"实际位姿未与预期吻合，已恢复并回读原偏距和组件位置。");
    }else{
        part_text_add(out,L"%ls 第 %d 条定位偏距 %.6g → %.6g mm。"
            L"Creo 回读实际位移 (%.6g, %.6g, %.6g) mm，其他实例位姿核对通过，未保存。",
            path,selected+1,old_value,new_value,(after[3][0]-before[3][0])*root_mm,
            (after[3][1]-before[3][1])*root_mm,(after[3][2]-before[3][2])*root_mm);
        if(move->audit_clearance&&(error=markup_assembly_move_clearance(component,&compare.component,root_mm,out,1)))
            part_text_add(out,L"\n移动已执行，移动后间隙读回失败（Creo %d）。",error);
    }
    aw_state_free(&other);
    return error;
}

static int markup_assembly_fastener_request(const wchar_t *text,wchar_t model[MAX_PATH]){
    static const wchar_t *const target[]={L"在这些孔上",L"在这些孔",L"这些孔",L"这几个孔",L"在标注孔位",
        L"标注的孔",L"圈中的孔",L"这个孔",L"这里",L"标注处"};
    static const wchar_t *const verbs[]={L"装配",L"安装",L"装"};
    const wchar_t *p=model_request_action_start(text);
    markup_take_phrase(&p,target,_countof(target));
    if(!markup_take_phrase(&p,verbs,_countof(verbs)))return 0;
    markup_take_phrase(&p,target,_countof(target));
    if(!fastener_pressfit_model(&p,model))return 0;
    markup_take_phrase(&p,target,_countof(target));
    return fastener_chat_end(p);
}

/* The captured bore center is on the visible lip. Use the one plane whose
   actual common rim meets that native point, never the far side of a hole. */
static ProError markup_assembly_fastener_site(AxComponent *panel,const MarkupHole *hole,double mm,
    const BsoFitting *fitting,double nominal,BsoSite *site){
    ProError error=markup_bore_read(panel->model,hole,hole->diameter);
    if(error)return error;
    if(fabs(hole->diameter*mm-nominal)>0.001)return PRO_TK_BAD_INPUTS;
    BsoFaces faces={.model=panel->model,.mm=mm,.radius=hole->diameter*mm*0.5,.read_planes=1};
    if((error=fastener_bso_model_faces(&faces)))return error;
    ProMatrix pose;
    ProPoint3d local={hole->center[0],hole->center[1],hole->center[2]},lip;
    if((error=ProAsmcomppathTrfGet(&panel->path,PRO_B_TRUE,pose))||(error=ProPntTrfEval(local,pose,lip)))return error;
    int matches=0;
    for(int i=0;i<faces.plane_count;i++){
        BsoSite candidate={0};
        const wchar_t *stage=NULL;
        if(fastener_pressfit_target(panel,hole->surface,faces.planes[i],mm,fitting->model,fitting,nominal,0,&candidate,&stage)||
           !ad_near(candidate.geometry.contact,lip,0.001/mm))continue;
        if(++matches==1)*site=candidate;
    }
    return matches==1?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}

static ProError markup_assembly_fasteners(const MarkupCapture *capture,const MarkupRequest *request,
    const wchar_t *model,PartText *out){
    if(!request->count){
        part_text_add(out,L"请圈选要安装压铆件的孔，或框出一组孔。");
        return PRO_TK_BAD_INPUTS;
    }
    const MarkupImage *image=capture->image;
    AxComponent panels[12]={0};
    int indices[12],count=0;
    double mm=image->mm_per_native;
    for(int i=0;i<request->count;i++){
        int hits[12];
        const MarkupMark *mark=&request->marks[i];
        int n=markup_hole_hits(image,mark,hits,mark->tool==MARKUP_ARROW?1:_countof(hits));
        if(n<1){part_text_add(out,L"%d 号标注无法定位到可见圆孔，请缩小标注范围。",i+1);return PRO_TK_BAD_INPUTS;}
        for(int j=0;j<n;j++){
            int seen=0;
            while(seen<count&&indices[seen]!=hits[j])seen++;
            if(seen<count)continue;
            if(count==_countof(indices)){part_text_add(out,L"一次最多核对 12 个压铆孔，请按孔位分组。");return PRO_TK_OUT_OF_RANGE;}
            const MarkupHole *hole=&image->holes[hits[j]];
            ProError error=markup_assembly_component(capture,&hole->occurrence,&panels[count]);
            if(error||!part_close(hole->occurrence.mm_per_native,mm)){
                part_text_add(out,L"第 %d 处孔的组件路径或单位无法与当前装配核对。",count+1);
                return error?error:PRO_TK_BAD_CONTEXT;
            }
            indices[count++]=hits[j];
        }
    }

    ProMdl source=NULL,project=NULL;
    ProPath library=L"",saved=L"";
    ProError error=pressfit_read_exact(model,&source,library,out);
    if(error)return error;
    const PressfitAcceptance *approved=pressfit_acceptance_lookup(source,library);
    if(!approved){part_text_add(out,L"该库件没有当前版本的独立 Creo 试装记录，本次未安装。");return PRO_TK_BAD_CONTEXT;}
    const double catalog=pressfit_acceptance_nominal(approved,library);
    const double nominal=catalog>0?catalog:approved->trial_hole_mm;
    BsoFitting fitting={0};
    error=fastener_pressfit_fitting_for_hole(source,mm,approved->shaft,approved->seat,nominal,&fitting,out);
    if(error)return error;
    BsoSite sites[12]={0};
    for(int i=0;i<count;i++){
        const MarkupHole *hole=&image->holes[indices[i]];
        if(fabs(hole->diameter*mm-nominal)>0.001){
            part_text_add(out,L"第 %d 个孔实际 Ø%.6g mm，所选型号已核对的安装孔为 Ø%.6g mm，本次未安装。",
                i+1,hole->diameter*mm,nominal);
            return PRO_TK_BAD_INPUTS;
        }
        error=markup_assembly_fastener_site(&panels[i],hole,mm,&fitting,nominal,&sites[i]);
        if(!error)error=fastener_pressfit_site_preflight(&sites[i],mm,0,0,NULL,out);
        if(error){
            part_text_add(out,L"第 %d 个孔的可见孔口无法唯一核对承压面、孔口圆边或压入方向，本次未安装。",i+1);
            return error;
        }
    }
    if((error=markup_check_capture(capture)))return error;
    error=pressfit_library_project(source,library,&project,saved,out);
    if(!error)error=markup_check_capture(capture);
    if(!error)error=fastener_pressfit_fitting(project,mm,fitting.shaft,fitting.seat,&fitting,out);
    if(error)return error;
    part_text_add(out,L"已核对 %ls、可见安装侧和 %d 个 Ø%.6g mm 圆孔。\n",approved->instance,count,nominal);
    PressfitBatchResult result={0};
    FastenerSavedView *guard=pressfit_project_placement_begin(project,saved);
    error=fastener_pressfit_place_batch(capture->model,project,&fitting,sites,count,mm,0,0,NULL,&result,out);
    int verified=0;
    for(int i=0;!ax_recovery_uncertain&&i<result.count;i++){
        const PressfitSiteReceipt *row=&result.rows[i];
        if(!row->regenerated||!row->id)continue;
        const ProError audit=fastener_pressfit_postfit(capture->model,&sites[i],row->id,mm,0,out);
        if(!audit){verified++;continue;}
        if(!error)error=audit;
        if(row->disposition==PRESSFIT_ADDED){
            fastener_pressfit_remove_trial(capture->model,row->id,out);
            if(!ax_recovery_uncertain)part_text_add(out,L"孔 %d 本次新增实例的间隙未通过，已撤销并核对。\n",i+1);
        }
    }
    part_text_add(out,L"安装板和邻近实体间隙已核对 %d/%d 孔。\n",verified,count);
    if(!ax_recovery_uncertain)pressfit_project_placement_end(source,project,saved,guard);
    return error?error:verified==count?PRO_TK_NO_ERROR:PRO_TK_INCOMPLETE;
}

static ProError pressfit_workspace_capture_marks(const MarkupCapture *capture,const MarkupRequest *request,PartText *out);

static int markup_assembly_action(const MarkupCapture *capture,const MarkupRequest *request,
    wchar_t *reply,size_t capacity,ProError *error){
    if(!capture->assembly)return 0;
    const wchar_t *text=markup_skip_space(request->text);
    const wchar_t *const select[]={L"选择这些孔用于压铆分组",L"选孔用于压铆分组",L"选择压铆孔位"};
    if(markup_take_phrase(&text,select,_countof(select))&&fastener_chat_end(text)){
        PartText out={reply,capacity,0};
        *error=markup_check_capture(capture);
        if(!*error)*error=pressfit_workspace_capture_marks(capture,request,&out);
        if(*error&&!*reply)part_text_add(&out,L"当前截图不能核对实际压铆孔位（Creo %d）。",*error);
        return 1;
    }
    MarkupAssemblyOffset move={0};
    wchar_t fastener[MAX_PATH];
    int moving=markup_assembly_offset_request(request->text,&move);
    if(!moving&&!markup_assembly_fastener_request(request->text,fastener))return 0;
    PartText out={reply,capacity,0};
    *error=markup_check_capture(capture);
    if(!*error&&(!storage_directory_on_drive(capture->directory,L'E')||!storage_output_directory(capture->directory)||ax_recovery_uncertain))*error=PRO_TK_BAD_CONTEXT;
    if(!*error)*error=moving?markup_assembly_move(capture,request,&move,&out):
        markup_assembly_fasteners(capture,request,fastener,&out);
    if(*error&&!*reply)part_text_add(&out,L"装配、工作目录或组件状态未通过核对（Creo %d）。",*error);
    return 1;
}
