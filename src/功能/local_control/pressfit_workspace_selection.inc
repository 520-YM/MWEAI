/* A read-only bridge from Creo's selection buffer and BWTL screenshot
   annotations to the project group's native occurrence and hole numbers. */
typedef struct {
    int panel_id,side,count,numbers[12];
} PressfitWorkspaceSelection;

static struct {
    ProMdl assembly,model;
    int window;
    ProPath directory;
    PressfitWorkspaceSelection selection;
    GroupPressfitHole holes[12];
} pressfit_workspace_markup;

static ProError pressfit_workspace_panels(ProMdl assembly,PartText *out){
    ProMdlType type;
    ProError error=ProMdlTypeGet(assembly,&type);
    if(error||type!=PRO_MDL_ASSEMBLY)return error?error:PRO_TK_BAD_CONTEXT;
    AxScan *scan=NULL;
    error=ax_scan(assembly,0,&scan);
    if(!error&&scan->limited)error=PRO_TK_INCOMPLETE;
    int count=0;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *item=&scan->occurrences[i];
        if(item->depth!=1||item->state!=PRO_FEAT_ACTIVE||item->type!=PRO_MDL_PART||!item->model)continue;
        ProMdlsubtype subtype;
        error=ProMdlSubtypeGet(item->model,&subtype);
        if(error||subtype!=PROMDLSTYPE_PART_SHEETMETAL)continue;
        ProIdTable ids={item->ids[0]};
        ProAsmcomppath path;
        ProMatrix pose;
        double mm;
        error=qa_mm_per_unit(assembly,&mm);
        if(!error)error=ProAsmcomppathInit((ProSolid)assembly,ids,1,&path);
        if(!error)error=ProAsmcomppathTrfGet(&path,PRO_B_TRUE,pose);
        if(error)break;
        part_text_add(out,L"钣金实例 ID:%d，%ls，模型原点 (%.6g, %.6g, %.6g) mm。\n",
            item->ids[0],item->name,pose[3][0]*mm,pose[3][1]*mm,pose[3][2]*mm);
        count++;
    }
    free(scan);
    if(!error&&!count)error=PRO_TK_E_NOT_FOUND;
    if(!error)part_text_add(out,L"用组件 ID 指定安装板可区分重复装配的同名钣金件。\n");
    return error;
}

static ProError group_pressfit_holediam_select(const GroupPressfitGeometry *geometry,double diameter,
    const int *except,int except_count,int numbers[12],int *count,PartText *out){
    if(!isfinite(diameter)||diameter<=0||except_count<0||except_count>12)return PRO_TK_BAD_INPUTS;
    *count=0;
    for(int i=0;i<except_count;i++){
        const int number=except[i];
        if(number<1||number>geometry->count||!geometry->holes[number-1].face_id||
           fabs(geometry->holes[number-1].diameter-diameter)>0.001){
            part_text_add(out,L"排除的 %d 号孔与当前安装侧或指定孔径不符。\n",number);
            return PRO_TK_BAD_INPUTS;
        }
        for(int j=0;j<i;j++)if(except[j]==number)return PRO_TK_BAD_INPUTS;
    }
    for(int i=0;i<geometry->count;i++){
        const GroupPressfitHole *hole=&geometry->holes[i];
        if(!hole->face_id||fabs(hole->diameter-diameter)>0.001)continue;
        int skip=0;
        for(int j=0;j<except_count;j++)if(except[j]==i+1)skip=1;
        if(skip)continue;
        if(*count==12){
            part_text_add(out,L"符合条件的孔超过每组 12 孔。缩小范围或分成多组。\n");
            return PRO_TK_OUT_OF_RANGE;
        }
        numbers[(*count)++]=i+1;
    }
    if(!*count)part_text_add(out,L"当前安装板这一侧没有符合条件的 Ø%.6g 通孔。\n",diameter);
    return *count?PRO_TK_NO_ERROR:PRO_TK_E_NOT_FOUND;
}

static int pressfit_workspace_cylinder_number(const GroupPressfitGeometry *geometry,int surface){
    for(int i=0;i<geometry->count;i++)
        if(geometry->holes[i].face_id&&geometry->holes[i].hole_id==surface)return i+1;
    int found=0;
    for(int i=0;i<geometry->count;i++){
        const GroupPressfitHole *hole=&geometry->holes[i];
        if(!hole->face_id)continue;
        BsoSite site={0};
        if(!group_pressfit_bso_site(geometry,hole,&site)&&
           fastener_pressfit_same_bore(&site,surface,geometry->mm)){
            if(found)return 0;
            found=i+1;
        }
    }
    return found;
}

static ProError pressfit_workspace_add_number(PressfitWorkspaceSelection *result,int number){
    if(number<1)return PRO_TK_E_NOT_FOUND;
    for(int i=0;i<result->count;i++)if(result->numbers[i]==number)return PRO_TK_NO_ERROR;
    if(result->count==_countof(result->numbers))return PRO_TK_OUT_OF_RANGE;
    result->numbers[result->count++]=number;
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_workspace_selection_path(ProMdl assembly,ProSelection item,ProAsmcomppath *path){
    ProError error=ProSelectionAsmcomppathGet(item,path);
    if(!error&&((ProMdl)path->owner!=assembly||path->table_num!=1||path->comp_id_table[0]<=0))
        error=PRO_TK_BAD_CONTEXT;
    return error;
}

static ProError pressfit_workspace_feature_pick(const GroupPressfitGeometry *geometry,int feature,
    PressfitWorkspaceSelection *selection){
    int found=0;
    for(int i=0;i<geometry->count;i++){
        const GroupPressfitHole *hole=&geometry->holes[i];
        if(!hole->face_id)continue;
        ProGeomitem item;
        ProFeature owner;
        ProError error=ProModelitemInit(geometry->panel.model,hole->hole_id,PRO_SURFACE,&item);
        if(!error)error=ProGeomitemFeatureGet(&item,&owner);
        if(error)return error;
        if(owner.id==feature){
            found=1;
            if((error=pressfit_workspace_add_number(selection,i+1)))return error;
        }
    }
    return found?PRO_TK_NO_ERROR:PRO_TK_E_NOT_FOUND;
}

static ProError pressfit_workspace_clicked(ProMdl assembly,int side,PressfitWorkspaceSelection *result,PartText *out){
    ProSelection *items=NULL;
    ProError error=ProSelbufferSelectionsGet(&items);
    int n=0,common_face=0,mixed_faces=0;
    ProModelitem picked[13]={0};
    if(!error)error=ProArraySizeGet((ProArray)items,&n);
    if(!error&&(!n||n>_countof(picked)))error=PRO_TK_OUT_OF_RANGE;
    double mm;
    if(!error)error=qa_mm_per_unit(assembly,&mm);
    for(int i=0;!error&&i<n;i++){
        ProAsmcomppath path={0};
        error=pressfit_workspace_selection_path(assembly,items[i],&path);
        if(error)break;
        if(result->panel_id&&result->panel_id!=path.comp_id_table[0]){
            part_text_add(out,L"当前选孔跨了多个钣金实例。请按安装板分别登记组。\n");
            error=PRO_TK_BAD_CONTEXT;
            break;
        }
        result->panel_id=path.comp_id_table[0];
        error=ProSelectionModelitemGet(items[i],&picked[i]);
        if(error||picked[i].type==PRO_FEATURE)continue;
        int face=0,holes[12]={0},count=0;
        error=fastener_pressfit_selection_item(&picked[i],mm,&face,holes,&count);
        if(!error&&face){
            if(common_face&&common_face!=face)mixed_faces=1;
            common_face=face;
        }
        if(!error&&count==0)picked[i].type=PRO_TYPE_UNUSED;
    }
    if(items)ProSelectionarrayFree(items);
    if(error){
        part_text_add(out,L"请在目标总装中选中同一块板的圆形孔边、孔壁或模型树孔特征。\n");
        return error;
    }
    if(!side&&mixed_faces){
        part_text_add(out,L"选中的孔边在不同安装面。请指定安装侧后重试。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    result->side=side?side:common_face;
    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    error=group_pressfit_geometry_side(assembly,result->panel_id,result->side,geometry,out);
    for(int i=0;!error&&i<n;i++){
        if(picked[i].owner!=geometry->panel.model){error=PRO_TK_BAD_CONTEXT;break;}
        if(picked[i].type==PRO_TYPE_UNUSED)continue;
        if(picked[i].type==PRO_FEATURE)
            error=pressfit_workspace_feature_pick(geometry,picked[i].id,result);
        else error=pressfit_workspace_add_number(result,pressfit_workspace_cylinder_number(geometry,picked[i].id));
    }
    if(!error&&!result->count)error=PRO_TK_E_NOT_FOUND;
    if(error)part_text_add(out,L"有选中的圆孔无法与这块板在指定安装侧的实际通孔核对。\n");
    free(geometry);
    return error;
}

static int pressfit_workspace_same_capture_hole(const GroupPressfitHole *saved,const GroupPressfitHole *actual){
    return saved->face_id==actual->face_id&&saved->hole_id==actual->hole_id&&
        ad_near(saved->contact,actual->contact,0.001)&&ad_near(saved->center,actual->center,0.001)&&
        ad_dot(saved->normal,actual->normal)>1-1e-7&&fabs(saved->diameter-actual->diameter)<=0.001&&
        fabs(saved->depth-actual->depth)<=0.001;
}

static ProError pressfit_workspace_selected(ProMdl assembly,int from_markup,int side,
    PressfitWorkspaceSelection *result,PartText *out){
    memset(result,0,sizeof(*result));
    if(!from_markup)return pressfit_workspace_clicked(assembly,side,result,out);
    int window;
    ProPath directory;
    const PressfitWorkspaceSelection *saved=&pressfit_workspace_markup.selection;
    if(!saved->count||assembly!=pressfit_workspace_markup.assembly||
       ProWindowCurrentGet(&window)||window!=pressfit_workspace_markup.window||
       ProDirectoryCurrentGet(directory)||_wcsicmp(directory,pressfit_workspace_markup.directory)){
        part_text_add(out,L"当前 Creo 装配没有可以继续使用的截图孔位。请在截图窗口圈孔后提交「选择这些孔用于压铆分组」。\n");
        return PRO_TK_E_NOT_FOUND;
    }
    GroupPressfitGeometry *checked=calloc(1,sizeof(*checked));
    if(!checked)return PRO_TK_GENERAL_ERROR;
    ProError error=group_pressfit_geometry_side(assembly,saved->panel_id,saved->side,checked,out);
    if(!error&&checked->panel.model!=pressfit_workspace_markup.model)error=PRO_TK_BAD_CONTEXT;
    for(int i=0;!error&&i<saved->count;i++){
        const int number=saved->numbers[i];
        if(number>checked->count||!pressfit_workspace_same_capture_hole(&pressfit_workspace_markup.holes[i],
            &checked->holes[number-1]))error=PRO_TK_BAD_CONTEXT;
    }
    if(!error){
        *result=*saved;
        if(side&&side!=saved->side){
            error=group_pressfit_geometry_side(assembly,saved->panel_id,side,checked,out);
            for(int i=0;!error&&i<saved->count;i++)
                if(!checked->holes[saved->numbers[i]-1].face_id)error=PRO_TK_BAD_CONTEXT;
            if(!error)result->side=side;
        }
    }
    if(error)part_text_add(out,L"截图中的孔、钣金实例或安装侧已变化。请重新圈选。\n");
    free(checked);
    return error;
}

static ProError pressfit_workspace_mark_indices(const MarkupRequest *request,const MarkupImage *image,
    int indices[12],int *count,PartText *out){
    if(request->count<1||request->count>MARKUP_MAX_MARKS)return PRO_TK_BAD_INPUTS;
    for(int i=0;i<request->count;i++){
        const MarkupMark *mark=&request->marks[i];
        int hits[12];
        const int n=markup_hole_hits(image,mark,hits,mark->tool==MARKUP_ARROW?1:_countof(hits));
        if(n<1){
            part_text_add(out,L"第 %d 处标注未唯一定位到可见孔位，或圈选超过每组 12 孔。\n",i+1);
            return PRO_TK_BAD_INPUTS;
        }
        for(int j=0;j<n;j++){
            int duplicate=0;
            for(int k=0;k<*count;k++)if(indices[k]==hits[j])duplicate=1;
            if(duplicate)continue;
            if(*count==12)return PRO_TK_OUT_OF_RANGE;
            indices[(*count)++]=hits[j];
        }
    }
    return PRO_TK_NO_ERROR;
}

static ProError pressfit_workspace_mark_map(const MarkupCapture *capture,const GroupPressfitGeometry *geometry,
    const int *indices,int count,PressfitWorkspaceSelection *selection){
    ProAsmcomppath path=geometry->panel.path;
    ProMatrix pose;
    ProError error=ProAsmcomppathTrfGet(&path,PRO_B_TRUE,pose);
    selection->count=0;
    for(int i=0;!error&&i<count;i++){
        const MarkupHole *marked=&capture->image->holes[indices[i]];
        const int number=pressfit_workspace_cylinder_number(geometry,marked->surface);
        if(!number)return PRO_TK_E_NOT_FOUND;
        const GroupPressfitHole *hole=&geometry->holes[number-1];
        ProPoint3d local,point;
        memcpy(local,marked->center,sizeof(local));
        error=ProPntTrfEval(local,pose,point);
        if(error)return error;
        for(int j=0;j<3;j++)point[j]*=geometry->mm;
        if(!ad_near(point,hole->contact,0.002))return PRO_TK_BAD_CONTEXT;
        error=pressfit_workspace_add_number(selection,number);
    }
    return error;
}

/* The screenshot editor hands off transient native evidence. No assembly or
   group changes are made until a separate explicit grouped placement request. */
static ProError pressfit_workspace_capture_marks(const MarkupCapture *capture,const MarkupRequest *request,PartText *out){
    memset(&pressfit_workspace_markup,0,sizeof(pressfit_workspace_markup));
    const MarkupImage *image=capture->image;
    if(!image||request->ticket!=capture->ticket)return PRO_TK_BAD_CONTEXT;
    int indices[12],count=0;
    ProError error=pressfit_workspace_mark_indices(request,image,indices,&count,out);
    if(error)return error;
    const int panel=image->holes[indices[0]].occurrence.ids[0];
    for(int j=0;j<count;j++){
        const MarkupHole *hole=&image->holes[indices[j]];
        AxComponent checked={0};
        if(!panel||hole->occurrence.depth!=1||hole->occurrence.ids[0]!=panel){
            part_text_add(out,L"截图圈选跨了多个钣金实例。请一次圈选一块板上的孔位。\n");
            return PRO_TK_BAD_CONTEXT;
        }
        error=markup_assembly_component(capture,&hole->occurrence,&checked);
        if(error)return error;
    }

    GroupPressfitGeometry *geometry=calloc(1,sizeof(*geometry));
    if(!geometry)return PRO_TK_GENERAL_ERROR;
    PressfitWorkspaceSelection selection={.panel_id=panel};
    error=group_pressfit_geometry_side(capture->model,panel,0,geometry,NULL);
    if(!error)error=pressfit_workspace_mark_map(capture,geometry,indices,count,&selection);
    if(error==PRO_TK_BAD_CONTEXT||error==PRO_TK_E_NOT_FOUND){
        selection.side=-1;
        error=group_pressfit_geometry_side(capture->model,panel,-1,geometry,NULL);
        if(!error)error=pressfit_workspace_mark_map(capture,geometry,indices,count,&selection);
    }
    BsoSite sites[12]={0};
    if(!error&&!selection.count)error=PRO_TK_E_NOT_FOUND;
    for(int i=0;!error&&i<selection.count;i++){
        const GroupPressfitHole *hole=&geometry->holes[selection.numbers[i]-1];
        error=group_pressfit_bso_site(geometry,hole,&sites[i]);
        if(!error)pressfit_workspace_markup.holes[i]=*hole;
    }
    if(!error){
        pressfit_workspace_markup.assembly=capture->model;
        pressfit_workspace_markup.model=geometry->panel.model;
        pressfit_workspace_markup.window=capture->window;
        wcscpy_s(pressfit_workspace_markup.directory,_countof(pressfit_workspace_markup.directory),capture->directory);
        pressfit_workspace_markup.selection=selection;
        fastener_pressfit_highlight(capture->model,sites,selection.count,out);
        part_text_add(out,L"钣金实例 ID:%d 已核对",panel);
        for(int i=0;i<selection.count;i++)
            part_text_add(out,L"%ls%d号孔",i?L"、":L" ",selection.numbers[i]);
        part_text_add(out,L"。可以在 MWEAI 说「给截图孔分组：左组 装 BSO M3×6」。\n");
    }else part_text_add(out,L"未能把全部圈选位置对应到同一钣金实例的可见原生孔口。请调整视角后重拍。\n");
    free(geometry);
    return error;
}