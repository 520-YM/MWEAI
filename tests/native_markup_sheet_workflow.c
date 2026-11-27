#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static FILE *logfile;
static ProMdl sheet;
static ProMdlName name;
static int cut_id,opening_id,phase,queued;
static ULONGLONG deadline;

static ProError run(const wchar_t *protocol){
    PartCommand command={0};
    wchar_t message[2000]=L"";
    ProError e=part_command_parse(protocol,&command)==1?
        foundation_execute(&command,sheet,message,_countof(message)):PRO_TK_BAD_INPUTS;
    fprintf(logfile,"command=%ls code=%d\n",protocol,e);
    return e;
}
static int finish(int pass){
    if(native_backend_window)KillTimer(native_backend_window,9010);
    fprintf(logfile,"MARKUP_SHEET_WORKFLOW_PASS=%d\n",pass);
    fclose(logfile);
    ProEngineerEnd();
    return 0;
}

static int sheet_native_pass(const wchar_t *label,ProError error,const wchar_t *reply){
    fprintf(logfile,"%ls=%d CODE=%d %ls\n",label,!error,error,reply?reply:L"");
    fflush(logfile);
    return !error;
}

static int verify(uiCmdCmdId id,uiCmdValue *v,void *data){
    ProError e=markup_take(&markup_capture);
    if(!sheet_native_pass(L"CAPTURE_RECTANGLE",e,NULL))return finish(0);
    const MarkupImage *image=markup_capture.image;
    const MarkupOpening *opening=NULL;
    for(int i=0;i<image->opening_count;i++)
        if(image->openings[i].feature==opening_id)opening=&image->openings[i];
    if(!opening){fprintf(logfile,"OPENING_VISIBILITY_COUNT=%d\n",image->opening_count);return finish(0);}
    MarkupOpening old=*opening;
    MarkupRequest resize={.ticket=image->ticket,.count=1,.text=L"把这个窗口加宽 4 mm，左边位置别动"};
    resize.marks[0]=(MarkupMark){MARKUP_ARROW,opening->x-12,opening->y-10,opening->x,opening->y,1};
    MarkupEdit edit;
    wchar_t reply[2000]=L"";
    ProError proposed=PRO_TK_BAD_CONTEXT;
    const int chosen=markup_opening_adjustment(&markup_capture,&resize,&edit,reply,_countof(reply),&proposed);
    e=chosen&&!proposed?markup_apply_edits(&markup_capture,&resize,&edit,1,reply,_countof(reply)):proposed;
    const int left=markup_left_side(&old);
    double actual[4][3],sizes[2]={old.size[0],old.size[1]};
    if(!e)sizes[edit.measure-MARKUP_WIDTH]=edit.value/image->mm_per_native;
    ProError verified=markup_opening_read_geometry(sheet,&old,sizes[0],sizes[1],actual);
    double displacement=0;
    if(left>=0&&!verified)for(int i=0;i<2;i++){
        int j=(left+i)%4;
        double delta[3];
        ad_sub(actual[j],old.native_corners[j],delta);
        displacement=fmax(displacement,hypot(hypot(delta[0],delta[1]),delta[2]));
    }
    int guarded=e&&chosen&&!proposed&&!verified&&left>=0&&displacement<0.001&&
        wcsstr(reply,L"左边位置发生变化")&&wcsstr(reply,L"恢复");
    int kept=!e&&chosen&&!verified&&left>=0&&displacement<0.001;
    fprintf(logfile,"RESIZE_CHOSEN=%d PROPOSED=%d DONE=%d VERIFIED=%d LEFT=%d DISPLACEMENT=%.10g SIZE=%.10g,%.10g\n",
        chosen,proposed,e,verified,left,displacement,sizes[0],sizes[1]);
    fprintf(logfile,"LEFT_EDGE_WIDENING_PASS=%d LEFT_EDGE_GUARDED_ROLLBACK=%d REPLY=%ls\n",kept,guarded,reply);
    if(!kept&&!guarded)return finish(0);
    
    ProMatrix section;
    FdSketch sketch={0};
    e=fd_sketch_open(sheet,L"OUTLINE",&sketch,0);
    if(!e)e=ProSectionLocationGet(sketch.section,section);
    fd_sketch_free(&sketch);
    if(!sheet_native_pass(L"STOCK_SKETCH_COORDINATES",e,NULL))return finish(0);
    const wchar_t *const commands[]={L"这里加两个 Ø5 通孔，孔距 12 mm",L"这里加一个长圆槽 12×5 mm",L"这里加一个矩形开口 8×4 mm"};
    const wchar_t *const labels[]={L"SHEET_ANNOTATED_ROUND_HOLES",L"SHEET_ANNOTATED_OBLONG_SLOT",L"SHEET_ANNOTATED_RECTANGLE"};
    ProPoint3d coordinates[]={{18,45,0},{57,11,0},{64,52,0}};
    for(int i=0;i<3;i++){
        markup_capture_clear();
        e=markup_take(&markup_capture);
        if(e){fprintf(logfile,"RECAPTURE=%d CODE=%d\n",i,e);return finish(0);}
        ProPoint3d point;
        if(ProPntTrfEval(coordinates[i],section,point))return finish(0);
        double difference[3],xy[2]={markup_capture.face_anchor[0],markup_capture.face_anchor[1]};
        ad_sub(point,markup_capture.face_origin,difference);
        for(int a=0;a<2;a++){
            double component=ad_dot(difference,markup_capture.face_basis[a])/
                ad_dot(markup_capture.face_basis[a],markup_capture.face_basis[a]);
            for(int j=0;j<2;j++)xy[j]+=component*markup_capture.face_pixels[a][j];
        }
        MarkupRequest request={.ticket=markup_capture.ticket,.count=1};
        wcscpy_s(request.text,_countof(request.text),commands[i]);
        request.marks[0]=(MarkupMark){MARKUP_ARROW,xy[0]-14,xy[1]-12,xy[0],xy[1],1};
        MarkupCreation creation;
        reply[0]=0;
        e=markup_create_parse(request.text,&creation)?markup_apply_creation(&markup_capture,&request,&creation,reply,_countof(reply)):
            PRO_TK_BAD_INPUTS;
        if(!sheet_native_pass(labels[i],e,reply))return finish(0);
    }
    return finish(1);
}

static int prepare(uiCmdCmdId id,uiCmdValue *v,void *data){
    logfile=fopen("markup-sheet-workflow.log","w");
    if(!logfile)return ProEngineerEnd(),0;
    setvbuf(logfile,NULL,_IONBF,0);
    const char *stage="empty";
    model_background_mode=1;
    ProError e=model_create_empty(2,name,&stage);
    if(!e)e=ProMdlnameInit(name,PRO_MDL_PART,&sheet);
    wchar_t cmd[512];
    if(!e){
        swprintf_s(cmd,_countof(cmd),L"CREO_OP SMT_SKETCH %ls OUTLINE PLANE:FRONT PLANE:RIGHT RECT 0 0 80 60",name);
        e=run(cmd);
    }
    if(!e){
        swprintf_s(cmd,_countof(cmd),L"CREO_OP SMT_FIRST_WALL %ls BASE SKETCH:OUTLINE 2 POS",name);
        e=run(cmd);
    }
    if(!e){
        swprintf_s(cmd,_countof(cmd),L"CREO_OP SMT_SKETCH %ls CUT_PROFILE PLANE:FRONT PLANE:RIGHT CIRCLE 20 20 5",name);
        e=run(cmd);
    }
    if(!e){
        swprintf_s(cmd,_countof(cmd),L"CREO_OP SMT_CUT %ls CUT_A SKETCH:CUT_PROFILE NEG",name);
        e=run(cmd);
    }
    ProModelitem item;
    if(!e)e=ProModelitemByNameInit(sheet,PRO_FEATURE,L"CUT_A",&item);
    if(!e)cut_id=item.id;
    if(!e){
        swprintf_s(cmd,_countof(cmd),L"CREO_OP SMT_SKETCH %ls OPEN_PROFILE PLANE:FRONT PLANE:RIGHT RECT 37 24 30 20",name);
        e=run(cmd);
    }
    if(!e){
        swprintf_s(cmd,_countof(cmd),L"CREO_OP SMT_CUT %ls OPEN_A SKETCH:OPEN_PROFILE NEG",name);
        e=run(cmd);
    }
    if(!e)e=ProModelitemByNameInit(sheet,PRO_FEATURE,L"OPEN_A",&item);
    if(!e)opening_id=item.id;
    if(!e)e=dv_display_new(sheet);
    if(!e)e=ProMacroLoad(L"~ Command " L"\x60" L"BWTLMarkupSheetVerify" L"\x60;");
    if(e)return finish(0);
    return 0;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    uiCmdCmdId id;
    ProError e=expansion_pattern_initialize(argc,argv,version,build,buffer);
    if(!e)e=ProCmdActionAdd("BWTLMarkupSheetPrepare",prepare,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    if(!e)e=ProCmdActionAdd("BWTLMarkupSheetVerify",verify,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return e?e:ProMacroLoad(L"~ Command " L"\x60" L"BWTLMarkupSheetPrepare" L"\x60;");
}
