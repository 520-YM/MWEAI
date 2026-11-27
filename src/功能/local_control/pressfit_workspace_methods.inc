typedef struct {
    wchar_t spec[64];
    int count,side;
    double dimensions[GROUP_PRESSFIT_MAX_HOLES][2];
} PressfitWorkspaceMethod;

static int pw_method_path(const wchar_t *name,wchar_t path[MAX_PATH]){
    for(const wchar_t *p=name;*p;p++)if(!grouped_pressfit_name_char(*p))return 0;
    return name[0]&&pressfit_record_path(path,L"压铆工艺",name,L".fit-method");
}

static int pw_method_read(const wchar_t *path,PressfitWorkspaceMethod *method){
    FILE *file=ax_learning_open(path);
    if(!file)return 0;
    wchar_t line[240];
    int end=0;
    PressfitSelector selector={0};
    int good=pressfit_record_line(file,line,_countof(line))&&!wcscmp(line,L"BWTL-FIT-METHOD-1")&&
       pressfit_record_line(file,line,_countof(line))&&!wcscpy_s(method->spec,_countof(method->spec),line)&&
       pressfit_selector(method->spec,&selector)&&pressfit_record_line(file,line,_countof(line))&&
       swscanf_s(line,L"%d %d%n",&method->count,&method->side,&end)==2&&!line[end]&&
       method->count>0&&method->count<=GROUP_PRESSFIT_MAX_HOLES&&
       (method->side==0||method->side==-1);
    for(int i=0;good&&i<method->count;i++)
        good=pressfit_record_line(file,line,_countof(line))&&
            swscanf_s(line,L"%lf %lf%n",&method->dimensions[i][0],&method->dimensions[i][1],&end)==2&&!line[end]&&
            isfinite(method->dimensions[i][0])&&isfinite(method->dimensions[i][1])&&
            method->dimensions[i][0]>0&&method->dimensions[i][1]>0;
    good=good&&!fgetws(line,_countof(line),file)&&!ferror(file);
    fclose(file);
    return good;
}

static ProError pw_store_method(ProMdl assembly,int panel,const wchar_t *group_name,const wchar_t *method_name,PartText *out){
    wchar_t path[MAX_PATH],temporary[MAX_PATH];
    if(!pw_method_path(method_name,path))return PRO_TK_CANT_WRITE;
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=pw_load(assembly,panel,&record,geometry,1,out);
    int index=error?-1:grouped_pressfit_find(&record,group_name);
    if(!error&&index<0)error=PRO_TK_E_NOT_FOUND;
    if(error)goto done;
    const GroupedPressfitGroup *group=&record.groups[index];
    GroupedPressfitView view={0};
    error=pw_audit_group(assembly,geometry,group,&view,out);
    int side=0;
    for(int i=0;!error&&i<group->count;i++){
        const GroupPressfitHole *saved=&group->sites[i].hole;
        const int number=pw_hole_number(geometry,saved);
        if(!number){error=PRO_TK_BAD_CONTEXT;break;}
        const int actual=grouped_pressfit_same_hole(saved,&geometry->holes[number-1])?0:-1;
        if(i&&actual!=side){
            part_text_add(out,L"该组含有两种安装侧。请先按安装侧拆组，再保存可复用的工艺。\n");
            error=PRO_TK_BAD_CONTEXT;
        }
        side=actual;
    }
    if(error)goto done;
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES){
        part_text_add(out,L"同名工艺已存在，请用新工艺名称保存。\n");
        error=PRO_TK_E_FOUND;
        goto done;
    }
    FILE *file=ax_study_atomic(path,temporary);
    if(!file){error=PRO_TK_CANT_WRITE;goto done;}
    fwprintf(file,L"BWTL-FIT-METHOD-1\n%ls\n%d %d\n",group->spec,group->count,side);
    for(int i=0;i<group->count;i++)
        fwprintf(file,L"%.17g %.17g\n",group->sites[i].hole.diameter,group->sites[i].hole.depth);
    const int good=!ferror(file);
    const int closed=!fclose(file);
    if(!good||!closed||!MoveFileExW(temporary,path,MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary);
        error=PRO_TK_CANT_WRITE;
        goto done;
    }
    part_text_add(out,L"已保存工艺 %ls。型号、安装侧、%d 个孔的底孔与板厚已写到 %ls。\n",method_name,group->count,path);
done:
    pressfit_checkpoint_close(&record.scope);
    free(geometry);
    return error;
}

static ProError pw_apply_method(ProMdl assembly,int panel,const wchar_t *method_name,
    const GroupPressfitGroup *destination,int save,PartText *out){
    PressfitWorkspaceMethod method={0};
    wchar_t path[MAX_PATH];
    if(!pw_method_path(method_name,path)||!pw_method_read(path,&method)){
        part_text_add(out,L"未找到可核验的压铆工艺「%ls」。\n",method_name);
        return PRO_TK_E_NOT_FOUND;
    }
    if(method.count!=destination->hole_count){
        part_text_add(out,L"该工艺登记 %d 孔，本次指定 %d 孔，请核对。\n",method.count,destination->hole_count);
        return PRO_TK_BAD_INPUTS;
    }
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=group_pressfit_geometry_side(assembly,panel,method.side,geometry,out);
    unsigned seen=0;
    for(int i=0;!error&&i<destination->hole_count;i++){
        const int number=destination->holes[i];
        if(number<1||number>geometry->count||geometry->holes[number-1].face_id<=0){
            error=PRO_TK_BAD_INPUTS;
            break;
        }
        const GroupPressfitHole *actual=&geometry->holes[number-1];
        int index=-1;
        for(int j=0;j<method.count;j++)if(!(seen&(1u<<j))&&
           fabs(actual->diameter-method.dimensions[j][0])<=0.001&&
           fabs(actual->depth-method.dimensions[j][1])<=0.001){index=j;break;}
        if(index<0){
            part_text_add(out,L"%d 号孔的实测孔径或板厚与工艺条件不符，尚未装配。\n",number);
            error=PRO_TK_BAD_CONTEXT;
        }else seen|=1u<<index;
    }
    if(!error){
        GroupPressfitPlan plan={.panel=geometry->panel_id,.side=method.side,.save=save,.group_count=1};
        plan.groups[0]=*destination;
        wcscpy_s(plan.groups[0].spec,_countof(plan.groups[0].spec),method.spec);
        error=grouped_pressfit_install(assembly,&plan,out);
    }
    free(geometry);
    return error;
}

/* Mirror the native measured mouths in assembly coordinates. All target sites
   must be unique, coaxial, on the reflected physical side and of matching size. */
static int pw_mirror_match(const GroupPressfitHole *source,const GroupPressfitHole *candidate,int axis,double at){
    if(candidate->face_id<=0||fabs(source->diameter-candidate->diameter)>0.001||
       fabs(source->depth-candidate->depth)>0.001)return 0;
    double point[3],normal[3],direction[3];
    memcpy(point,source->contact,sizeof(point));
    memcpy(normal,source->normal,sizeof(normal));
    memcpy(direction,source->axis,sizeof(direction));
    point[axis]=2*at-point[axis];
    normal[axis]=-normal[axis];
    direction[axis]=-direction[axis];
    return ad_near(point,candidate->contact,0.002)&&ad_dot(normal,candidate->normal)>=1-1e-7&&
        fabs(ad_dot(direction,candidate->axis))>=1-1e-7;
}

static ProError pw_mirror(ProMdl assembly,int source_panel,const wchar_t *source_name,int target_panel,
    const wchar_t *target_name,int target_side,int axis,double at,int save,PartText *out){
    if(!source_panel||!target_panel||source_panel==target_panel||axis<0||axis>2||!isfinite(at))return PRO_TK_BAD_INPUTS;
    GroupedPressfitRecord record={0};
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    ProError error=pw_load(assembly,source_panel,&record,geometry,1,out);
    const int index=error?-1:grouped_pressfit_find(&record,source_name);
    if(!error&&index<0)error=PRO_TK_E_NOT_FOUND;
    const GroupedPressfitGroup *source=index<0?NULL:&record.groups[index];
    GroupedPressfitView view={0};
    if(!error)error=pw_audit_group(assembly,geometry,source,&view,out);
    pressfit_checkpoint_close(&record.scope);
    if(!error)error=group_pressfit_geometry_side(assembly,target_panel,target_side,geometry,out);
    GroupPressfitPlan plan={.panel=target_panel,.side=target_side,.save=save,.group_count=1};
    GroupPressfitGroup *target=&plan.groups[0];
    if(!error){
        wcscpy_s(target->name,_countof(target->name),target_name);
        wcscpy_s(target->spec,_countof(target->spec),source->spec);
    }
    unsigned char used[GROUP_PRESSFIT_MAX_NUMBER+1]={0};
    for(int i=0;!error&&i<source->count;i++){
        int number=0;
        for(int j=0;j<geometry->count;j++){
            if(!pw_mirror_match(&source->sites[i].hole,&geometry->holes[j],axis,at))continue;
            if(number){error=PRO_TK_E_FOUND;break;}
            number=j+1;
        }
        if(!number||used[number])error=PRO_TK_BAD_CONTEXT;
        if(!error){
            used[number]=1;
            target->holes[target->hole_count++]=number;
        }else part_text_add(out,L"源组第 %d 个孔的镜像位置未在指定板件唯一匹配，未装入新件。\n",i+1);
    }
    if(!error)error=grouped_pressfit_install(assembly,&plan,out);
    free(geometry);
    return error;
}