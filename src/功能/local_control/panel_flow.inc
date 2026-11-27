#include "panel_six_sites.inc"

static ProError panel_multi_remember(ProMdl root,ProMdl sheet,int childId,
    const PanelPressfitPlan *plan,PartText *out);

/* One native request owns the newly created model handles and the complete receipt. */
typedef enum {
    PANEL_FLOW_IDLE, PANEL_FLOW_CREATE, PANEL_FLOW_RETURN, PANEL_FLOW_RESULT
} PanelFlowPhase;

static struct {
    PanelFlowPhase phase;
    PanelPressfitPlan request;
    AxReturnLink link;
    ProWVerstamp child_stamp,parent_stamp;
    unsigned long long child_features,parent_features;
    ProError result;
    int bores[6],component;
    wchar_t report[9800];
} panel_flow;

static int panel_flow_busy(void){
    return panel_flow.phase==PANEL_FLOW_CREATE||panel_flow.phase==PANEL_FLOW_RETURN;
}

static void panel_flow_cancel(void){
    if(panel_flow.phase==PANEL_FLOW_CREATE)default_part.ready=NULL;
    if(panel_flow.phase==PANEL_FLOW_RETURN)ax_return_cancel();
    if(panel_flow.child_stamp)ProVerstampFree(&panel_flow.child_stamp);
    if(panel_flow.parent_stamp)ProVerstampFree(&panel_flow.parent_stamp);
    memset(&panel_flow,0,sizeof(panel_flow));
}

static int panel_flow_take_reply(ProError *result){
    if(panel_flow.phase!=PANEL_FLOW_CREATE&&panel_flow.phase!=PANEL_FLOW_RESULT)return 0;
    *result=panel_flow.result;
    panel_flow_cancel();
    return 1;
}

static void panel_flow_pair_error(ProError error){
    if(panel_flow.phase==PANEL_FLOW_CREATE)panel_flow.result=error;
}

static void panel_flow_finish(ProError error,const wchar_t *stage){
    wchar_t final[10500]=L"";
    PartText out={final,_countof(final),0};
    part_text_add(&out,L"%ls",panel_flow.report);
    if(error&&stage){
        part_text_add(&out,L"\n%ls未完成（Creo %d）。已完成的模型留在此 Creo 会话。",stage,error);
        smart_failures++;
    }
    if(panel_flow.phase==PANEL_FLOW_RETURN)ax_return_cancel();
    panel_flow.phase=PANEL_FLOW_RESULT;
    panel_flow.result=error;
    if(panel_flow.link.root){
        if(error)chat_task_record_target(panel_flow.link.root,L"同图号钣金压铆流程",error,final,
            ax_recovery_uncertain||cm_recovery_uncertain||sh_recovery_uncertain||smart_unsafe_receipt(final));
        else chat_task_record_pair(panel_flow.link.root,panel_flow.link.child,panel_flow.component,final);
    }
    smart_record_tool(final);
    smart_reply(final);
}

static int panel_flow_stop(void){
    if(!panel_flow_busy())return 0;
    if(panel_flow.phase==PANEL_FLOW_CREATE){
        default_part.ready=NULL;
        default_part_end_transition();
    }
    PartText out={panel_flow.report,_countof(panel_flow.report),0};
    part_text_add(&out,L"\n同图号钣金压铆流程已停止。已经完成的步骤保留，后续操作不再执行。");
    panel_flow_finish(PRO_TK_USER_ABORT,NULL);
    return 1;
}

static ProError panel_flow_bound(ProMdl model,int window){
    ProPath directory;
    ProError error=default_part_active(model,window,NULL);
    if(!error)error=ProDirectoryCurrentGet(directory);
    if(!error&&(_wcsicmp(directory,panel_flow.link.directory)||!storage_output_directory(directory)))error=PRO_TK_BAD_CONTEXT;
    return error;
}

static int panel_flow_same_stamp(ProMdl model,ProWVerstamp expected){
    ProWVerstamp actual=NULL;
    const ProError error=ProMdlVerstampGet(model,&actual);
    const int same=!error&&actual&&expected&&ProVerstampEqual(actual,expected);
    if(actual)ProVerstampFree(&actual);
    return same;
}

static ProError panel_flow_feature(ProFeature *feature,ProError status,ProAppData data){
    if(status)return status;
    ProFeattype type;
    ProError error=ProFeatureTypeGet(feature,&type);
    if(error)return error;
    unsigned long long *value=data;
    *value=(*value^(unsigned int)feature->id)*1099511628211ULL;
    *value=(*value^(unsigned int)type)*1099511628211ULL;
    return PRO_TK_NO_ERROR;
}

static ProError panel_flow_features(ProMdl model,unsigned long long *value){
    *value=14695981039346656037ULL;
    return ProSolidFeatVisit((ProSolid)model,panel_flow_feature,NULL,value);
}

static ProError panel_flow_model_step(const wchar_t *operation,PartText *out){
    ProError error=panel_flow_bound(panel_flow.link.child,panel_flow.link.child_window);
    PartCommand cmd={0};
    if(!error&&part_command_parse(operation,&cmd)!=1)error=PRO_TK_BAD_INPUTS;
    PartSnapshot *snapshot=NULL;
    if(!error){snapshot=calloc(1,sizeof(*snapshot));if(!snapshot)error=PRO_TK_OUT_OF_MEMORY;}
    if(!error)error=part_snapshot_take_basic(panel_flow.link.child,snapshot);
    wchar_t receipt[2400]=L"";
    if(!error)error=part_execute(&cmd,snapshot,receipt,_countof(receipt));
    if(snapshot){part_snapshot_clear(snapshot);free(snapshot);}
    if(receipt[0])part_text_add(out,L"%ls\n",receipt);
    if(!error){smart_steps++;smart_progress++;}
    return error;
}

static ProError panel_flow_verify_sheet(PartText *out){
    return panel_six_sites(panel_flow.link.child,&panel_flow.request,panel_flow.bores,out);
}

static ProError panel_flow_make(ProMdl part,ProMdl parent,PartText *out){
    if(panel_flow.phase!=PANEL_FLOW_CREATE)return PRO_TK_BAD_CONTEXT;
    const PanelPressfitPlan *r=&panel_flow.request;
    ProMdlName part_name,assembly_name;
    ProMdlsubtype subtype;
    AxReturnLink *link=&panel_flow.link;
    ProError error=ProMdlMdlnameGet(part,part_name);
    if(!error)error=ProMdlMdlnameGet(parent,assembly_name);
    if(!error)error=ProMdlSubtypeGet(part,&subtype);
    if(!error&&(_wcsicmp(assembly_name,r->drawing)||_wcsicmp(part_name,r->drawing)||
       subtype!=PROMDLSTYPE_PART_SHEETMETAL||default_part.component_id<1))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=default_part_active(part,-1,&link->child_window);
    if(!error)error=ProMdlWindowGet(parent,&link->root_window);
    if(!error&&link->child_window==link->root_window)error=PRO_TK_BAD_CONTEXT;
    if(error)return error;
    link->child=part;
    link->root=parent;
    panel_flow.component=default_part.component_id;
    swprintf_s(link->occurrence,_countof(link->occurrence),L"ID:%d",panel_flow.component);
    if(!ax_return_link_live(link))return PRO_TK_BAD_CONTEXT;
    double centers[6][2];
    const int holes=panel_six_layout(r,centers);
    if(!holes)return PRO_TK_BAD_INPUTS;
    wchar_t body[256];
    swprintf_s(body,_countof(body),L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 %.12g %.12g",
        part_name,r->length,r->width);
    error=panel_flow_model_step(body,out);
    if(!error){
        swprintf_s(body,_countof(body),L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE %.12g POS",part_name,r->thickness);
        error=panel_flow_model_step(body,out);
    }
    for(int i=0;!error&&i<holes;i++){
        wchar_t sketch[PRO_NAME_SIZE]=L"HOLE_SK",cut[PRO_NAME_SIZE]=L"HOLE_CUT";
        if(holes>1){
            swprintf_s(sketch,_countof(sketch),L"HOLE_%d_SK",i+1);
            swprintf_s(cut,_countof(cut),L"HOLE_%d_CUT",i+1);
        }
        swprintf_s(body,_countof(body),L"CREO_OP SMT_SKETCH %ls %ls PLANE:FRONT PLANE:RIGHT CIRCLE %.12g %.12g %.12g",
            part_name,sketch,centers[i][0],centers[i][1],r->hole/2.0);
        error=panel_flow_model_step(body,out);
        if(!error){
            swprintf_s(body,_countof(body),L"CREO_OP SMT_CUT %ls %ls SKETCH:%ls NEG",part_name,cut,sketch);
            error=panel_flow_model_step(body,out);
        }
    }
    if(!error)error=panel_flow_verify_sheet(out);
    if(!error)error=ProMdlVerstampGet(part,&panel_flow.child_stamp);
    if(!error)error=ProMdlVerstampGet(parent,&panel_flow.parent_stamp);
    if(!error)error=panel_flow_features(part,&panel_flow.child_features);
    if(!error)error=panel_flow_features(parent,&panel_flow.parent_features);
    return error;
}

static int panel_flow_after_pair(ProMdl part,ProMdl parent,ProError error,const wchar_t *receipt){
    if(panel_flow.phase!=PANEL_FLOW_CREATE)return 0;
    if(error||part!=panel_flow.link.child||parent!=panel_flow.link.root){
        panel_flow.result=error?error:PRO_TK_BAD_CONTEXT;
        panel_flow.phase=PANEL_FLOW_RESULT;
        return 0;
    }
    wcsncpy_s(panel_flow.report,_countof(panel_flow.report),receipt,_TRUNCATE);
    PartText out={panel_flow.report,_countof(panel_flow.report),0};
    error=panel_flow_bound(part,panel_flow.link.child_window);
    if(!error&&!ax_return_link_live(&panel_flow.link))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_return_parent(panel_flow.request.drawing,&out);
    if(!error){
        panel_flow.phase=PANEL_FLOW_RETURN;
        error=ProMacroLoad(L"~ Command \x0060" L"BWTLPanelFlowConfirm\x0060;");
    }
    if(error)panel_flow_finish(error,L"切回同图号装配");
    return 1;
}

static ProError panel_flow_pressfit(const wchar_t *spec,const int *bores,int count,int face,int preview,PartText *out){
    wchar_t command[400]=L"",result[7600]=L"";
    PartText wire={command,_countof(command),0};
    part_text_add(&wire,L"%ls%ls %ls SURFACE:%d",preview?L"PREVIEW ":L"",spec,panel_flow.link.occurrence,face);
    for(int i=0;i<count;i++)part_text_add(&wire,L" SURFACE:%d",bores[i]);
    if(wire.truncated)return PRO_TK_BAD_INPUTS;
    const ProError error=local_control_pressfit(panel_flow.link.root,command,result,_countof(result));
    if(result[0]&&(!preview||error))part_text_add(out,L"%ls\n",result);
    return error;
}

static ProError panel_flow_tree(int expect){
    const AxReturnLink *link=&panel_flow.link;
    AxScan *scan=NULL;
    ProError error=ax_scan(link->root,1,&scan);
    if(!error&&(scan->count!=expect||scan->parts!=expect||scan->suppressed))error=PRO_TK_BAD_CONTEXT;
    int panel=0;
    if(!error)
        for(int i=0;i<scan->count;i++){
            const AxOccurrence *item=&scan->occurrences[i];
            if(item->depth!=1||item->state!=PRO_FEAT_ACTIVE)error=PRO_TK_BAD_CONTEXT;
            if(item->model==link->child)panel++;
        }
    if(!error&&panel!=1)error=PRO_TK_BAD_CONTEXT;
    free(scan);
    return error;
}

/* Activating a parent can regenerate it after the child was edited. Verify
   the actual occurrences, template pose, and changed models' native shape
   and feature identities before using any refs in the activated window. */
static ProError panel_flow_rebind(void){
    const AxReturnLink *link=&panel_flow.link;
    ProMdlName root_name,child_name;
    const int same_child=panel_flow_same_stamp(link->child,panel_flow.child_stamp);
    const int same_parent=panel_flow_same_stamp(link->root,panel_flow.parent_stamp);
    trace("Panel flow child version unchanged",same_child);
    trace("Panel flow parent version unchanged",same_parent);
    ProError error=ProMdlMdlnameGet(link->root,root_name);
    if(!error)error=ProMdlMdlnameGet(link->child,child_name);
    if(!error&&(_wcsicmp(root_name,panel_flow.request.drawing)||_wcsicmp(child_name,panel_flow.request.drawing)))
        error=PRO_TK_BAD_CONTEXT;
    if(!error)error=panel_flow_tree(1);
    if(!error)error=part_health(link->root);
    ProAsmcomp feature;
    ProMatrix pose,identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    double scale;
    if(!error)error=ax_component_feature(link->root,panel_flow.component,&feature);
    if(!error)error=ax_default_verify(&feature);
    if(!error)error=qa_mm_per_unit(link->root,&scale);
    if(!error)error=ax_actual_pose(link->root,&feature,pose);
    if(!error&&!ax_same_pose(pose,identity,scale))error=PRO_TK_BAD_CONTEXT;
    unsigned long long actual;
    if(!error&&!same_parent){
        error=panel_flow_features(link->root,&actual);
        if(!error&&actual!=panel_flow.parent_features)error=PRO_TK_BAD_CONTEXT;
    }
    if(!error&&!same_child){
        error=panel_flow_features(link->child,&actual);
        if(!error&&actual!=panel_flow.child_features)error=PRO_TK_BAD_CONTEXT;
    }
    return error?error:panel_flow_verify_sheet(NULL);
}

static int panel_flow_confirm(uiCmdCmdId id,uiCmdValue *value,void *data){
    if(panel_flow.phase!=PANEL_FLOW_RETURN)return 0;
    if(!smart_active){panel_flow_cancel();return 0;}
    const AxReturnLink *link=&panel_flow.link;
    wchar_t result[7600]=L"";
    PartText out={panel_flow.report,_countof(panel_flow.report),0};
    const wchar_t *stage=L"验证原装配窗口和组件版本";
    ProError error=ax_return_readback(result,_countof(result));
    if(result[0])part_text_add(&out,L"%ls\n",result);
    if(!error)error=panel_flow_bound(link->root,link->root_window);
    trace("Panel flow original active window and directory",error);
    if(!error&&!ax_return_link_live(link))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=panel_flow_rebind();
    trace("Panel flow verified returned assembly and board",error);
    const PanelPressfitPlan *plan=&panel_flow.request;
    const int holes=plan->six?6:1;
    int face;
    if(!error){
        stage=L"核对唯一的实际安装面";
        error=panel_six_install_face(link->root,link->occurrence,panel_flow.bores,holes,&face);
    }
    if(!error&&plan->six){
        stage=L"预核对四角组和中间组的两个实测规格";
        error=panel_flow_pressfit(plan->spec,panel_flow.bores,4,face,1,&out);
        if(!error)error=panel_flow_pressfit(plan->center_spec,panel_flow.bores+4,2,face,1,&out);
        if(!error)part_text_add(&out,L"已分组核对四角 4 孔和中间 2 孔的库件规格、安装面和孔位。\n");
    }
    if(!error){
        stage=plan->six?L"装入并核对四角组 4 颗压铆件":L"装入并核对精确压铆规格";
        const int first=plan->six?4:1;
        error=panel_flow_pressfit(plan->spec,panel_flow.bores,first,face,0,&out);
        if(!error)error=panel_flow_tree(1+first);
    }
    if(!error&&plan->six){
        stage=L"装入并核对中间组 2 颗压铆件";
        error=panel_flow_pressfit(plan->center_spec,panel_flow.bores+4,2,face,0,&out);
        if(!error)error=panel_flow_tree(7);
    }
    if(!error&&panel_flow.request.save){
        stage=L"保存同图号装配和钣金件";
        result[0]=0;
        error=panel_flow_bound(link->root,link->root_window);
        if(!error)error=bwtl_save_guarded(link->root,result,_countof(result));
        if(result[0])part_text_add(&out,L"%ls\n",result);
        if(!error){
            ProPath board;
            error=ax_native_source(link->directory,panel_flow.request.drawing,PRO_MDL_PART,board);
            if(!error&&!dv_file_nonempty(board))error=PRO_TK_CANT_OPEN;
            if(!error)part_text_add(&out,L"已核对项目钣金件 %ls。\n",board);
        }
    }else if(!error)part_text_add(&out,L"装配和钣金件仍在 Creo 会话中，项目中的压铆件副本已核对。");
    if(!error&&plan->six){
        stage=L"登记已验证的六孔装配分组";
        error=panel_multi_remember(link->root,link->child,panel_flow.component,plan,&out);
    }
    if(!error)smart_progress+=2;
    panel_flow_finish(error,stage);
    return 0;
}

static ProError panel_flow_begin(const wchar_t *text,const PanelPressfitPlan *request,wchar_t *out,size_t capacity){
    if(!text||!request||!out||!capacity)return PRO_TK_BAD_INPUTS;
    out[0]=0;
    if(panel_flow.phase!=PANEL_FLOW_IDLE||native_busy||smart_active||ai_process){
        wcsncpy_s(out,capacity,L"当前 Creo 会话有任务正在处理。",_TRUNCATE);
        return PRO_TK_E_IN_USE;
    }
    const size_t length=wcsnlen(request->drawing,PRO_NAME_SIZE);
    if(length<=3||length>=PRO_NAME_SIZE||!company_pair_revision(request->drawing+length-3)||
       !model_assembly_name_valid(request->drawing)||!dv_safe_leaf(request->drawing))return PRO_TK_INVALID_NAME;
    double centers[6][2];
    if(!panel_six_layout(request,centers)){
        wcsncpy_s(out,capacity,L"孔位几何超出板边或孔间重叠，本次未创建。",_TRUNCATE);
        return PRO_TK_BAD_INPUTS;
    }
    ProPath directory;
    ProError error=ProDirectoryCurrentGet(directory);
    if(!error&&!storage_output_directory(directory))error=PRO_TK_CANT_WRITE;
    if(!error)error=default_part_name_available(request->drawing,1);
    if(error){
        if(error==PRO_TK_E_FOUND)swprintf_s(out,capacity,L"当前会话或项目目录已有图号 %ls，本次未创建。",request->drawing);
        else wcsncpy_s(out,capacity,L"无法核对 E 盘当前工作目录或图号可用性，本次未创建。",_TRUNCATE);
        return error;
    }
    ModelingPartIntent model={.scope=MODELING_PART_NEW,.kind=2,.require_company=1};
    wcscpy_s(model.name,_countof(model.name),request->drawing);
    panel_flow.request=*request;
    panel_flow.phase=PANEL_FLOW_CREATE;
    panel_flow.result=PRO_TK_BAD_CONTEXT;
    wcscpy_s(panel_flow.link.directory,_countof(panel_flow.link.directory),directory);
    if(!default_part_begin_request(text,&model,panel_flow_make)){
        panel_flow_cancel();
        wcsncpy_s(out,capacity,L"未能启动同图号装配与钣金件创建。",_TRUNCATE);
        return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}
