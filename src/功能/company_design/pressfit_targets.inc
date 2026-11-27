#include "pressfit_selection.inc"

/* Resolve target holes against exact Creo occurrence paths and live geometry. */
typedef struct {
    BsoSite sites[12],alternative[12];
    int count,groups,opposite,max_matches,ambiguous_order,rejected_kind,rejected_hole;
    double observed;
} PressfitNaturalSites;

typedef struct {
    const FastenerPressfitRequest *request;
    const BsoFitting *fitting;
    double mm,diameter;
    ProMatrix view;
    int has_view;
} PressfitSearch;

typedef struct {
    ProMdl model;
    ProSurface plane;
    int plane_id,edge_id;
    ProVector center;
    double mm,best;
} PressfitBendSearch;

static ProError fastener_pressfit_bend_edge(ProEdge edge,ProError status,ProAppData data){
    if(status)return status;
    PressfitBendSearch *search=data;
    ProEnttype kind;
    ProSurface adjoining[2];
    ProError error=ProEdgeTypeGet(edge,&kind);
    if(error||kind!=PRO_ENT_LINE)return error;
    error=ProEdgeNeighborsGet(edge,NULL,NULL,&adjoining[0],&adjoining[1]);
    if(error)return error;
    int first,second,id;
    if((error=ProSurfaceIdGet(adjoining[0],&first))||(error=ProSurfaceIdGet(adjoining[1],&second))||
       (error=ProEdgeIdGet(edge,&id)))return error;
    int other=first==search->plane_id?1:second==search->plane_id?0:-1;
    if(other<0)return PRO_TK_NO_ERROR;
    ProSrftype shape;
    if((error=ProSurfaceTypeGet(adjoining[other],&shape)))return error;
    if(shape!=PRO_SRF_CYL)return PRO_TK_NO_ERROR;
    double angle;
    if(ProSmtBendsrfInfoGet((ProPart)search->model,adjoining[other],NULL,NULL,&angle,NULL,NULL)||
       !isfinite(angle)||fabs(angle)<0.01)return PRO_TK_NO_ERROR;
    ProVector a,b;
    if((error=ProEdgeXyzdataEval(edge,0,a,NULL,NULL,NULL))||
       (error=ProEdgeXyzdataEval(edge,1,b,NULL,NULL,NULL)))return error;
    const double distance=in_segment_distance(search->center,a,b)*search->mm;
    if(distance<search->best){search->best=distance;search->edge_id=id;}
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_bend_contour(ProContour contour,ProError status,ProAppData data){
    PressfitBendSearch *search=data;
    return status?status:ProContourEdgeVisit(search->plane,contour,fastener_pressfit_bend_edge,NULL,search);
}

static ProError fastener_pressfit_bend_distance(const BsoSite *site,double mm,double *distance,int *edge){
    ProMdlsubtype subtype;
    ProError error=ProMdlSubtypeGet(site->panel.model,&subtype);
    if(error||subtype!=PROMDLSTYPE_PART_SHEETMETAL)return error?error:PRO_TK_E_NOT_FOUND;
    PressfitBendSearch search={.model=site->panel.model,.plane_id=site->face_id,.mm=mm,.best=INFINITY};
    wchar_t hole[32],face[32];
    fastener_bso_surface(site->hole_id,hole);
    fastener_bso_surface(site->face_id,face);
    AxHardwareSite local;
    AxComponent panel=site->panel;
    error=ax_hw_site(&panel,hole,face,mm,&local);
    if(!error)error=ProSurfaceInit((ProSolid)search.model,search.plane_id,&search.plane);
    if(error)return error;
    memcpy(search.center,local.contact,sizeof(ProVector));
    error=ProSurfaceContourVisit(search.plane,fastener_pressfit_bend_contour,NULL,&search);
    if(error||!isfinite(search.best))return error?error:PRO_TK_E_NOT_FOUND;
    *distance=search.best;
    *edge=search.edge_id;
    return PRO_TK_NO_ERROR;
}

#include "pressfit_site_audit.inc"

static ProError fastener_pressfit_openings(ProMdl assembly,double mm,PartText *out){
    AxComponent panel={0};
    int face=0,count=0,holes[12];
    ProError error=fastener_pressfit_selection_workbench(assembly,1,mm,&panel,&face,holes,&count);
    if(error||!face||!count){
        part_text_add(out,L"请在装配中选安装面和同一块板上要检查的圆弧孔壁。\n");
        return error?error:PRO_TK_BAD_CONTEXT;
    }
    for(int i=0;i<count;i++){
        ShopMouth opening={0};
        const int kind=fastener_pressfit_classify(&panel,face,holes[i],mm,&opening);
        part_text_add(out,L"孔壁 SURFACE:%d，安装面 SURFACE:%d，%ls",holes[i],face,
            fastener_pressfit_opening_label(kind));
        if(opening.radius>0)part_text_add(out,L"，孔口 Ø%.6g mm",2*opening.radius);
        part_text_add(out,L"。\n");
    }
    return PRO_TK_NO_ERROR;
}

static int fastener_pressfit_same_group(const BsoSite *a,const BsoSite *b,int n,double mm){
    if(a->panel.path.owner!=b->panel.path.owner||!ax_same_occurrence(&a->panel,&b->panel))return 0;
    for(int i=0;i<n;i++){
        int found=0;
        for(int j=0;j<n;j++){
            if(fabs(ad_dot(a[i].geometry.cylinder.direction,b[j].geometry.cylinder.direction))>1-1e-7&&
               ax_hw_separation(a[i].geometry.contact,b[j].geometry.contact,b[j].geometry.cylinder.direction,mm)<0.001){
                found=1;
                break;
            }
        }
        if(!found)return 0;
    }
    return 1;
}

static double fastener_pressfit_read_bore(AxComponent *panel,int hole,double mm,AxGeometry *shape){
    wchar_t token[32];
    ProVector ends[2];
    int bore;
    fastener_bso_surface(hole,token);
    return !ax_hw_cylinder(panel,token,shape,ends,&bore)&&bore?2*shape->radius*mm:0.0;
}

#include "pressfit_example.inc"

static double fastener_pressfit_uniform_bores(AxComponent *panel,int face,const int *holes,int n,int expected,
    double mm,int *verified_count){
    if(verified_count)*verified_count=0;
    AxGeometry seen[12];
    int count=0;
    double diameter=0;
    wchar_t face_token[32];
    fastener_bso_surface(face,face_token);
    FdTxn tx={.model=panel->model,.scale=1/mm};
    for(int i=0;i<n;i++){
        AxGeometry shape;
        double actual=fastener_pressfit_read_bore(panel,holes[i],mm,&shape);
        if(actual<=0)continue;
        wchar_t token[32];
        fastener_bso_surface(holes[i],token);
        ShopMouth mouth={0};
        if(mp_mouth(&tx,face_token,token,&mouth))continue;
        if(diameter>0&&fabs(diameter-actual)>0.001)return 0;
        diameter=actual;
        int repeat=0;
        for(int j=0;j<count;j++)if(ax_hw_separation(shape.point,seen[j].point,shape.direction,mm)<0.001){repeat=1;break;}
        if(repeat)continue;
        if(count>=12)return 0;
        seen[count++]=shape;
    }
    if(verified_count)*verified_count=count;
    return (!expected||count==expected)?diameter:0;
}

typedef struct { BsoSite site;double score;int bend_edge; } PressfitRanked;

static int fastener_pressfit_rank_compare(const void *one,const void *two){
    const PressfitRanked *a=one,*b=two;
    return (a->score>b->score)-(a->score<b->score);
}

static double fastener_pressfit_view_vector(const PressfitSearch *search,const double *direction,int axis){
    double aligned=0;
    for(int i=0;i<3;i++)aligned+=direction[i]*search->view[i][axis];
    return aligned;
}

static int fastener_pressfit_panel_zone(const PressfitSearch *search,const BsoSite *site,int explicit_face){
    if(explicit_face||!search->request->side_zone)return 1;
    if(!search->has_view)return 0;
    const double *normal=site->geometry.plane.direction;
    const double forward=fabs(fastener_pressfit_view_vector(search,normal,2));
    const double horizontal=fabs(fastener_pressfit_view_vector(search,normal,0));
    return search->request->side_zone==1?forward>=0.9:horizontal>=0.9&&forward<=0.44;
}

static int fastener_pressfit_choose_order(const PressfitSearch *search,PressfitRanked *rows,int count){
    const FastenerPressfitRequest *request=search->request;
    if(!request->view_side&&!request->nearest_bend)return count;
    if(request->view_side&&!search->has_view)return 0;
    for(int i=0;i<count;i++){
        if(request->nearest_bend){
            if(fastener_pressfit_bend_distance(&rows[i].site,search->mm,&rows[i].score,&rows[i].bend_edge))
                return 0;
        }else{
            rows[i].score=-request->view_side*fastener_pressfit_view_vector(search,rows[i].site.geometry.contact,0)*search->mm;
        }
    }
    qsort(rows,(size_t)count,sizeof(*rows),fastener_pressfit_rank_compare);
    const double tolerance=request->nearest_bend?0.05:0.01;
    int limit=request->expected?request->expected:1;
    if(!request->expected&&request->nearest_bend){
        while(limit<count&&fabs(rows[limit].score-rows[0].score)<=tolerance)limit++;
    }
    if(limit>count)return 0;
    if(limit<count&&(request->expected||!request->nearest_bend)&&
       fabs(rows[limit].score-rows[limit-1].score)<=tolerance)return 0;
    if(request->nearest_bend)for(int i=1;i<limit;i++)if(rows[i].bend_edge!=rows[0].bend_edge)return 0;
    return limit;
}

static ProError fastener_pressfit_scan_plane(const PressfitSearch *search,AxComponent *panel,int face,
    const int *holes,int n,int expected,int explicit_holes,int explicit_face,PressfitNaturalSites *found){
    if(expected&&n<expected)return PRO_TK_NO_ERROR;
    PressfitRanked rows[128];
    int count=0;
    const double mm=search->mm;
    const FastenerPressfitRequest *request=search->request;
    for(int i=0;i<n;i++){
        ProModelitem item;
        AxGeometry shape;
        if(ProModelitemInit(panel->model,holes[i],PRO_SURFACE,&item)||ax_geometry(&item,&shape)||
           shape.type!=AX_GEOM_CYLINDER||fabs(2*shape.radius*mm-search->diameter)>0.001)continue;
        const wchar_t *stage=L"";
        BsoSite site={0};
        ProError error=fastener_pressfit_target(panel,holes[i],face,mm,search->fitting->model,search->fitting,
            search->diameter,request->interference,&site,&stage);
        if(error&&error!=PRO_TK_BAD_CONTEXT&&error!=PRO_TK_BAD_INPUTS&&error!=PRO_TK_INVALID_TYPE)return error;
        if(!error||explicit_holes){
            ShopMouth mouth={0};
            const int kind=fastener_pressfit_classify(panel,face,holes[i],mm,&mouth);
            if(kind==PF_OPENING_SLOT||kind==PF_OPENING_STEPPED||kind==PF_OPENING_BLIND){
                found->rejected_kind=kind;
                found->rejected_hole=holes[i];
                continue;
            }
        }
        if(error)continue;
        if(!fastener_pressfit_panel_zone(search,&site,explicit_face))return PRO_TK_NO_ERROR;
        int duplicate=0;
        for(int j=0;j<count;j++)if(ad_near(site.geometry.contact,rows[j].site.geometry.contact,0.001/mm)){duplicate=1;break;}
        if(duplicate)continue;
        if(count>=_countof(rows))return PRO_TK_OUT_OF_RANGE;
        rows[count++]=(PressfitRanked){.site=site};
    }
    if(count>found->max_matches)found->max_matches=count;
    if(!count||(explicit_holes&&count!=n)||(!(request->view_side||request->nearest_bend)&&expected&&count!=expected)){
        const double observed=fastener_pressfit_uniform_bores(panel,face,holes,n,expected,mm,NULL);
        if(observed>0&&found->observed>=0){
            if(found->observed>0&&fabs(found->observed-observed)>0.001)found->observed=-1;
            else found->observed=observed;
        }
        return PRO_TK_NO_ERROR;
    }
    count=fastener_pressfit_choose_order(search,rows,count);
    if(!count){found->ambiguous_order=1;return PRO_TK_NO_ERROR;}
    if(count>12)return PRO_TK_OUT_OF_RANGE;

    BsoSite compact[12];
    for(int i=0;i<count;i++)compact[i]=rows[i].site;
    if(found->groups){
        if(count==found->count&&fastener_pressfit_same_group(found->sites,compact,count,mm)){
            if(ad_dot(found->sites[0].geometry.plane.direction,compact[0].geometry.plane.direction)<-1+1e-7){
                found->opposite=1;
                memcpy(found->alternative,compact,(size_t)count*sizeof(*compact));
            }else if(found->sites[0].face_id!=compact[0].face_id)found->groups++;
        }else found->groups++;
        return PRO_TK_NO_ERROR;
    }
    found->count=count;
    found->groups=1;
    memcpy(found->sites,compact,(size_t)count*sizeof(*compact));
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_scan_panel(const PressfitSearch *search,AxComponent *panel,int face,
    const int *holes,int n,int expected,PressfitNaturalSites *found){
    const int explicit_holes=n>0;
    if(face&&n)return fastener_pressfit_scan_plane(search,panel,face,holes,n,expected,1,1,found);
    BsoFaces candidates={.model=panel->model,.mm=search->mm,.radius=search->diameter/2,.read_planes=!face,.all_holes=1};
    ProError error=fastener_bso_model_faces(&candidates);
    if(error)return error;
    if(!n){holes=candidates.holes;n=candidates.hole_count;}
    if(face)return fastener_pressfit_scan_plane(search,panel,face,holes,n,expected,explicit_holes,1,found);
    for(int i=0;!error&&i<candidates.plane_count&&found->groups<2;i++)
        error=fastener_pressfit_scan_plane(search,panel,candidates.planes[i],holes,n,expected,explicit_holes,0,found);
    return error;
}

static ProError fastener_pressfit_flip(const PressfitSearch *search,const PressfitNaturalSites *found,BsoSite sites[12],PartText *out){
    if(found->opposite){
        memcpy(sites,found->alternative,(size_t)found->count*sizeof(*sites));
        return PRO_TK_NO_ERROR;
    }
    const BsoSite *first=found->sites;
    AxComponent panel=first->panel;
    BsoFaces faces={.model=panel.model,.mm=search->mm,.radius=search->diameter/2,.read_planes=1};
    ProError error=fastener_bso_model_faces(&faces);
    int verified=0;
    for(int i=0;!error&&i<faces.plane_count;i++){
        if(faces.planes[i]==first->face_id)continue;
        BsoSite alternate[12];
        int matches=0;
        for(int j=0;j<found->count;j++){
            const wchar_t *stage;
            ProError fit=fastener_pressfit_target(&panel,found->sites[j].hole_id,faces.planes[i],search->mm,
                search->fitting->model,search->fitting,search->diameter,search->request->interference,&alternate[j],&stage);
            if(fit||ad_dot(alternate[j].geometry.plane.direction,found->sites[j].geometry.plane.direction)>-1+1e-7)break;
            matches++;
        }
        if(matches!=found->count||!fastener_pressfit_same_group(found->sites,alternate,matches,search->mm))continue;
        if(++verified>1){error=PRO_TK_E_FOUND;break;}
        memcpy(sites,alternate,(size_t)matches*sizeof(*sites));
    }
    if(error||verified!=1){
        part_text_add(out,L"在同一组孔的另一侧无法核对唯一安装面。请直接选中另一侧的实体面。\n");
        return error?error:PRO_TK_E_NOT_FOUND;
    }
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_short_sites(ProMdl assembly,double mm,const FastenerPressfitRequest *request,
    const BsoFitting *fitting,BsoSite sites[12],int *count,PartText *out){
    AxComponent panel={0};
    int face=0,n=0,holes[12];
    *count=0;
    PressfitSearch search={.request=request,.fitting=fitting,.mm=mm,
        .diameter=request->hole?request->hole:2*fitting->geometry.cylinder.radius*mm};
    if(request->view_side||request->side_zone||request->choose_side)
        search.has_view=!ProViewMatrixGet(assembly,NULL,search.view);
    PressfitNaturalSites found={0};
    ProError error=request->bound_hole_mm>0.0?PRO_TK_E_NOT_FOUND:
        fastener_pressfit_selection_workbench(assembly,1,mm,&panel,&face,holes,&n);
    if(request->panel_path[0]){
        AxComponent explicit_panel={0};
        const ProError resolved=ax_hw_occurrence(assembly,request->panel_path,mm,&explicit_panel);
        if(resolved){part_text_add(out,L"找不到指定安装板实例 %ls（Creo %d）。\n",request->panel_path,resolved);return resolved;}
        if(error||!ax_same_occurrence(&panel,&explicit_panel)){face=n=0;}
        panel=explicit_panel;
        error=PRO_TK_NO_ERROR;
    }
    if(request->face_id){
        if(error||!panel.model){part_text_add(out,L"指定安装面时也要指定板的实例路径或在装配中选中该板。\n");return PRO_TK_BAD_CONTEXT;}
        face=request->face_id;
        if(request->panel_path[0]&&!request->mapped_count)n=0;
    }
    if(request->recommend&&request->all_on_face)n=0;
    if(request->mapped_count<0||request->mapped_count>12)return PRO_TK_BAD_INPUTS;
    if(request->mapped_count){
        n=request->mapped_count;
        memcpy(holes,request->mapped_holes,(size_t)n*sizeof(holes[0]));
    }
    if((request->all_on_face||request->flip_side)&&(!face||error)){
        part_text_add(out,L"请先选中安装面，或给出板的实例路径和面号。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    const int rank=request->view_side||request->nearest_bend;
    if(!error){
        if(n&&request->expected&&n!=request->expected&&!rank){
            part_text_add(out,L"实选 %d 个孔，指令指定 %d 个。请核对选中的孔。\n",n,request->expected);
            return PRO_TK_BAD_INPUTS;
        }
        const int desired=request->expected?request->expected:request->all_on_face||rank?0:n?n:1;
        error=fastener_pressfit_scan_panel(&search,&panel,face,holes,n,desired,&found);
    }else if(error==PRO_TK_E_NOT_FOUND){
        const int desired=request->expected?request->expected:rank?0:1;
        AxScan *scan=NULL;
        error=ax_scan(assembly,1,&scan);
        for(int i=0;!error&&i<scan->count&&found.groups<2;i++){
            const AxOccurrence *item=&scan->occurrences[i];
            if(item->type!=PRO_MDL_PART||item->state!=PRO_FEAT_ACTIVE||!item->model)continue;
            wchar_t path[AX_PATH_DEPTH*12+4];
            if(!ax_path_label(item->ids,item->depth,path,_countof(path))||ax_hw_occurrence(assembly,path,mm,&panel))continue;
            error=fastener_pressfit_scan_panel(&search,&panel,0,NULL,0,desired,&found);
        }
        free(scan);
    }
    if(error||found.groups!=1||(!face&&found.opposite&&!request->choose_side)){
        if(error==PRO_TK_OUT_OF_RANGE)part_text_add(out,L"同一面符合条件的孔超过单次 12 个或读取范围，请分组指定。\n");
        else if(found.groups>1)part_text_add(out,L"有多组可安装的孔，请在目标板上选安装面或孔壁，也可指定实例路径和面号。\n");
        else if(found.groups==1&&found.opposite)part_text_add(out,L"同一组孔的两侧都符合。请选要安装的一侧（SURFACE:%d 或 SURFACE:%d）。\n",
            found.sites[0].face_id,found.alternative[0].face_id);
        else if(found.ambiguous_order)part_text_add(out,L"当前视图或实际折弯距离无法唯一划分这排孔。请选明目标孔，或将板调整到正视图。\n");
        else if(found.rejected_kind)
            part_text_add(out,L"孔壁 SURFACE:%d 为%ls，压铆自动定位需要圆形通孔。\n",
                found.rejected_hole,fastener_pressfit_opening_label(found.rejected_kind));
        else if(found.observed>0&&fabs(found.observed-search.diameter)>0.001)
            part_text_add(out,L"当前孔组实测 Ø%.6g mm，要求 Ø%.6g mm。请核对库件和孔规格。\n",found.observed,search.diameter);
        else if(found.max_matches>1)
            part_text_add(out,L"找到一处有 %d 个匹配孔。请选中目标孔，或指定安装面和数量。\n",found.max_matches);
        else part_text_add(out,L"没有唯一匹配的 Ø%.6g mm 实体孔组。请点安装面或选圆孔孔壁。\n",search.diameter);
        return error?error:PRO_TK_BAD_CONTEXT;
    }
    if(request->flip_side){
        error=fastener_pressfit_flip(&search,&found,sites,out);
        if(error)return error;
        part_text_add(out,L"已核对同一组圆孔的另一侧安装面 SURFACE:%d。\n",sites[0].face_id);
    }else{
        const BsoSite *chosen=found.sites;
        if(!face&&found.opposite){
            const double facing=search.has_view?fastener_pressfit_view_vector(&search,found.sites[0].geometry.plane.direction,2):0;
            if(fabs(facing)<0.05){
                part_text_add(out,L"两个可装面在当前视角近似侧视。请点要装的一侧。\n");
                return PRO_TK_BAD_CONTEXT;
            }
            if(facing<0)chosen=found.alternative;
            part_text_add(out,L"按你授权的安装方向选择朝向当前视图的一侧 SURFACE:%d。\n",chosen[0].face_id);
        }
        memcpy(sites,chosen,(size_t)found.count*sizeof(*sites));
    }
    *count=found.count;
    wchar_t path[AX_PATH_DEPTH*12+4];
    if(ax_path_label(sites[0].panel.path.comp_id_table,sites[0].panel.path.table_num,path,_countof(path)))
        part_text_add(out,L"已核对板实例 %ls，实际安装面 SURFACE:%d，匹配 %d 个孔。\n",path,sites[0].face_id,*count);
    return PRO_TK_NO_ERROR;
}
