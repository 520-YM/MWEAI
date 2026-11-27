#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
#include <psapi.h>
#include <ProToolkit.h>
#include <ProCore.h>
#include <ProUtil.h>
#include <ProUICmd.h>
#include <ProMenu.h>

static FILE *runtime_open(const wchar_t *name,const wchar_t *mode){return _wfopen(name,mode);}
static void trace(const char *operation,int status){
    FILE *log=fopen("toolkit.log","a");
    if(log){fprintf(log,"%s: %d\n",operation,status);fclose(log);}
}
#include "../../native_model.h"
#include "../../part_tools.h"
static ProError or_outline(ProMdl model,ProOutline bounds){
    ProMatrix identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    ProSolidOutlExclTypes excludes[]={PRO_OUTL_EXC_DATUM_PLANE,PRO_OUTL_EXC_DATUM_POINT,
        PRO_OUTL_EXC_DATUM_CSYS,PRO_OUTL_EXC_DATUM_AXES,PRO_OUTL_EXC_FACETS,PRO_OUTL_EXC_ALL_CRVS};
    return ProSolidOutlineCompute((ProSolid)model,identity,excludes,_countof(excludes),bounds);
}
static FILE *receipt;
static int verify;
static ProError isolation(void){
    HMODULE modules[2048]; DWORD bytes=0;
    if(!EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes)||bytes>sizeof(modules))
        return PRO_TK_GENERAL_ERROR;
    for(DWORD i=0;i<bytes/sizeof(HMODULE);i++){
        wchar_t path[4096]=L"";
        GetModuleFileNameW(modules[i],path,_countof(path));
        _wcslwr(path);
        if(wcsstr(path,L"creo_safe_resident")||wcsstr(path,L"creosafe")||wcsstr(path,L"codex-creo")||
           wcsstr(path,L"bwtl-native.dll")||wcsstr(path,L"mweai.dll")||wcsstr(path,L"bwtl.dll"))
            return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}

static const wchar_t *variant=L"MW49_FLOORSTAND_NARROW";
#define STEP(w,label) do {e=(w);fprintf(receipt,"%s %s status=%d\n",e?"FAIL":"PASS",label,e);fflush(receipt);if(e)return e;}while(0)

static ProError model_lookup(const wchar_t *name,ProMdlType type,ProMdl *model){
    ProMdlName native;
    wcscpy_s(native,PRO_NAME_SIZE,name);
    return ProMdlnameInit(native,type,model);
}
typedef struct {int id;ProDimension result;int found;} Dim;
static ProError pick_dim(ProDimension *d,ProError status,ProAppData data){
    Dim *r=data;
    if(!status&&d->id==r->id&&d->type==PRO_DIMENSION){r->result=*d;r->found=1;}
    return 0;
}
static ProError dimension(ProMdl model,int feature,int id,double *value,int write){
    ProFeature f;Dim d={.id=id};
    ProError e=ProFeatureInit((ProSolid)model,feature,&f);
    if(!e)e=ProFeatureDimensionVisit(&f,pick_dim,NULL,&d);
    if(e)return e;
    if(!d.found)return PRO_TK_E_NOT_FOUND;
    return write?ProDimensionValueSet(&d.result,*value):ProDimensionValueGet(&d.result,value);
}
static ProError change(ProMdl model,int feature,int id,double value){
    double old;
    ProError e=dimension(model,feature,id,&old,0);
    if(!e)e=dimension(model,feature,id,&value,1);
    if(!e)fprintf(receipt,"EDIT feature=%d d%d %.6f -> %.6f\n",feature,id,old,value);
    return e;
}
static ProError confirmed(ProMdl model,int feature,int id,double expected){
    double actual;ProError e=dimension(model,feature,id,&actual,0);
    if(!e)fprintf(receipt,"READBACK feature=%d d%d %.6f (expected %.6f)\n",feature,id,actual,expected);
    if(!e&&fabs(actual-expected)>.005)e=PRO_TK_GENERAL_ERROR;
    return e;
}
typedef struct {ProSurface face;double width,height;ProOutline found;int hits;} Window;
typedef struct {ProOutline bounds;int count;} EdgeBounds;
static ProError span_edge(ProEdge edge,ProError status,ProAppData data){
    if(status)return status;
    EdgeBounds *b=data;
    for(int i=0;i<2;i++){
        ProVector p,d1,d2,d3;
        if(ProEdgeXyzdataEval(edge,i,p,d1,d2,d3))continue;
        for(int j=0;j<3;j++){
            b->bounds[0][j]=fmin(b->bounds[0][j],p[j]);
            b->bounds[1][j]=fmax(b->bounds[1][j],p[j]);
        }
        b->count++;
    }
    return 0;
}
static ProError find_loop(ProContour contour,ProError status,ProAppData data){
    if(status)return status;
    Window *target=data;
    EdgeBounds b={.bounds={{HUGE_VAL,HUGE_VAL,HUGE_VAL},{-HUGE_VAL,-HUGE_VAL,-HUGE_VAL}}};
    if(ProContourEdgeVisit(target->face,contour,span_edge,NULL,&b)||!b.count)return 0;
    if(fabs(b.bounds[1][0]-b.bounds[0][0]-target->width)<.02&&
        fabs(b.bounds[1][1]-b.bounds[0][1]-target->height)<.02&&
        fabs(b.bounds[1][2]-b.bounds[0][2])<.02){
        memcpy(target->found,b.bounds,sizeof(ProOutline));
        target->hits++;
    }
    return 0;
}
static ProError find_face(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    ProSrftype type;
    if(ProSurfaceTypeGet(surface,&type)||type!=PRO_SRF_PLANE)return 0;
    Window *t=data;
    t->face=surface;
    ProSurfaceContourVisit(surface,find_loop,NULL,t);
    return 0;
}
static ProError cut_edge(ProGeomitem *item,ProError status,ProAppData data){
    if(status)return 0;
    ProEdge edge;
    if(!ProGeomitemToEdge(item,&edge))return span_edge(edge,0,data);
    return 0;
}
static void section_point(EdgeBounds *b,ProMatrix pose,const double *xy){
    ProVector p={xy[0],xy[1],0},world;
    if(ProPntTrfEval(p,pose,world))return;
    for(int j=0;j<3;j++){
        b->bounds[0][j]=fmin(b->bounds[0][j],world[j]);
        b->bounds[1][j]=fmax(b->bounds[1][j],world[j]);
    }
    b->count++;
}
static ProError cut_aperture(ProMdl panel,double width,double height,Window *t){
    ProFeature f;
    ProError e=ProFeatureInit((ProSolid)panel,704,&f);
    if(e)return e;
    EdgeBounds b={.bounds={{HUGE_VAL,HUGE_VAL,HUGE_VAL},{-HUGE_VAL,-HUGE_VAL,-HUGE_VAL}}};
    e=ProFeatureGeomitemVisit(&f,PRO_EDGE,cut_edge,NULL,&b);
    if(b.count)fprintf(receipt,"CUT_EDGES found=%d span=%.6f %.6f %.6f\n",b.count,b.bounds[1][0]-b.bounds[0][0],b.bounds[1][1]-b.bounds[0][1],b.bounds[1][2]-b.bounds[0][2]);
    if(!b.count||fabs(b.bounds[1][0]-b.bounds[0][0]-width)>.05||fabs(b.bounds[1][1]-b.bounds[0][1]-height)>.05){
        ProSection section=NULL;int n=0,*ids=NULL;ProMatrix pose;
        e=ProFeatureSectionCopy(&f,0,&section);
        fprintf(receipt,"COPY_CUT_SECTION=%d\n",e);
        if(!e)e=ProSectionLocationGet(section,pose);
        if(!e)e=ProSectionEntityIdsGet(section,&ids,&n);
        if(!e){
            b=(EdgeBounds){.bounds={{HUGE_VAL,HUGE_VAL,HUGE_VAL},{-HUGE_VAL,-HUGE_VAL,-HUGE_VAL}}};
            for(int i=0;i<n;i++){
                Pro2dEntdef *item=NULL;
                if(ProSectionEntityGet(section,ids[i],&item)||!item)continue;
                fprintf(receipt,"CUT_SECT entity=%d type=%d\n",ids[i],item->type);
                if(item->type==PRO_2D_LINE){
                    Pro2dLinedef *l=(Pro2dLinedef *)item;
                    section_point(&b,pose,l->end1);
                    section_point(&b,pose,l->end2);
                }
            }
        }
        if(section)ProSectionFree(&section);
        if(e)return e;
    }
    double *a=b.bounds[0],*z=b.bounds[1];
    fprintf(receipt,"CUT profile xy=%.6f x %.6f center=%.6f,%.6f points=%d\n",
        z[0]-a[0],z[1]-a[1],(z[0]+a[0])/2,(z[1]+a[1])/2,b.count);
    if(!b.count||fabs(z[0]-a[0]-width)>.06||fabs(z[1]-a[1]-height)>.06)return PRO_TK_E_NOT_FOUND;
    memcpy(t->found,b.bounds,sizeof(t->found));
    t->hits=1;
    return 0;
}


static ProError aperture(ProMdl panel,double width,double height,Window *window){
    *window=(Window){.width=width,.height=height};
    ProError e=ProSolidSurfaceVisit((ProSolid)panel,find_face,NULL,window);
    if(!e&&window->hits){
        double *a=window->found[0],*b=window->found[1];
        fprintf(receipt,"WINDOW_HITS=%d BOUNDS [%.6f %.6f %.6f]-[%.6f %.6f %.6f] CENTRE %.6f %.6f\n",
            window->hits,a[0],a[1],a[2],b[0],b[1],b[2],(a[0]+b[0])/2,(a[1]+b[1])/2);
    }
    return !e&&window->hits?0:cut_aperture(panel,width,height,window);
}
static ProError regen(ProMdl mdl){
    ProError e=0;
    for(int k=0;k<3;k++){
        e=ProSolidRegenerate((ProSolid)mdl,PRO_REGEN_NO_RESOLVE_MODE|PRO_REGEN_UNDO_IF_FAIL);
        if(e!=PRO_TK_REGEN_AGAIN)break;
    }
    return e;
}
static ProError report_box(ProMdl model,const char *label,ProOutline b){
    ProError e=or_outline(model,b);
    if(!e)fprintf(receipt,"%s SIZE [%.5f %.5f %.5f] CENTRE [%.5f %.5f %.5f]\n",label,
        b[1][0]-b[0][0],b[1][1]-b[0][1],b[1][2]-b[0][2],
        (b[0][0]+b[1][0])/2,(b[0][1]+b[1][1])/2,(b[0][2]+b[1][2])/2);
    return e;
}
static ProError suppress_component(ProMdl owner,int id){
    int *list=NULL;ProFeatureDeleteOptions *opt=NULL;
    ProError e=ProArrayAlloc(1,sizeof(*list),1,(ProArray *)&list);
    if(!e)e=ProArrayAlloc(1,sizeof(*opt),1,(ProArray *)&opt);
    if(!e){
        list[0]=id;
        opt[0]=PRO_FEAT_DELETE_NO_OPTS;
        e=ProFeatureWithoptionsSuppress((ProSolid)owner,list,opt,PRO_REGEN_NO_RESOLVE_MODE|PRO_REGEN_UNDO_IF_FAIL);
    }
    if(list)ProArrayFree((ProArray *)&list);
    if(opt)ProArrayFree((ProArray *)&opt);
    return e;
}
static ProError install_screen(ProMdl parent,ProMdl screen,ProMatrix pose,ProAsmcomp *component){
    ProError e;
    PartCommand parsed={0};
    wchar_t result[PART_TEXT_MAX]=L"";
    const wchar_t *line=L"CREO_OP DATUM_CSYS M26M4163002 SCREEN49_LOCATOR CSYS:ASM_DEF_CSYS -0.2 7 34.9 90 0 180";
    ProCsysdata coords;
    ProSelection ref=NULL;
    ProMatrix locator;
    memcpy(locator,pose,sizeof(locator));
    if(part_command_parse(line,&parsed)!=1)return PRO_TK_BAD_INPUTS;
    STEP(fd_create_datum(&parsed,parent,result,_countof(result)),"Create 49-inch screen positioning CSYS");
    STEP(ax_csys_selection(parent,L"CSYS:SCREEN49_LOCATOR",1,&ref,&coords),"Read new assembly locator");
    ProSelectionFree(&ref);
    memcpy(locator[0],coords.x_vector,sizeof(ProVector));
    memcpy(locator[1],coords.y_vector,sizeof(ProVector));
    memcpy(locator[2],coords.z_vector,sizeof(ProVector));
    memcpy(locator[3],coords.origin,sizeof(ProVector));
    if(!ax_same_pose(locator,pose,1)){
        fprintf(receipt,"LOCATOR differs from original matrix\n");
        for(int i=0;i<4;i++)fprintf(receipt,"LOCATOR %.5f %.5f %.5f EXPECT %.5f %.5f %.5f\n",locator[i][0],locator[i][1],locator[i][2],pose[i][0],pose[i][1],pose[i][2]);
        return PRO_TK_BAD_CONTEXT;
    }
    STEP(ProAsmcompAssemble((ProAssembly)parent,(ProSolid)screen,pose,component),"Insert 49-inch reference screen");
    PartCommand cmd={0};
    swprintf_s(cmd.arg[2],PART_TOKEN_MAX,L"ID:%d",component->id);
    wcscpy_s(cmd.arg[3],PART_TOKEN_MAX,L"CSYS:SCREEN49_LOCATOR");
    wcscpy_s(cmd.arg[4],PART_TOKEN_MAX,L"CSYS:49___3D______");
    PartText out={result,_countof(result),0};
    STEP(ax_constrain(parent,&cmd,1,&out),"Constrain new screen CSYS to existing position");
    ProBoolean state;
    STEP(ProAsmcompIsUnderconstrained(component,&state),"Read screen constraint state");
    if(state)return PRO_TK_GENERAL_ERROR;
    fprintf(receipt,"SCREEN_OCCURRENCE=%d fully_constrained=1\n",component->id);
    return 0;
}
static ProError narrow_structure(ProMdl root,ProMdl panel);
static ProError make_models(void){
    ProError e;
    ProMdl root=NULL,door=NULL,panel=NULL,glass=NULL,parent=NULL,film=NULL;
    ProMdl screen_ref=NULL,screen=NULL;
    STEP(ProMdlnameRetrieve(L"M26G2000000",PRO_MDL_ASSEMBLY,&root),"Open original floorstand assembly copy");
    STEP(model_lookup(L"M26G2041002",PRO_MDL_ASSEMBLY,&door),"Locate front door assembly");
    STEP(model_lookup(L"M26G2081032",PRO_MDL_PART,&panel),"Locate front sheetmetal panel");
    STEP(model_lookup(L"55_GLASS",PRO_MDL_PART,&glass),"Locate glass component");
    STEP(model_lookup(L"M26M4163002",PRO_MDL_ASSEMBLY,&parent),"Locate display carrier assembly");
    STEP(model_lookup(L"55S-156",PRO_MDL_PART,&film),"Locate touch film component");
    ProOutline root_box,glass_box,screen_box;
    STEP(report_box(root,"ORIGINAL_SYSTEM",root_box),"Measure original assembly");
    STEP(ProMdlnameRetrieve(L"49INCH",PRO_MDL_PART,&screen_ref),"Open saved 49-inch reference geometry");
    STEP(confirmed(panel,40,1,900),"Check exact source case width");
    STEP(ProMdlnameCopy(screen_ref,L"P49_SCREEN_MODULE",&screen),"Create local 49-inch screen variant");
    STEP(report_box(screen,"SCREEN49",screen_box),"Measure reference screen variant");
    if(fabs(screen_box[1][0]-screen_box[0][0]-1101.8)>.1 || fabs(screen_box[1][1]-screen_box[0][1]-632)>.1 || fabs(screen_box[1][2]-screen_box[0][2]-56)>.1)return PRO_TK_BAD_INPUTS;
    Window original,next;
    STEP(aperture(panel,680.4,1209.6,&original),"Read original aperture geometry");
    STEP(change(panel,704,22,610.2),"Edit opening width");
    STEP(change(panel,704,23,1084.8),"Edit opening height");
    STEP(regen(panel),"Regenerate front panel");
    STEP(aperture(panel,610.2,1084.8,&next),"Read updated aperture geometry");
    double shift=(original.found[0][1]+original.found[1][1]-next.found[0][1]-next.found[1][1])/2;
    if(fabs(shift)>.05){
        double before;
        STEP(dimension(panel,704,57,&before,0),"Read aperture vertical offset");
        STEP(change(panel,704,57,before-shift),"Restore original screen center");
        STEP(regen(panel),"Regenerate centered opening");
        STEP(aperture(panel,610.2,1084.8,&next),"Measure centered opening geometry");
        if(fabs(original.found[0][1]+original.found[1][1]-next.found[0][1]-next.found[1][1])>.1){
            STEP(change(panel,704,57,before+shift),"Correct vertical offset direction");
            STEP(regen(panel),"Regenerate corrected opening");
            STEP(aperture(panel,610.2,1084.8,&next),"Measure corrected opening geometry");
        }
    }
    if(fabs(original.found[0][1]+original.found[1][1]-next.found[0][1]-next.found[1][1])>.1 ||
       fabs(original.found[0][0]+original.found[1][0]-next.found[0][0]-next.found[1][0])>.1)return PRO_TK_GENERAL_ERROR;
    STEP(change(glass,15,4,660.2),"Edit glass width");
    STEP(change(glass,15,5,1134.8),"Edit glass height");
    STEP(regen(glass),"Regenerate glass");
    STEP(report_box(glass,"GLASS49",glass_box),"Read glass geometry");
    if(fabs(glass_box[1][0]-glass_box[0][0]-660.2)>.03||fabs(glass_box[1][1]-glass_box[0][1]-1134.8)>.03)return PRO_TK_GENERAL_ERROR;
    ProAsmcomp old,inserted;
    ProMatrix original_pose;
    STEP(ProFeatureInit((ProSolid)parent,95,&old),"Identify active 55-inch screen occurrence");
    STEP(ax_actual_pose(parent,&old,original_pose),"Read original screen orientation and mounting center");
    STEP(suppress_component(parent,95),"Suppress original screen occurrence");
    original_pose[3][0]=-0.2;
    original_pose[3][2]=34.9;
    STEP(install_screen(parent,screen,original_pose,&inserted),"Insert and constrain 49-inch screen");

    const int film_dims[]={1,2,5,6};
    const double film_sizes[]={620.2,1098.2,642.8,1126.6};
    for(int i=0;i<4;i++)STEP(change(film,39,film_dims[i],film_sizes[i]),"Resize touch-film reference");
    STEP(regen(film),"Regenerate touch film");
    STEP(part_health(film),"Check film features");
    STEP(ProMdlSave(film),"Save resized touch-film reference");
    STEP(narrow_structure(root,panel),"Narrow original structure and regenerate all references");

    STEP(regen(parent),"Regenerate display carrier assembly");
    STEP(regen(door),"Regenerate front door assembly");
    e=regen(root);
    if(e)return e;
    fprintf(receipt,"OVERALL_REGEN=%d\n",e);
    STEP(report_box(root,"RESULT_SYSTEM",root_box),"Measure result assembly");
    STEP(ProMdlnameRename(root,(wchar_t *)variant),"Rename the derived top assembly");
    STEP(ProMdlSave(screen),"Save reference screen copy");
    STEP(ProMdlSave(panel),"Save actual front sheetmetal modifications");
    STEP(ProMdlSave(glass),"Save resized glass");
    STEP(ProMdlSave(parent),"Save display carrier and its 49-inch constraint");
    STEP(ProMdlSave(door),"Save front door assembly");
    STEP(ProMdlSave(root),"Save 49-inch floorstand variant");
    return 0;
}


typedef struct {const wchar_t *name;int feature,dimension;double old_value,new_value;} WidthEdit;
static const WidthEdit width_edits[]={
 {L"M26G2081032",40,1,900,830},{L"M26G2081001",40,1,900,830},
 {L"M26M4181063",40,2,900,830},{L"M26M4181060",40,2,894,824},
 {L"M26M4181007",985,45,896,826},{L"M26G2081008",41,0,897,827},
 {L"M26M4181038",352,21,420,385},{L"M26M4181038",39,0,897,827},
 {L"M26M4181061",1636,112,370,335},{L"M26M4181061",1285,93,870,800},
 {L"SUS340-LIANGAN-890",39,14,890,820},{L"SUS340-LIANGAN-924",39,14,924,854}
};
static ProError narrow_structure(ProMdl root,ProMdl panel){
    ProError e;
    ProMdl changed[_countof(width_edits)],crossmember;
    for(int i=0;i<_countof(width_edits);i++){
        const WidthEdit *entry=&width_edits[i];
        STEP(model_lookup(entry->name,PRO_MDL_PART,&changed[i]),"Find the required original structure part");
        fwprintf(stderr,L"%ls\n",entry->name);
        STEP(confirmed(changed[i],entry->feature,entry->dimension,entry->old_value),"Validate exact original feature dimension");
        STEP(change(changed[i],entry->feature,entry->dimension,entry->new_value),"Apply the 830-mm structure design");
    }
    STEP(change(panel,1261,51,385),"Keep the front panel relief aligned to its reinforcement");
    for(int i=0;i<_countof(changed);i++){
        if(i+1<_countof(changed)&&changed[i]==changed[i+1])continue;
        STEP(regen(changed[i]),"Regenerate edited part");
        STEP(part_health(changed[i]),"Verify edited part regeneration");
        STEP(ProMdlSave(changed[i]),"Save edited native geometry to task directory");
    }
    STEP(model_lookup(L"M26M4181061",PRO_MDL_ASSEMBLY,&crossmember),"Find the base crossmember hardware assembly");
    STEP(regen(crossmember),"Re-solve the eight fasteners");
    STEP(part_health(crossmember),"Check base fasteners");
    STEP(ProMdlSave(crossmember),"Save base fastener assembly");
    return 0;
}

static ProError load_variant(ProMdl *root,ProMdl *door,ProMdl *panel,ProMdl *glass,ProMdl *carrier,ProMdl *screen,ProMdl *film){
    ProError e;
    ProMdlName name;
    wcscpy_s(name,PRO_NAME_SIZE,variant);
    STEP(ProMdlnameRetrieve(name,PRO_MDL_ASSEMBLY,root),"Reopen new named 49-inch top assembly from disk");
    STEP(model_lookup(L"M26G2041002",PRO_MDL_ASSEMBLY,door),"Resolve door subassembly");
    STEP(model_lookup(L"M26G2081032",PRO_MDL_PART,panel),"Resolve sheet metal panel");
    STEP(model_lookup(L"55_GLASS",PRO_MDL_PART,glass),"Resolve glass");
    STEP(model_lookup(L"M26M4163002",PRO_MDL_ASSEMBLY,carrier),"Resolve screen carrier");
    STEP(model_lookup(L"P49_SCREEN_MODULE",PRO_MDL_PART,screen),"Resolve installed 49-inch screen");
    STEP(model_lookup(L"55S-156",PRO_MDL_PART,film),"Resolve touch film");
    return 0;
}
static ProError bounds_in_door(ProMdl door,ProMdl item,int *ids,int n,ProOutline local,ProOutline bounds){
    ProIdTable path_ids={0};
    for(int i=0;i<n;i++)path_ids[i]=ids[i];
    ProAsmcomppath path;
    ProMatrix pose;
    ProError e=ProAsmcomppathInit((ProSolid)door,path_ids,n,&path);
    if(!e)e=ProAsmcomppathTrfGet(&path,PRO_B_TRUE,pose);
    if(e)return e;
    for(int i=0;i<3;i++){bounds[0][i]=HUGE_VAL;bounds[1][i]=-HUGE_VAL;}
    for(int mask=0;mask<8;mask++){
        ProVector p={local[mask&1][0],local[(mask>>1)&1][1],local[(mask>>2)&1][2]},q;
        e=ProPntTrfEval(p,pose,q);
        if(e)return e;
        for(int i=0;i<3;i++){
            bounds[0][i]=fmin(bounds[0][i],q[i]);
            bounds[1][i]=fmax(bounds[1][i],q[i]);
        }
    }
    return 0;
}
static void verify_coverage(const char *name,ProOutline box,ProOutline aperture_box){
    fprintf(receipt,"%s projected edge margins LEFT=%.6f RIGHT=%.6f LOWER=%.6f UPPER=%.6f\n",name,
        aperture_box[0][0]-box[0][0],box[1][0]-aperture_box[1][0],
        aperture_box[0][1]-box[0][1],box[1][1]-aperture_box[1][1]);
    fprintf(receipt,"%s APERTURE_FULLY_COVERED=%d Z=[%.6f %.6f]\n",name,
        box[0][0]<=aperture_box[0][0]+.01&&box[1][0]>=aperture_box[1][0]-.01&&
        box[0][1]<=aperture_box[0][1]+.01&&box[1][1]>=aperture_box[1][1]-.01,box[0][2],box[1][2]);
}

typedef struct {ProMdl screen;int id,count;} ScreenOccurrence;
static ProError find_active_screen(ProFeature *f,ProError status,ProAppData app){
    if(status)return status;
    ProFeattype type; ProFeatStatus state; ProMdl model;
    ScreenOccurrence *screen=app;
    if(!ProFeatureTypeGet(f,&type)&&type==PRO_FEAT_COMPONENT&&
       !ProFeatureStatusGet(f,&state)&&state==PRO_FEAT_ACTIVE&&
       !ProAsmcompMdlGet(f,&model)&&model==screen->screen){
        screen->id=f->id;
        screen->count++;
    }
    return 0;
}

static ProError verify_models(void){
    ProMdl root,door,panel,glass,carrier,screen,film;
    ProError e;
    STEP(load_variant(&root,&door,&panel,&glass,&carrier,&screen,&film),"Load delivery assembly and changed models");
    STEP(regen(root),"Regenerate delivery assembly from fresh session");
    for(int i=0;i<4;i++){
        ProMdl model=(ProMdl[]){panel,glass,carrier,door}[i];
        STEP(part_health(model),"Check changed assemblies and parts for failed features");
    }
    Window window;
    STEP(aperture(panel,610.2,1084.8,&window),"Verify resized opening using its native edges");
    STEP(confirmed(panel,704,57,197.4),"Verify preserved viewing center");
    ProOutline wb,local,gb,sb;
    ScreenOccurrence active={.screen=screen};
    STEP(ProSolidFeatVisit((ProSolid)carrier,find_active_screen,NULL,&active),"Find one active 49-inch screen");
    if(active.count!=1)return PRO_TK_GENERAL_ERROR;
    int win_ids[]={40,40,40},glass_ids[]={41},screen_ids[]={594,active.id};
    STEP(bounds_in_door(door,panel,win_ids,3,window.found,wb),"Transform opening into door coordinates");
    STEP(report_box(glass,"GLASS49",local),"Measure installed resized glass");
    STEP(bounds_in_door(door,glass,glass_ids,1,local,gb),"Read glass position in door coordinates");
    STEP(report_box(screen,"SCREEN49",local),"Measure installed 49-inch screen body");
    STEP(bounds_in_door(door,screen,screen_ids,2,local,sb),"Read screen position in door coordinates");
    verify_coverage("GLASS",gb,wb);
    verify_coverage("SCREEN_BODY",sb,wb);
    for(int i=0;i<2;i++)if(sb[0][i]>wb[0][i]+.01||sb[1][i]<wb[1][i]-.01)return PRO_TK_GENERAL_ERROR;
    ProAsmcomp old,new_comp;ProMdl found;ProFeatStatus old_state;
    STEP(ProFeatureInit((ProSolid)carrier,95,&old),"Find previous screen feature");
    STEP(ProFeatureStatusGet(&old,&old_state),"Verify previous screen suppression");
    if(old_state!=PRO_FEAT_SUPPRESSED)return PRO_TK_GENERAL_ERROR;
    STEP(ProFeatureInit((ProSolid)carrier,active.id,&new_comp),"Find 49-inch screen occurrence");
    STEP(ProAsmcompMdlGet(&new_comp,&found),"Read linked screen model");
    if(found!=screen)return PRO_TK_GENERAL_ERROR;
    ProBoolean pack,under;
    STEP(ProAsmcompIsPackaged(&new_comp,&pack),"Read packaged status");
    STEP(ProAsmcompIsUnderconstrained(&new_comp,&under),"Read constraint completeness");
    ProAsmcompconstraint *rows=NULL;
    ProAsmcompConstrType type;
    int count;
    STEP(ax_read_constraints(&new_comp,NULL,&rows,&count),"Read saved screen constraint");
    if(count!=1)e=PRO_TK_GENERAL_ERROR;
    else e=ProAsmcompconstraintTypeGet(rows[0],&type);
    if(rows)ProAsmcompconstraintArrayFree(rows);
    if(e||type!=PRO_ASM_CSYS||pack||under)return PRO_TK_GENERAL_ERROR;
    fprintf(receipt,"SCREEN49_CSYSTEM_CONSTRAINT=1 PACKAGED=0 UNDERCONSTRAINED=0\n");
    double fw,fh;
    STEP(dimension(film,39,5,&fw,0),"Read film outline width");
    STEP(dimension(film,39,6,&fh,0),"Read film outline height");
    fprintf(receipt,"TOUCH_FILM_OUTLINE_DIMS %.6f %.6f\n",fw,fh);
    STEP(report_box(panel,"DOOR_SHEET",local),"Read original envelope of reused door sheet");
    STEP(report_box(root,"ASSEMBLY_ENVELOPE_WITH_CONSTRUCTION",local),"Read overall Creo model envelope");
    ProMdl rear,foot,crossmember;
    STEP(model_lookup(L"M26G2081001",PRO_MDL_PART,&rear),"Find rear shell");
    STEP(model_lookup(L"M26M4181063",PRO_MDL_PART,&foot),"Find base plate");
    STEP(model_lookup(L"M26M4181061",PRO_MDL_ASSEMBLY,&crossmember),"Find base hardware");
    STEP(part_health(root),"Check top model health");
    STEP(part_health(crossmember),"Check base hardware health");
    ProMdl shell[]={panel,rear,foot};
    for(int i=0;i<_countof(shell);i++){
        STEP(report_box(shell[i],"STRUCTURE830",local),"Measure delivered native structural width");
        if(fabs(local[1][0]-local[0][0]-830)>.05)return PRO_TK_GENERAL_ERROR;
    }

    return 0;
}


static int job(uiCmdCmdId id,uiCmdValue *value,void *data){
    receipt=fopen(verify?"verify.log":"build-run.log","w");
    if(!receipt){ProEngineerEnd();return 0;}
    fprintf(receipt,"Creo PID=%lu phase=%s\n",GetCurrentProcessId(),verify?"verify":"build");
    ProError status=isolation();
    fprintf(receipt,"Runtime isolation check=%d\n",status);
    fflush(receipt);
    model_background_mode=1;
    if(!status)status=verify?verify_models():make_models();
    fprintf(receipt,"FINAL=%d\n",status);
    fclose(receipt);
    ProEngineerEnd();
    return 0;
}
__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    if(!GetEnvironmentVariableW(L"MWEAI_BACKGROUND_JOB",NULL,0))return PRO_TK_BAD_CONTEXT;
    verify=GetEnvironmentVariableW(L"MWEAI_BACKGROUND_VERIFY",NULL,0)!=0;
    uiCmdCmdId id;
    ProError status=ProCmdActionAdd("MweaiBackgroundFloorstand",job,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return status?status:ProMacroLoad(L"~ Command \x0060" L"MweaiBackgroundFloorstand\x0060;");
}
__declspec(dllexport) void user_terminate(void){foundation_reset();}




















