/* Limit local press-fit placement to explicitly identified geometry. Use
   the same library resolver, native constraints and readback as BWTL chat. */
static int local_control_pressfit_surface(const wchar_t *token,int *id){
    if(_wcsnicmp(token,L"SURFACE:",8))return 0;
    const wchar_t *value=token;
    return fastener_pressfit_id(&value,id)&&!*value;
}

static int local_control_pressfit_request(const wchar_t *payload,FastenerPressfitRequest *request){
    const size_t length=wcsnlen(payload,1152);
    PartCommand tokens={0};
    if(!length||length>=1152||wcspbrk(payload,L"\r\n")||!part_tokens(payload,length,&tokens))return 0;

    request->inspect=!_wcsicmp(tokens.arg[0],L"PREVIEW");
    const int first=request->inspect;
    const int holes=tokens.count-first-3;
    if(holes<1||holes>(int)_countof(request->mapped_holes))return 0;

    const wchar_t *model=tokens.arg[first];
    PressfitSelector selector={0};
    if(wcsspn(model,L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-: ×")!=wcslen(model)||
       !pressfit_selector(model,&selector)||wcscpy_s(request->model,_countof(request->model),model))return 0;

    const wchar_t *path=tokens.arg[first+1];
    int ids[AX_PATH_DEPTH];
    if(!ax_path_parse(path,ids)||wcscpy_s(request->panel_path,_countof(request->panel_path),path)||
       !local_control_pressfit_surface(tokens.arg[first+2],&request->face_id))return 0;

    for(int i=0;i<holes;i++){
        int id;
        if(!local_control_pressfit_surface(tokens.arg[first+3+i],&id)||id==request->face_id)return 0;
        for(int j=0;j<i;j++)if(request->mapped_holes[j]==id)return 0;
        request->mapped_holes[i]=id;
    }
    request->action=PRESSFIT_PLACE;
    request->short_form=1;
    request->check_clearance=1;
    request->mapped_count=request->expected=holes;
    return 1;
}

/* Shared by the explicit local action and the independently verified panel
   task. A caller supplies native refs, which still have to match live geometry. */
static ProError local_control_pressfit_apply(ProMdl current_model,FastenerPressfitRequest *request,
    const wchar_t *description,PartText *reply){
    double mm;
    AxComponent panel={0};
    ProError error=qa_mm_per_unit(current_model,&mm);
    if(!error)error=ax_hw_occurrence(current_model,request->panel_path,mm,&panel);
    wchar_t face[32];
    fastener_bso_surface(request->face_id,face);
    for(int i=0;!error&&i<request->mapped_count;i++){
        AxHardwareSite site;
        wchar_t hole[32];
        fastener_bso_surface(request->mapped_holes[i],hole);
        error=ax_hw_site(&panel,hole,face,mm,&site);
        if(error){
            part_text_add(reply,L"安装板 %ls 的孔壁 %ls 与安装面 %ls 无法配对（Creo %d）。本次未装配。",request->panel_path,hole,face,error);
            return error;
        }
        const double diameter=site.cylinder.radius*2*mm;
        if(!isfinite(diameter)||diameter<=0.0||
           (i&&fabs(diameter-request->bound_hole_mm)>0.001)){
            part_text_add(reply,L"选定孔壁的实测孔径不同，请按同一孔径分组。本次未装配。");
            return PRO_TK_BAD_CONTEXT;
        }
        request->bound_hole_mm=diameter;
    }
    if(error){
        part_text_add(reply,L"无法核对装配中的安装板实例 %ls（Creo %d）。本次未装配。",request->panel_path,error);
        return error;
    }
    return fastener_pressfit_execute(*request,description,reply);
}

static ProError local_control_pressfit(ProMdl current_model,const wchar_t *payload,wchar_t *text,size_t capacity){
    PartText reply={text,capacity,0};
    ProMdlType type;
    FastenerPressfitRequest request={0};
    if(!current_model||ProMdlTypeGet(current_model,&type)||type!=PRO_MDL_ASSEMBLY){
        part_text_add(&reply,L"请先打开并核对要安装压铆件的 Creo 装配。");
        return PRO_TK_BAD_CONTEXT;
    }
    if(!local_control_pressfit_request(payload,&request)){
        part_text_add(&reply,L"请提供压铆件精确型号、安装板实例路径、安装面及 1–12 个不同孔壁号。格式为 [PREVIEW] 型号 ID:组件 SURFACE:安装面 SURFACE:孔壁…");
        return PRO_TK_BAD_INPUTS;
    }
    return local_control_pressfit_apply(current_model,&request,payload,&reply);
}
