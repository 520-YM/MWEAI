/* Number the usable openings from native sheet geometry. All coordinates in
   the public hole rows are millimetres. Numbering uses the sheet's own axes. */
typedef struct {
    int hole_id,face_id,order_id;
    double center[3],contact[3],normal[3],axis[3],diameter,depth;
} GroupPressfitHole;

typedef struct {
    AxComponent panel;
    int panel_id,count;
    double mm;
    GroupPressfitHole holes[DV_BORE_MAX];
} GroupPressfitGeometry;

static int group_pressfit_hole_order(const void *left,const void *right){
    const GroupPressfitHole *a=left,*b=right;
    const int axes[]={1,0,2};
    for(int i=0;i<_countof(axes);i++){
        const int axis=axes[i];
        const double first=round(a->center[axis]*1000.0),second=round(b->center[axis]*1000.0);
        if(first<second)return -1;
        if(first>second)return 1;
    }
    return (a->order_id>b->order_id)-(a->order_id<b->order_id);
}

static ProError group_pressfit_panel(ProMdl assembly,int requested,GroupPressfitGeometry *result,PartText *out){
    ProMdlType type;
    ProError error=ProMdlTypeGet(assembly,&type);
    if(error||type!=PRO_MDL_ASSEMBLY)return error?error:PRO_TK_BAD_CONTEXT;

    int id=requested;
    if(!id){
        AxScan *scan=NULL;
        error=ax_scan(assembly,0,&scan);
        if(!error&&scan->limited)error=PRO_TK_INCOMPLETE;
        for(int i=0;!error&&i<scan->count;i++){
            const AxOccurrence *item=&scan->occurrences[i];
            if(item->depth!=1||item->state!=PRO_FEAT_ACTIVE)continue;
            if(!item->model&&item->type!=PRO_MDL_ASSEMBLY){
                error=PRO_TK_INCOMPLETE;
                break;
            }
            if(item->type!=PRO_MDL_PART)continue;
            ProMdlsubtype subtype;
            error=ProMdlSubtypeGet(item->model,&subtype);
            if(error||subtype!=PROMDLSTYPE_PART_SHEETMETAL)continue;
            if(id)error=PRO_TK_E_FOUND;
            else id=item->ids[0];
        }
        free(scan);
        if(error||!id){
            if(out)part_text_add(out, L"无法唯一确定顶层钣金件。请指定要安装的钣金实例 ID。\n");
            return error?error:PRO_TK_E_NOT_FOUND;
        }
    }

    wchar_t path[32];
    swprintf_s(path,_countof(path),L"ID:%d",id);
    error=qa_mm_per_unit(assembly,&result->mm);
    if(!error)error=ax_hw_occurrence(assembly,path,result->mm,&result->panel);
    ProMdlsubtype subtype;
    if(!error)error=ProMdlSubtypeGet(result->panel.model,&subtype);
    if(!error&&subtype!=PROMDLSTYPE_PART_SHEETMETAL)error=PRO_TK_BAD_CONTEXT;
    if(!error)result->panel_id=id;
    else if(out)part_text_add(out,L"无法读取顶层钣金实例 ID:%d，或它的单位与装配不一致（Creo %d）。\n",id,error);
    return error;
}

/* Find the native planar lip at the requested end of the bore. Circle edges
   can divide the cylinder into equivalent surfaces; use the lowest valid
   surface identifier on that same face. */
static ProError group_pressfit_bore_opening(GroupPressfitGeometry *target,const DeliveryBore *bore,
    const BsoFaces *faces,const AxGeometry *planes,int side,GroupPressfitHole *hole){
    AxHardwareSite selected={0};
    const double mm=target->mm;
    const double depth=(bore->high-bore->low)*mm;
    if(!isfinite(depth)||depth<=0.001||!isfinite(bore->radius)||bore->radius<=0)return PRO_TK_BAD_CONTEXT;
    for(int i=0;i<faces->plane_count;i++){
        const int face_id=faces->planes[i];
        const AxGeometry *plane=&planes[i];
        const double facing=ad_dot(plane->direction,bore->axis);
        if(side>0&&side!=face_id)continue;
        if(fabs(facing)<1-1e-8||(!side&&facing>0)||(side==-1&&facing<0))continue;
        const double end=facing<0?bore->low:bore->high;
        if(fabs((ad_dot(plane->point,bore->axis)-end)*mm)>0.002)continue;

        for(int j=0;j<bore->face_count;j++){
            const int hole_id=bore->faces[j];
            ShopMouth mouth={0};
            if(hole->face_id==face_id&&hole->hole_id<=hole_id)continue;
            if(fastener_pressfit_classify(&target->panel,face_id,hole_id,mm,&mouth)!=PF_OPENING_THROUGH)continue;

            wchar_t face[32],cylinder[32];
            fastener_bso_surface(face_id,face);
            fastener_bso_surface(hole_id,cylinder);
            AxHardwareSite local={0};
            ProError error=ax_hw_site(&target->panel,cylinder,face,mm,&local);
            if(error)return error;
            if(!local.bore||fabs((ad_dot(local.contact,bore->axis)-end)*mm)>0.002||
               fabs((local.cylinder.radius-bore->radius)*mm)>0.001)continue;
            if(hole->face_id&&hole->face_id!=face_id)return PRO_TK_E_FOUND;
            if((error=ax_hw_world(&target->panel,&local)))return error;
            hole->hole_id=hole_id;
            hole->face_id=face_id;
            selected=local;
        }
    }
    if(!hole->face_id)return PRO_TK_E_NOT_FOUND;

    const double mid=(bore->low+bore->high)/2.0;
    const double axis_sign=ad_dot(selected.cylinder.direction,selected.plane.direction)<0?1.0:-1.0;
    for(int i=0;i<3;i++){
        hole->center[i]=(bore->centerline[i]+mid*bore->axis[i])*mm;
        hole->contact[i]=selected.contact[i]*mm;
        hole->normal[i]=selected.plane.direction[i];
        hole->axis[i]=selected.cylinder.direction[i]*axis_sign;
        if(!isfinite(hole->center[i])||!isfinite(hole->contact[i]))return PRO_TK_BAD_CONTEXT;
    }
    hole->depth=depth;
    hole->diameter=2*bore->radius*mm;
    return isfinite(hole->diameter)?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}

/* Keep ordinal numbers stable when changing sides. An entry with no face on
   the requested side is displayed as unavailable instead of shifting IDs. */
static ProError group_pressfit_geometry_side(ProMdl assembly,int requested_panel,int side,
    GroupPressfitGeometry *result,PartText *out){
    if(!assembly||requested_panel<0||side<-1||!result)return PRO_TK_BAD_INPUTS;
    memset(result,0,sizeof(*result));
    ProError error=group_pressfit_panel(assembly,requested_panel,result,out);
    if(error)return error;

    const OrganizeView view={.model=result->panel.model,.mm=result->mm};
    DeliveryBores *scan=NULL;
    error=org_bores(&view,&scan);
    if(!error&&scan->limited)error=PRO_TK_OUT_OF_RANGE;
    BsoFaces faces={.model=result->panel.model,.mm=result->mm,.radius=-1,.read_planes=1};
    if(!error)error=fastener_bso_model_faces(&faces);

    AxGeometry planes[_countof(faces.planes)];
    for(int i=0;!error&&i<faces.plane_count;i++){
        ProGeomitem item;
        error=ProModelitemInit(result->panel.model,faces.planes[i],PRO_SURFACE,&item);
        if(!error)error=ax_geometry(&item,&planes[i]);
    }
    int no_opening=0,no_side=0,valid=0;
    for(int i=0;!error&&i<scan->count;i++){
        const DeliveryBore *bore=&scan->bores[i];
        if(bore->classification!=2)continue;
        GroupPressfitHole baseline={0};
        error=group_pressfit_bore_opening(result,bore,&faces,planes,0,&baseline);
        if(error==PRO_TK_E_FOUND||error==PRO_TK_E_NOT_FOUND){no_opening++;error=PRO_TK_NO_ERROR;continue;}
        if(error)break;
        GroupPressfitHole opening=baseline;
        if(side){
            memset(&opening,0,sizeof(opening));
            error=group_pressfit_bore_opening(result,bore,&faces,planes,side,&opening);
            if(error==PRO_TK_E_FOUND||error==PRO_TK_E_NOT_FOUND){
                no_side++;
                opening=baseline;
                opening.face_id=0;
                error=PRO_TK_NO_ERROR;
            }
        }
        if(error)break;
        opening.order_id=baseline.hole_id;
        if(opening.face_id)valid++;
        result->holes[result->count++]=opening;
    }
    free(scan);
    if(!error&&!valid)error=PRO_TK_E_NOT_FOUND;
    if(error){
        if(out)part_text_add(out,L"该钣金实例没有读到能唯一核对指定安装侧的圆形通孔，或孔位读取不完整（Creo %d）。\n",error);
        return error;
    }
    if(no_opening&&out)part_text_add(out,L"另有 %d 个孔的安装面无法唯一核对，未给这些孔编号。\n",no_opening);
    if(no_side&&out)part_text_add(out,L"%d 个孔不连到指定安装侧，保留原编号并显示为该侧不可用。\n",no_side);
    qsort(result->holes,(size_t)result->count,sizeof(result->holes[0]),group_pressfit_hole_order);
    return PRO_TK_NO_ERROR;
}

static ProError group_pressfit_geometry(ProMdl assembly,int requested_panel,GroupPressfitGeometry *result,PartText *out){
    return group_pressfit_geometry_side(assembly,requested_panel,0,result,out);
}

static ProError group_pressfit_bso_site(const GroupPressfitGeometry *geometry,const GroupPressfitHole *hole,BsoSite *site){
    if(!hole->face_id||!hole->hole_id)return PRO_TK_BAD_INPUTS;
    AxComponent panel=geometry->panel;
    wchar_t bore[32],face[32];
    fastener_bso_surface(hole->hole_id,bore);
    fastener_bso_surface(hole->face_id,face);
    ProError error=ax_hw_site(&panel,bore,face,geometry->mm,&site->geometry);
    if(!error)error=ax_hw_world(&panel,&site->geometry);
    if(error)return error;
    const AxHardwareSite *actual=&site->geometry;
    double start,end;
    ax_hw_axial_span(actual,actual->contact,actual->plane.direction,geometry->mm,&start,&end);
    if(!actual->bore||fabs(start+hole->depth)>0.002||fabs(end)>0.001||
       fabs(actual->cylinder.radius*2*geometry->mm-hole->diameter)>0.001||
       ad_dot(actual->plane.direction,hole->normal)<1-1e-7)return PRO_TK_BAD_CONTEXT;
    for(int i=0;i<3;i++)if(fabs(actual->contact[i]*geometry->mm-hole->contact[i])>0.002)return PRO_TK_BAD_CONTEXT;
    site->panel=panel;
    site->hole_id=hole->hole_id;
    site->face_id=hole->face_id;
    return PRO_TK_NO_ERROR;
}

