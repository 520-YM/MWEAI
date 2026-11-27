/* This snapshot contains only exact source revisions that completed native placement. */
enum {
    PRESSFIT_BLIND_STANDOFF=1,PRESSFIT_THROUGH_STANDOFF,PRESSFIT_SEALED_NUT,
    PRESSFIT_EXPANSION_NUT,PRESSFIT_FLUSH_NUT,PRESSFIT_SERRATED_NUT,
    PRESSFIT_LOCATING_PIN,PRESSFIT_HEX_RIVET,PRESSFIT_ROUND_RIVET
};
typedef struct {
    const wchar_t *source,*instance,*sha256;
    int shaft,seat;
    double pilot_mm,trial_hole_mm,library_hole_mm,trial_thickness_mm;
    int kind;
} PressfitAcceptance;

static const PressfitAcceptance pressfit_acceptance_rows[]={
#include "pressfit_acceptance_rows.inc"
    {0}
};

#include "pressfit_evidence.inc"

static int pressfit_acceptance_document(wchar_t path[MAX_PATH]){
    return swprintf_s(path,MAX_PATH,L"%ls\\docs\\压铆件逐规格验收.csv",runtime_root)>=0&&
        GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
}

static int pressfit_specification_chat(const wchar_t *text);

static int pressfit_acceptance_chat(const wchar_t *text){
    if(pressfit_specification_chat(text))return 1;
    const wchar_t *p=model_request_action_start(text);
    if(!assembly_take(&p,L"查看压铆件验收")&&!assembly_take(&p,L"打开压铆件验收"))return 0;
    assembly_take(&p,L"清单");
    if(!fastener_chat_end(p))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    wchar_t path[MAX_PATH],reply[640];
    if(!pressfit_acceptance_document(path)){
        wcscpy_s(reply,_countof(reply),L"当前加载的 MWEAI 版本没有找到压铆件逐规格验收文件。");
    }else{
        const int opened=(INT_PTR)ShellExecuteW(native_window,L"open",path,NULL,NULL,SW_SHOWNORMAL)>32;
        swprintf_s(reply,_countof(reply),L"%ls压铆件逐规格验收清单。本版本已收录 %zu 个通过原生装配的规格。\n%ls",
            opened?L"已打开":L"可以在资源管理器中打开",_countof(pressfit_acceptance_rows)-1,path);
    }
    smart_record_tool(reply);
    smart_reply(reply);
    return 1;
}

static const PressfitAcceptance *pressfit_acceptance_lookup(ProMdl model,const wchar_t *source){
    const wchar_t *leaf=wcsrchr(source,L'\\');
    ProMdlName name;
    if(!leaf||ProMdlMdlnameGet(model,name))return NULL;
    for(size_t i=0;i+1<_countof(pressfit_acceptance_rows);i++){
        const PressfitAcceptance *row=&pressfit_acceptance_rows[i];
        if(_wcsicmp(leaf+1,row->source)||_wcsicmp(name,row->instance))continue;
        if(pressfit_acceptance_source_matches(row,source))return row;
        break;
    }
    return NULL;
}



static double pressfit_acceptance_nominal(const PressfitAcceptance *row,const wchar_t *source){
    if(row->library_hole_mm<=0.0||!PRESSFIT_ACCEPTANCE_CATALOG_SHA256[0])return 0.0;
    const wchar_t *leaf=wcsrchr(source,L'\\');
    wchar_t catalog[MAX_PATH],sha256[65];
    if(!leaf||swprintf_s(catalog,MAX_PATH,L"%.*ls\\%ls",(int)(leaf-source),source,
           native_company_fasteners[4].catalog_file)<0)return 0.0;
    return pressfit_evidence_hash(catalog,sha256)&&!_wcsicmp(sha256,PRESSFIT_ACCEPTANCE_CATALOG_SHA256)
        ?row->library_hole_mm:0.0;
}

#include "pressfit_specification.inc"
