#include "reference_request.inc"
#include "reference_geometry.inc"

static ProError reference_load(const wchar_t *directory,const wchar_t *input,ProMdlType type,
                               ProMdl *model,PartText *out){
    AxSessionSelector selector={0};
    ProPath full,origin;
    wchar_t actual[MAX_PATH];
    ProError error=local_control_open_path(directory,input,full,&selector);
    if(!error&&selector.type!=type)error=PRO_TK_INVALID_TYPE;
    const ProMdlfileType filetype=type==PRO_MDL_PART?PRO_MDLFILE_PART:PRO_MDLFILE_ASSEMBLY;
    if(!error){
        error=ProMdlnameInit(selector.name,filetype,model);
        if(error==PRO_TK_E_NOT_FOUND)error=ProMdlFiletypeLoad(full,filetype,PRO_B_FALSE,model);
        if(!error&&(!*model||ProMdlOriginGet(*model,origin)||!or_real_path(origin,0,actual)||
                    _wcsicmp(actual,full)))error=PRO_TK_E_IN_USE;
    }
    if(error)part_text_add(out,L"未能读取指定模型版本 %ls（Creo %d）。\n",input,error);
    return error;
}

#include "reference_skeleton.inc"

static ProError reference_outline(ProMdl model,const ImitationShapeMetrics *base,
                                  const ReferenceDesign *plan,PartText *out){
    ImitationShapeMetrics actual;
    ProError error=imitation_shape_read(model,&actual);
    if(error)return error;
    double extent[3];
    for(int axis=0;axis<3;axis++){
        const double expected=axis==plan->axis?plan->size:base->bounds[1][axis]-base->bounds[0][axis];
        extent[axis]=actual.bounds[1][axis]-actual.bounds[0][axis];
        if(fabs(extent[axis]-expected)>IMITATION_SIZE_TOL){
            part_text_add(out,L"%lc 外廓实测 %.8g mm，要求 %.8g mm。\n",L'X'+axis,extent[axis],expected);
            return PRO_TK_INCOMPLETE;
        }
    }
    if(actual.bodies!=base->bodies||actual.bodies!=1)return PRO_TK_INCOMPLETE;
    part_text_add(out,L"外廓回读 %.8g × %.8g × %.8g mm，单实体。\n",extent[0],extent[1],extent[2]);
    return PRO_TK_NO_ERROR;
}

static ProError reference_replay_preflight(ProMdl source,PartText *out){
    int external=0;
    ProError error=replay_source_dependencies(source,&external);
    if(error)return error;
    ReplayPreview preview={0};
    error=ProSolidFeatVisit((ProSolid)source,replay_preview_feature,NULL,&preview);
    const ProError cleanup=replay_pattern_source_close(&preview.patterns);
    if(!error)error=cleanup;
    if(!error&&preview.first_block_step){
        part_text_add(out,L"第 %d 道原特征 %ls（ID %d）所用定义当前无法按原树重建。\n",
            preview.first_block_step,preview.first_block_operation,preview.first_block_id);
        error=PRO_TK_UNSUPPORTED;
    }
    return error;
}
static ProError reference_workflow(const LocalControlContext *context,ReferenceDesign plan,PartText *out){
    ProMdl source=NULL,part=NULL,assembly=NULL,placement_source=NULL;
    ProMdlsubtype subtype=0;
    ProWVerstamp before=NULL,after=NULL;
    DeliveryOutput scratch={0};
    ImitationSourceInfo info={0};
    ImitationShapeMetrics base;
    ImitationFlatPlan *outline=NULL;
    ProMdlName name,assembly_name;
    const char *native_stage=NULL;
    const wchar_t *stage=L"核对当前工作目录和新图号";
    int steps=0,built=0,renamed=0,paired=0,placed=0,pair_saved=0,target_saved=0;
    ProAsmcomp occurrence={0};
    const int previous_background=model_background_mode;
    ProError error=PRO_TK_BAD_CONTEXT;
    if(!storage_directory_on_drive(context->directory,L'E')||
       (plan.instance&&(context->model!=context->owner||context->type!=PRO_MDL_ASSEMBLY)))
        goto finish;
    if((error=default_part_name_available(plan.number,1)))goto finish;

    stage=L"读取原件及定位参照";
    if((error=reference_load(context->directory,plan.source,PRO_MDL_PART,&source,out))||
       (error=imitation_read_source(source,&info))||
       (error=ProMdlSubtypeGet(source,&subtype))||
       (error=imitation_shape_read(source,&base))||
       (error=ProMdlVerstampGet(source,&before)))goto finish;
    if(!plan.redraw&&info.imports){error=PRO_TK_UNSUPPORTED;goto finish;}
    if(!plan.redraw&&(error=reference_replay_preflight(source,out)))goto finish;
    if((plan.flat||plan.redraw)&&subtype!=PROMDLSTYPE_PART_SHEETMETAL){
        error=PRO_TK_INVALID_TYPE;
        goto finish;
    }
    if(plan.instance&&(error=reference_load(context->directory,plan.parent,PRO_MDL_ASSEMBLY,&placement_source,out)))
        goto finish;
    if(plan.instance){
        AxComponent source_occurrence;
        wchar_t parent_frame[PART_TOKEN_MAX],part_frame[PART_TOKEN_MAX];
        AxStudyRule placement_rule={0};
        error=reference_source_placement(placement_source,plan.instance,&source_occurrence,&placement_rule,parent_frame,part_frame);
        if(!error&&source_occurrence.model!=source)error=PRO_TK_BAD_CONTEXT;
        if(error){
            part_text_add(out,L"指定实例必须指向当前参考零件的确切版本。\n");
            goto finish;
        }
    }
    if((error=reference_skeleton_size(context,&plan,out)))goto finish;
    LocalControlContext readback;
    if((error=local_control_collect(&readback)))goto finish;
    if(!local_control_same_context(context,&readback)){error=PRO_TK_BAD_CONTEXT;goto finish;}
    part_text_add(out,L"参考 %ls，新图号 %ls。\n",plan.source,plan.number);

    stage=L"从空模板创建可编辑零件";
    bwtl_live_emit(L"progress",1,0,stage);
    model_background_mode=1;
    if((error=fab_scratch(&scratch))||(error=dv_stage_enter(&scratch)))goto finish;
    if(plan.redraw){
        outline=calloc(1,sizeof(*outline));
        if(!outline){error=PRO_TK_OUT_OF_MEMORY;goto finish;}
        ReferenceFlat draft={.flat=outline,.resize_axis=plan.axis};
        error=imitation_flat_identify(source,&base,outline);
        if(!error&&plan.axis>=0){
            const double source_size=base.bounds[1][plan.axis]-base.bounds[0][plan.axis];
            draft.shift=(plan.size-source_size)*0.5;
            error=reference_flat_resize(&draft);
            if(error==PRO_TK_BAD_INPUTS)
                part_text_add(out,L"新规格让开口贴近板边或可能互相接触，请调整目标尺寸。\n");
        }
        if(!error)error=model_create_empty(2,name,&native_stage);
        if(!error)error=ProMdlnameInit(name,PRO_MDL_PART,&part);
        if(!error)error=reference_draw_sheet(part,outline,out);
        if(!error)error=imitation_material_replay(source,part,subtype,scratch.stage,out);
        if(!error)error=reference_material_labels(source,part,out);
        if(error)goto finish;
        part_text_add(out,L"从实测轮廓新画平板首壁，板厚由原件实测。\n");
    }else{
        imitation_warn_wall(source);
        ReplayFailure failure={0};
        if((error=imitation_replay_new_impl(source,subtype,&part,&steps,&failure,NULL,0,out))||
           (error=imitation_material_replay(source,part,subtype,scratch.stage,out))||
           (error=replay_regenerate_model_relations(part))||
           (error=imitation_sheet_compare(source,part,out))||
           (error=imitation_bend_table_replay(source,part,subtype,out))||
           (error=replay_verify_order_impl(source,part,1,NULL,out)))goto finish;
        if((error=imitation_shape_verify_mode(source,part,&base,0,out)))goto finish;
        if(plan.axis>=0&&(error=reference_resize_replay(part,plan.axis,plan.size,out)))goto finish;
    }
    if((error=part_health(part))||(error=dv_no_external_dependencies(part))||
       (error=reference_outline(part,&base,&plan,out)))goto finish;
    if(plan.redraw&&plan.axis<0&&(error=imitation_shape_verify_mode(source,part,&base,0,out)))goto finish;
    built=1;

    stage=L"试改驱动尺寸并恢复再生";
    bwtl_live_emit(L"progress",2,0,stage);
    if((error=imitation_edit_trial(part,out))||
       (error=reference_outline(part,&base,&plan,out)))goto finish;
    if(outline&&plan.axis>=0&&(error=reference_verify_contours(part,outline,out)))goto finish;
    if(plan.flat){
        stage=L"钣金展开验收";
        if((error=reference_dxf(part,scratch.stage,out)))goto finish;
    }

    stage=L"确认源件并创建同号装配";
    bwtl_live_emit(L"progress",3,0,stage);
    if((error=ProMdlVerstampGet(source,&after)))goto finish;
    if(!ProVerstampEqual(before,after)){error=PRO_TK_E_IN_USE;goto finish;}
    if((error=ProMdlnameRename(part,plan.number)))goto finish;
    renamed=1;
    if((error=ProDirectoryChange(scratch.parent)))goto finish;
    scratch.in_stage=0;
    if((error=local_control_collect(&readback)))goto finish;
    if(!local_control_same_context(context,&readback)){error=PRO_TK_BAD_CONTEXT;goto finish;}
    if((error=model_create_empty_named(3,plan.number,assembly_name,&native_stage))||
       (error=ProMdlnameInit(assembly_name,PRO_MDL_ASSEMBLY,&assembly)))goto finish;
    double mm;
    if((error=qa_mm_per_unit(assembly,&mm)))goto finish;
    ProMatrix identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    if((error=ax_place_model(assembly,part,identity,AX_PLACE_DEFAULT,mm,&occurrence,out)))goto finish;
    paired=occurrence.id;
    part_text_add(out,L"新建 %ls.ASM 和 %ls.PRT；组件 ID %d，Creo 默认定位已回读。\n",
        assembly_name,plan.number,paired);
    if(plan.instance){
        stage=L"回读目标总装及定位参照";
        bwtl_live_emit(L"progress",4,0,stage);
        if((error=local_control_collect(&readback)))goto finish;
        if(!local_control_same_context(context,&readback)){error=PRO_TK_BAD_CONTEXT;goto finish;}
        if((error=reference_place_component(context->model,placement_source,plan.instance,part,&placed,out)))
            goto finish;
    }
    if(plan.save){
        stage=L"保存并核对磁盘文件";
        bwtl_live_emit(L"progress",5,0,stage);
        wchar_t saved[1000]=L"";
        error=bwtl_save_guarded(assembly,saved,_countof(saved));
        part_text_add(out,L"%ls\n",saved);
        if(error)goto finish;
        pair_saved=1;
        if(placed){
            saved[0]=0;
            error=bwtl_save_guarded(context->model,saved,_countof(saved));
            part_text_add(out,L"%ls\n",saved);
            if(error)goto finish;
            target_saved=1;
        }
        ProPath part_file,asm_file;
        if((error=ax_native_source(context->directory,plan.number,PRO_MDL_PART,part_file))||
           (error=ax_native_source(context->directory,assembly_name,PRO_MDL_ASSEMBLY,asm_file)))goto finish;
        part_text_add(out,L"partFile=%ls\nassemblyFile=%ls\n",part_file,asm_file);
    }
    if(!plan.background){
        stage=L"打开并激活新零件";
        if((error=dv_display_new(part)))goto finish;
    }else{
        LocalControlContext final_context;
        const ProError observed=local_control_collect(&final_context);
        if(!observed&&context->window==final_context.window&&context->owner==final_context.owner)
            part_text_add(out,L"新件已载入，回读确认原活动模型和窗口保持一致。\n");
        else part_text_add(out,L"新件已载入。后台流程未调用激活窗口，Creo 的活动模型以状态回读为准。\n");
    }
    stage=NULL;
finish:
    model_background_mode=previous_background;
    free(outline);
    if(before)ProVerstampFree(&before);
    if(after)ProVerstampFree(&after);
    if(error&&part&&!built){
        const ProError erased=ProMdlErase(part);
        if(erased)part_text_add(out,L"暂存零件仍留在会话，清理返回 Creo %d。\n",erased);
    }else if(error&&part){
        ProMdlName partial=L"";
        if(!ProMdlMdlnameGet(part,partial))
            part_text_add(out,L"已绘制的新件 %ls.PRT 留在会话；验收以各步回执为准。\n",partial);
    }
    if(error&&!paired&&assembly)(void)ProMdlErase(assembly);
    error=fab_discard(&scratch,error,out);
    if(error)part_text_add(out,L"卡在%ls（Creo %d）。\n",stage?stage:L"工作目录恢复",error);
    if(paired){
        part_text_add(out,L"result=%ls\npart=%ls.PRT\nassembly=%ls.ASM\ncomponentId=%d\ntargetComponentId=%d\npairSaved=%d\ntargetSaved=%d\n",
            error?L"PARTIAL":L"OK",renamed?plan.number:L"",assembly_name,paired,placed,pair_saved,target_saved);
    }
    return error;
}

static ProError reference_dispatch(const LocalControlContext *context,const wchar_t *payload,wchar_t *text,size_t size){
    ReferenceDesign request;
    if(!reference_request(payload,&request))return local_control_error(PRO_TK_BAD_INPUTS,text,size,reference_help);
    PartText out={text,size,0};
    bwtl_live_refresh_model();
    bwtl_live_begin(0);
    bwtl_live_emit(L"progress",0,0,L"开始参考建模与装配。");
    const ProError result=reference_workflow(context,request,&out);
    local_control_invalidate_context();
    return result;
}

static int reference_design_chat(const wchar_t *text){
    static const wchar_t prefix[]=L"参考建模与装配";
    const size_t length=_countof(prefix)-1;
    if(wcsncmp(text,prefix,length))return 0;
    const wchar_t *input=text+length;
    if(*input&&!iswspace(*input)&&*input!=L'：'&&*input!=L':')return 0;
    if(native_busy||smart_active||ai_process||imitation_pending){
        append(L"请等待当前任务完成。");
        return 1;
    }
    while(iswspace(*input)||*input==L'：'||*input==L':')input++;
    wchar_t *result=calloc(12000,sizeof(*result));
    if(!result){append(L"未能为参考建模分配回执空间。");return 1;}
    LocalControlContext context;
    const ProError status=local_control_collect(&context);
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    if(status)swprintf_s(result,12000,L"未能核对当前 Creo 会话（Creo %d）。",status);
    else{
        const ProError executed=reference_dispatch(&context,input,result,12000);
        if(bwtl_live_task)bwtl_live_emit(L"receipt",1,executed,result);
    }
    append_card(L"AI",result);
    save_turn(L"助手",result);
    free(result);
    return 1;
}
