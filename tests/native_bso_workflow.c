#define user_initialize bwtl_bso_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>
typedef struct {AxComponent owner;int shaft,holes[8],n,front,back;double low,high;} HardwareFaces;
static ProError fixture_faces(ProSurface surface,ProError status,ProAppData data){
    HardwareFaces *faces=data;
    if(status)return status;
    ProGeomitem item;
    AxGeometry shape;
    if(ProSurfaceToGeomitem((ProSolid)faces->owner.model,surface,&item)||ax_geometry(&item,&shape))return 0;
    if(shape.type==AX_GEOM_PLANE){
        if(shape.direction[2]>0.999&&fabs(shape.point[2]-faces->high)<0.001)faces->front=item.id;
        if(shape.direction[2]<-0.999&&fabs(shape.point[2]-faces->low)<0.001)faces->back=item.id;
    }else if(shape.type==AX_GEOM_CYLINDER){
        wchar_t token[40];
        int bore;
        ProVector ends[2];
        swprintf_s(token,_countof(token),L"SURFACE:%d",item.id);
        if(ax_hw_cylinder(&faces->owner,token,&shape,ends,&bore))return 0;
        if(!bore)faces->shaft=item.id;
        else if(faces->n<8){
            for(int i=0;i<faces->n;i++){
                ProSurface old;
                ProGeomitem previous;
                AxGeometry axis;
                if(!ProSurfaceInit((ProSolid)faces->owner.model,faces->holes[i],&old)&&
                   !ProSurfaceToGeomitem((ProSolid)faces->owner.model,old,&previous)&&!ax_geometry(&previous,&axis)&&
                   ax_hw_separation(shape.point,axis.point,axis.direction,1)<0.001)return 0;
            }
            faces->holes[faces->n++]=item.id;
        }
    }
    return 0;
}
static ProError find_fixture_faces(ProMdl model,double high,HardwareFaces *faces){
    faces->owner.model=model;faces->high=high;
    return ProSolidSurfaceVisit((ProSolid)model,fixture_faces,NULL,faces);
}
static int occurrences(ProMdl assembly,ProMdl model,ProAsmcomp *out,int cap){
    AxScan *scan=NULL;
    if(ax_scan(assembly,1,&scan))return -1;
    int n=0;
    for(int i=0;i<scan->count;i++)if(scan->occurrences[i].depth==1&&scan->occurrences[i].model==model){
        if(n>=cap||ax_component_feature(assembly,scan->occurrences[i].ids[0],out+n)){n=-1;break;}
        n++;
    }
    free(scan);
    return n;
}

static ProError add_component(ProMdl assembly,ProMdl child,double x,ProAsmcomp *component){
    ProMatrix pose={{1,0,0,0},{0,1,0,0},{0,0,1,0},{x,0,0,1}};
    return ProAsmcompAssemble((ProSolid)assembly,child,pose,component);
}

static FILE *bso_report;

static ProError bso_select_face(ProAsmcomppath *path,ProMdl owner,int id){
    ProModelitem item;
    ProSelection selection=NULL;
    ProError error=ProModelitemInit(owner,id,PRO_SURFACE,&item);
    if(!error)error=ProSelectionAlloc(path,&item,&selection);
    if(!error)error=ProSelbufferSelectionAdd(selection);
    if(selection)ProSelectionFree(&selection);
    return error;
}

static int bso_check(const char *name,ProError error){
    fprintf(bso_report,"%s=%d\n",name,error==0);
    if(error)fprintf(bso_report,"CREO_STATUS=%d\n",error);
    fflush(bso_report);
    return error!=0;
}

static int bso_flow(uiCmdCmdId id,uiCmdValue *value,void *data){
    int verify=bso_report!=NULL;
    if(!verify)bso_report=fopen("bso-workflow.log","w");
    if(!bso_report){ProEngineerEnd();return 0;}
    ProError error=0;
    static ProMdl root,panel,saved_part;
    static ProPath context_data;
    static int context_test;
    ProMdl source=NULL;
    static ProAsmcomp base;
    ProAsmcomp bolts[6];
    ProMdlName assembly_name=L"",plate_name=L"BSO_FIXTURE_PANEL";
    ProAsmcomppath place;
    ProIdTable panel_path;


    static HardwareFaces faces;
    wchar_t report[PART_TEXT_MAX]=L"",origin[MAX_PATH]=L"",saved[MAX_PATH]=L"";
    const char *stage="new isolated test assembly";
    PartText out={report,_countof(report),0};
    int window;
    static double mm;
#define STEP(label,expr) do{error=(expr);if(bso_check(label,error))goto done;}while(0)
    if(verify)goto select_holes;
    context_test=GetEnvironmentVariableW(L"BWTL_TEST_BSO_CONTEXT",context_data,_countof(context_data))>0;
    STEP("NATURAL_REQUESTS",fastener_bso_request(L"我要bsoM3X6")!=BSO_SAVE||
        fastener_bso_request(L"装配 BSO M3×6 到选中的孔位")!=BSO_PLACE||
        fastener_bso_request(L"查找 BSO_3_5M3X6")!=BSO_FIND||
        fastener_bso_request(L"装配 BSO M3×60")||
        fastener_bso_request(L"不要装配 BSO M3×6")?PRO_TK_BAD_INPUTS:0);
    wcscpy_s(smart_latest_request,_countof(smart_latest_request),L"读取公司库");
    wchar_t sandbox_data[MAX_PATH];
    wcscpy_s(sandbox_data,_countof(sandbox_data),runtime_data);
    ProPath local_fixture;
    if(context_test){ /* Exact library flow runs after active assembly window and face are verified. */ }
    else if(GetEnvironmentVariableW(L"BWTL_TEST_COMPANY_DATA",runtime_data,_countof(runtime_data))){
        error=company_list(L"公司库",L"",0,&out);
        memset(smart_latest_request,0,sizeof(smart_latest_request));
        if(!error)error=fastener_bso_read_exact(6,&source,origin,&out);
        wcscpy_s(runtime_data,_countof(runtime_data),sandbox_data);
        if(bso_check("LIBRARY_ALIAS_AND_EXACT_INSTANCE",error))goto done;
        STEP("PROJECT_COPY",fastener_bso_copy_exact(6,source,&saved_part,saved,&out));
        STEP("FILE_EXISTS",GetFileAttributesW(saved)==INVALID_FILE_ATTRIBUTES?PRO_TK_CANT_OPEN:0);
        ProMdl again=NULL;
        STEP("REUSE_SAME_PROJECT_FILE",fastener_bso_copy_exact(6,source,&again,saved,&out));
        STEP("SAME_PROJECT_MODEL",saved_part!=again?PRO_TK_BAD_CONTEXT:0);
    }else{
        if(!GetEnvironmentVariableW(L"BWTL_TEST_BSO_PART",local_fixture,_countof(local_fixture))){error=PRO_TK_BAD_INPUTS;goto done;}
        STEP("LOAD_PREVIOUSLY_VERIFIED_LOCAL_FASTENER",ProMdlFiletypeLoad(local_fixture,PRO_MDLFILE_PART,PRO_B_FALSE,&saved_part));
    }
    FdTxn target={0};
    QuickPlan plan={.width=120,.depth=90,.thickness=1.5};
    for(int row=0;row<2;row++)for(int col=0;col<3;col++)quick_opening(&plan,Q_HOLE,25+35*col,25+40*row,5.4,5.4);
    STEP("SEED_PANEL",quick_seed(plate_name,&target.model));
    ModelPlan stock={0,plan.width,plan.depth,plan.thickness,0,1};
    STEP("CREATE_PANEL",model_build_internal(&stock,plate_name,&stage,target.model,NULL,NULL));
    double panel_mm=0;
    STEP("PANEL_UNIT",qa_mm_per_unit(target.model,&panel_mm));
    target.scale=1/panel_mm;
    STEP("CREATE_SIX_HOLES",quick_cuts(&target,&plan));
    STEP("REGEN_PANEL",part_regenerate(target.model));
    panel=target.model;
    STEP("FIND_HOLE_WALLS",find_fixture_faces(panel,1.5,&faces));
    STEP("SIX_HOLES_AND_FACE",faces.n!=6||!faces.front?PRO_TK_BAD_CONTEXT:0);

    model_background_mode=0;
    STEP("CREATE_ASSEMBLY",model_create_empty(3,assembly_name,&stage));
    STEP("READ_ASSEMBLY",ProMdlnameInit(assembly_name,PRO_MDL_ASSEMBLY,&root));
    STEP("READ_ASSEMBLY_WINDOW",ProMdlWindowGet(root,&window));
    STEP("SET_CURRENT_WINDOW",ProWindowCurrentSet(window));
    STEP("READ_MM",qa_mm_per_unit(root,&mm));
    STEP("ADD_PANEL",add_component(root,panel,0,&base));
    STEP("REGENERATE",part_regenerate(root));
    STEP("WAIT_FOR_ACTIVE_WINDOW",ProMacroLoad(L"~ Command `BWTLBSOFlow`;"));
    return 0;

select_holes:
    panel_path[0]=base.id;
    STEP("ASSEMBLY_PATH",ProAsmcomppathInit((ProSolid)root,panel_path,1,&place));
    STEP("CLEAR_SELECTION",ProSelbufferClear());
    if(context_test){
        PartCommand generic={0};
        wchar_t command[256];
        ProMdlName current_name;
        STEP("ACTIVE_ASSEMBLY_NAME",ProMdlMdlnameGet(root,current_name));
        swprintf_s(command,_countof(command),L"CREO_OP ASM_INSERT %ls BSO_3_5M3XX DEFAULT",current_name);
        STEP("GENERIC_PARENT_PROTOCOL_PARSES",part_command_parse(command,&generic)!=1?PRO_TK_BAD_INPUTS:0);
        STEP("GENERIC_PARENT_REJECTED",ax_insert(root,&generic,mm,&out)!=PRO_TK_BAD_INPUTS||
            !wcsstr(report,L"族表母件")?PRO_TK_BAD_CONTEXT:0);
        AxScan *before=NULL;
        STEP("READ_BEFORE_CONTEXTUAL_CHAT",ax_scan(root,1,&before));
        int n=before->count;
        free(before);
        STEP("NO_GENERIC_ADDED",n!=1?PRO_TK_BAD_CONTEXT:0);
        STEP("PRESSING_FACE_FOR_CHAT",bso_select_face(&place,panel,faces.front));
        const wchar_t *latest=L"那你帮我装配M3X5的吧";
        FastenerBsoRequest parsed={0};
        conversation[0]=0;
        STEP("UNSCOPED_M3X5_IS_NOT_BSO",fastener_bso_parse(latest,&parsed,1)?PRO_TK_BAD_CONTEXT:0);
        wcscpy_s(conversation,_countof(conversation),L"\n用户：装配 BSO M3×6 到六个孔。\n");
        STEP("CONTEXTUAL_M3X5_AND_SIX_HOLES",!fastener_bso_parse(latest,&parsed,1)||
            parsed.length!=5||parsed.expected!=6||parsed.action!=BSO_PLACE?PRO_TK_BAD_CONTEXT:0);
        ProPath previous_data;
        wcscpy_s(previous_data,_countof(previous_data),runtime_data);
        wcscpy_s(runtime_data,_countof(runtime_data),context_data);
        const int handled=fastener_bso_chat(latest);
        wcscpy_s(runtime_data,_countof(runtime_data),previous_data);
        STEP("CONTEXTUAL_CHAT_WAS_HANDLED",!handled?PRO_TK_BAD_CONTEXT:0);
        wcscpy_s(report,_countof(report),history);
        STEP("SIX_OF_SIX_VERIFIED_RECEIPT",!wcsstr(history,L"实际核对 6/6 孔")?PRO_TK_BAD_CONTEXT:0);
        ProMdlName exact=L"BSO_3_5M3X5_BWTL";
        STEP("READ_CREATED_M3X5_PART",ProMdlnameInit(exact,PRO_MDL_PART,&saved_part));
        STEP("EXACT_FIVE_MM_PART_GEOMETRY",fastener_bso_measure_length(saved_part,5,&out));
        STEP("ACTUAL_SIX_M3X5_OCCURRENCES",occurrences(root,saved_part,bolts,6)!=6?PRO_TK_BAD_CONTEXT:0);
        goto done;
    }

    BsoSite sites[12];
    int count=0;
    STEP("UNSELECTED_INSTALL_DIRECTION_IS_AMBIGUOUS",
        !fastener_bso_resolve_sites(root,mm,6,sites,&count,&out)?PRO_TK_BAD_CONTEXT:0);
    STEP("PRESSING_FACE",bso_select_face(&place,panel,faces.front));
    STEP("WRONG_QUANTITY_IS_REJECTED",
        !fastener_bso_resolve_sites(root,mm,5,sites,&count,&out)?PRO_TK_BAD_CONTEXT:0);
    STEP("ONE_FACE_AUTO_DISCOVERS_SIX_OPENINGS",fastener_bso_resolve_sites(root,mm,6,sites,&count,&out));
    STEP("SIX_ACTUAL_SITES",count!=6?PRO_TK_BAD_CONTEXT:0);
    for(int i=0;i<6;i++)STEP("SELECT_HOLE",bso_select_face(&place,panel,faces.holes[i]));
    STEP("VALIDATE_EXPLICIT_SIX",fastener_bso_resolve_sites(root,mm,6,sites,&count,&out));
    report[0]=0;
    int face=sites[2].face_id;
    sites[2].face_id=0;
    STEP("FAILED_REFERENCE_DOES_NOT_DISCARD_FIVE_VERIFIED_INSTALLS",
        fastener_bso_place(root,saved_part,sites,count,mm,&out)!=PRO_TK_INCOMPLETE?PRO_TK_BAD_CONTEXT:0);
    STEP("FIVE_COMPONENTS_PRESERVED",occurrences(root,saved_part,bolts,6)!=5?PRO_TK_BAD_CONTEXT:0);
    sites[2].face_id=face;
    report[0]=0;
    STEP("RETRY_ADDS_ONLY_ONE_MISSING",fastener_bso_place(root,saved_part,sites,count,mm,&out));
    STEP("READ_BACK_FIVE_REUSED_ONE_NEW",!wcsstr(report,L"新增 1，已装复用 5，待处理 0")?PRO_TK_BAD_CONTEXT:0);
    STEP("SIX_PLACED_COMPONENTS",occurrences(root,saved_part,bolts,6)!=6?PRO_TK_BAD_CONTEXT:0);
    ProAsmcomppath root_path;
    STEP("ROOT_PATH",ax_root_path(root,&root_path));
    for(int i=0;i<6;i++){
        ProAsmcompconstraint *rows=NULL;
        int actual=0;
        ProAsmcompConstrType first=PRO_ASM_FIX,second=PRO_ASM_FIX;
        error=ax_read_constraints(&bolts[i],&root_path,&rows,&actual);
        if(!error&&actual==2)error=ProAsmcompconstraintTypeGet(rows[0],&first);
        if(!error&&actual==2)error=ProAsmcompconstraintTypeGet(rows[1],&second);
        if(rows)ProAsmcompconstraintArrayFree(rows);
        STEP("ACTUAL_COAXIAL_AND_MATE",error||first!=PRO_ASM_INSERT||second!=PRO_ASM_MATE?PRO_TK_BAD_CONTEXT:0);
    }
    report[0]=0;
    STEP("DUPLICATE_RETRY_IDEMPOTENT",fastener_bso_place(root,saved_part,sites,count,mm,&out));
    STEP("READ_BACK_ALL_SIX_EXISTING",!wcsstr(report,L"新增 0，已装复用 6，待处理 0")?PRO_TK_BAD_CONTEXT:0);
    STEP("NO_DUPLICATE",occurrences(root,saved_part,bolts,6)!=6?PRO_TK_BAD_CONTEXT:0);

done:
    if(error){
        FILE *detail=_wfopen(L"bso-workflow-detail.txt",L"wt, ccs=UTF-8");
        if(detail){fputws(report,detail);fclose(detail);}
    }
    fprintf(bso_report,"BSO_WORKFLOW_PASS=%d\n",error==0);
    fclose(bso_report);
    ProEngineerEnd();
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    ProError error=bwtl_bso_initialize(argc,argv,version,build,buffer);
    uiCmdCmdId id;
    if(!error)error=ProCmdActionAdd("BWTLBSOFlow",bso_flow,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command \x0060" L"BWTLBSOFlow\x0060;");
}
