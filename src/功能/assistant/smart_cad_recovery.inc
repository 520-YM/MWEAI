/* The CAD step's measurements are taken directly from the bound Creo model. */
typedef enum {
    CAD_TRACK_NONE, CAD_TRACK_SKETCH, CAD_TRACK_SHEET_SKETCH,
    CAD_TRACK_FIRST_WALL, CAD_TRACK_SHEET_CUT, CAD_TRACK_SOLID_EXTRUDE
} SmartCadTrack;

typedef struct {
    SmartCadTrack track;
    SmartPartSignature signature;
    int bodies,has_volume,has_outline,has_thickness;
    double volume,thickness;
    ProOutline outline;
} SmartCadEvidence;

static SmartCadTrack smart_cad_track(const PartCommand *cmd){
    if(cmd->verb!=PART_FOUNDATION)return CAD_TRACK_NONE;
    if(!wcscmp(cmd->arg[0],L"SKETCH_CREATE")||!wcscmp(cmd->arg[0],L"SKETCH_ADD"))return CAD_TRACK_SKETCH;
    if(!wcscmp(cmd->arg[0],L"SMT_SKETCH"))return CAD_TRACK_SHEET_SKETCH;
    if(!wcscmp(cmd->arg[0],L"SMT_FIRST_WALL"))return CAD_TRACK_FIRST_WALL;
    if(!wcscmp(cmd->arg[0],L"SMT_CUT"))return CAD_TRACK_SHEET_CUT;
    if(!wcscmp(cmd->arg[0],L"SOLID_EXTRUDE"))return CAD_TRACK_SOLID_EXTRUDE;
    return CAD_TRACK_NONE;
}

static ProError smart_cad_read_geometry(ProMdl model,ProMdlsubtype subtype,SmartCadEvidence *out){
    ProError error=sd_body_count(model,&out->bodies);
    if(error)return error;
    ProMassProperty mass;
    out->has_volume=!qa_geometry_mass(model,&mass);
    out->volume=out->has_volume?mass.volume:0;
    out->has_outline=out->has_volume&&!ProSolidOutlineGet((ProSolid)model,out->outline);
    if(subtype==PROMDLSTYPE_PART_SHEETMETAL&&out->has_volume){
        ProDimension thickness;
        error=ProSmtPartThicknessGet((ProPart)model,&thickness);
        if(!error)error=ProDimensionValueGet(&thickness,&out->thickness);
        if(!error&&(!isfinite(out->thickness)||out->thickness<=0))error=PRO_TK_BAD_CONTEXT;
        if(error)return error;
        out->has_thickness=1;
    }
    return PRO_TK_NO_ERROR;
}

static SmartCadEvidence smart_cad_capture(const PartCommand *cmd,const PartSnapshot *snapshot){
    SmartCadEvidence before={0};
    const SmartCadTrack track=smart_cad_track(cmd);
    if(!track||snapshot->type!=PRO_MDL_PART||snapshot->in_assembly||
       !snapshot->model||snapshot->model!=snapshot->window_model)return before;
    ProMdl bound=NULL;
    if(part_check_binding(cmd,snapshot,&bound)||smart_cad_read_geometry(bound,snapshot->subtype,&before))return before;
    before.signature=smart_part_signature(bound);
    if(!before.signature.error)before.track=track;
    return before;
}

static ProError smart_cad_target(const PartCommand *cmd,const PartSnapshot *snapshot,ProMdl *model){
    ProMdlName name;
    ProPath directory;
    ProMdlsubtype subtype;
    ProError error=ProMdlCurrentGet(model);
    if(!error&&(*model!=snapshot->model||*model!=snapshot->window_model))return PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlMdlnameGet(*model,name);
    if(!error&&(_wcsicmp(name,snapshot->name)||_wcsicmp(name,cmd->arg[1])))return PRO_TK_BAD_CONTEXT;
    if(!error)error=ProDirectoryCurrentGet(directory);
    if(!error&&_wcsicmp(directory,snapshot->directory))return PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlSubtypeGet(*model,&subtype);
    if(!error&&subtype!=snapshot->subtype)return PRO_TK_BAD_CONTEXT;
    return error?error:part_health(*model);
}

static int smart_cad_recovered(const PartCommand *cmd,const PartSnapshot *snapshot,
    const SmartCadEvidence *before,ProError native_error){
    if(!before->track)return 0;

    const int verified_rollback=
        (before->track==CAD_TRACK_SKETCH&&fd_sketch_rolled_back_model==snapshot->model&&
         fd_sketch_rolled_back_error==native_error)||
        ((before->track==CAD_TRACK_SHEET_SKETCH||before->track==CAD_TRACK_FIRST_WALL||
          before->track==CAD_TRACK_SHEET_CUT)&&sh_cad_rolled_back_model==snapshot->model&&
          sh_cad_rolled_back_kind==sh_kind(cmd->arg[0])&&sh_cad_rolled_back_error==native_error)||
        (before->track==CAD_TRACK_SOLID_EXTRUDE&&sd_cad_rolled_back_model==snapshot->model&&
         sd_cad_rolled_back_error==native_error);
    ProMdl model=NULL;
    if(!verified_rollback&&part_check_binding(cmd,snapshot,&model))return 0;
    if(smart_cad_target(cmd,snapshot,&model))return 0;
    SmartPartSignature after=smart_part_signature(model);
    if(after.error||after.hash!=before->signature.hash)return 0;
    SmartCadEvidence actual={0};
    if(smart_cad_read_geometry(model,snapshot->subtype,&actual)||actual.bodies!=before->bodies||
       actual.has_volume!=before->has_volume||!part_close(actual.volume,before->volume)||
       actual.has_outline!=before->has_outline||actual.has_thickness!=before->has_thickness||
       !part_close(actual.thickness,before->thickness))return 0;
    for(int side=0;before->has_outline&&side<2;side++)for(int axis=0;axis<3;axis++)
        if(!part_close(actual.outline[side][axis],before->outline[side][axis]))return 0;
    return 1;
}

typedef struct {
    ProVector normal;
    int count;
    PartText *out;
} SmartCadOrientation;

static ProError smart_cad_offer_perpendicular(ProFeature *feature,ProError status,ProAppData data){
    SmartCadOrientation *offer=data;
    if(status||offer->count==3)return PRO_TK_NO_ERROR;
    ProFeatStatus state;
    ProFeattype type;
    ProGeomitem geometry;
    ProVector point,normal;
    if(ProFeatureTypeGet(feature,&type)||type!=PRO_FEAT_DATUM||
       ProFeatureStatusGet(feature,&state)||state!=PRO_FEAT_ACTIVE||
       fd_feature_geom(feature,PRO_SURFACE,&geometry)||fd_plane_data(&geometry,point,normal)||
       fabs(ad_dot(normal,offer->normal))>1e-7)return PRO_TK_NO_ERROR;
    ProName name=L"";
    ProModelitemNameGet(feature,name);
    part_text_add(offer->out,L"\n可核对的垂直定向面 %ls SURFACE:%d。",name,geometry.id);
    offer->count++;
    return PRO_TK_NO_ERROR;
}


static void smart_cad_retry_references(const PartCommand *cmd,const PartSnapshot *snapshot,
    const SmartCadEvidence *before,PartText *out){
    part_text_add(out,L"\n已核对原活动零件及工作目录，已有特征、驱动尺寸及实测实体状态均保持。"
        L"请按原要求补做 %ls，保留已经核对成功的特征。以下参照来自当前模型。",cmd->arg[2]);
    if(before->track==CAD_TRACK_SKETCH||before->track==CAD_TRACK_SHEET_SKETCH){
        if(!wcscmp(cmd->arg[0],L"SKETCH_ADD"))return;
        SmartCadOrientation offered={.out=out};
        int plane_read=0;
        for(int i=3;i<=4;i++){
            FdRef reference={0};
            ProVector point,normal;
            ProError error=fd_ref_resolve(snapshot->model,cmd->arg[i],PRO_SURFACE,&reference);
            if(!error)error=fd_plane_data(&reference.item,point,normal);
            if(!error&&i==3){memcpy(offered.normal,normal,sizeof(offered.normal));plane_read=1;}
            if(!error)part_text_add(out,L"\n%ls 对应 SURFACE:%d，实测法向 (%.6g, %.6g, %.6g)。",
                cmd->arg[i],reference.item.id,normal[0],normal[1],normal[2]);
            else part_text_add(out,L"\n当前模型未能读出 %ls 的平面参照（Creo %d）。",cmd->arg[i],error);
            fd_ref_free(&reference);
        }
        if(plane_read)ProSolidFeatVisit((ProSolid)snapshot->model,smart_cad_offer_perpendicular,NULL,&offered);
    }else{
        const wchar_t *sketch=cmd->arg[3];
        if(before->track!=CAD_TRACK_SOLID_EXTRUDE)sketch+=7;
        FdRef reference={0};
        ProSectionShape shape;
        ProError error=sd_sketch_ref(snapshot->model,sketch,&reference,&shape);
        if(!error)part_text_add(out,L"\n%ls 是可读的闭合草绘，特征 ID:%d。",sketch,reference.owner_feature);
        else part_text_add(out,L"\n%ls 未能通过闭合草绘读取（Creo %d）。",sketch,error);
        fd_ref_free(&reference);
    }
}

static int smart_cad_explicit_cut_direction(const wchar_t *text){
    static const wchar_t *const terms[]={
        L"向上",L"向下",L"向左",L"向右",L"从正面",L"从背面",L"从反面",L"从外侧",L"从内侧",
        L"从上方",L"从下方",L"向外",L"向内",L"正方向",L"负方向",L"反方向",L"正向",
        L"反向",L"负向",L"POS",L"NEG",L"pos",L"neg",L"同方向",L"不要换方向",L"保持方向"
    };
    return smart_has_text(text,terms,_countof(terms));
}

static int smart_cad_cut_alternative(const PartCommand *cmd,const PartSnapshot *snapshot,ProError error,
    const SmartCadEvidence *before,PartCommand *alternative){
    if(before->track!=CAD_TRACK_SHEET_CUT||!sh_cut_rolled_back_no_material||
       sh_cut_rolled_back_model!=snapshot->model||sh_cut_rolled_back_error!=error||
       smart_cad_explicit_cut_direction(smart_goal))return 0;
    FdRef sketch={0};
    ProSectionShape shape;
    ProError checked=sd_sketch_ref(snapshot->model,cmd->arg[3]+7,&sketch,&shape);
    fd_ref_free(&sketch);
    if(checked)return 0;
    *alternative=*cmd;
    wcscpy_s(alternative->arg[4],PART_TOKEN_MAX,!wcscmp(cmd->arg[4],L"POS")?L"NEG":L"POS");
    return !smart_protocol_seen(alternative);
}

static ProError smart_cad_verify_result(const PartCommand *cmd,const PartSnapshot *snapshot,
    const SmartCadEvidence *before,PartText *out){
    if(!before->track)return PRO_TK_NO_ERROR;
    ProMdl model=NULL;
    ProFeature feature;
    ProFeatStatus state;
    ProFeattype type;
    ProError error=smart_cad_target(cmd,snapshot,&model);
    if(!error)error=ux_named_feature(model,cmd->arg[2],&feature);
    if(!error)error=ProFeatureStatusGet(&feature,&state);
    if(!error)error=ProFeatureTypeGet(&feature,&type);
    const ProFeattype intended=before->track==CAD_TRACK_SKETCH||before->track==CAD_TRACK_SHEET_SKETCH?PRO_FEAT_CURVE:
        before->track==CAD_TRACK_FIRST_WALL?PRO_FEAT_DATUM_SURF:before->track==CAD_TRACK_SHEET_CUT?PRO_FEAT_CUT_SMT:
        !wcscmp(cmd->arg[4],L"CUT")?PRO_FEAT_CUT:PRO_FEAT_PROTRUSION;
    if(!error&&(state!=PRO_FEAT_ACTIVE||type!=intended))error=PRO_TK_INCOMPLETE;
    if(error){
        part_text_add(out,L"\n失败原因：Creo 返回完成，实际没有读回请求的启用特征 %ls 及其正确原生类型。请核对现有模型。",
            cmd->arg[2]);
        return error;
    }
    SmartCadEvidence after={0};
    error=smart_cad_read_geometry(model,snapshot->subtype,&after);
    if(error){
        part_text_add(out,L"\n失败原因：已读到特征 %ls（ID:%d），实体、板厚或体积还未能读回。",cmd->arg[2],feature.id);
        return error;
    }
    const int sketch=before->track==CAD_TRACK_SKETCH||before->track==CAD_TRACK_SHEET_SKETCH;
    const int cut=before->track==CAD_TRACK_SHEET_CUT||
        (before->track==CAD_TRACK_SOLID_EXTRUDE&&!wcscmp(cmd->arg[4],L"CUT"));
    const int has_cut_material=cut&&before->has_volume&&after.volume<before->volume;
    const double mm=snapshot->mm_per_native;
    const double cube=mm*mm*mm;
    if(sketch){
        if(before->bodies!=after.bodies||before->has_volume!=after.has_volume||!part_close(before->volume,after.volume))
            error=PRO_TK_INCOMPLETE;
    }else if(!after.has_volume||after.bodies<1||(cut&&!has_cut_material)||
        (!cut&&after.volume<=before->volume))error=PRO_TK_INCOMPLETE;
    if(!error&&before->track==CAD_TRACK_FIRST_WALL&&(!after.has_thickness||
        !part_close(after.thickness*mm,as_arg(cmd,4))))error=PRO_TK_INCOMPLETE;
    if(!error&&before->track==CAD_TRACK_SHEET_CUT&&(!after.has_thickness||
        !part_close(after.thickness,before->thickness)))error=PRO_TK_INCOMPLETE;
    if(error){
        part_text_add(out,L"\n失败原因：已有活动特征 ID:%d。实际实体数 %d，体积 %.8g → %.8g mm³",
            feature.id,after.bodies,before->volume*cube,after.volume*cube);
        if(after.has_thickness)part_text_add(out,L"，板厚 %.8g mm",after.thickness*mm);
        part_text_add(out,L"，结果未通过本步材料变化或板厚校验。请核对当前特征。");
        return error;
    }
    part_text_add(out,L"\n原生回读：%ls（ID:%d）已启用，模型重生成状态通过",cmd->arg[2],feature.id);
    if(!sketch)part_text_add(out,L"，%d 个实体，体积 %.8g → %.8g mm³",after.bodies,before->volume*cube,after.volume*cube);
    if(after.has_thickness)part_text_add(out,L"，实际板厚 %.8g mm",after.thickness*mm);
    if(!sketch&&after.has_outline)part_text_add(out,L"，实测 XYZ 外形 %.8g × %.8g × %.8g mm",
        (after.outline[1][0]-after.outline[0][0])*mm,(after.outline[1][1]-after.outline[0][1])*mm,
        (after.outline[1][2]-after.outline[0][2])*mm);
    part_text_add(out,L"。");
    return PRO_TK_NO_ERROR;
}
