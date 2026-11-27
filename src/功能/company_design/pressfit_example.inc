/* Reuse a real installation's native references rather than model names alone. */
typedef struct {
    AxComponent installed,panel;
    BsoFitting tool;
    BsoSite site;
    ProMdlName name,generic;
    wchar_t occurrence[AX_PATH_DEPTH*12+4],panel_path[AX_PATH_DEPTH*12+4];
    wchar_t selector[MAX_PATH];
    double hole,overlap;
} PressfitInstalledExample;

static ProError fastener_pressfit_example_reference(ProMdl assembly,const ProAsmcomppath *parent,
        ProSelection selection,ProModelitem *item,wchar_t occurrence[AX_PATH_DEPTH*12+4]){
    ProAsmcomppath path={0};
    ProError error=ProSelectionModelitemGet(selection,item);
    if(!error)error=ProSelectionAsmcomppathGet(selection,&path);
    if(error)return error;
    if(item->type!=PRO_SURFACE||path.table_num<1||path.table_num>AX_PATH_DEPTH)return PRO_TK_BAD_CONTEXT;
    ProIdTable ids={0};
    int depth=path.table_num;
    if(path.owner==(ProSolid)assembly){
        memcpy(ids,path.comp_id_table,(size_t)depth*sizeof(*ids));
    }else{
        ProSolid local=NULL;
        ProAsmcomppath context=*parent;
        error=ProAsmcomppathMdlGet(&context,&local);
        if(error||path.owner!=local||parent->table_num+depth>AX_PATH_DEPTH)
            return error?error:PRO_TK_BAD_CONTEXT;
        memcpy(ids,parent->comp_id_table,(size_t)parent->table_num*sizeof(*ids));
        memcpy(ids+parent->table_num,path.comp_id_table,(size_t)depth*sizeof(*ids));
        depth+=parent->table_num;
    }
    return ax_path_label(ids,depth,occurrence,AX_PATH_DEPTH*12+4)?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}

static ProError fastener_pressfit_installed_example(ProMdl assembly,const wchar_t *occurrence,double mm,
        PressfitInstalledExample *sample,PartText *out){
    ProAsmcomppath parent={0};
    ProAsmcompconstraint *rows=NULL;
    int count=0,base_ids[2]={0},own_ids[2]={0};
    ProError error=PRO_TK_NO_ERROR;
    if(occurrence&&*occurrence)
        error=wcscpy_s(sample->occurrence,_countof(sample->occurrence),occurrence)?PRO_TK_BAD_INPUTS:PRO_TK_NO_ERROR;
    else error=ax_open_selected(assembly,sample->occurrence);
    if(!error)error=ax_hw_occurrence(assembly,sample->occurrence,mm,&sample->installed);
    if(!error)error=ProAsmcomppathInit((ProSolid)assembly,sample->installed.path.comp_id_table,
        sample->installed.path.table_num-1,&parent);
    if(!error)error=ax_read_constraints(&sample->installed.feature,&parent,&rows,&count);
    if(!error&&count!=2)error=PRO_TK_BAD_CONTEXT;
    for(int i=0;!error&&i<count;i++){
        ProAsmcompConstrType type;
        int flags;
        ProSelection base=NULL,own=NULL;
        ProDatumside side;
        ProModelitem base_item,own_item;
        wchar_t panel[AX_PATH_DEPTH*12+4];
        error=ProAsmcompconstraintTypeGet(rows[i],&type);
        if(!error)error=ProAsmcompconstraintAttributesGet(rows[i],&flags);
        if(!error&&(flags&PRO_ASM_CONSTR_ATTR_FORCE))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ProAsmcompconstraintAsmreferenceGet(rows[i],&base,&side);
        if(!error)error=ProAsmcompconstraintCompreferenceGet(rows[i],&own,&side);
        if(!error&&(!base||!own))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=fastener_pressfit_example_reference(assembly,&parent,base,&base_item,panel);
        if(!error)error=ProSelectionModelitemGet(own,&own_item);
        if(error)break;
        if(own_item.type!=PRO_SURFACE||own_item.owner!=sample->installed.model){error=PRO_TK_BAD_CONTEXT;break;}
        AxGeometry first,second;
        if((error=ax_geometry(&base_item,&first))||(error=ax_geometry(&own_item,&second)))break;
        const int slot=(type==PRO_ASM_INSERT||type==PRO_ASM_ALIGN)&&first.type==AX_GEOM_CYLINDER&&second.type==AX_GEOM_CYLINDER?0:
            (type==PRO_ASM_MATE||type==PRO_ASM_ALIGN)&&first.type==AX_GEOM_PLANE&&second.type==AX_GEOM_PLANE?1:-1;
        if(slot<0||own_ids[slot]||(sample->panel_path[0]&&wcscmp(sample->panel_path,panel))){
            error=PRO_TK_BAD_CONTEXT;
            break;
        }
        if(!sample->panel_path[0])wcscpy_s(sample->panel_path,_countof(sample->panel_path),panel);
        if(!sample->panel.model)error=ax_hw_occurrence(assembly,panel,mm,&sample->panel);
        if(!error&&base_item.owner!=sample->panel.model)error=PRO_TK_BAD_CONTEXT;
        if(error)break;
        own_ids[slot]=own_item.id;
        base_ids[slot]=base_item.id;
    }
    if(rows)ProAsmcompconstraintArrayFree(rows);
    if(!error&&(!own_ids[0]||!own_ids[1]))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=fastener_pressfit_check_fitting(&sample->installed,mm,own_ids[0],own_ids[1],&sample->tool);
    AxGeometry bore;
    if(!error)sample->hole=fastener_pressfit_read_bore(&sample->panel,base_ids[0],mm,&bore);
    if(!error&&sample->hole<=0)error=PRO_TK_BAD_CONTEXT;
    if(!error){
        const wchar_t *stage;
        sample->overlap=fmax(0,2*sample->tool.geometry.cylinder.radius*mm-sample->hole);
        error=fastener_pressfit_target(&sample->panel,base_ids[0],base_ids[1],mm,sample->installed.model,
            &sample->tool,sample->hole,sample->overlap,&sample->site,&stage);
    }
    if(!error){
        ProMatrix pose;
        double radial,gap,facing,parallel;
        error=ProAsmcomppathTrfGet(&sample->installed.path,PRO_B_TRUE,pose);
        if(!error)error=fastener_bso_measure(&sample->tool,&sample->site,pose,mm,&radial,&gap,&facing,&parallel);
        if(!error&&(radial>0.001||fabs(gap)>0.001||facing>-1+1e-6||parallel<1-1e-6))error=PRO_TK_BAD_CONTEXT;
    }
    if(!error)error=ProMdlMdlnameGet(sample->installed.model,sample->name);
    if(error){
        part_text_add(out,L"样件没有可核对的原生圆柱同轴、承压面贴合及实际朝向（Creo %d）。\n",error);
        return error;
    }
    ProMdl generic=NULL;
    if(!ProFaminstanceGenericGet(sample->installed.model,PRO_B_TRUE,&generic)&&generic)
        (void)ProMdlMdlnameGet(generic,sample->generic);
    if(sample->generic[0])
        swprintf_s(sample->selector,_countof(sample->selector),L"%ls:%ls",sample->generic,sample->name);
    else if(!wcsstr(sample->name,L"_BWTL_")&&!wcsstr(sample->name,L"_bwtl_"))
        wcscpy_s(sample->selector,_countof(sample->selector),sample->name);
    part_text_add(out,L"已读取样件 %ls（实例 %ls）：与板实例 %ls 上的 SURFACE:%d 同轴、与 SURFACE:%d 贴合，孔径 Ø%.6g mm，实际方向已核对。\n",
        sample->name,sample->occurrence,sample->panel_path,sample->site.hole_id,sample->site.face_id,sample->hole);
    return PRO_TK_NO_ERROR;
}