typedef enum {
    PW_RENAME,PW_MERGE,PW_SPLIT,PW_DROP,PW_ADD
} PressfitWorkspaceEdit;

static ProError pw_load(ProMdl assembly,int panel,GroupedPressfitRecord *record,
    GroupPressfitGeometry *geometry,int verify,PartText *out){
    ProError error=grouped_pressfit_open(assembly,panel,record);
    if(error){
        part_text_add(out,L"当前 E 盘项目没有可读取的目标板件分组记录（Creo %d）。\n",error);
        return error;
    }
    return verify?grouped_pressfit_bind(assembly,record,geometry,out):
        group_pressfit_geometry(assembly,record->panel,geometry,out);
}

static int pw_hole_number(const GroupPressfitGeometry *geometry,const GroupPressfitHole *hole){
    for(int i=0;i<geometry->count;i++){
        const GroupPressfitHole *candidate=&geometry->holes[i];
        if(ad_near(candidate->center,hole->center,0.001)&&fabs(candidate->diameter-hole->diameter)<=0.001)return i+1;
    }
    return 0;
}

static int pw_site_number(const GroupPressfitGeometry *geometry,const GroupedPressfitGroup *group,int number){
    if(number<1||number>geometry->count)return -1;
    for(int i=0;i<group->count;i++)if(number==pw_hole_number(geometry,&group->sites[i].hole))return i;
    return -1;
}

static ProError pw_audit_group(ProMdl assembly,const GroupPressfitGeometry *geometry,
    const GroupedPressfitGroup *group,GroupedPressfitView *view,PartText *out){
    if(group->pending[0]){
        part_text_add(out,L"%ls仍在续做状态，计划规格 %ls。\n",group->name,group->pending);
        return PRO_TK_INCOMPLETE;
    }
    ProError error=grouped_pressfit_read_view(assembly,geometry,group,2,view,out);
    if(error)return error;
    int incomplete=0;
    for(int i=0;i<group->count;i++){
        const GroupedPressfitSite *item=&group->sites[i];
        const int number=pw_hole_number(geometry,&item->hole);
        const int id=view->ids[i];
        if(!id){
            incomplete++;
            part_text_add(out,L"%ls %d 号孔空置，登记规格 %ls。\n",group->name,number,group->spec);
            continue;
        }
        BsoSite site={0};
        ProError checked=group_pressfit_bso_site(geometry,&item->hole,&site);
        wchar_t detail[1500]=L"";
        PartText report={detail,_countof(detail),0};
        if(!checked)checked=fastener_pressfit_postfit(assembly,&site,id,geometry->mm,0,&report);
        if(checked||id!=item->id)incomplete++;
        part_text_add(out,L"%ls %d 号孔 ID:%d %ls。%ls\n",group->name,number,id,
            checked?L"干涉或间隙待核":id!=item->id?L"占位已更换，需核对记录":L"规格、同轴、贴合和间隙已验",
            checked?detail:L"");
    }
    return incomplete?PRO_TK_INCOMPLETE:PRO_TK_NO_ERROR;
}

static ProError pw_audit(ProMdl assembly,int panel,const wchar_t *name,PartText *out){
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=pw_load(assembly,panel,&record,geometry,1,out);
    int found=0;
    if(!error){
        for(int i=0;i<record.count;i++){
            const GroupedPressfitGroup *g=&record.groups[i];
            if(name&&_wcsicmp(name,g->name))continue;
            found++;
            GroupedPressfitView view={0};
            const ProError status=pw_audit_group(assembly,geometry,g,&view,out);
            if(status&&!error)error=status;
        }
        if(!found){
            part_text_add(out,L"未找到指定分组。\n");
            error=PRO_TK_E_NOT_FOUND;
        }
    }
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}

static ProError pw_fill_group(ProMdl assembly,const GroupPressfitGeometry *geometry,GroupedPressfitRecord *record,
    int index,PartText *out){
    GroupedPressfitGroup *group=&record->groups[index];
    if(group->pending[0])return PRO_TK_INCOMPLETE;
    GroupedPressfitView before={0},after={0};
    ProError error=grouped_pressfit_read_view(assembly,geometry,group,2,&before,out);
    if(error)return error;
    GroupedPressfitGroup missing={0};
    wcscpy_s(missing.name,_countof(missing.name),group->name);
    wcscpy_s(missing.pending,_countof(missing.pending),group->spec);
    for(int i=0;i<group->count;i++){
        if(before.ids[i])continue;
        missing.sites[missing.count++]=group->sites[i];
    }
    if(missing.count){
        error=grouped_pressfit_apply(assembly,geometry,&missing,1,out);
        if(!error)error=grouped_pressfit_apply(assembly,geometry,&missing,0,out);
        if(error){
            part_text_add(out,L"%ls有尚未补齐的孔。可再次补装该组，已装实例会逐孔核对保留。\n",group->name);
            return error;
        }
    }
    error=grouped_pressfit_read_view(assembly,geometry,group,2,&after,out);
    for(int i=0;!error&&i<group->count;i++){
        if(!after.ids[i]||(before.ids[i]&&(before.ids[i]!=after.ids[i]||
           !ax_same_pose(before.poses[i],after.poses[i],geometry->mm))))error=PRO_TK_BAD_CONTEXT;
    }
    if(error)return error;
    for(int i=0;i<group->count;i++)group->sites[i].id=after.ids[i];
    if(!grouped_pressfit_write(record))return PRO_TK_CANT_WRITE;
    part_text_add(out,L"%ls补装 %d 处，原来已装的 %d 处经复核保持。\n",
        group->name,missing.count,group->count-missing.count);
    return PRO_TK_NO_ERROR;
}

static ProError pw_recover(ProMdl assembly,int panel,const wchar_t *name,int fill,int save,PartText *out){
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=pw_load(assembly,panel,&record,geometry,1,out);
    int found=0;
    for(int i=0;!error&&i<record.count;i++){
        GroupedPressfitGroup *group=&record.groups[i];
        if((name&&_wcsicmp(group->name,name))||(!fill&&!group->pending[0]))continue;
        found++;
        if(fill&&!group->pending[0]){
            error=pw_fill_group(assembly,geometry,&record,i,out);
        }else{
            GroupedPressfitView before={0};
            error=grouped_pressfit_read_view(assembly,geometry,group,0,&before,out);
            if(!error)error=grouped_pressfit_apply(assembly,geometry,group,1,out);
            if(!error)error=grouped_pressfit_finish_group(assembly,&record,geometry,i,out);
        }
    }
    if(!error&&!found){
        part_text_add(out,L"目标板件没有对应的待续做记录或指定分组。\n");
        error=PRO_TK_E_NOT_FOUND;
    }
    if(!error)error=grouped_pressfit_save(assembly,save,out);
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}

static ProError pw_edit(ProMdl assembly,int panel,PressfitWorkspaceEdit mode,
    const wchar_t *name,const GroupPressfitGroup *other,int side,int save,PartText *out){
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=pw_load(assembly,panel,&record,geometry,1,out);
    const int index=error?-1:grouped_pressfit_find(&record,name);
    const int peer=!error&&other->name[0]?grouped_pressfit_find(&record,other->name):-1;
    if(!error&&(index<0||index==peer)){
        part_text_add(out,L"分组不存在，或源组和目标组相同。\n");
        error=PRO_TK_BAD_INPUTS;
    }
    GroupedPressfitGroup *group=index>=0?&record.groups[index]:NULL;
    for(int i=0;!error&&i<record.count;i++){
        const GroupedPressfitGroup *g=&record.groups[i];
        if(g->pending[0]){
            part_text_add(out,L"%ls还有未完成的装配，请先继续分组装配。\n",g->name);
            error=PRO_TK_INCOMPLETE;
        }else{
            GroupedPressfitView view={0};
            error=grouped_pressfit_read_view(assembly,geometry,g,1,&view,out);
        }
    }
    if(error)goto done;
    if((mode==PW_RENAME||mode==PW_SPLIT)&&peer>=0){
        error=PRO_TK_E_FOUND;
        goto done;
    }
    if(mode==PW_RENAME){
        wcscpy_s(group->name,_countof(group->name),other->name);
    }else if(mode==PW_MERGE){
        if(peer<0||_wcsicmp(group->spec,record.groups[peer].spec)||
           group->count+record.groups[peer].count>GROUP_PRESSFIT_MAX_HOLES){
            part_text_add(out,L"合组要求两个现有组的规格相同，且合计不超过 12 孔。\n");
            error=PRO_TK_BAD_INPUTS;
            goto done;
        }
        GroupedPressfitGroup *destination=&record.groups[peer];
        memcpy(destination->sites+destination->count,group->sites,(size_t)group->count*sizeof(group->sites[0]));
        destination->count+=group->count;
        memmove(record.groups+index,record.groups+index+1,(size_t)(--record.count-index)*sizeof(record.groups[0]));
    }else if(mode==PW_SPLIT||mode==PW_DROP){
        if(mode==PW_SPLIT&&record.count==GROUP_PRESSFIT_MAX_GROUPS){
            error=PRO_TK_OUT_OF_RANGE;
            goto done;
        }
        unsigned selected=0;
        for(int i=0;i<other->hole_count;i++){
            const int row=pw_site_number(geometry,group,other->holes[i]);
            if(row<0){error=PRO_TK_BAD_INPUTS;goto done;}
            selected|=1u<<row;
        }
        if(!selected||other->hole_count>=group->count){
            part_text_add(out,L"请指定部分已入组的孔号，原组至少保留一孔。\n");
            error=PRO_TK_BAD_INPUTS;
            goto done;
        }
        GroupedPressfitGroup *destination=&record.groups[record.count];
        if(mode==PW_SPLIT){
            memset(destination,0,sizeof(*destination));
            wcscpy_s(destination->name,_countof(destination->name),other->name);
            wcscpy_s(destination->spec,_countof(destination->spec),group->spec);
            record.count++;
        }
        int retained=0;
        for(int i=0;i<group->count;i++){
            if(selected&(1u<<i)){
                if(mode==PW_SPLIT)destination->sites[destination->count++]=group->sites[i];
            }else group->sites[retained++]=group->sites[i];
        }
        group->count=retained;
    }else{
        if(group->count+other->hole_count>GROUP_PRESSFIT_MAX_HOLES){
            error=PRO_TK_OUT_OF_RANGE;
            goto done;
        }
        if(side){
            error=group_pressfit_geometry_side(assembly,record.panel,side,geometry,out);
            if(error)goto done;
        }
        GroupedPressfitGroup addition={0};
        wcscpy_s(addition.name,_countof(addition.name),group->name);
        wcscpy_s(addition.pending,_countof(addition.pending),group->spec);
        for(int i=0;!error&&i<other->hole_count;i++){
            const int no=other->holes[i];
            if(no<1||no>geometry->count||geometry->holes[no-1].face_id<=0){error=PRO_TK_BAD_INPUTS;break;}
            for(int j=0;!error&&j<record.count;j++)
                if(pw_site_number(geometry,&record.groups[j],no)>=0)error=PRO_TK_E_FOUND;
            if(!error)addition.sites[addition.count++].hole=geometry->holes[no-1];
        }
        if(!error)error=grouped_pressfit_apply(assembly,geometry,&addition,1,out);
        if(!error)error=grouped_pressfit_apply(assembly,geometry,&addition,0,out);
        GroupedPressfitView checked={0};
        if(!error)error=grouped_pressfit_read_view(assembly,geometry,&addition,1,&checked,out);
        if(error)goto done;
        for(int i=0;i<addition.count;i++){
            addition.sites[i].id=checked.ids[i];
            group->sites[group->count++]=addition.sites[i];
        }
    }
    if(!grouped_pressfit_write(&record)){error=PRO_TK_CANT_WRITE;goto done;}
    part_text_add(out,L"板件 ID:%d 的分组已更新到 E 盘。%ls\n",record.panel,
        mode==PW_ADD?L"新增孔位已按组规格装配。":L"孔内现有零件及定位保持。");
    if(mode==PW_ADD)error=grouped_pressfit_save(assembly,save,out);
done:
    if(error&&!out->text[0])part_text_add(out,L"改组前的孔位、分组名称或规格核对未通过（Creo %d）。\n",error);
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}

static ProError pw_board_diff(ProMdl assembly,int panel,PartText *out){
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=pw_load(assembly,panel,&record,geometry,0,out);
    int changed=0;
    for(int i=0;!error&&i<record.count;i++){
        const GroupedPressfitGroup *group=&record.groups[i];
        for(int j=0;j<group->count;j++){
            const GroupPressfitHole *h=&group->sites[j].hole;
            const GroupPressfitHole *actual=NULL;
            for(int k=0;k<geometry->count;k++)if(geometry->holes[k].hole_id==h->hole_id)actual=&geometry->holes[k];
            const int identical=actual&&ad_near(h->center,actual->center,0.001)&&
                fabs(h->diameter-actual->diameter)<=0.001&&fabs(h->depth-actual->depth)<=0.001;
            if(identical)continue;
            changed++;
            if(!actual)part_text_add(out,L"%ls SURFACE:%d 原 (%.6g, %.6g, %.6g) Ø%.6g mm，当前该通孔参照未找到。\n",
                group->name,h->hole_id,h->center[0],h->center[1],h->center[2],h->diameter);
            else part_text_add(out,L"%ls SURFACE:%d 从 (%.6g, %.6g, %.6g) Ø%.6g，变为 (%.6g, %.6g, %.6g) Ø%.6g mm；孔深 %.6g → %.6g mm。\n",
                group->name,h->hole_id,h->center[0],h->center[1],h->center[2],h->diameter,
                actual->center[0],actual->center[1],actual->center[2],actual->diameter,h->depth,actual->depth);
        }
    }
    if(!error)part_text_add(out,L"板件 ID:%d 共核对 %d 组，发现 %d 处圆孔变化。此检查未改动模型。\n",record.panel,record.count,changed);
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}