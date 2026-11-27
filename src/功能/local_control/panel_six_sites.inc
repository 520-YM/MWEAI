static ProError panel_six_sites(ProMdl panel,const PanelPressfitPlan *plan,int ids[6],PartText *out){
    double centers[6][2],scale;
    const int count=panel_six_layout(plan,centers);
    if(!count)return PRO_TK_BAD_INPUTS;
    memset(ids,0,6*sizeof(*ids));
    ProOutline bounds;
    ProError error=qa_mm_per_unit(panel,&scale);
    if(!error)error=or_outline(panel,bounds);
    if(!error){
        const double dimensions[]={plan->length,plan->width,plan->thickness};
        for(int i=0;i<3;i++){
            if(fabs(bounds[0][i]*scale)>0.002||fabs(bounds[1][i]*scale-dimensions[i])>0.002){
                error=PRO_TK_BAD_CONTEXT;
                break;
            }
        }
    }
    ProMassProperty mass;
    if(!error)error=ProSolidMassPropertyWithDensityGet((ProSolid)panel,NULL,PRO_MP_DENS_USE_ALWAYS,1,&mass);
    if(!error){
        const double expected=(plan->length*plan->width-count*AS_PI*plan->hole*plan->hole/4)*plan->thickness;
        if(fabs(mass.volume*scale*scale*scale-expected)>fmax(0.01,expected*1e-7))error=PRO_TK_BAD_CONTEXT;
    }
    DeliveryBores *scan=NULL;
    OrganizeView view={.model=panel,.mm=error?0:scale};
    if(!error)error=org_bores(&view,&scan);
    if(!error&&(scan->limited||scan->count!=count))error=PRO_TK_BAD_CONTEXT;
    for(int i=0;!error&&i<count;i++){
        const DeliveryBore *bore=&scan->bores[i];
        if(bore->classification!=2||bore->face_count<1||bore->axis[2]<1-1e-8||
           fabs(bore->low*scale)>0.002||fabs(bore->high*scale-plan->thickness)>0.002||
           fabs(bore->radius*2*scale-plan->hole)>0.002){error=PRO_TK_BAD_CONTEXT;break;}
        const double x=bore->centerline[0]*scale,y=bore->centerline[1]*scale;
        int slot=-1;
        for(int j=0;j<count;j++){
            if(fabs(x-centers[j][0])<=0.002&&fabs(y-centers[j][1])<=0.002){
                if(slot>=0){error=PRO_TK_E_FOUND;break;}
                slot=j;
            }
        }
        if(!error){
            if(slot<0||ids[slot]||bore->faces[0]<=0)error=PRO_TK_BAD_CONTEXT;
            else ids[slot]=bore->faces[0];
        }
    }
    free(scan);
    if(out){
        if(error)part_text_add(out,L"未能核对实际板外形及 %d 个指定位置的贯穿圆孔。",count);
        else if(count==1)part_text_add(out,L"钣金板实测 %.8g×%.8g×%.8g mm，X=%.8g、Y=%.8g 处 Ø%.8g mm 圆孔贯穿板厚。\n",
            plan->length,plan->width,plan->thickness,centers[0][0],centers[0][1],plan->hole);
        else{
            part_text_add(out,L"钣金板实测 %.8g×%.8g×%.8g mm，已核对 6 个 Ø%.8g mm 通孔的孔径、孔位和贯穿。\n",
                plan->length,plan->width,plan->thickness,plan->hole);
            part_text_add(out,L"四角孔位 (%.8g,%.8g)、(%.8g,%.8g)、(%.8g,%.8g)、(%.8g,%.8g)，中间孔位 (%.8g,%.8g)、(%.8g,%.8g) mm。\n",
                centers[0][0],centers[0][1],centers[1][0],centers[1][1],centers[2][0],centers[2][1],
                centers[3][0],centers[3][1],centers[4][0],centers[4][1],centers[5][0],centers[5][1]);
        }
    }
    return error;
}

/* Choose the one real lower seating face shared by all requested bores. */
static ProError panel_six_install_face(ProMdl assembly,const wchar_t *panel_path,const int *bores,int count,int *face){
    if(!assembly||!panel_path||!bores||count<1||count>6||!face)return PRO_TK_BAD_INPUTS;
    double mm;
    AxComponent panel={0};
    ProError error=qa_mm_per_unit(assembly,&mm);
    if(!error)error=ax_hw_occurrence(assembly,panel_path,mm,&panel);
    if(error)return error;
    BsoFaces candidates={.model=panel.model,.mm=mm,.read_planes=1};
    error=fastener_bso_model_faces(&candidates);
    if(error)return error;
    *face=0;
    for(int i=0;i<candidates.plane_count;i++){
        const int id=candidates.planes[i];
        wchar_t plane[32];
        fastener_bso_surface(id,plane);
        int complete=1;
        for(int j=0;j<count;j++){
            wchar_t hole[32];
            fastener_bso_surface(bores[j],hole);
            AxHardwareSite actual={0};
            ShopMouth mouth={0};
            if(ax_hw_site(&panel,hole,plane,mm,&actual)||!actual.bore||
               actual.plane.direction[2]>-1+1e-8||fabs(actual.contact[2]*mm)>0.002||
               fastener_pressfit_classify(&panel,id,bores[j],mm,&mouth)!=PF_OPENING_THROUGH){
                complete=0;
                break;
            }
        }
        if(!complete)continue;
        if(*face)return PRO_TK_E_FOUND;
        *face=id;
    }
    return *face?PRO_TK_NO_ERROR:PRO_TK_E_NOT_FOUND;
}
