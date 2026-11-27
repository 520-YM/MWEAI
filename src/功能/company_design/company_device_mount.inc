/* Company electronics use the hardware placement's measured two-hole frame. */
static ProError company_device_source(const wchar_t *model,NativeFile *file,ProMdl *part,int *loaded,PartText *out){
    wchar_t library[MAX_PATH],root[MAX_PATH];
    HANDLE dir=company_open_root(L"公司标准件",library);
    if(dir==INVALID_HANDLE_VALUE){
        part_text_add(out,L"未能打开注册的公司标准件目录。\n");
        return PRO_TK_CANT_OPEN;
    }
    ProPath candidate;
    ProError error=ax_find_standard(library,model,PRO_MDL_PART,candidate);
    CloseHandle(dir);
    if(!error&&(!company_normalize_path(candidate,root)||!company_is_under(root,library)))error=PRO_TK_BAD_CONTEXT;
    if(error){
        part_text_add(out,L"在公司标准件中未找到唯一型号 %ls（Creo %d）。\n",model,error);
        return error;
    }
    HANDLE lock=company_library_source(root,file,out);
    if(lock==INVALID_HANDLE_VALUE)return PRO_TK_CANT_OPEN;
    error=attachment_open_native(file,part,loaded,out);
    CloseHandle(lock);
    return error;
}

static ProError company_device_site(AxComponent *owner,int plane,int hole,double mm,AxHardwareSite *site){
    wchar_t face[32],wall[32];
    fastener_bso_surface(plane,face);
    fastener_bso_surface(hole,wall);
    FdTxn test={.model=owner->model,.scale=1/mm};
    ShopMouth mouth={0};
    ProError error=mp_mouth(&test,face,wall,&mouth);
    if(!error)error=ax_hw_site(owner,wall,face,mm,site);
    return !error&&!site->bore?PRO_TK_BAD_CONTEXT:error;
}

static ProError company_device_neighbors(ProMdl assembly,const AxBatchItem *placed,AxComponent *panel,
    double mm,PartText *out){
    AxScan *scan=NULL;
    ProError error=ax_scan(assembly,1,&scan);
    if(!error&&(scan->limited||scan->unavailable))error=PRO_TK_INCOMPLETE;
    int checked=0;
    for(int i=0;!error&&i<scan->count;i++){
        AxOccurrence *entry=&scan->occurrences[i];
        if(entry->type!=PRO_MDL_PART||entry->state!=PRO_FEAT_ACTIVE||
           (entry->depth==1&&entry->ids[0]==placed->feature.id))continue;
        AxComponent other={0};
        wchar_t key[AX_PATH_DEPTH*12+4];
        if(!ax_path_label(entry->ids,entry->depth,key,_countof(key))){error=PRO_TK_BAD_CONTEXT;break;}
        error=ax_hw_occurrence(assembly,key,mm,&other);
        if(error||ax_same_occurrence(&other,panel))continue;
        ProBoolean skeleton=PRO_B_FALSE;
        error=ProMdlIsSkeleton(other.model,&skeleton);
        if(error||skeleton)continue;
        double volume=0;
        AxBatchItem readback=*placed;
        error=ax_device_fit(assembly,&readback,&other,NULL,mm,&volume);
        if(!error&&volume>0.01)error=PRO_TK_BAD_CONTEXT;
        if(error)part_text_add(out,L"邻近零件 %ls %ls 的干涉检查未通过，实测重叠 %.8g mm³，Creo %d。\n",
            key,entry->name,volume,error);
        else checked++;
    }
    free(scan);
    if(!error)part_text_add(out,L"安装板与另外 %d 个活动实体零件的干涉检查通过。\n",checked);
    return error;
}

static ProError company_device_place(const wchar_t *name,const int refs[3],PartText *out){
    FastenerPressfitTarget binding={0};
    ProError error=fastener_pressfit_capture(&binding,out);
    if(error){
        if(binding.stamp)ProVerstampFree(&binding.stamp);
        return error;
    }
    int face=0,holes[12],count=0,loaded=0;
    AxComponent panel={0},device={0};
    AxHardwareSite target[2],source[2];
    AxDeviceFrame panel_frame,device_frame;
    double target_pitch=0,source_pitch=0,device_mm=0;
    NativeFile file={0};
    AxBatchItem placed={0};
    wchar_t saved[MAX_PATH]=L"";
    const double mm=binding.mm;
    const wchar_t *stage=L"选中的两个安装孔及板面";
#define DEVICE_TRY(label,call) do{stage=(label);error=(call);if(error)goto done;}while(0)
    error=fastener_pressfit_selection_workbench(binding.assembly,1,mm,&panel,&face,holes,&count);
    if(error||!face||count!=2){
        part_text_add(out,L"请在同一个组件上，按对应顺序选两个圆孔口边。也可选安装面和两道孔壁。\n");
        if(!error)error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
    for(int i=0;i<2;i++){
        DEVICE_TRY(L"核对钣金孔口属于指定安装面",company_device_site(&panel,face,holes[i],mm,&target[i]));
        DEVICE_TRY(L"换算安装孔的装配坐标",ax_hw_world(&panel,&target[i]));
    }
    double gauge=pressfit_chat_panel_thickness(panel.model);
    if(!gauge){error=PRO_TK_BAD_CONTEXT;stage=L"回读钣金板厚";goto done;}
    DEVICE_TRY(L"读取公司库精确版本",company_device_source(name,&file,&device.model,&loaded,out));
    DEVICE_TRY(L"检查电子件原生状态",part_health(device.model));
    DEVICE_TRY(L"核对电子件模型单位",qa_mm_per_unit(device.model,&device_mm));
    if(fabs(mm-device_mm)>1e-9){error=PRO_TK_BAD_CONTEXT;goto done;}
    for(int i=0;i<2;i++)
        DEVICE_TRY(L"核对电子件安装面和两个对应孔口",company_device_site(&device,refs[0],refs[i+1],mm,&source[i]));
    DEVICE_TRY(L"计算板孔距",ax_hole_pair_frame(target,&panel_frame,&target_pitch));
    DEVICE_TRY(L"计算电子件孔距",ax_hole_pair_frame(source,&device_frame,&source_pitch));
    part_text_add(out,L"已读取 %ls。板厚 %.6g mm，板孔径 Ø%.6g / Ø%.6g mm，电子件孔径 Ø%.6g / Ø%.6g mm，"
        L"两边孔距 %.6g / %.6g mm。\n",file.name,gauge,2*target[0].cylinder.radius*mm,2*target[1].cylinder.radius*mm,
        2*source[0].cylinder.radius*mm,2*source[1].cylinder.radius*mm,target_pitch*mm,source_pitch*mm);
    if(fabs(target_pitch-source_pitch)*mm>0.01){
        stage=L"匹配孔距";error=PRO_TK_BAD_CONTEXT;goto done;
    }
    for(int i=0;i<3;i++){panel_frame.axis[1][i]*=-1;panel_frame.axis[2][i]*=-1;}
    ax_device_align(&device_frame,&panel_frame,placed.pose);
    DEVICE_TRY(L"另存并核对项目电子件",pressfit_library_project(device.model,file.source,&placed.model,saved,out));
    DEVICE_TRY(L"复核当前装配和工作目录",fastener_pressfit_bound(&binding,out));
    AxDevicePlane mounting={0};
    memcpy(mounting.point,target[0].contact,sizeof(ProVector));
    memcpy(mounting.normal,target[0].plane.direction,sizeof(ProVector));
    wchar_t receipt[450]=L"";
    PartText placement={receipt,_countof(receipt),0};
    DEVICE_TRY(L"定位并核对板件干涉",ax_device_create(binding.assembly,&placed,1,mm,&panel,&mounting,NULL,NULL,&placement));
    error=company_device_neighbors(binding.assembly,&placed,&panel,mm,out);
    if(error){
        stage=L"核对周边组件干涉";
        ax_discard_occurrence(binding.assembly,&placed.feature,out);
        goto done;
    }
    part_text_add(out,L"%ls已按孔序贴合并核对固定定位。项目件：%ls\n",receipt,saved);
done:
#undef DEVICE_TRY
    if(error){
        part_text_add(out,L"电子件未通过「%ls」（Creo %d）。\n",stage,error);
        if(saved[0])part_text_add(out,L"已核对的项目件保留在 %ls。\n",saved);
    }
    if(loaded&&device.model)ProMdlErase(device.model);
    if(binding.stamp)ProVerstampFree(&binding.stamp);
    return error;
}

static int company_device_chat(const wchar_t *text){
    const wchar_t *p=model_request_action_start(text);
    if(!assembly_take(&p,L"装配公司库电子件"))return 0;
    if(wcspbrk(p,L"?？\r\n"))return 0;
    p=fastener_chat_separator(p);
    const size_t size=wcsspn(p,L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-");
    ProMdlName model=L"";
    int refs[3]={0};
    if(!size||size>=_countof(model)||wcsncpy_s(model,_countof(model),p,size)||!part_name(model)){
        append(L"请填写公司库电子件的具体型号。");return 1;
    }
    p+=size;
    for(int i=0;i<3;i++)if(!fastener_pressfit_id(&p,&refs[i])){
        append(L"请依次填写电子件的安装面、第一和第二个圆孔的 SURFACE 编号。");
        return 1;
    }
    if(!fastener_chat_end(p)||refs[1]==refs[2])return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    wchar_t answer[4800]=L"";
    PartText out={answer,_countof(answer),0};
    company_device_place(model,refs,&out);
    smart_record_tool(answer);
    smart_reply(answer);
    return 1;
}
