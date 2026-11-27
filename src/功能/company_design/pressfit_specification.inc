/* Catalog dimensions carry their original evidence. A fixture result is not
   a supplier drawing or a qualification of the actual panel in Creo. */
typedef struct {
    const PressfitAcceptance *row;
    double nominal;
} PressfitOption;

static const wchar_t *pressfit_kind_name(int kind){
    static const wchar_t *const names[]={
        L"压铆件",L"盲孔螺母柱",L"通孔螺母柱",L"防水铆母",L"涨铆螺母",
        L"平齐压铆螺母",L"齿形压铆螺母",L"定位销",L"六角头压铆螺钉",L"圆头压铆螺钉"
    };
    return kind>0&&kind<_countof(names)?names[kind]:names[0];
}

static int pressfit_kind_matches(int kind,const wchar_t *requested){
    if(!requested||!*requested||!wcscmp(requested,L"压铆件"))return 1;
    if(!wcscmp(requested,L"压铆螺母"))
        return kind==PRESSFIT_SEALED_NUT||kind==PRESSFIT_EXPANSION_NUT||
            kind==PRESSFIT_FLUSH_NUT||kind==PRESSFIT_SERRATED_NUT;
    if(!wcscmp(requested,L"压铆螺母柱")||!wcscmp(requested,L"螺母柱"))
        return kind==PRESSFIT_BLIND_STANDOFF||kind==PRESSFIT_THROUGH_STANDOFF;
    if(!wcscmp(requested,L"压铆螺钉"))
        return kind==PRESSFIT_HEX_RIVET||kind==PRESSFIT_ROUND_RIVET;
    return !wcscmp(requested,pressfit_kind_name(kind));
}

static int pressfit_thread_matches(const wchar_t *instance,const wchar_t *thread){
    if(!thread||!*thread)return 1;
    ProName expected;
    if(!pressfit_name_key(thread,expected)||expected[0]!=L'M')return 0;
    for(const wchar_t *p=instance;*p;p++){
        if(towupper(*p)!=L'M'||!iswdigit(p[1]))continue;
        const wchar_t *end=p+1;
        while(iswdigit(*end))end++;
        if((*end==L'_'||*end==L'.')&&iswdigit(end[1])){
            end++;
            while(iswdigit(*end))end++;
        }
        ProName designation,actual;
        if(!wcsncpy_s(designation,_countof(designation),p,end-p)&&
           pressfit_name_key(designation,actual)&&!wcscmp(actual,expected))return 1;
    }
    return 0;
}

static int pressfit_acceptance_family(const PressfitAcceptance *row,ProName family){
    const wchar_t *suffix=wcsstr(row->source,L".prt.");
    return suffix&&suffix>row->source&&!wcsncpy_s(family,PRO_NAME_SIZE,row->source,suffix-row->source);
}

static int pressfit_acceptance_selector(const PressfitAcceptance *row,wchar_t selector[MAX_PATH]){
    ProName family;
    return pressfit_acceptance_family(row,family)&&
        swprintf_s(selector,MAX_PATH,L"%ls:%ls",family,row->instance)>=0;
}

static int pressfit_acceptance_current_source(const PressfitAcceptance *row,const wchar_t *root,
    wchar_t path[MAX_PATH]){
    ProName family;
    ProPath latest;
    if(!pressfit_acceptance_family(row,family)||ax_native_source(root,family,PRO_MDL_PART,latest))return 0;
    const wchar_t *leaf=wcsrchr(latest,L'\\');
    return leaf&&!_wcsicmp(leaf+1,row->source)&&company_is_under(latest,root)&&
        pressfit_acceptance_source_matches(row,latest)&&!wcscpy_s(path,MAX_PATH,latest);
}

static int pressfit_acceptance_equal(double actual,double evidence){
    return actual>0.0&&evidence>0.0&&fabs(actual-evidence)<=0.001;
}

static void pressfit_acceptance_dimensions(const PressfitAcceptance *row,double nominal,double hole,
    double sheet,PartText *out){
    if(nominal>0.0)
        part_text_add(out,L"公司库目录标注底孔 Ø%.6g mm。",nominal);
    else if(row->library_hole_mm>0.0)
        part_text_add(out,L"当前公司目录版本无法与已记录的底孔资料核对。");
    else
        part_text_add(out,L"公司目录没有标注该规格的正式底孔。");
    part_text_add(out,L"独立 Creo 试装时使用 Ø%.6g mm 孔、%.6g mm 板；实际定位柱 Ø%.6g mm，"
        L"保存和重开验收通过。\n公司库资料未给出最小板厚、孔径公差或压铆工艺；试板厚度和孔径仅用于验证 Creo 装配。\n",
        row->trial_hole_mm,row->trial_thickness_mm,row->pilot_mm);
    if(hole>0.0&&nominal>0.0&&!pressfit_acceptance_equal(hole,nominal))
        part_text_add(out,L"实测 Ø%.6g mm 与公司目录底孔 Ø%.6g mm 相差 %.6g mm；保留孔径时请核对其他型号。\n",
            hole,nominal,fabs(hole-nominal));
    if(sheet>0.0&&!pressfit_acceptance_equal(sheet,row->trial_thickness_mm))
        part_text_add(out,L"实测板厚 %.6g mm，既有 Creo 试装板厚 %.6g mm。\n",sheet,row->trial_thickness_mm);
}

static void pressfit_acceptance_describe(ProMdl model,const wchar_t *source,double hole,double sheet,PartText *out){
    const PressfitAcceptance *row=pressfit_acceptance_lookup(model,source);
    if(!row){
        part_text_add(out,L"本次读取的库件版本没有匹配的独立 Creo 试装记录。当前几何结果以本次原生读回为准。\n");
        return;
    }
    part_text_add(out,L"验收规格 %ls（%ls），库件版本 %ls，定位圆柱 %d、承压面 %d。\n",
        row->instance,pressfit_kind_name(row->kind),row->source,row->shaft,row->seat);
    pressfit_acceptance_dimensions(row,pressfit_acceptance_nominal(row,source),hole,sheet,out);
}

#include "pressfit_preferences.inc"

static void pressfit_acceptance_option(const PressfitOption *choice,PartText *out){
    wchar_t selector[MAX_PATH];
    if(!pressfit_acceptance_selector(choice->row,selector))return;
    part_text_add(out,L"  %ls · %ls · %ls Ø%.6g mm · 试板 %.6g mm\n",selector,
        pressfit_kind_name(choice->row->kind),choice->nominal>0.0?L"目录底孔":L"Creo 试装孔",
        choice->nominal>0.0?choice->nominal:choice->row->trial_hole_mm,choice->row->trial_thickness_mm);
}

static ProError pressfit_acceptance_recommend(double hole,double sheet,const wchar_t *thread,
    const wchar_t *kind,wchar_t selector[MAX_PATH],PartText *out){
    selector[0]=0;
    if(!isfinite(hole)||!isfinite(sheet)||hole<=0.0||sheet<0.0){
        part_text_add(out,L"请先读取实际孔径，板厚可从目标钣金件回读。\n");
        return PRO_TK_BAD_INPUTS;
    }
    wchar_t root[MAX_PATH],source[MAX_PATH];
    if(!company_fastener_resolve_report(COMPANY_OPEN_FASTENER_FIRST+4,root,out))return PRO_TK_CANT_OPEN;
    const PressfitAcceptance *preferred=pressfit_preference_read(hole,sheet);
    PressfitOption exact[6]={0},geometry[4]={0},nearby={0},selection={0},preferred_selection={0};
    unsigned exact_count=0,geometry_count=0,compatible=0;
    const wchar_t *last_file=NULL;
    int verified=0;
    for(size_t i=0;i+1<_countof(pressfit_acceptance_rows);i++){
        const PressfitAcceptance *row=pressfit_acceptance_rows+i;
        if(!pressfit_kind_matches(row->kind,kind)||!pressfit_thread_matches(row->instance,thread))continue;
        const int same_catalog=pressfit_acceptance_equal(row->library_hole_mm,hole);
        const int same_fixture=pressfit_acceptance_equal(row->trial_hole_mm,hole);
        if(!same_catalog&&!same_fixture&&(row->library_hole_mm<=0.0||
           (nearby.row&&fabs(row->library_hole_mm-hole)>=fabs(nearby.nominal-hole))))continue;
        if(!last_file||_wcsicmp(last_file,row->source)){
            verified=pressfit_acceptance_current_source(row,root,source);
            last_file=row->source;
        }
        if(!verified)continue;
        const double nominal=pressfit_acceptance_nominal(row,source);
        const PressfitOption specimen={row,nominal};
        if(pressfit_acceptance_equal(nominal,hole)){
            if(exact_count<_countof(exact))exact[exact_count]=specimen;
            exact_count++;
            if(pressfit_acceptance_equal(sheet,row->trial_thickness_mm)){
                selection=specimen;
                compatible++;
                if(preferred==row)preferred_selection=specimen;
            }
        }else if(same_fixture){
            if(geometry_count<_countof(geometry))geometry[geometry_count]=specimen;
            geometry_count++;
        }else if(nominal>0.0)nearby=specimen;
    }
    part_text_add(out,L"选型依据：实测孔径 Ø%.6g mm",hole);
    if(sheet>0.0)part_text_add(out,L"，板厚 %.6g mm",sheet);
    part_text_add(out,L"。\n");
    if(preferred_selection.row){
        selection=preferred_selection;
        part_text_add(out,L"已核对先前保存的公司选型偏好，现场孔径、试板厚度和现行库件版本适用。\n");
    }else if(compatible!=1)selection.row=NULL;
    if(selection.row&&pressfit_acceptance_selector(selection.row,selector)){
        part_text_add(out,L"可进入 Creo 原生定位核对的规格：\n");
        pressfit_acceptance_option(&selection,out);
        part_text_add(out,L"公司库未列最小板厚与生产公差；装配结果仍须回读实际孔、承压面方向与间隙。\n");
        return PRO_TK_NO_ERROR;
    }
    if(exact_count){
        part_text_add(out,L"有 %u 个目录底孔匹配的已验收规格。",exact_count);
        if(compatible>1)part_text_add(out,L"其中 %u 个也与已验收板厚匹配，请指定规格。",compatible);
        else if(sheet>0.0)part_text_add(out,L"本次板厚没有等厚试装回执，请指定规格核对实际几何。");
        else part_text_add(out,L"请读取目标板厚后再核对适用范围。");
        part_text_add(out,L"\n");
        for(unsigned i=0;i<exact_count&&i<_countof(exact);i++)pressfit_acceptance_option(exact+i,out);
    }else if(nearby.row){
        part_text_add(out,L"当前筛选下，最近的有目录依据的底孔为 Ø%.6g mm，需将孔径从 Ø%.6g mm 改到该尺寸才匹配。\n",
            nearby.nominal,hole);
        pressfit_acceptance_option(&nearby,out);
    }
    if(geometry_count){
        part_text_add(out,L"另有 %u 个规格使用相同孔径通过 Creo 试装。只有试装孔时，正式底孔仍待核实。\n",geometry_count);
        for(unsigned i=0;i<geometry_count&&i<_countof(geometry);i++)pressfit_acceptance_option(geometry+i,out);
    }
    if(!exact_count&&!nearby.row&&!geometry_count)
        part_text_add(out,L"当前登记库没有与这些条件匹配的现行验收。可指定完整型号读取实体与公司规格。\n");
    part_text_add(out,L"当前模型未改动。\n");
    return exact_count?PRO_TK_E_FOUND:PRO_TK_E_NOT_FOUND;
}

static int pressfit_specification_input(const wchar_t *p,wchar_t result[MAX_PATH]){
    p=fastener_chat_separator(p);
    const wchar_t *end=p+wcslen(p);
    while(end>p&&(iswspace(end[-1])||wcschr(L"。！!？?",end[-1])))end--;
    if(end>p&&((*p==L'「'&&end[-1]==L'」')||(*p==L'“'&&end[-1]==L'”')||(*p==L'"'&&end[-1]==L'"'))){
        p++;end--;
    }
    PressfitSelector checked={0};
    return end>p&&(size_t)(end-p)<MAX_PATH&&!wcsncpy_s(result,MAX_PATH,p,end-p)&&pressfit_selector(result,&checked);
}

static int pressfit_specification_chat(const wchar_t *text){
    const wchar_t *p=model_request_action_start(text);
    int refresh=0,save=0;
    if(assembly_take(&p,L"刷新压铆件目录"))refresh=1;
    else if(assembly_take(&p,L"保存压铆选型")||assembly_take(&p,L"记住压铆选型"))save=1;
    else if(!assembly_take(&p,L"查询压铆件验收")&&!assembly_take(&p,L"查看压铆件验收")&&
       !assembly_take(&p,L"核对压铆件规格"))return 0;
    wchar_t selector[MAX_PATH];
    if(refresh?!fastener_chat_end(p):!pressfit_specification_input(p,selector))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    wchar_t reply[4600]=L"";
    PartText out={reply,_countof(reply),0};
    if(refresh){
        pressfit_hash_refresh();
        wchar_t root[MAX_PATH];
        if(company_fastener_resolve_report(COMPANY_OPEN_FASTENER_FIRST+4,root,&out))
            part_text_add(&out,L"压铆件目录可读，已清除本会话的验收指纹缓存。下一次选型会重新读库文件。\n");
    }else{
        ProMdl model=NULL;
        wchar_t source[MAX_PATH];
        ProError error=pressfit_read_exact(selector,&model,source,&out);
        if(!error){
            if(save)error=pressfit_preference_save(model,source,&out);
            else pressfit_acceptance_describe(model,source,0.0,0.0,&out);
        }
        if(error)part_text_add(&out,L"本次%ls没有完成（Creo %d）。\n",save?L"选型保存":L"规格查询",error);
    }
    smart_record_tool(reply);
    smart_reply(reply);
    return 1;
}