
#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static FILE *test_log;
static ProMdl stock;
static Fixture faces;
static int holes[2];

static int done(int pass){
    fwprintf(test_log,L"MARKUP_PART_WORKFLOW_PASS=%d\n",pass);
    fclose(test_log);
    ProEngineerEnd();
    return 0;
}

static int result(const wchar_t *test,ProError error,const wchar_t *reply){
    fwprintf(test_log,L"%ls=%d CODE=%d\n%ls\n",test,!error,error,reply?reply:L"");
    return !error;
}

static MarkupMark at_hole(const MarkupHole *hole){
    return (MarkupMark){MARKUP_CIRCLE,hole->x-5,hole->y-5,hole->x+5,hole->y+5,1};
}

static int verify(uiCmdCmdId id,uiCmdValue *unused,void *value){
    MarkupCapture capture={0};
    ProError error=markup_take(&capture);
    if(!result(L"INITIAL_SCREENSHOT",error,NULL))return done(0);
    MarkupHole saved[2]={0};
    for(int i=0;i<capture.image->count;i++)for(int j=0;j<2;j++)
        if(capture.image->holes[i].feature==holes[j])saved[j]=capture.image->holes[i];
    if(!saved[0].surface||!saved[1].surface){markup_capture_clear();return done(0);}
    MarkupRequest user={.ticket=capture.ticket,.count=1,.text=L"这个孔改成 15 mm，旁边的孔别动"};
    user.marks[0]=at_hole(&saved[0]);
    wchar_t reply[2000]=L"";
    MarkupEdit edits[MARKUP_MAX_EDITS];
    int count;
    MarkupRequest batch={.count=3,.text=L"1、2 改 Ø8，3 保留"};
    if(!markup_direct_partial(&batch,edits,&count)||count!=2||edits[0].mark!=0||edits[1].mark!=1||
       !part_close(edits[0].value,8)||!part_close(edits[1].value,8))return done(0);
    fwprintf(test_log,L"NUMBERED_BATCH_WITH_PROTECTION=1\n");
    error=markup_direct_partial(&user,edits,&count)?
        markup_apply_edits(&capture,&user,edits,count,reply,_countof(reply)):PRO_TK_BAD_CONTEXT;
    if(!error&&(!wcsstr(reply,L"1 处保留部位")||markup_bore_read(stock,&saved[0],15)||
       markup_bore_read(stock,&saved[1],8)))error=PRO_TK_GENERAL_ERROR;
    int pass=result(L"NATIVE_EDIT_AND_PROTECT",error,reply);
    markup_capture_clear();
    if(!pass)return done(0);

    capture=(MarkupCapture){0};
    error=markup_take(&capture);
    MarkupRequest next={.ticket=capture.ticket,.text=L"右边那个再大 1 mm"};
    if(!error&&!markup_followup(&capture,&next,edits,&count,reply,_countof(reply),&error))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=markup_apply_edits(&capture,&next,edits,count,reply,_countof(reply));
    int changed_first=!markup_bore_read(stock,&saved[0],16)&&!markup_bore_read(stock,&saved[1],8);
    int changed_second=!markup_bore_read(stock,&saved[0],15)&&!markup_bore_read(stock,&saved[1],9);
    if(!error&&!(changed_first||changed_second))error=PRO_TK_GENERAL_ERROR;
    if(!result(L"FOLLOWUP_WITHOUT_REDRAW",error,reply))return done(0);

    /* Use the backend's real screen map to put a mark on a known synthetic
       planar point, and read it back through the same ray hit path as UI. */
    ProPoint3d world={25,46,20};
    double delta[3],xy[2]={capture.face_anchor[0],capture.face_anchor[1]};
    ad_sub(world,capture.face_origin,delta);
    for(int a=0;a<2;a++){
        double v=ad_dot(delta,capture.face_basis[a])/ad_dot(capture.face_basis[a],capture.face_basis[a]);
        for(int j=0;j<2;j++)xy[j]+=v*capture.face_pixels[a][j];
    }
    MarkupRequest add={.ticket=capture.ticket,.count=1,.text=L"这里加两个 Ø5 通孔，孔距 14 mm"};
    add.marks[0]=(MarkupMark){MARKUP_ARROW,xy[0]-12,xy[1]-12,xy[0],xy[1],1};
    MarkupCreation plan;
    reply[0]=0;
    error=markup_create_parse(add.text,&plan)?markup_apply_creation(&capture,&add,&plan,reply,_countof(reply)):PRO_TK_BAD_INPUTS;
    if(!result(L"ANNOTATED_HOLE_PAIR",error,reply))return done(0);
    ProVector right={1,0,0};
    ProName feature;
    static const wchar_t *const commands[]={L"这里加一个长圆槽 12×5 mm",L"这里加一个矩形开口 8×4 mm"};
    ProPoint3d places[2]={{56,49,20},{17,7,20}};
    for(int i=0;i<2;i++){
        reply[0]=0;
        error=markup_create_parse(commands[i],&plan)?
            markup_create_profiles(stock,faces.top,&places[i],right,NULL,&plan,feature,reply,_countof(reply)):PRO_TK_BAD_INPUTS;
        if(!result(i?L"NATIVE_RECTANGULAR_OPENING":L"NATIVE_OBLONG_SLOT",error,reply))return done(0);
    }
    return done(1);
}

static int prep(uiCmdCmdId id,uiCmdValue *unused,void *value){
    test_log=_wfopen(L"markup-part-workflow.log",L"wt, ccs=UTF-8");
    if(!test_log){ProEngineerEnd();return 0;}
    setvbuf(test_log,NULL,_IONBF,0);
    model_background_mode=1;
    ProMdlName name;
    ProError error=fixture(&stock,name,&faces);
    for(int i=0;!error&&i<2;i++){
        wchar_t command[512];
        ProName feature;
        wcscpy_s(feature,PRO_NAME_SIZE,i?L"NEIGHBOR":L"TARGET");
        swprintf_s(command,_countof(command),L"CREO_OP HOLE %ls %ls SURFACE:%d SURFACE:%d %d SURFACE:%d %d %d THROUGH 0",
            name,feature,faces.top,faces.x,i?53:21,faces.y,i?35:19,i?8:10);
        error=expansion_build(stock,command,test_log);
        ProFeature f;
        if(!error)error=ux_named_feature(stock,feature,&f);
        if(!error)holes[i]=f.id;
    }
    if(!error)error=dv_display_new(stock);
    if(error)return done(0);
    return ProMacroLoad(L"~ Command \x60" L"BWTLMarkupPartVerify\x60;");
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    uiCmdCmdId cmd;
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    if(!error)error=ProCmdActionAdd("BWTLMarkupPartPrep",prep,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!error)error=ProCmdActionAdd("BWTLMarkupPartVerify",verify,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLMarkupPartPrep\x60;");
}
