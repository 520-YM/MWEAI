#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static FILE *report;
static ProMdl root,branch,stock;
static ProAsmcomp direct,nested_branch,nested,overlap;
static int failures;

static int record(int pass,const wchar_t *label,ProError code){
    fwprintf(report,L"%ls=%d Creo=%d\n",label,pass,code);
    fflush(report);
    if(!pass)failures++;
    return pass;
}

static int finish(void){
    wchar_t name[] = L"MARKUP_ASSEMBLY_CONTEXT_PASS";
    record(!failures,name,0);
    fclose(report);
    markup_capture_clear();
    ProEngineerEnd();
    return 0;
}

static ProError insert(ProMdl parent,ProMdl component,double x,ProAsmcomp *added){
    ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{x,0,0,1}};
    return ProAsmcompAssemble((ProSolid)parent,component,pose,added);
}

static ProError show_top(ProMdl model){
    ProMdlName name;
    int window;
    ProError error=ProMdlMdlnameGet(model,name);
    if(!error)error=native_activate_model_window(model,name,PRO_ASSEMBLY,&window);
    trace("markup fixture activate assembly",error);
    ProMatrix top={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    if(!error)error=ProViewMatrixSet(model,NULL,top);
    trace("markup fixture top matrix",error);
    if(!error)error=ProViewRefit(model,NULL);
    trace("markup fixture refit",error);
    if(!error)error=ProWindowCurrentGet(&window);
    if(!error)ProWindowBrowserSizeSet(window,0);
    if(!error)ProWindowRepaint(window);
    return error;
}

static MarkupMark mark_point(const MarkupCapture *capture,double px,double py,double pz){
    const double delta[]={px-capture->face_origin[0],py-capture->face_origin[1],pz-capture->face_origin[2]};
    double x=capture->face_anchor[0],y=capture->face_anchor[1];
    for(int a=0;a<2;a++){
        const double along=ad_dot((double *)delta,(double *)capture->face_basis[a])/ad_dot((double *)capture->face_basis[a],(double *)capture->face_basis[a]);
        x+=along*capture->face_pixels[a][0];
        y+=along*capture->face_pixels[a][1];
    }
    return (MarkupMark){MARKUP_ARROW,x-12,y+12,x,y,1};
}

static int verify_branch(uiCmdCmdId id,uiCmdValue *value,void *context){
    ProError error=markup_take(&markup_capture);
    if(!record(!error&&markup_capture.assembly&&markup_capture.image->assembly,L"SUBASSEMBLY_CAPTURE",error))return finish();
    MarkupMark mark=mark_point(&markup_capture,15,13,20);
    MarkupAssemblyPick pick={0};
    error=markup_assembly_pick(&markup_capture,&mark,&pick);
    record(!error&&pick.component.path.table_num==1&&pick.component.path.comp_id_table[0]==nested.id,
        L"SUBASSEMBLY_RELATIVE_COMPONENT_PATH",error);
    return finish();
}

static int verify_root(uiCmdCmdId id,uiCmdValue *value,void *context){
    ProError error=markup_take(&markup_capture);
    if(!record(!error&&markup_capture.assembly&&markup_capture.image->assembly,L"TOP_ASSEMBLY_CAPTURE",error))return finish();
    fwprintf(report,L"IMAGE=%dx%d HOLES=%d MEMBERS=%d\n",markup_capture.image->width,
        markup_capture.image->height,markup_capture.image->count,markup_capture.member_count);
    MarkupRequest request={.ticket=markup_capture.ticket,.count=2,.text=L"测量标注"};
    request.marks[0]=mark_point(&markup_capture,15,13,20);
    request.marks[1]=mark_point(&markup_capture,115,13,20);
    MarkupAssemblyPick first={0},second={0};
    error=markup_assembly_pick(&markup_capture,&request.marks[0],&first);
    record(!error&&first.component.model==stock&&first.component.path.table_num==1&&
        first.component.path.comp_id_table[0]==direct.id&&fabs(first.local_point[0]-15)<1e-4,
        L"DIRECT_FACE_PICK",error);
    error=markup_assembly_pick(&markup_capture,&request.marks[1],&second);
    record(!error&&second.component.model==stock&&second.component.path.table_num==2&&
        second.component.path.comp_id_table[0]==nested_branch.id&&second.component.path.comp_id_table[1]==nested.id&&
        fabs(second.local_point[0]-15)<1e-4&&fabs(second.root_point[0]-115)<1e-4,
        L"REPEATED_STOCK_NESTED_PICK",error);
    wchar_t reply[2000];
    error=markup_assembly_read(&markup_capture,&request,1,reply,_countof(reply));
    fwprintf(report,L"%ls\n",reply);
    record(!error&&wcsstr(reply,L"最小间隙 20 mm")!=NULL,L"NATIVE_GAP_20_MM",error);
    record(wcsstr(reply,L"已高亮")!=NULL,L"HIGHLIGHT",error);
    request.marks[0]=request.marks[1];
    request.marks[1]=mark_point(&markup_capture,238,13,20);
    error=markup_assembly_read(&markup_capture,&request,1,reply,_countof(reply));
    fwprintf(report,L"%ls\n",reply);
    record(!error&&wcsstr(reply,L"存在实体干涉")!=NULL,L"NATIVE_INTERFERENCE",error);
    MarkupHole *bores[2]={0};
    for(int i=0;i<markup_capture.image->count;i++){
        MarkupHole *h=&markup_capture.image->holes[i];
        if(h->occurrence.depth==1&&h->occurrence.ids[0]==direct.id)bores[0]=h;
        if(h->occurrence.depth==2&&h->occurrence.ids[0]==nested_branch.id&&h->occurrence.ids[1]==nested.id)bores[1]=h;
    }
    if(record(bores[0]&&bores[1],L"PER_OCCURRENCE_BORES",0)){
        for(int i=0;i<2;i++){
            MarkupHole *hole=bores[i];
            request.marks[i]=(MarkupMark){MARKUP_ARROW,hole->x-12,hole->y+12,hole->x,hole->y,1};
        }
        error=markup_assembly_read(&markup_capture,&request,1,reply,_countof(reply));
        fwprintf(report,L"%ls\n",reply);
        record(!error&&wcsstr(reply,L"可见孔口中心距 100 mm")&&wcsstr(reply,L"横向偏差 100 mm"),
            L"BORE_CENTERS_AND_ALIGNMENT",error);
        MarkupRequest modify={.ticket=markup_capture.ticket,.count=1,.text=L"这个孔改成 11 mm"};
        modify.marks[0]=request.marks[1];
        const MarkupEdit edit={.mark=0,.measure=MARKUP_DIAMETER,.value=11};
        error=markup_apply_edits(&markup_capture,&modify,&edit,1,reply,_countof(reply));
        fwprintf(report,L"%ls\n",reply);
        record(error==PRO_TK_BAD_CONTEXT&&wcsstr(reply,L"3 个实例")&&
            !markup_bore_read(stock,bores[1],10),L"SHARED_CHILD_WITHOUT_CONSENT_DENIED",error);
        wcscpy_s(modify.text,_countof(modify.text),L"这个孔改成 11 mm，允许修改所有同名实例");
        error=markup_apply_edits(&markup_capture,&modify,&edit,1,reply,_countof(reply));
        fwprintf(report,L"%ls\n",reply);
        ProMdl active=NULL;
        int same_root=!ProMdlCurrentGet(&active)&&active==root;
        record(!error&&!markup_bore_read(stock,bores[1],11)&&same_root&&
            wcsstr(reply,L"3 个实例")!=NULL,L"AUTHORIZED_NESTED_SHARED_CHILD_CHANGED_ROOT_ACTIVE",error);
        if(!error){
            AxComponent same={0};
            error=markup_assembly_component(&markup_capture,&bores[0]->occurrence,&same);
            record(error==PRO_TK_BAD_CONTEXT,L"STALE_CHILD_REJECTED",error);
        }
    }
    markup_capture_clear();
    error=show_top(branch);
    if(!record(!error,L"DISPLAY_SUBASSEMBLY",error))return finish();
    if(ProMacroLoad(L"~ Command \x0060" L"BWTLMarkupContextBranch\x0060;"))return finish();
    return 0;
}

static int prepare(uiCmdCmdId id,uiCmdValue *value,void *context){
    report=_wfopen(L"markup-assembly-context.log",L"wt, ccs=UTF-8");
    if(!report)return ProEngineerEnd(),0;
    model_background_mode=1;
    Fixture refs;
    ProMdlName stock_name;
    ProError error=fixture(&stock,stock_name,&refs);
    if(!error){
        wchar_t line[512];
        swprintf_s(line,_countof(line),L"CREO_OP HOLE %ls MARKUP_BORE SURFACE:%d SURFACE:%d 32 SURFACE:%d 31 10 THROUGH 0",
            stock_name,refs.top,refs.x,refs.y);
        error=expansion_build(stock,line,report);
    }
    if(!error)error=ax_new_subassembly(L"BWTL_MARKUP_TEST_TOP",&root);
    if(!error)error=ax_new_subassembly(L"BWTL_MARKUP_TEST_SUB",&branch);
    if(!error)error=insert(branch,stock,0,&nested);
    if(!error)error=part_regenerate(branch);
    if(!error)error=insert(root,stock,0,&direct);
    if(!error)error=insert(root,branch,100,&nested_branch);
    if(!error)error=insert(root,stock,176,&overlap);
    if(!error)error=part_regenerate(root);
    if(!error)error=show_top(root);
    if(!record(!error,L"REAL_ASSEMBLY_READY",error))return finish();
    if(ProMacroLoad(L"~ Command \x0060" L"BWTLMarkupContextRoot\x0060;"))return finish();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLMarkupContextPrepare",prepare,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLMarkupContextRoot",verify_root,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!error)error=ProCmdActionAdd("BWTLMarkupContextBranch",verify_branch,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLMarkupContextPrepare\x0060;");
}
