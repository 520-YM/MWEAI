#define user_initialize packaging_production_initialize
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>

typedef struct { double x,y,z,w,d; } SamplePocket;
static ProError sample_profile(ProSection section,const ProMatrix frame,double scale,void *data){
    SamplePocket *p=data;
    ImitationGeometry geometry={0};
    ImitationSketch s={&geometry,IMITATION_WINDOW,0,1,p->z,section,frame,scale};
    const ImitationPoint pts[]={{p->x,p->y},{p->x+p->w,p->y},{p->x+p->w,p->y+p->d},{p->x,p->y+p->d}};
    return imitation_polygon(&s,pts,_countof(pts));
}
static ProError sample_cut(ProMdl model,ProMdlName name,ProOutline bounds,
    double w,double d,double z,double depth,PartText *out){
    const wchar_t *basis=imitation_template_plane(model,2),*orient=imitation_template_plane(model,0);
    if(!basis||!orient)return PRO_TK_E_NOT_FOUND;
    ProError error=imitation_offset(model,name,out,L"POCKET_START",basis,2,z);
    double frame[4][4];
    SamplePocket pocket={(bounds[0][0]+bounds[1][0]-w)/2,(bounds[0][1]+bounds[1][1]-d)/2,z,w,d};
    if(!error)error=smx_custom_sketch(model,L"POCKET_PROFILE",L"PLANE:POCKET_START",orient,sample_profile,&pocket,frame);
    if(!error)error=imitation_operation(model,out,0,L"CREO_OP SOLID_EXTRUDE %ls POCKET POCKET_PROFILE CUT BLIND %.12g %ls",
        name,depth,frame[2][2]>0?L"NEG":L"POS");
    return error;
}
static int run_sample(uiCmdCmdId command,uiCmdValue *value,void *data){
    FILE *log=_wfopen(L"packaging-built.log",L"w, ccs=UTF-8");
    if(!log)return 0;
    const double sizes[][3]={{570,440,65},{570,440,20},{578,448,93}};
    const wchar_t *parts[]={L"MWE215_FOAM1",L"MWE215_FOAM2",L"MWE215_MAILER"};
    const double position[][3]={{-285,-260,-66.23234011},{-285,-260,-1.23234011},{-289,-264,-70.23234011}};
    const char *stage="initialize";
    ProMdl models[3]={0},assembly=NULL;
    ProMdlName name;
    ProOutline bounds[3];
    ProError error=0;
    wchar_t notes[4800]=L"";
    PartText output={notes,_countof(notes),0};
    model_background_mode=1;
    for(int i=0;i<3;i++){
        stage="create named empty part";
        error=model_create_empty_named(1,parts[i],name,&stage);
        if(!error)error=ProMdlnameInit(name,PRO_MDL_PART,&models[i]);
        ModelPlan plan={0};plan.a=sizes[i][0];plan.b=sizes[i][1];plan.height=sizes[i][2];plan.ready=1;
        if(!error)error=model_build_internal(&plan,name,&stage,models[i],NULL,NULL);
        if(!error)error=or_outline(models[i],bounds[i]);
        if(!error&&i==0){stage="bottom foam pocket";error=sample_cut(models[i],name,bounds[i],530,320,bounds[i][1][2]-45,45,&output);}
        if(!error&&i==2){stage="mailer cavity";error=sample_cut(models[i],name,bounds[i],574,444,bounds[i][0][2]+2,89,&output);}
        ProMassProperty mass;
        if(!error)error=ProSolidMassPropertyWithDensityGet((ProSolid)models[i],NULL,PRO_MP_DENS_USE_ALWAYS,1.0,&mass);
        if(!error&&i==2&&fabs(mass.volume-(578.0*448*93-574.0*444*89))>2)error=PRO_TK_INCOMPLETE;
        fwprintf(log,L"%ls result=%d stage=%hs\n%ls\n",parts[i],error,stage,notes);
        if(!error)fwprintf(log,L"volume=%.5f\n",mass.volume);
        fflush(log);
        if(!error)error=ProMdlSave(models[i]);
        if(error)goto done;
    }
    error=model_create_empty_named(3,L"MWE215_PACK_SAMPLE",name,&stage);
    if(!error)error=ProMdlnameInit(name,PRO_MDL_ASSEMBLY,&assembly);
    for(int i=0;!error&&i<3;i++){
        double point[3];
        for(int j=0;j<3;j++)point[j]=position[i][j]-bounds[i][0][j];
        error=packaging_place(assembly,models[i],point,1.0,NULL);
    }
    if(!error)error=ProMdlSave(assembly);
done:
    fwprintf(log,L"delivery=%d\n",error);fclose(log);
    ProEngineerEnd();return 0;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t message[80]){
    uiCmdCmdId command;
    ProError error=ProCmdActionAdd("MWEAIPackBuildSample",run_sample,uiProe2ndImmediate,access_command,PRO_B_TRUE,PRO_B_TRUE,&command);
    if(!error)error=ProMacroLoad(L"~ Command \x0060" L"MWEAIPackBuildSample\x0060;");
    return error;
}