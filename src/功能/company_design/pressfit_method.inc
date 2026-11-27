/* A learned method records measured, verified assembly rules, not a prior board's face IDs. */
static int pressfit_fitting_model_name(ProMdl model,ProMdlName name){
    if(ProMdlMdlnameGet(model,name))return 0;
    for(wchar_t *p=name;*p;p++)
        if(!_wcsnicmp(p,L"_BWTL",5)&&(p[5]==0||p[5]==L'_')){*p=0;break;}
    return name[0]!=0;
}


static int pressfit_method_path(const wchar_t *hash,const wchar_t *selector,wchar_t path[MAX_PATH]){
    unsigned long long key=14695981039346656037ULL;
    for(const wchar_t *p=selector;*p;p++)if(iswalnum(*p)){key^=towlower(*p);key*=1099511628211ULL;}
    wchar_t name[96];
    return swprintf_s(name,_countof(name),L"%.32ls-%016llx",hash,key)>=0&&
        pressfit_record_path(path,L"压铆装配方法",name,L".txt");
}

static ProError pressfit_verify_checkpoint_readback(ProMdl assembly,PressfitCheckpoint *state,
    ProMdl *source,BsoFitting *fitting,BsoSite sites[12],PressfitOccupancy current[12],PartText *out){
    if(!pressfit_checkpoint_read(state)||!state->complete)return PRO_TK_E_NOT_FOUND;
    ProMdl part=NULL;
    wchar_t library[MAX_PATH];
    char stamp[240];
    ProError error=ax_study_verstamp(assembly,stamp);
    if(!error&&strcmp(state->last,stamp))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=pressfit_read_exact(state->selector,source,library,out);
    if(!error)error=fastener_pressfit_fitting(*source,state->mm,state->shaft,state->seat,fitting,out);
    if(!error)error=pressfit_checkpoint_sites(assembly,library,fitting,state,sites,out);
    if(error)return error;
    for(int i=0;i<state->count;i++){
        const PressfitCheckpointSite *row=&state->sites[i];
        ProAsmcomp component;
        ProMdl model=NULL;
        if(row->id<=0)return PRO_TK_BAD_CONTEXT;
        error=ax_component_feature(assembly,row->id,&component);
        if(!error)error=ProAsmcompMdlGet(&component,&model);
        if(error)return error;
        if(part&&part!=model)return PRO_TK_BAD_CONTEXT;
        part=model;
    }
    error=fastener_pressfit_fitting(part,state->mm,state->shaft,state->seat,fitting,out);
    wchar_t validation[512]=L"";
    PartText check={validation,_countof(validation),0};
    if(!error&&*source!=part)error=pressfit_verify_project(*source,part,&check);
    if(!error)error=fastener_pressfit_scan_occupancy(assembly,part,fitting,sites,state->count,state->mm,current);
    if(error)return error;
    for(int i=0;i<state->count;i++)if(current[i].count!=1||!current[i].verification.regenerated||
        current[i].id!=state->sites[i].id)return PRO_TK_INCOMPLETE;
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_method_learn(ProMdl assembly,PartText *out){
    PressfitCheckpoint state={0};
    ProError error=pressfit_checkpoint_lock(assembly,&state);
    ProMdl source=NULL;
    BsoFitting fitting={0};
    BsoSite sites[12]={0};
    PressfitOccupancy current[12]={0};
    wchar_t path[MAX_PATH],temp[MAX_PATH];
    if(!error)error=pressfit_verify_checkpoint_readback(assembly,&state,&source,&fitting,sites,current,out);
    if(error)goto done;

    ProMdlName source_name;
    if(!pressfit_fitting_model_name(source,source_name)||!pressfit_method_path(state.source_hash,source_name,path)){
        error=PRO_TK_CANT_WRITE;goto done;
    }
    FILE *file=ax_study_atomic(path,temp);
    if(!file){error=PRO_TK_CANT_WRITE;goto done;}
    const double pilot=2*fitting.geometry.cylinder.radius*state.mm;
    fwprintf(file,L"BWTL-PRESSFIT-METHOD-1\n%ls\n%ls\n%d %d %.17g %.17g %d\n",
        source_name,state.source_hash,state.shaft,state.seat,pilot,state.interference,state.count);
    for(int i=0;i<state.count;i++){
        double start,end;
        const BsoSite *site=&sites[i];
        ax_hw_axial_span(&site->geometry,site->geometry.contact,site->geometry.plane.direction,state.mm,&start,&end);
        fwprintf(file,L"%.17g %.17g\n",state.sites[i].diameter,end-start);
    }
    fwprintf(file,L"%% 圆柱同轴 + 实际板面与承压端零偏移贴合。每次从当前装配重新读取孔和面，验证方向、承压与再生。\n");
    if(!ax_study_commit(file,temp,path)){error=PRO_TK_CANT_WRITE;goto done;}
    part_text_add(out,L"已从 %d 个原生定位实例复验并保存压铆方法：%ls。\n",state.count,path);
done:
    pressfit_checkpoint_close(&state);
    if(error)part_text_add(out,L"未取得本装配最近一次完整验收的同规格压铆批次（Creo %d）。\n",error);
    return error;
}

static int pressfit_method_match(const wchar_t *selector,const wchar_t *library,const BsoFitting *fitting,
    const BsoSite *sites,int count,double mm,PartText *out){
    wchar_t hash[65],path[MAX_PATH],line[512],saved_selector[PRO_NAME_SIZE*2+3],saved_hash[68];
    unsigned long long bytes;
    ProMdlName source_name;
    if(!selector||!library||!fitting||!sites||count<1||count>12||
       !pressfit_fitting_model_name(fitting->model,source_name)||!dv_sha256(library,hash,&bytes)||
       !pressfit_method_path(hash,source_name,path))return 0;
    FILE *file=ax_learning_open(path);
    if(!file)return 0;
    int n=0,shaft=0,seat=0;
    double pilot=0.0,allowance=0.0,holes[12]={0},depths[12]={0};
    int match=pressfit_record_line(file,line,_countof(line))&&!wcscmp(line,L"BWTL-PRESSFIT-METHOD-1")&&
        pressfit_record_line(file,saved_selector,_countof(saved_selector))&&!_wcsicmp(saved_selector,source_name)&&
        pressfit_record_line(file,saved_hash,_countof(saved_hash))&&!_wcsicmp(saved_hash,hash)&&
        pressfit_record_line(file,line,_countof(line))&&swscanf_s(line,L"%d %d %lf %lf %d",
            &shaft,&seat,&pilot,&allowance,&n)==5&&
        shaft==fitting->shaft&&seat==fitting->seat&&fabs(pilot-2*fitting->geometry.cylinder.radius*mm)<=0.001&&
        n>0&&n<=12&&isfinite(allowance)&&allowance>=0.0;
    for(int i=0;match&&i<n;i++)match=pressfit_record_line(file,line,_countof(line))&&
        swscanf_s(line,L"%lf %lf",&holes[i],&depths[i])==2&&holes[i]>0.0&&depths[i]>0.0&&
        isfinite(holes[i])&&isfinite(depths[i]);
    fclose(file);
    for(int i=0;match&&i<count;i++){
        const BsoSite *site=&sites[i];
        double from,to;
        ax_hw_axial_span(&site->geometry,site->geometry.contact,site->geometry.plane.direction,mm,&from,&to);
        int found=0;
        for(int j=0;j<n;j++)if(fabs(holes[j]-2*site->geometry.cylinder.radius*mm)<=0.001&&
            fabs(depths[j]-(to-from))<=0.001){found=1;break;}
        match=found;
    }
    if(match)part_text_add(out,L"已匹配保存的压铆方法，当前板的实测孔径和孔深符合该记录。将按当前孔轴和板面核验定位。\n");
    return match;
}
