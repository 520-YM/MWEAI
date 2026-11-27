#include "grouped_pressfit_geometry.inc"
#include "pressfit_workspace_selection.inc"

typedef struct {
    GroupPressfitHole hole;
    int id;
} GroupedPressfitSite;

typedef struct {
    wchar_t name[24],spec[64],pending[64];
    int count;
    GroupedPressfitSite sites[GROUP_PRESSFIT_MAX_HOLES];
} GroupedPressfitGroup;

typedef struct {
    PressfitCheckpoint scope;
    ProMdlName panel_name;
    int panel,count;
    GroupedPressfitGroup groups[GROUP_PRESSFIT_MAX_GROUPS];
} GroupedPressfitRecord;

typedef struct {
    ProName stem;
    ProMdl model;
    const PressfitAcceptance *catalog;
} GroupedPressfitSource;

typedef struct {
    int ids[GROUP_PRESSFIT_MAX_HOLES];
    ProMatrix poses[GROUP_PRESSFIT_MAX_HOLES];
} GroupedPressfitView;

/* Only the most recently listed positions are needed to catch stale numbering.
   The durable record uses physical references and survives a Creo restart. */
static struct {
    ProMdl assembly,model;
    ProMdlName name,panel_name;
    int panel,count,side;
    GroupPressfitHole *holes;
} grouped_pressfit_listing;

static ProError grouped_pressfit_scope(ProMdl assembly,GroupedPressfitRecord *record){
    ProError error=default_part_active(assembly,-1,NULL);
    if(!error)error=pressfit_record_scope(assembly,&record->scope);
    if(error)return error;
    if(!storage_directory_on_drive(record->scope.workdir,L'E'))return PRO_TK_BAD_CONTEXT;
    wchar_t *extension=wcsrchr(record->scope.file,L'.');
    if(!extension||wcscpy_s(extension,_countof(record->scope.file)-(extension-record->scope.file),L".hole-groups"))
        return PRO_TK_OUT_OF_RANGE;
    wchar_t guard[MAX_PATH];
    if(swprintf_s(guard,_countof(guard),L"%ls\\.%ls.bwtl-hole-groups.lock",
       record->scope.workdir,record->scope.target)<0)return PRO_TK_OUT_OF_RANGE;
    record->scope.lock=CreateFileW(guard,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    return record->scope.lock==INVALID_HANDLE_VALUE?PRO_TK_E_IN_USE:PRO_TK_NO_ERROR;
}

static int grouped_pressfit_write(const GroupedPressfitRecord *record){
    wchar_t temporary[MAX_PATH];
    FILE *file=ax_study_atomic(record->scope.file,temporary);
    if(!file)return 0;
    fwprintf(file,L"BWTL-HOLE-GROUPS-1\n%ls\n%ls\n%ls\n%d %d\n",
        record->scope.workdir,record->scope.target,record->panel_name,record->panel,record->count);
    for(int i=0;i<record->count;i++){
        const GroupedPressfitGroup *group=&record->groups[i];
        fwprintf(file,L"%ls\n%ls\n%ls\n%d\n",group->name,group->spec[0]?group->spec:L"-",
            group->pending[0]?group->pending:L"-",group->count);
        for(int j=0;j<group->count;j++){
            const GroupedPressfitSite *site=&group->sites[j];
            const GroupPressfitHole *h=&site->hole;
            fwprintf(file,L"%d %d %d %.17g %.17g",
                site->id,h->hole_id,h->face_id,h->diameter,h->depth);
            const double *vectors[]={h->center,h->contact,h->normal,h->axis};
            for(int k=0;k<_countof(vectors);k++)
                fwprintf(file,L" %.17g %.17g %.17g",vectors[k][0],vectors[k][1],vectors[k][2]);
            fwprintf(file,L"\n");
        }
    }
    return ax_study_commit(file,temporary,record->scope.file);
}

static int grouped_pressfit_read_site(const wchar_t *line,GroupedPressfitSite *site){
    GroupPressfitHole *h=&site->hole;
    int end=0;
    const int n=swscanf_s(line,L"%d %d %d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf%n",
        &site->id,&h->hole_id,&h->face_id,&h->diameter,&h->depth,
        &h->center[0],&h->center[1],&h->center[2],&h->contact[0],&h->contact[1],&h->contact[2],
        &h->normal[0],&h->normal[1],&h->normal[2],&h->axis[0],&h->axis[1],&h->axis[2],&end);
    if(n!=17||line[end]||site->id<0||h->hole_id<=0||h->face_id<=0||
       !isfinite(h->diameter)||h->diameter<=0||!isfinite(h->depth)||h->depth<=0)return 0;
    const double *vectors[]={h->center,h->contact,h->normal,h->axis};
    for(int k=0;k<_countof(vectors);k++)for(int c=0;c<3;c++)if(!isfinite(vectors[k][c]))return 0;
    return fabs(ad_dot(h->normal,h->normal)-1)<1e-5&&fabs(ad_dot(h->axis,h->axis)-1)<1e-5;
}

static int grouped_pressfit_read_group(FILE *file,GroupedPressfitGroup *group){
    wchar_t line[600];
    int end=0;
    if(!pressfit_record_line(file,line,_countof(line))||wcscpy_s(group->name,_countof(group->name),line)||
       !pressfit_record_line(file,line,_countof(line))||wcscpy_s(group->spec,_countof(group->spec),line)||
       !pressfit_record_line(file,line,_countof(line))||wcscpy_s(group->pending,_countof(group->pending),line)||
       !pressfit_record_line(file,line,_countof(line))||
       swscanf_s(line,L"%d%n",&group->count,&end)!=1||line[end]||
       group->count<1||group->count>GROUP_PRESSFIT_MAX_HOLES)return 0;
    if(!wcscmp(group->spec,L"-"))group->spec[0]=0;
    if(!wcscmp(group->pending,L"-"))group->pending[0]=0;
    PressfitSelector spec={0},pending={0};
    if((!group->spec[0]&&!group->pending[0])||
       (group->spec[0]&&!pressfit_selector(group->spec,&spec))||
       (group->pending[0]&&!pressfit_selector(group->pending,&pending)))return 0;
    for(int j=0;j<group->count;j++){
        if(!pressfit_record_line(file,line,_countof(line))||
           !grouped_pressfit_read_site(line,&group->sites[j])||
           (group->spec[0]&&!group->sites[j].id)||(!group->spec[0]&&group->sites[j].id))return 0;
    }
    return 1;
}

static ProError grouped_pressfit_read(GroupedPressfitRecord *record){
    const DWORD attributes=GetFileAttributesW(record->scope.file);
    if(attributes==INVALID_FILE_ATTRIBUTES){
        const DWORD error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND?PRO_TK_E_NOT_FOUND:PRO_TK_CANT_OPEN;
    }
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return PRO_TK_CANT_OPEN;
    FILE *file=ax_learning_open(record->scope.file);
    if(!file)return PRO_TK_CANT_OPEN;
    wchar_t line[600];
    int end=0;
    int good=pressfit_record_line(file,line,_countof(line))&&!wcscmp(line,L"BWTL-HOLE-GROUPS-1")&&
        pressfit_record_line(file,line,_countof(line))&&!_wcsicmp(line,record->scope.workdir)&&
        pressfit_record_line(file,line,_countof(line))&&!_wcsicmp(line,record->scope.target)&&
        pressfit_record_line(file,record->panel_name,_countof(record->panel_name))&&
        pressfit_record_line(file,line,_countof(line))&&
        swscanf_s(line,L"%d %d%n",&record->panel,&record->count,&end)==2&&!line[end]&&
        record->panel>0&&record->count>0&&record->count<=GROUP_PRESSFIT_MAX_GROUPS;
    for(int i=0;good&&i<record->count;i++)good=grouped_pressfit_read_group(file,&record->groups[i]);
    if(good)good=!fgetws(line,_countof(line),file)&&!ferror(file);
    fclose(file);
    return good?PRO_TK_NO_ERROR:PRO_TK_CANT_OPEN;
}

/* Earlier one-sheet records remain valid. Additional top-level occurrences
   have separate state, while a single per-assembly guard serializes updates. */
static ProError grouped_pressfit_open(ProMdl assembly,int panel,GroupedPressfitRecord *record){
    ProError error=grouped_pressfit_scope(assembly,record);
    if(error)return error;
    error=grouped_pressfit_read(record);
    if(!panel&&error==PRO_TK_E_NOT_FOUND){
        GroupPressfitGeometry target={0};
        if(!group_pressfit_panel(assembly,0,&target,NULL))panel=target.panel_id;
    }
    if(!panel||(!error&&record->panel==panel))return error;
    if(error!=PRO_TK_E_NOT_FOUND&&error)return error;
    wchar_t *ending=wcsrchr(record->scope.file,L'.');
    if(!ending||swprintf_s(ending,_countof(record->scope.file)-(ending-record->scope.file),
       L"-P%d.hole-groups",panel)<0)return PRO_TK_OUT_OF_RANGE;
    record->count=0;
    record->panel=panel;
    record->panel_name[0]=0;
    memset(record->groups,0,sizeof(record->groups));
    error=grouped_pressfit_read(record);
    return !error&&record->panel!=panel?PRO_TK_BAD_CONTEXT:error;
}

static int grouped_pressfit_same_hole(const GroupPressfitHole *a,const GroupPressfitHole *b){
    return a->hole_id==b->hole_id&&a->face_id==b->face_id&&
        fabs(a->diameter-b->diameter)<=0.001&&fabs(a->depth-b->depth)<=0.001&&
        ad_near(a->center,b->center,0.001)&&
        ad_near(a->contact,b->contact,0.001)&&
        ad_dot(a->normal,b->normal)>=1-1e-7&&
        fabs(ad_dot(a->axis,b->axis))>=1-1e-7;
}

static int grouped_pressfit_find(const GroupedPressfitRecord *record,const wchar_t *name){
    for(int i=0;i<record->count;i++)if(!_wcsicmp(record->groups[i].name,name))return i;
    return -1;
}

static ProError grouped_pressfit_bind(ProMdl assembly,GroupedPressfitRecord *record,
    GroupPressfitGeometry *geometry,PartText *out){
    ProError error=group_pressfit_geometry(assembly,record->panel,geometry,out);
    if(error)return error;
    ProMdlName name;
    error=ProMdlMdlnameGet(geometry->panel.model,name);
    if(error)return error;
    if(!record->panel_name[0]){
        record->panel=geometry->panel_id;
        wcscpy_s(record->panel_name,_countof(record->panel_name),name);
        return PRO_TK_NO_ERROR;
    }
    if(_wcsicmp(name,record->panel_name))return PRO_TK_BAD_CONTEXT;
    GroupPressfitGeometry *reverse=NULL;
    for(int i=0;i<record->count;i++){
        const GroupedPressfitGroup *group=&record->groups[i];
        for(int j=0;j<group->count;j++){
            const GroupPressfitHole *stored=&group->sites[j].hole;
            int match=0;
            for(int k=0;k<geometry->count;k++)
                if(grouped_pressfit_same_hole(stored,&geometry->holes[k]))match++;
            if(!match){
                if(!reverse){
                    reverse=calloc(1,sizeof(*reverse));
                    if(reverse&&group_pressfit_geometry_side(assembly,record->panel,-1,reverse,NULL))reverse->count=0;
                }
                for(int k=0;reverse&&k<reverse->count;k++)
                    if(grouped_pressfit_same_hole(stored,&reverse->holes[k]))match++;
            }
            if(match!=1){
                part_text_add(out,L"%ls的孔壁 SURFACE:%d 与记录不符。可用「检查改板影响」查看具体变化。\n",group->name,stored->hole_id);
                free(reverse);
                return PRO_TK_BAD_CONTEXT;
            }
        }
    }
    free(reverse);
    return PRO_TK_NO_ERROR;
}

static ProError grouped_pressfit_source(const wchar_t *spec,GroupedPressfitSource *source,PartText *out){
    wchar_t library[MAX_PATH]=L"",detail[1200]=L"";
    PartText report={detail,_countof(detail),0};
    ProError error=pressfit_read_exact(spec,&source->model,library,&report);
    if(!error){
        source->catalog=pressfit_acceptance_lookup(source->model,library);
        error=pressfit_project_stem(source->model,library,source->stem);
    }
    if(error&&detail[0])part_text_add(out,L"%ls\n",detail);
    return error;
}

static ProError grouped_pressfit_fitting(const GroupedPressfitSource *source,double mm,double diameter,
    BsoFitting *fitting,PartText *out){
    const PressfitAcceptance *row=source->catalog;
    return fastener_pressfit_fitting_for_hole(source->model,mm,row?row->shaft:0,row?row->seat:0,diameter,fitting,out);
}

static int grouped_pressfit_named_copy(ProMdl model,const GroupedPressfitSource *source){
    if(model==source->model)return 1;
    ProMdlName name;
    if(ProMdlMdlnameGet(model,name))return 0;
    const size_t length=wcslen(source->stem);
    if(_wcsnicmp(name,source->stem,length))return 0;
    return !name[length]||(name[length]==L'_'&&pressfit_bso_size(name+length+1));
}

static ProError grouped_pressfit_read_piece(ProMdl assembly,const GroupedPressfitSource *source,
    const PressfitOccupancy *row,const BsoSite *site,double mm,ProMdl *checked,ProMatrix pose,PartText *out){
    ProError error=PRO_TK_NO_ERROR;
    if(!row->paired||row->depth!=1||row->id<=0)return PRO_TK_BAD_CONTEXT;
    if(*checked!=row->model){
        error=pressfit_verify_project(source->model,row->model,out);
        if(!error)*checked=row->model;
    }
    BsoFitting loaded={0};
    if(!error)error=grouped_pressfit_fitting(source,mm,site->geometry.cylinder.radius*2*mm,&loaded,out);
    if(!error)error=fastener_pressfit_fitting(row->model,mm,loaded.shaft,loaded.seat,&loaded,out);
    PressfitSiteReceipt receipt={0};
    if(!error)error=fastener_pressfit_recheck_added(assembly,row->model,&loaded,site,row->id,mm,&receipt);
    ProAsmcomp feature=row->feature;
    if(!error)error=ax_actual_pose(assembly,&feature,pose);
    return error;
}

/* A pending update can be retried only with the same requested specification.
   A partially installed group may contain empty positions and exact new pieces.
   A partially revised group may contain its original verified IDs and exact new pieces. */
static ProError grouped_pressfit_read_view(ProMdl assembly,const GroupPressfitGeometry *geometry,
    const GroupedPressfitGroup *group,int complete,GroupedPressfitView *view,PartText *out){
    GroupedPressfitSource old={0},next={0};
    ProError error=group->spec[0]?grouped_pressfit_source(group->spec,&old,out):PRO_TK_NO_ERROR;
    if(!error&&group->pending[0])error=grouped_pressfit_source(group->pending,&next,out);
    const GroupedPressfitSource *probe=next.model?&next:&old;
    ProMdl checked_old=NULL,checked_new=NULL;
    unsigned visited=0;
    for(int i=0;!error&&i<group->count;i++){
        if(visited&(1u<<i))continue;
        BsoSite sites[GROUP_PRESSFIT_MAX_HOLES]={0};
        int slots[GROUP_PRESSFIT_MAX_HOLES],count=0;
        const double diameter=group->sites[i].hole.diameter;
        for(int j=i;!error&&j<group->count;j++){
            if(fabs(group->sites[j].hole.diameter-diameter)>0.001)continue;
            error=group_pressfit_bso_site(geometry,&group->sites[j].hole,&sites[count]);
            slots[count++]=j;
            visited|=1u<<j;
        }
        BsoFitting fitting={0};
        if(!error)error=grouped_pressfit_fitting(probe,geometry->mm,diameter,&fitting,out);
        PressfitOccupancy occupied[GROUP_PRESSFIT_MAX_HOLES]={0};
        if(!error)error=fastener_pressfit_scan_occupancy(assembly,probe->model,&fitting,sites,count,geometry->mm,occupied);
        for(int k=0;!error&&k<count;k++){
            const int j=slots[k];
            const PressfitOccupancy *row=&occupied[k];
            if(!row->count&&(complete==2||(!complete&&!old.model)))continue;
            if(row->count!=1){error=PRO_TK_BAD_CONTEXT;break;}
            const int is_new=next.model&&grouped_pressfit_named_copy(row->model,&next);
            if(!is_new&&((complete==1&&next.model)||!old.model||(complete!=2&&row->id!=group->sites[j].id)||
                !grouped_pressfit_named_copy(row->model,&old))){
                error=PRO_TK_BAD_CONTEXT;
                break;
            }
            error=grouped_pressfit_read_piece(assembly,is_new?&next:&old,row,&sites[k],geometry->mm,
                is_new?&checked_new:&checked_old,view->poses[j],out);
            if(!error)view->ids[j]=row->id;
        }
    }
    if(error)part_text_add(out,L"%ls的孔位、规格或现有组件约束未能核对（Creo %d）。\n",group->name,error);
    return error;
}

static ProError grouped_pressfit_apply(ProMdl assembly,const GroupPressfitGeometry *geometry,
    const GroupedPressfitGroup *group,int preview,PartText *out){
    unsigned visited=0;
    ProError error=PRO_TK_NO_ERROR;
    for(int i=0;!error&&i<group->count;i++){
        if(visited&(1u<<i))continue;
        const GroupPressfitHole *site=&group->sites[i].hole;
        FastenerPressfitRequest request={.action=PRESSFIT_PLACE,.short_form=1,.check_clearance=1,
            .inspect=preview,.replace=group->spec[0]!=0,.face_id=site->face_id};
        wcscpy_s(request.model,_countof(request.model),group->pending);
        swprintf_s(request.panel_path,_countof(request.panel_path),L"ID:%d",geometry->panel_id);
        for(int j=i;j<group->count;j++){
            const GroupPressfitHole *item=&group->sites[j].hole;
            if(item->face_id!=site->face_id||fabs(item->diameter-site->diameter)>0.001)continue;
            request.mapped_holes[request.mapped_count++]=item->hole_id;
            visited|=1u<<j;
        }
        request.expected=request.mapped_count;
        wchar_t detail[8500]=L"";
        PartText report={detail,_countof(detail),0};
        error=local_control_pressfit_apply(assembly,&request,group->name,&report);
        if(error&&detail[0])part_text_add(out,L"%ls\n",detail);
    }
    return error;
}

static ProError grouped_pressfit_finish_group(ProMdl assembly,GroupedPressfitRecord *record,
    const GroupPressfitGeometry *geometry,int index,PartText *out){
    GroupedPressfitGroup *group=&record->groups[index];
    ProError error=grouped_pressfit_apply(assembly,geometry,group,0,out);
    GroupedPressfitView checked={0};
    if(!error)error=grouped_pressfit_read_view(assembly,geometry,group,1,&checked,out);
    if(!error){
        for(int i=0;i<group->count;i++)group->sites[i].id=checked.ids[i];
        wcscpy_s(group->spec,_countof(group->spec),group->pending);
        group->pending[0]=0;
        if(!grouped_pressfit_write(record))error=PRO_TK_CANT_WRITE;
    }
    if(error){
        part_text_add(out,L"%ls尚未完成；已装的孔保留。同一条分组指令可按原规格和孔位核对后续做。\n",group->name);
    }else{
        part_text_add(out,L"%ls已核对 %d 处 %ls，孔轴、贴合面和两条原生约束均通过。组件 ID",group->name,group->count,group->spec);
        for(int i=0;i<group->count;i++)part_text_add(out,L"%ls%d",i?L"、":L" ",group->sites[i].id);
        part_text_add(out,L"。\n");
    }
    return error;
}

static ProError grouped_pressfit_save(ProMdl assembly,int save,PartText *out){
    if(!save){part_text_add(out,L"分组记录已写入 E 盘，装配改动保留在当前 Creo 会话。\n");return PRO_TK_NO_ERROR;}
    wchar_t reason[2048]=L"";
    const ProError error=bwtl_save_guarded(assembly,reason,_countof(reason));
    if(reason[0])part_text_add(out,L"%ls\n",reason);
    return error;
}

static ProError grouped_pressfit_list_for(ProMdl assembly,int panel,int side,PartText *out){
    ProError error=default_part_active(assembly,-1,NULL);
    if(error)return error;
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    error=group_pressfit_geometry_side(assembly,panel,side,geometry,out);
    ProMdlName assembly_name,panel_name;
    if(!error)error=ProMdlMdlnameGet(assembly,assembly_name);
    if(!error)error=ProMdlMdlnameGet(geometry->panel.model,panel_name);
    if(!error){
        const int count=geometry->count;
        GroupPressfitHole *copy=malloc((size_t)count*sizeof(*copy));
        if(!copy){error=PRO_TK_GENERAL_ERROR;goto done;}
        memcpy(copy,geometry->holes,(size_t)count*sizeof(*copy));
        free(grouped_pressfit_listing.holes);
        grouped_pressfit_listing.holes=copy;
        grouped_pressfit_listing.assembly=assembly;
        grouped_pressfit_listing.model=geometry->panel.model;
        grouped_pressfit_listing.panel=geometry->panel_id;
        grouped_pressfit_listing.count=count;
        grouped_pressfit_listing.side=side;
        wcscpy_s(grouped_pressfit_listing.name,_countof(grouped_pressfit_listing.name),assembly_name);
        wcscpy_s(grouped_pressfit_listing.panel_name,_countof(grouped_pressfit_listing.panel_name),panel_name);
        part_text_add(out,L"安装件 %ls（组件 ID:%d）的 %d 个通孔。孔号按板件坐标 Y、X、Z 顺序排列，单位 mm。\n",
            panel_name,geometry->panel_id,count);
        for(int i=0;i<count;i++){
            const GroupPressfitHole *h=&geometry->holes[i];
            if(h->face_id<=0){part_text_add(out,L"%d号孔在指定安装面上不可用。\n",i+1);continue;}
            part_text_add(out,L"%d号孔 (%.6g, %.6g, %.6g) Ø%.6g，孔壁 SURFACE:%d / 安装面 SURFACE:%d。\n",
                i+1,h->center[0],h->center[1],h->center[2],h->diameter,h->hole_id,h->face_id);
        }
        part_text_add(out,L"可以说「按孔位分组装配：左组 1、3号孔 装 BSO M3×6；右组 2号孔 装 BSO M3×8」。\n");
    }
done:
    free(geometry);
    return error;
}

static ProError grouped_pressfit_list(ProMdl assembly,PartText *out){
    return grouped_pressfit_list_for(assembly,0,0,out);
}

static ProError grouped_pressfit_register(GroupedPressfitRecord *record,const GroupPressfitGeometry *geometry,
    const GroupPressfitGroup *request,int *index,int side,PartText *out){
    *index=grouped_pressfit_find(record,request->name);
    GroupedPressfitGroup *existing=*index<0?NULL:&record->groups[*index];
    if(existing&&(existing->count!=request->hole_count||
       _wcsicmp(existing->pending[0]?existing->pending:existing->spec,request->spec))){
        part_text_add(out,L"%ls已有不同的分组记录。修改规格时请按组名发改规格指令。\n",request->name);
        return PRO_TK_BAD_CONTEXT;
    }
    const int listed=grouped_pressfit_listing.holes&&grouped_pressfit_listing.assembly==record->scope.assembly&&
        grouped_pressfit_listing.panel==record->panel&&!_wcsicmp(grouped_pressfit_listing.name,record->scope.target);
    if(!existing&&record->count>=GROUP_PRESSFIT_MAX_GROUPS)return PRO_TK_OUT_OF_RANGE;
    GroupedPressfitGroup proposed={.count=request->hole_count};
    wcscpy_s(proposed.name,_countof(proposed.name),request->name);
    wcscpy_s(proposed.pending,_countof(proposed.pending),request->spec);
    for(int j=0;j<request->hole_count;j++){
        const int number=request->holes[j];
        if(number<1||number>geometry->count){
            part_text_add(out,L"%ls中 %d 号孔不在当前可装配清单，请重新查看孔位。\n",request->name,number);
            return PRO_TK_BAD_INPUTS;
        }
        const GroupPressfitHole *hole=&geometry->holes[number-1];
        if(hole->face_id<=0){
            part_text_add(out,L"%d 号孔无法核对指定安装侧。",number);
            return PRO_TK_BAD_CONTEXT;
        }
        if(listed&&(grouped_pressfit_listing.model!=geometry->panel.model||
           grouped_pressfit_listing.panel!=geometry->panel_id||
           _wcsicmp(grouped_pressfit_listing.panel_name,record->panel_name)||
           number>grouped_pressfit_listing.count||
           (side==grouped_pressfit_listing.side&&!grouped_pressfit_same_hole(hole,&grouped_pressfit_listing.holes[number-1]))||
           !ad_near(hole->center,grouped_pressfit_listing.holes[number-1].center,0.001)||
           fabs(hole->diameter-grouped_pressfit_listing.holes[number-1].diameter)>0.001||
           fabs(hole->depth-grouped_pressfit_listing.holes[number-1].depth)>0.001)){
            part_text_add(out,L"%d 号孔与上次显示的位置不同。请重新查看可装配孔位。\n",number);
            return PRO_TK_BAD_CONTEXT;
        }
        for(int k=0;k<record->count;k++){
            const GroupedPressfitGroup *other=&record->groups[k];
            for(int z=0;z<other->count;z++)if(k!=*index&&ad_near(other->sites[z].hole.center,hole->center,0.001)&&
                fabs(other->sites[z].hole.diameter-hole->diameter)<=0.001){
                part_text_add(out,L"%d 号孔已属于%ls。每个孔只能加入一组。\n",number,other->name);
                return PRO_TK_BAD_CONTEXT;
            }
        }
        if(existing){
            int same=0;
            for(int k=0;k<existing->count;k++)
                if(grouped_pressfit_same_hole(hole,&existing->sites[k].hole))same=1;
            if(!same)return PRO_TK_BAD_CONTEXT;
        }
        proposed.sites[j].hole=*hole;
    }
    if(!existing){
        *index=record->count++;
        record->groups[*index]=proposed;
    }
    return PRO_TK_NO_ERROR;
}

static ProError grouped_pressfit_install(ProMdl assembly,const GroupPressfitPlan *plan,PartText *out){
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=grouped_pressfit_open(assembly,plan->panel,&record);
    if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    if(!error)error=grouped_pressfit_bind(assembly,&record,geometry,out);
    if(!error&&plan->side)error=group_pressfit_geometry_side(assembly,record.panel,plan->side,geometry,out);
    int indexes[GROUP_PRESSFIT_MAX_GROUPS]={0};
    for(int i=0;!error&&i<plan->group_count;i++)
        error=grouped_pressfit_register(&record,geometry,&plan->groups[i],&indexes[i],plan->side,out);
    for(int i=0;!error&&i<record.count;i++){
        const GroupedPressfitGroup *group=&record.groups[i];
        if(!group->pending[0])continue;
        int requested=0;
        for(int j=0;j<plan->group_count;j++)if(indexes[j]==i)requested=1;
        if(!requested||group->spec[0]){
            part_text_add(out,L"%ls仍有未验收的改动，先用原指令续做该组。\n",group->name);
            error=PRO_TK_BAD_CONTEXT;
        }
    }
    for(int i=0;!error&&i<record.count;i++){
        GroupedPressfitView view={0};
        error=grouped_pressfit_read_view(assembly,geometry,&record.groups[i],0,&view,out);
        if(!error&&record.groups[i].pending[0])
            error=grouped_pressfit_apply(assembly,geometry,&record.groups[i],1,out);
    }
    if(!error&&plan->preview){
        part_text_add(out,L"板件 ID:%d 的 %d 个分组已通过库件、孔位、底孔、安装面和边界预检。\n",record.panel,plan->group_count);
        goto done;
    }
    if(!error&&!grouped_pressfit_write(&record))error=PRO_TK_CANT_WRITE;
    for(int i=0;!error&&i<plan->group_count;i++){
        const int index=indexes[i];
        if(record.groups[index].pending[0])error=grouped_pressfit_finish_group(assembly,&record,geometry,index,out);
        else part_text_add(out,L"%ls已按登记的规格和孔位装好，本轮复核一致。\n",record.groups[index].name);
    }
    if(!error)error=grouped_pressfit_save(assembly,plan->save,out);
done:
    if(error&&!out->text[0])part_text_add(out,L"分组装配在核对当前装配、E 盘记录或孔位时停止（Creo %d）。\n",error);
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}

static ProError grouped_pressfit_revise(ProMdl assembly,const GroupPressfitRevision *revision,PartText *out){
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=grouped_pressfit_open(assembly,revision->panel,&record);
    const int index=error?-1:grouped_pressfit_find(&record,revision->name);
    if(index<0){
        if(!error)error=PRO_TK_E_NOT_FOUND;
        part_text_add(out,L"当前装配和 E 盘项目目录未找到可核对的「%ls」分组（Creo %d）。\n",revision->name,error);
        goto done;
    }
    error=grouped_pressfit_bind(assembly,&record,geometry,out);
    if(error)goto done;
    GroupedPressfitGroup *group=&record.groups[index];
    wchar_t next[64]=L"";
    int former_length=0;
    if(revision->length&&group->spec[0]){
        if(!panel_multi_length_selector(group->spec,revision->length,next,&former_length)){
            part_text_add(out,L"%ls的登记规格无法只改长度，请给出完整的新型号。\n",group->name);
            error=PRO_TK_BAD_INPUTS;
            goto done;
        }
    }else wcscpy_s(next,_countof(next),revision->spec);
    if(!next[0]||!group->spec[0]||(group->pending[0]&&_wcsicmp(group->pending,next))){
        part_text_add(out,L"%ls还有未验收的操作，请用原指令续做。\n",group->name);
        error=PRO_TK_BAD_CONTEXT;
        goto done;
    }
    GroupedPressfitView before[GROUP_PRESSFIT_MAX_GROUPS]={0};
    for(int i=0;!error&&i<record.count;i++){
        if(i!=index&&record.groups[i].pending[0]){
            part_text_add(out,L"%ls还有未验收的操作，请先续做该组。\n",record.groups[i].name);
            error=PRO_TK_BAD_CONTEXT;
        }
        if(!error)error=grouped_pressfit_read_view(assembly,geometry,&record.groups[i],0,&before[i],out);
    }
    if(error)goto done;
    if(!_wcsicmp(next,group->spec)||(!group->pending[0]&&revision->length&&former_length==revision->length)){
        part_text_add(out,L"%ls已是所需规格，原组件已复核。\n",group->name);
        error=grouped_pressfit_save(assembly,revision->save,out);
        goto done;
    }
    wcscpy_s(group->pending,_countof(group->pending),next);
    error=grouped_pressfit_apply(assembly,geometry,group,1,out);
    if(error)goto done;
    if(!grouped_pressfit_write(&record)){error=PRO_TK_CANT_WRITE;goto done;}
    part_text_add(out,L"【回溯风险重点提醒】连续重定义旧组件曾触发 Creo 回溯。本次逐孔核对新件并备份，再移除无下游依赖的旧实例。\n");
    error=grouped_pressfit_finish_group(assembly,&record,geometry,index,out);
    for(int i=0;!error&&i<record.count;i++){
        if(i==index)continue;
        GroupedPressfitView after={0};
        const GroupedPressfitGroup *other=&record.groups[i];
        error=grouped_pressfit_read_view(assembly,geometry,other,1,&after,out);
        for(int j=0;!error&&j<other->count;j++){
            if(before[i].ids[j]!=after.ids[j]||!ax_same_pose(before[i].poses[j],after.poses[j],geometry->mm)){
                part_text_add(out,L"%ls的组件变动与本轮范围不符，装配尚未保存。\n",other->name);
                error=PRO_TK_BAD_CONTEXT;
            }
        }
        if(!error)part_text_add(out,L"%ls的 %d 个组件 ID、位置和原生约束均已核对，保持原样。\n",other->name,other->count);
    }
    if(!error)error=grouped_pressfit_save(assembly,revision->save,out);
done:
    if(error&&!out->text[0])part_text_add(out,L"更改分组时未通过当前装配或 E 盘记录核对（Creo %d）。\n",error);
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}