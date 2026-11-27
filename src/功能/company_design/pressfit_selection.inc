/* Selection is always tied to the exact part occurrence, even for edge picks. */
static ProError fastener_pressfit_selection_item(ProModelitem *item,double mm,int *face,int holes[12],int *count){
    if(item->type==PRO_EDGE){
        ProEdge edge;
        ProEnttype type;
        ProSurface adjacent[2];
        ProSrftype surface_type[2];
        int ids[2];
        ProError error=ProGeomitemToEdge(item,&edge);
        if(!error)error=ProEdgeTypeGet(edge,&type);
        if(!error&&type!=PRO_ENT_CIRCLE&&type!=PRO_ENT_ARC)error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ProEdgeNeighborsGet(edge,NULL,NULL,&adjacent[0],&adjacent[1]);
        for(int i=0;!error&&i<2;i++){
            error=ProSurfaceTypeGet(adjacent[i],&surface_type[i]);
            if(!error)error=ProSurfaceIdGet(adjacent[i],&ids[i]);
        }
        if(error)return error;
        const int plane=surface_type[0]==PRO_SRF_PLANE?0:surface_type[1]==PRO_SRF_PLANE?1:-1;
        if(plane<0||surface_type[1-plane]!=PRO_SRF_CYL||(*face&&*face!=ids[plane]))return PRO_TK_BAD_CONTEXT;
        FdTxn tx={.model=item->owner,.scale=1/mm};
        ShopMouth mouth={0};
        wchar_t face_token[32],hole_token[32];
        fastener_bso_surface(ids[plane],face_token);
        fastener_bso_surface(ids[1-plane],hole_token);
        error=mp_mouth(&tx,face_token,hole_token,&mouth);
        if(error)return error;
        *face=ids[plane];
        item->id=ids[1-plane];
        item->type=PRO_SURFACE;
    }
    if(item->type!=PRO_SURFACE)return PRO_TK_BAD_CONTEXT;
    AxGeometry shape;
    const ProError error=ax_geometry(item,&shape);
    if(error)return error;
    if(shape.type==AX_GEOM_PLANE&&(!*face||*face==item->id)){*face=item->id;return PRO_TK_NO_ERROR;}
    if(shape.type!=AX_GEOM_CYLINDER)return PRO_TK_BAD_CONTEXT;
    for(int i=0;i<*count;i++)if(holes[i]==item->id)return PRO_TK_NO_ERROR;
    if(*count>=12)return PRO_TK_OUT_OF_RANGE;
    holes[(*count)++]=item->id;
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_selection_workbench(ProMdl model,int assembly,double mm,AxComponent *panel,
        int *face,int holes[12],int *count){
    ProSelection *selection=NULL;
    int selected=0;
    wchar_t key[AX_PATH_DEPTH*12+4]=L"";
    ProError error=ProSelbufferSelectionsGet(&selection);
    if(!error)error=ProArraySizeGet((ProArray)selection,&selected);
    if(!error&&!selected)error=PRO_TK_E_NOT_FOUND;
    if(!error&&selected>13)error=PRO_TK_OUT_OF_RANGE;
    for(int i=0;!error&&i<selected;i++){
        ProModelitem item;
        error=ProSelectionModelitemGet(selection[i],&item);
        if(error)break;
        if(assembly){
            ProAsmcomppath path={0};
            wchar_t occurrence[AX_PATH_DEPTH*12+4];
            error=ProSelectionAsmcomppathGet(selection[i],&path);
            if(!error&&(path.owner!=(ProSolid)model||path.table_num<1||path.table_num>AX_PATH_DEPTH||
               !ax_path_label(path.comp_id_table,path.table_num,occurrence,_countof(occurrence))))error=PRO_TK_BAD_CONTEXT;
            if(error)break;
            if(!key[0]){
                wcscpy_s(key,_countof(key),occurrence);
                error=ax_hw_occurrence(model,key,mm,panel);
            }
            if(!error&&(item.owner!=panel->model||wcscmp(key,occurrence)))error=PRO_TK_BAD_CONTEXT;
        }else if(item.owner!=model)error=PRO_TK_BAD_CONTEXT;
        if(!error)error=fastener_pressfit_selection_item(&item,mm,face,holes,count);
    }
    if(selection)ProSelectionarrayFree(selection);
    return error;
}

typedef struct {
    AxReturnLink link;
    AxSessionIdentity source,parent;
    int face,count,holes[12];
} PressfitParentSelection;

typedef struct {
    AxReturnLink first;
    const wchar_t *requested;
    int count,incomplete;
} PressfitParentSearch;

static void fastener_pressfit_parent_clear(PressfitParentSelection *selection){
    if(selection->source.stamp)ProVerstampFree(&selection->source.stamp);
    if(selection->parent.stamp)ProVerstampFree(&selection->parent.stamp);
    memset(selection,0,sizeof(*selection));
}

static ProError fastener_pressfit_find_parent(int window,ProAppData data){
    PressfitParentSearch *search=data;
    ProMdl root=NULL;
    ProMdlType type;
    ProMdlName name;
    if(window==search->first.child_window||ProWindowMdlGet(window,&root)||!root||
       ProMdlTypeGet(root,&type)||type!=PRO_MDL_ASSEMBLY||ProMdlMdlnameGet(root,name)||
       (search->requested&&_wcsicmp(name,search->requested)))return PRO_TK_NO_ERROR;
    AxScan *scan=NULL;
    const ProError error=ax_scan(root,0,&scan);
    if(error){search->incomplete=1;return PRO_TK_NO_ERROR;}
    if(scan->limited||scan->unavailable)search->incomplete=1;
    for(int i=0;i<scan->count;i++){
        const AxOccurrence *item=&scan->occurrences[i];
        if(item->state!=PRO_FEAT_ACTIVE||item->model!=search->first.child)continue;
        if(search->count&&search->first.root==root){
            ProIdTable ids={0};
            const int depth=ax_path_parse(search->first.occurrence,ids);
            if(depth==item->depth&&!memcmp(ids,item->ids,depth*sizeof(int)))continue;
        }
        if(++search->count!=1)continue;
        search->first.root=root;
        search->first.root_window=window;
        if(!ax_path_label(item->ids,item->depth,search->first.occurrence,_countof(search->first.occurrence)))
            search->incomplete=1;
    }
    free(scan);
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_capture_parent_selection(const wchar_t *parent_name,
        PressfitParentSelection *selection,PartText *out){
    PressfitParentSearch search={0};
    ProMdlType type;
    ProMdl owner=NULL;
    ProError error=ProMdlCurrentGet(&search.first.child);
    if(!error)error=ProWindowCurrentGet(&search.first.child_window);
    if(!error)error=ProWindowMdlGet(search.first.child_window,&owner);
    if(!error&&owner!=search.first.child)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlTypeGet(owner,&type);
    if(!error&&type!=PRO_MDL_PART)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProDirectoryCurrentGet(search.first.directory);
    if(error)return error;
    ProMdlName requested=L"";
    if(parent_name&&*parent_name){
        size_t length=wcslen(parent_name);
        if(length>4&&!_wcsicmp(parent_name+length-4,L".ASM"))length-=4;
        if(length>=_countof(requested)||wcsncpy_s(requested,_countof(requested),parent_name,length)||
           !part_name(requested))return PRO_TK_BAD_INPUTS;
        search.requested=requested;
    }
    double mm;
    error=qa_mm_per_unit(owner,&mm);
    if(!error)error=fastener_pressfit_selection_workbench(owner,0,mm,NULL,&selection->face,selection->holes,&selection->count);
    if(error){
        part_text_add(out,L"单开的零件窗口请先选圆孔孔壁、孔口边或安装实体面。\n");
        return error;
    }
    for(unsigned i=0;i<_countof(ax_return_history);i++){
        const AxReturnLink *entry=&ax_return_history[i];
        ProMdlName name;
        if(entry->child!=owner||entry->child_window!=search.first.child_window||!ax_return_link_live(entry)||
           ProMdlMdlnameGet(entry->root,name)||(search.requested&&_wcsicmp(search.requested,name)))continue;
        search.first=*entry;
        search.count=1;
        break;
    }
    if(!search.count){
        error=ProWindowsVisit(fastener_pressfit_find_parent,&search);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    }
    if(!error&&(search.incomplete||search.count!=1||!ax_return_link_live(&search.first))){
        if(search.count>1)part_text_add(out,L"当前零件在已打开的装配里出现 %d 次。请在目标装配中选择具体实例后执行。\n",search.count);
        else if(search.incomplete)part_text_add(out,L"打开的装配含未能读全的组件，无法保证安装实例唯一。请指定装配或直接在装配中选孔。\n");
        else if(!search.count)part_text_add(out,L"已打开的装配里找不到当前零件的活动实例。请从目标装配中打开这块板，再选孔。\n");
        else part_text_add(out,L"定位到安装实例 %ls，但窗口或组件关系尚未完成核对。请在目标装配中选孔。\n",search.first.occurrence);
        return search.count>1?PRO_TK_E_FOUND:PRO_TK_BAD_CONTEXT;
    }
    if(error)return error;
    selection->link=search.first;
    selection->source.window=search.first.child_window;
    selection->parent.window=search.first.root_window;
    error=ax_session_identity_take(owner,&selection->source);
    if(!error)error=ax_session_identity_take(search.first.root,&selection->parent);
    if(error)fastener_pressfit_parent_clear(selection);
    return error;
}

static ProError fastener_pressfit_apply_parent_selection(const PressfitParentSelection *selection,
        FastenerPressfitRequest *request,PartText *out){
    if(!selection->link.root||!ax_return_link_live(&selection->link)||
       ax_session_identity_verify(&selection->source,0)||ax_session_identity_verify(&selection->parent,1)){
        part_text_add(out,L"已核对的原零件、装配窗口、目录或版本发生变化，未使用原选孔。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    wcscpy_s(request->panel_path,_countof(request->panel_path),selection->link.occurrence);
    request->face_id=selection->face;
    request->mapped_count=selection->count;
    memcpy(request->mapped_holes,selection->holes,(size_t)selection->count*sizeof(selection->holes[0]));
    return PRO_TK_NO_ERROR;
}
/* Temporary preflight overlays do not alter the Creo selection buffer. */
static ProSelection fastener_pressfit_highlights[12];

static void fastener_pressfit_highlight_clear(void){
    for(int i=0;i<_countof(fastener_pressfit_highlights);i++){
        ProSelection *selection=&fastener_pressfit_highlights[i];
        if(!*selection)continue;
        ProSelectionUnhighlight(*selection);
        ProSelectionFree(selection);
    }
}

static void fastener_pressfit_highlight(ProMdl assembly,const BsoSite *sites,int count,PartText *out){
    fastener_pressfit_highlight_clear();
    ProMdl current=NULL;
    if(count<1||count>_countof(fastener_pressfit_highlights)||ProMdlCurrentGet(&current)||current!=assembly)return;
    int found=0;
    for(int i=0;i<count;i++){
        ProModelitem item;
        ProAsmcomppath path=sites[i].panel.path;
        ProSelection *selection=&fastener_pressfit_highlights[i];
        if(path.owner!=(ProSolid)assembly||ProModelitemInit(sites[i].panel.model,sites[i].hole_id,PRO_SURFACE,&item)||
           ProSelectionAlloc(&path,&item,selection))continue;
        if(!ProSelectionHighlight(*selection,PRO_COLOR_HIGHLITE))found++;
        else ProSelectionFree(selection);
    }
    if(found)part_text_add(out,L"已在 Creo 窗口高亮 %d 个待装孔位。\n",found);
}
