/* The local request owns only its receipt. The default part workflow owns Creo
   model creation, staged templates, activation and its normal chat result. */
static struct {
    int active,component,assembly_window,part_window;
    ProMdlName requested,assembly,part;
    ProError verification;
} local_control_pair;

static void local_control_pair_clear(void){
    memset(&local_control_pair,0,sizeof(local_control_pair));
}

static ProError local_control_pair_verify(ProMdl part,ProMdl assembly,PartText *receipt){
    (void)receipt;
    if(!local_control_pair.active)return PRO_TK_NO_ERROR;
    ProMdlName part_name,assembly_name;
    int part_window,assembly_window;
    ProMdlsubtype subtype;
    ProError error=ProMdlMdlnameGet(assembly,assembly_name);
    if(!error)error=ProMdlMdlnameGet(part,part_name);
    if(!error&&(_wcsicmp(assembly_name,local_control_pair.requested)||
       _wcsicmp(part_name,local_control_pair.requested)))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlSubtypeGet(part,&subtype);
    if(!error&&subtype!=PROMDLSTYPE_PART_SHEETMETAL)error=PRO_TK_INVALID_TYPE;
    if(!error)error=ProMdlWindowGet(assembly,&assembly_window);
    if(!error)error=default_part_active(part,-1,&part_window);
    if(!error&&(assembly_window==part_window||default_part.component_id<=0))error=PRO_TK_BAD_CONTEXT;
    local_control_pair.verification=error;
    if(error)return error;
    wcscpy_s(local_control_pair.assembly,_countof(local_control_pair.assembly),assembly_name);
    wcscpy_s(local_control_pair.part,_countof(local_control_pair.part),part_name);
    local_control_pair.component=default_part.component_id;
    local_control_pair.assembly_window=assembly_window;
    local_control_pair.part_window=part_window;
    return PRO_TK_NO_ERROR;
}

static void local_control_pair_reply(const wchar_t *text){
    if(!local_control_pair.active)return;
    local_control_pair.active=0;
    if(!local_control_pair.component){
        const ProError error=local_control_pair.verification;
        memset(&local_control_pair,0,sizeof(local_control_pair));
        local_control_pair_finish(error,
            text&&*text?text:L"Creo 未完成同号装配和钣金件的创建及打开核对。");
        return;
    }
    wchar_t result[8300]=L"";
    PartText output={result,_countof(result),0};
    part_text_add(&output,L"%ls\nassembly=%ls.ASM\npart=%ls.PRT\ncomponentId=%d\nassemblyWindow=%d\npartWindow=%d\nsubtype=SHEETMETAL",
        text,local_control_pair.assembly,local_control_pair.part,local_control_pair.component,
        local_control_pair.assembly_window,local_control_pair.part_window);
    memset(&local_control_pair,0,sizeof(local_control_pair));
    local_control_pair_finish(PRO_TK_NO_ERROR,result);
}

static ProError local_control_pair_begin(const wchar_t *drawing_number,wchar_t *out,size_t capacity){
    if(!out||!capacity)return PRO_TK_BAD_INPUTS;
    out[0]=0;
    if(local_control_pair.active||native_busy||smart_active||ai_process){
        wcsncpy_s(out,capacity,L"当前 Creo 会话有任务正在处理。",_TRUNCATE);
        return PRO_TK_E_IN_USE;
    }
    size_t length=drawing_number?wcsnlen(drawing_number,PRO_NAME_SIZE):0;
    if(length<=3||length>=PRO_NAME_SIZE||!company_pair_revision(drawing_number+length-3)||
       !model_assembly_name_valid(drawing_number)||!dv_safe_leaf(drawing_number)){
        wcsncpy_s(out,capacity,L"请提供完整图号和 V00 格式的两位版本号。图号含版本最多 31 个字符，只接受字母、数字、短横线和下划线。",_TRUNCATE);
        return PRO_TK_INVALID_NAME;
    }

    ProError error=default_part_name_available(drawing_number,1);
    if(error){
        if(error==PRO_TK_E_FOUND)
            swprintf_s(out,capacity,L"当前会话或工作目录已有 %ls 的装配或零件，本次未创建。",drawing_number);
        else
            wcsncpy_s(out,capacity,L"无法核对该图号的可用性，本次未创建。",_TRUNCATE);
        return error;
    }

    ModelingPartIntent request={.scope=MODELING_PART_NEW,.kind=2,.require_company=1};
    wcscpy_s(request.name,_countof(request.name),drawing_number);
    wchar_t command[160];
    swprintf_s(command,_countof(command),L"按图号创建同号装配和钣金件 %ls",drawing_number);

    memset(&local_control_pair,0,sizeof(local_control_pair));
    local_control_pair.active=1;
    local_control_pair.verification=PRO_TK_GENERAL_ERROR;
    wcscpy_s(local_control_pair.requested,_countof(local_control_pair.requested),drawing_number);
    if(!default_part_begin_request(command,&request,local_control_pair_verify)){
        local_control_pair.active=0;
        wcsncpy_s(out,capacity,L"MWEAI 未能启动本次同号装配与钣金件创建。",_TRUNCATE);
        return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}
