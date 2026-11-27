/* Approved direct chat requests must identify an operation. Geometry qualifiers
   are retained until the native assembly has resolved the actual occurrence. */
static int pressfit_chat_path(const wchar_t **input,wchar_t *path,size_t capacity){
    const wchar_t *p=fastener_chat_separator(*input);
    int ids[AX_PATH_DEPTH];
    size_t length=wcsspn(p,L"IDid:/0123456789");
    if(!length||length>=capacity||wcsncpy_s(path,capacity,p,length)||ax_path_parse(path,ids)<1)return 0;
    *input=p+length;
    return 1;
}

static int pressfit_chat_target(const wchar_t **input,FastenerPressfitRequest *request){
    const wchar_t *p=fastener_chat_separator(*input);
    if(assembly_take(&p,L"在组件")||assembly_take(&p,L"组件")){
        if(request->panel_path[0]||!pressfit_chat_path(&p,request->panel_path,_countof(request->panel_path)))return -1;
        p=fastener_chat_separator(p);
        if(assembly_take(&p,L"安装面")||assembly_take(&p,L"面")){
            if(request->face_id||!fastener_pressfit_id(&p,&request->face_id))return -1;
        }
    }else if(assembly_take(&p,L"照已装组件")||assembly_take(&p,L"照参照组件")){
        if(request->example_path[0]||!pressfit_chat_path(&p,request->example_path,_countof(request->example_path)))return -1;
    }else if(assembly_take(&p,L"选中安装面全部")||assembly_take(&p,L"这一面全部")||assembly_take(&p,L"所选安装面全部")||
             assembly_take(&p,L"安装面全部")||assembly_take(&p,L"全部孔")){
        request->all_on_face=1;
    }else if(assembly_take(&p,L"左边")||assembly_take(&p,L"左侧")){
        if(request->view_side>0)return -1;
        request->view_side=-1;
    }else if(assembly_take(&p,L"右边")||assembly_take(&p,L"右侧")){
        if(request->view_side<0)return -1;
        request->view_side=1;
    }else if(assembly_take(&p,L"靠折边那排")||assembly_take(&p,L"靠折边的一排")){
        request->nearest_bend=1;
    }else if(assembly_take(&p,L"正面")){
        if(request->side_zone==2)return -1;
        request->side_zone=1;
    }else if(assembly_take(&p,L"侧面")){
        if(request->side_zone==1)return -1;
        request->side_zone=2;
    }else if(assembly_take(&p,L"反面安装")||assembly_take(&p,L"从反面")||assembly_take(&p,L"翻到反面")){
        request->flip_side=1;
    }else return 0;
    *input=fastener_chat_separator(p);
    return 1;
}

static int pressfit_chat_lead(const wchar_t **input,FastenerPressfitRequest *request){
    const wchar_t *p=fastener_chat_separator(*input);
    if(!assembly_take(&p,L"把"))assembly_take(&p,L"给");
    const int target=pressfit_chat_target(&p,request);
    if(target<0)return 0;
    const int holes=fastener_bso_hole_count(&p);
    if(holes)request->expected=holes;
    else if(!assembly_take(&p,L"这几个孔")&&!assembly_take(&p,L"这些孔")){
        const wchar_t *quantity=p;
        int count=target?fastener_bso_count(&quantity):0;
        if(count&&(assembly_take(&quantity,L"个")||assembly_take(&quantity,L"颗")||assembly_take(&quantity,L"孔"))){
            request->expected=count;
            p=quantity;
        }
    }
    p=fastener_chat_separator(p);
    assembly_take(&p,L"各");
    if(assembly_take(&p,L"全部")||assembly_take(&p,L"都")){
        if(target==1&&!request->expected&&request->panel_path[0])request->all_on_face=1;
    }
    *input=fastener_chat_separator(p);
    return 1;
}

static int pressfit_chat_family_after(const wchar_t **input,wchar_t model[MAX_PATH]){
    /* e.g. M3×8 的 BSO. The family remains explicit in the user's sentence. */
    if(towupper(model[0])!=L'M'||!wcspbrk(model,L"xX×"))return 0;
    const wchar_t *p=fastener_chat_separator(*input);
    assembly_take(&p,L"的");
    p=fastener_chat_separator(p);
    const wchar_t *family=NULL;
    if(!_wcsnicmp(p,L"BSOS",4))family=L"BSOS";
    else if(!_wcsnicmp(p,L"BSO",3))family=L"BSO";
    if(!family)return 0;
    size_t n=wcslen(family);
    if(p[n]&&wcschr(L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-",p[n]))return 0;
    wchar_t original[MAX_PATH];
    wcscpy_s(original,_countof(original),model);
    if(swprintf_s(model,MAX_PATH,L"%ls %ls",family,original)<0)return 0;
    *input=p+n;

    return 1;
}