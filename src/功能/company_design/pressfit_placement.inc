/* Press-fit placement uses measured library geometry and native Creo constraints. */
static ProError fastener_pressfit_ring(ProMdl model,int face_id,const AxHardwareSite *site,
    double radius,double mm){
    ProSurface surface;
    ProError error=ProSurfaceInit((ProSolid)model,face_id,&surface);
    if(error)return error;
    ProVector basis[3];
    ax_hw_basis(site->plane.direction,basis);
    for(int i=0;i<16;i++){
        ProVector point;
        ProUvParam uv;
        ProUvStatus inside;
        const double angle=2*AS_PI*i/16;
        for(int j=0;j<3;j++)point[j]=site->contact[j]+radius/mm*
            (cos(angle)*basis[0][j]+sin(angle)*basis[1][j]);
        error=ProSurfaceParamEval((ProSolid)model,surface,point,uv);
        if(!error)error=ProSurfaceUvpntVerify((ProSolid)model,surface,uv,&inside);
        if(error)return error;
        if(inside!=PRO_UV_INSIDE)return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}

typedef struct {
    AxComponent owner;
    int shafts[128],seats[128],shaft_count,seat_count;
} PressFitFaces;

static ProError fastener_pressfit_face_visit(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    PressFitFaces *faces=data;
    ProSrftype type;
    int id;
    ProError error=ProSurfaceTypeGet(surface,&type);
    if(!error)error=ProSurfaceIdGet(surface,&id);
    if(error)return error;
    if(type==PRO_SRF_CYL){
        if(faces->shaft_count>=_countof(faces->shafts))return PRO_TK_OUT_OF_RANGE;
        faces->shafts[faces->shaft_count++]=id;
    }else if(type==PRO_SRF_PLANE){
        if(faces->seat_count>=_countof(faces->seats))return PRO_TK_OUT_OF_RANGE;
        faces->seats[faces->seat_count++]=id;
    }
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_check_fitting(AxComponent *part,double mm,int shaft_id,int seat_id,
    BsoFitting *result){
    wchar_t shaft[32],seat[32];
    fastener_bso_surface(shaft_id,shaft);
    fastener_bso_surface(seat_id,seat);
    AxHardwareSite geometry={0};
    ProError error=ax_hw_site(part,shaft,seat,mm,&geometry);
    if(error)return error;
    double start,end;
    ax_hw_axial_span(&geometry,geometry.contact,geometry.plane.direction,mm,&start,&end);
    if(geometry.bore||fabs(start)>0.001||end<=0.001)return PRO_TK_BAD_CONTEXT;
    const double radius=geometry.cylinder.radius*mm;
    error=fastener_pressfit_ring(part->model,seat_id,&geometry,radius+0.01,mm);
    if(error)return error;
    *result=(BsoFitting){.geometry=geometry,.shaft=shaft_id,.seat=seat_id,.model=part->model};
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_fitting_for_hole(ProMdl part,double mm,int shaft,int seat,
    double hole_mm,BsoFitting *result,PartText *out){
    if(!part||!result||!isfinite(mm)||mm<=0||shaft<0||seat<0||(!shaft!=!seat)||!isfinite(hole_mm)||hole_mm<0)
        return PRO_TK_BAD_INPUTS;
    double actual_units;
    ProError error=qa_mm_per_unit(part,&actual_units);
    if(error||!part_close(mm,actual_units))return error?error:PRO_TK_BAD_CONTEXT;
    PressFitFaces faces={.owner={.model=part}};
    if(shaft){
        error=fastener_pressfit_check_fitting(&faces.owner,mm,shaft,seat,result);
        if(error)part_text_add(out,L"指定的定位圆柱 SURFACE:%d 和承压面 SURFACE:%d 未通过朝向、圆柱端部或承压环核对（Creo %d）。\n",shaft,seat,error);
        return error;
    }
    ProMdlName name=L"";
    if(!ProMdlMdlnameGet(part,name)&&fastener_bso_model_family(name)&&!fastener_bso_fitting(part,mm,result))
        return PRO_TK_NO_ERROR;
    error=ProSolidSurfaceVisit((ProSolid)part,fastener_pressfit_face_visit,NULL,&faces);
    if(error)return error;
    int matches=0,exact_matches=0;
    BsoFitting exact={0};
    wchar_t candidate_text[1200]=L"";
    PartText details={candidate_text,_countof(candidate_text),0};
    for(int i=0;i<faces.shaft_count;i++)for(int j=0;j<faces.seat_count;j++){
        BsoFitting candidate={0};
        error=fastener_pressfit_check_fitting(&faces.owner,mm,faces.shafts[i],faces.seats[j],&candidate);
        if(error)continue;
        if(matches&&result->seat==candidate.seat&&
           ad_near(result->geometry.contact,candidate.geometry.contact,0.001/mm)&&
           fabs(ad_dot(result->geometry.cylinder.direction,candidate.geometry.cylinder.direction))>1-1e-7&&
           part_close(result->geometry.cylinder.radius,candidate.geometry.cylinder.radius))continue;
        if(hole_mm&&fabs(2*candidate.geometry.cylinder.radius*mm-hole_mm)<=0.001&&
           (!exact_matches||exact.seat!=candidate.seat||
            !ad_near(exact.geometry.contact,candidate.geometry.contact,0.001/mm))){
            if(!exact_matches)exact=candidate;
            exact_matches++;
        }
        if(matches<12)part_text_add(&details,L"定位圆柱 SURFACE:%d，承压面 SURFACE:%d，定位直径 Ø%.6g mm。\n",
            candidate.shaft,candidate.seat,2*candidate.geometry.cylinder.radius*mm);
        if(!matches)*result=candidate;
        matches++;
    }
    if(exact_matches==1){*result=exact;return PRO_TK_NO_ERROR;}
    if(matches==1)return PRO_TK_NO_ERROR;
    if(matches)part_text_add(out,L"找到 %d 组可用的压入圆柱与承压面。请指定库件的定位圆柱及承压面。\n%ls",matches,candidate_text);
    else part_text_add(out,L"没有读到与承压端面相接、朝安装孔伸出的外圆柱。请核对库件的定位结构。\n");
    return matches?PRO_TK_E_FOUND:PRO_TK_E_NOT_FOUND;
}

static ProError fastener_pressfit_fitting(ProMdl part,double mm,int shaft,int seat,BsoFitting *result,PartText *out){
    return fastener_pressfit_fitting_for_hole(part,mm,shaft,seat,0,result,out);
}

static ProError fastener_pressfit_target(AxComponent *panel,int hole_id,int face_id,double mm,
    ProMdl fitting_model,const BsoFitting *fitting,double requested,double interference,BsoSite *site,
    const wchar_t **stage){
    wchar_t hole[32],face[32];
    fastener_bso_surface(hole_id,hole);
    fastener_bso_surface(face_id,face);
    const double shaft_diameter=2*fitting->geometry.cylinder.radius*mm;
    AxHardwareSite geometry={0};
    *stage=L"读取实际孔壁与安装面";
    ProError error=ax_hw_site(panel,hole,face,mm,&geometry);
    if(error)return error;
    const double diameter=2*geometry.cylinder.radius*mm;
    double start,end;
    ax_hw_axial_span(&geometry,geometry.contact,geometry.plane.direction,mm,&start,&end);
    *stage=L"核对孔口及安装面的朝向";
    if(!geometry.bore||start>=-0.001||fabs(end)>0.001)return PRO_TK_BAD_CONTEXT;
    *stage=L"核对板孔与定位径";
    if(diameter<shaft_diameter-interference-0.001||fabs(diameter-requested)>0.001)return PRO_TK_BAD_INPUTS;
    FdTxn txn={.model=panel->model,.scale=1/mm};
    ShopMouth mouth={0};
    *stage=L"核对孔口与安装面是否共用圆边";
    if((error=mp_mouth(&txn,face,hole,&mouth)))return error;
    const double radius=0.5*fmax(diameter,shaft_diameter);
    const double offsets[]={0.01,0.025,0.05,0.10,0.20,0.30};
    for(int i=0;i<_countof(offsets);i++){
        const double seating_radius=radius+offsets[i];
        *stage=L"核对紧固件承压面能否覆盖孔口";
        error=fastener_pressfit_ring(fitting_model,fitting->seat,&fitting->geometry,seating_radius,mm);
        if(error==PRO_TK_BAD_CONTEXT)continue;
        if(error)return error;
        *stage=L"核对板面孔口四周是否有连续承压区";
        error=fastener_pressfit_ring(panel->model,face_id,&geometry,seating_radius,mm);
        if(error!=PRO_TK_BAD_CONTEXT)break;
    }
    if(error)return error;
    *stage=L"读取安装组件在装配中的位姿";
    error=ax_hw_world(panel,&geometry);
    if(error)return error;
    *site=(BsoSite){.panel=*panel,.geometry=geometry,.hole_id=hole_id,.face_id=face_id};
    return PRO_TK_NO_ERROR;
}

static ProError fastener_pressfit_read_selection(ProMdl assembly,double mm,AxComponent *panel,int *face,
    int holes[12],int *count){
    ProSelection *selection=NULL;
    int selected=0;
    wchar_t key[AX_PATH_DEPTH*12+4]=L"";
    ProError error=ProSelbufferSelectionsGet(&selection);
    if(!error)error=ProArraySizeGet((ProArray)selection,&selected);
    if(!error&&!selected)error=PRO_TK_E_NOT_FOUND;
    if(!error&&selected>13)error=PRO_TK_BAD_INPUTS;
    for(int i=0;!error&&i<selected;i++){
        ProModelitem item;
        ProAsmcomppath path;
        AxGeometry shape;
        wchar_t occurrence[AX_PATH_DEPTH*12+4];
        error=ProSelectionModelitemGet(selection[i],&item);
        if(!error)error=ProSelectionAsmcomppathGet(selection[i],&path);
        if(!error&&(item.type!=PRO_SURFACE||path.owner!=(ProSolid)assembly||path.table_num<1||
            path.table_num>AX_PATH_DEPTH||!ax_path_label(path.comp_id_table,path.table_num,occurrence,_countof(occurrence))))
            error=PRO_TK_BAD_CONTEXT;
        if(error)break;
        if(!key[0]){
            wcscpy_s(key,_countof(key),occurrence);
            error=ax_hw_occurrence(assembly,key,mm,panel);
        }
        if(!error&&(item.owner!=panel->model||wcscmp(key,occurrence)))error=PRO_TK_BAD_CONTEXT;
        if(!error)error=ax_geometry(&item,&shape);
        if(error)break;
        if(shape.type==AX_GEOM_PLANE&&!*face)*face=item.id;
        else if(shape.type==AX_GEOM_CYLINDER&&*count<12)holes[(*count)++]=item.id;
        else error=PRO_TK_BAD_INPUTS;
    }
    if(selection)ProSelectionarrayFree(selection);
    return error;
}

static ProError fastener_pressfit_selection(ProMdl assembly,double mm,AxComponent *panel,int *face,
    int holes[12],int *count,PartText *out){
    const ProError error=fastener_pressfit_read_selection(assembly,mm,panel,face,holes,count);
    if(error||!*face){
        part_text_add(out,L"请选中同一块板的一张安装面，再按住 Ctrl 选 1–12 个孔壁；也可只选安装面并指定孔数。\n");
        return error?error:PRO_TK_BAD_INPUTS;
    }
    return PRO_TK_NO_ERROR;
}

static double fastener_pressfit_selected_hole(ProMdl assembly,double mm){
    AxComponent panel={0};
    int face=0,count=0,holes[12];
    wchar_t message[256]=L"";
    PartText silent={message,_countof(message),0};
    if(fastener_pressfit_selection(assembly,mm,&panel,&face,holes,&count,&silent))return 0;
    double diameter=0;
    for(int i=0;i<count;i++){
        wchar_t token[32];
        AxGeometry geometry;
        ProVector ends[2];
        int bore;
        fastener_bso_surface(holes[i],token);
        if(ax_hw_cylinder(&panel,token,&geometry,ends,&bore)||!bore)return 0;
        const double actual=2*geometry.radius*mm;
        if(diameter&&fabs(diameter-actual)>0.001)return 0;
        diameter=actual;
    }
    return diameter;
}

static int fastener_pressfit_site_repeat(const BsoSite *site,const BsoSite sites[12],int count,double mm){
    for(int i=0;i<count;i++)if(ad_near(site->geometry.contact,sites[i].geometry.contact,0.001/mm))return 1;
    return 0;
}

static ProError fastener_pressfit_resolve_sites(ProMdl assembly,double mm,int expected,const BsoFitting *fitting,
    double nominal_hole_mm,double interference_mm,BsoSite sites[12],int *count,PartText *out){
    if(!fitting||!count||expected<0||expected>12||!isfinite(mm)||mm<=0||
       !isfinite(nominal_hole_mm)||nominal_hole_mm<0||!isfinite(interference_mm)||interference_mm<0)
        return PRO_TK_BAD_INPUTS;
    ProMdl part=fitting->model;
    if(!part)return PRO_TK_BAD_CONTEXT;
    *count=0;
    ProMdlName name=L"";
    ProMdlMdlnameGet(part,name);
    const double shaft=2*fitting->geometry.cylinder.radius*mm;
    const double requested=nominal_hole_mm?nominal_hole_mm:
        (fastener_bso_model_family(name)&&fabs(shaft-5)<0.001)?5.4:shaft;
    AxComponent panel={0};
    int face=0,n=0,holes[12];
    ProError error=fastener_pressfit_selection(assembly,mm,&panel,&face,holes,&n,out);
    if(error)return error;
    if((!n&&!expected)||(expected&&n&&n!=expected)){
        part_text_add(out,L"已选孔壁 %d 个，指定孔数 %d 个。请选对应孔壁，或只选安装面并指定数量。\n",n,expected);
        return PRO_TK_BAD_INPUTS;
    }
    const wchar_t *stage=L"";
    if(n){
        for(int i=0;i<n;i++){
            error=fastener_pressfit_target(&panel,holes[i],face,mm,part,fitting,requested,interference_mm,&sites[i],&stage);
            if(error){
                part_text_add(out,L"孔位 %d（SURFACE:%d）未通过「%ls」。预期孔径 Ø%.6g mm，定位径 Ø%.6g mm（Creo %d）。\n",
                    i+1,holes[i],stage,requested,shaft,error);
                return error;
            }
            if(fastener_pressfit_site_repeat(&sites[i],sites,i,mm)){
                part_text_add(out,L"两个选面落在同一孔口，请每个孔只选一面。\n");
                return PRO_TK_E_FOUND;
            }
        }
        *count=n;
        return PRO_TK_NO_ERROR;
    }
    BsoFaces candidates={.model=panel.model,.mm=mm,.radius=requested/2};
    if((error=fastener_bso_model_faces(&candidates)))return error;
    int matches=0;
    for(int i=0;i<candidates.hole_count;i++){
        BsoSite site={0};
        error=fastener_pressfit_target(&panel,candidates.holes[i],face,mm,part,fitting,requested,interference_mm,&site,&stage);
        if(error==PRO_TK_BAD_CONTEXT||error==PRO_TK_BAD_INPUTS||error==PRO_TK_INVALID_TYPE)continue;
        if(error)return error;
        if(fastener_pressfit_site_repeat(&site,sites,matches,mm))continue;
        if(matches>=12)return PRO_TK_OUT_OF_RANGE;
        sites[matches++]=site;
    }
    if(matches!=expected){
        part_text_add(out,L"安装面 SURFACE:%d 实测符合 Ø%.6g mm 和承压条件的孔有 %d 个，要求 %d 个。请选定要安装的孔壁。%ls%ls\n",
            face,requested,matches,expected,*stage?L" 最后一次核对：":L"",stage);
        return PRO_TK_BAD_CONTEXT;
    }
    *count=matches;
    return PRO_TK_NO_ERROR;
}

#include "pressfit_journal.inc"
#include "pressfit_checks.inc"
#include "pressfit_batch.inc"
#include "pressfit_method.inc"
#include "pressfit_batch_audit.inc"
