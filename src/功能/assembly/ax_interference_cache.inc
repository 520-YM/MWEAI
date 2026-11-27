#include <ProDisplist.h>
#include <ProGraphic.h>

typedef struct {
    int index[2];
    double volume;
} AxInterferencePair;

typedef struct {
    ProWVerstamp version;
    ProMatrix pose;
} AxInterferenceStamp;

typedef struct {
    ProMdl root;
    ProMdlName name;
    ProPath directory;
    AxScan *scan;
    AxInterferencePair *pairs;
    AxInterferenceStamp *stamps;
    ProWVerstamp version;
    int count, stale, visible, selected;
} AxInterferenceCache;

static AxInterferenceCache ax_interference_cache;
static struct { int window, exists; ProMdl model; } ax_interference_overlay;
enum { AX_INTERFERENCE_DISPLAY = 0x42574946, AX_INTERFERENCE_BOX_LIMIT = 256 };

/* Cache no opaque interference data or selections across TOOLKIT calls. */
static void ax_interference_result_free(AxInterferenceCache *result){
    if(result->version)ProVerstampFree(&result->version);
    if(result->stamps){
        for(int i=0;i<result->scan->count;i++){
            ProWVerstamp *version=&result->stamps[i].version;
            if(*version)ProVerstampFree(version);
        }
    }
    free(result->stamps);
    free(result->pairs);
    free(result->scan);
    *result=(AxInterferenceCache){0};
}

static ProError ax_interference_clear_overlay(void){
    if(!ax_interference_overlay.exists)return PRO_TK_NO_ERROR;
    ProError error=ProDisplist3dDelete(AX_INTERFERENCE_DISPLAY);
    if(error&&error!=PRO_TK_E_NOT_FOUND&&error!=PRO_TK_NOT_EXIST)return error;
    ProMdl model=NULL;
    const int window=ax_interference_overlay.window;
    const int repaint=!ProWindowMdlGet(window,&model)&&model==ax_interference_overlay.model;
    ax_interference_overlay.exists=0;
    return repaint?ProWindowRepaint(window):PRO_TK_NO_ERROR;
}

static void ax_interference_close(void){
    (void)ax_interference_clear_overlay();
    ax_interference_result_free(&ax_interference_cache);
}

static void ax_interference_mark_changed(ProMdl model){
    AxInterferenceCache *cache=&ax_interference_cache;
    if(!cache->scan)return;
    if(!model||model==cache->root)cache->stale=1;
    for(int i=0;!cache->stale&&i<cache->scan->count;i++)
        if(cache->scan->occurrences[i].model==model)cache->stale=1;
}

static void ax_interference_refresh_view(void){
    int window;
    ProMdl model=NULL,active=NULL;
    if(!ax_interference_overlay.exists)return;
    if(ax_interference_cache.stale||ProWindowCurrentGet(&window)||window!=ax_interference_overlay.window||
       ProWindowMdlGet(window,&model)||model!=ax_interference_overlay.model||
       ProMdlCurrentGet(&active)||active!=model)
        (void)ax_interference_clear_overlay();
}

static ProError ax_interference_occurrence_path(ProMdl root,const AxOccurrence *item,ProAsmcomppath *path){
    ProIdTable ids={0};
    memcpy(ids,item->ids,(size_t)item->depth*sizeof(int));
    return ProAsmcomppathInit((ProSolid)root,ids,item->depth,path);
}

static ProError ax_interference_stamp(ProMdl root,const AxOccurrence *item,AxInterferenceStamp *value){
    *value=(AxInterferenceStamp){0};
    if(item->state!=PRO_FEAT_ACTIVE)return PRO_TK_NO_ERROR;
    ProAsmcomppath path;
    ProError error=ax_interference_occurrence_path(root,item,&path);
    if(!error)error=ProAsmcomppathTrfGet(&path,PRO_B_TRUE,value->pose);
    if(!error)error=ProMdlVerstampGet(item->model,&value->version);
    if(!error&&!value->version)error=PRO_TK_INCOMPLETE;
    return error;
}

static ProError ax_interference_capture(AxInterferenceCache *result){
    ProError error=ProMdlVerstampGet(result->root,&result->version);
    if(!error&&!result->version)error=PRO_TK_INCOMPLETE;
    if(error||!result->scan->count)return error;
    result->stamps=calloc((size_t)result->scan->count,sizeof(*result->stamps));
    if(!result->stamps)return PRO_TK_OUT_OF_MEMORY;
    for(int i=0;!error&&i<result->scan->count;i++)
        error=ax_interference_stamp(result->root,&result->scan->occurrences[i],&result->stamps[i]);
    return error;
}

static ProError ax_interference_validate(AxInterferenceCache *cache,const wchar_t *directory){
    if(!cache->scan||!cache->version)return PRO_TK_E_NOT_FOUND;
    if(cache->stale||_wcsicmp(cache->directory,directory))return PRO_TK_BAD_CONTEXT;
    ProMdl root=NULL;
    ProError error=ProMdlnameInit(cache->name,PRO_MDL_ASSEMBLY,&root);
    if(error||root!=cache->root)return PRO_TK_BAD_CONTEXT;
    AxScan *scan=NULL;
    ProWVerstamp version=NULL;
    if(!(error=ax_master(root)))error=ProMdlVerstampGet(root,&version);
    if(!error&&(!version||!ProVerstampEqual(version,cache->version)))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_scan(root,1,&scan);
    if(!error&&scan->count!=cache->scan->count)error=PRO_TK_BAD_CONTEXT;
    for(int i=0;!error&&i<scan->count;i++){
        const AxOccurrence *current=&scan->occurrences[i],*stored=&cache->scan->occurrences[i];
        if(current->depth!=stored->depth||current->state!=stored->state||current->type!=stored->type||
           current->model!=stored->model||_wcsicmp(current->name,stored->name)||
           memcmp(current->ids,stored->ids,(size_t)current->depth*sizeof(int))){
            error=PRO_TK_BAD_CONTEXT;
            break;
        }
        if(current->state!=PRO_FEAT_ACTIVE)continue;
        AxInterferenceStamp observed;
        error=ax_interference_stamp(root,current,&observed);
        const AxInterferenceStamp *saved=&cache->stamps[i];
        if(!error&&(!ProVerstampEqual(observed.version,saved->version)||
           memcmp(observed.pose,saved->pose,sizeof(observed.pose))))error=PRO_TK_BAD_CONTEXT;
        if(observed.version)ProVerstampFree(&observed.version);
    }
    if(version)ProVerstampFree(&version);
    free(scan);
    return error;
}

static int ax_interference_index(const AxScan *scan,const int *ids,int depth){
    for(int i=0;i<scan->count;i++){
        const AxOccurrence *item=&scan->occurrences[i];
        if(item->depth==depth&&!memcmp(ids,item->ids,(size_t)depth*sizeof(int)))return i;
    }
    return -1;
}

static ProError ax_interference_end(const AxInterferenceCache *result,ProSelection selection,int *index){
    ProAsmcomppath path;
    if(!selection)return PRO_TK_BAD_INPUTS;
    ProError error=ProSelectionAsmcomppathGet(selection,&path);
    if(error)return error;
    if(path.owner!=(ProSolid)result->root||path.table_num<1||path.table_num>AX_PATH_DEPTH)return PRO_TK_BAD_CONTEXT;
    *index=ax_interference_index(result->scan,path.comp_id_table,path.table_num);
    if(*index<0)return PRO_TK_INCOMPLETE;
    return result->scan->occurrences[*index].type==PRO_MDL_PART?PRO_TK_NO_ERROR:PRO_TK_INVALID_TYPE;
}

static ProError ax_interference_compute_mode(ProMdl assembly,const wchar_t *directory,AxInterferenceCache *result,int strict,int *red){
    ProInterferenceInfo *pairs=NULL;
    result->root=assembly;
    wcscpy_s(result->directory,_countof(result->directory),directory);
    ProError error=ProMdlMdlnameGet(assembly,result->name);
    if(!error&&strict)error=ax_master(assembly);
    if(!error)error=ax_scan(assembly,strict,&result->scan);
    if(!error&&result->scan->parts>=2)
        error=ProFitGlobalinterferenceCompute((ProAssembly)assembly,PRO_FIT_PART,
            PRO_B_FALSE,PRO_B_FALSE,PRO_B_FALSE,&pairs);
    if(!error&&pairs)error=ProArraySizeGet((ProArray)pairs,&result->count);
    if(!error&&result->count){
        result->pairs=calloc((size_t)result->count,sizeof(*result->pairs));
        if(!result->pairs)error=PRO_TK_OUT_OF_MEMORY;
    }
    for(int i=0;!error&&i<result->count;i++){
        error=ax_interference_end(result,pairs[i].part_1,&result->pairs[i].index[0]);
        if(!error)error=ax_interference_end(result,pairs[i].part_2,&result->pairs[i].index[1]);
        double volume=0;
        result->pairs[i].volume=NAN;
        if(pairs[i].interf_data&&!ProFitInterferencevolumeCompute(pairs[i].interf_data,&volume)&&
           isfinite(volume)&&volume>=0)result->pairs[i].volume=volume;
    }
    if(!error&&red){
        for(int i=0;i<result->count;i++)
            if(pairs[i].interf_data&&!ProFitInterferencevolumeDisplay(pairs[i].interf_data,PRO_COLOR_ERROR))(*red)++;
    }
    if(pairs)ProInterferenceInfoProarrayFree(pairs);
    return error;
}

static ProError ax_interference_compute(ProMdl assembly,const wchar_t *directory,AxInterferenceCache *result){
    return ax_interference_compute_mode(assembly,directory,result,1,NULL);
}
typedef struct {
    const AxOccurrence *item;
    ProMatrix from_root;
    int window;
} AxInterferenceView;

static ProError ax_interference_view(const AxInterferenceCache *cache,ProMdl model,const wchar_t *selector,
                                    AxInterferenceView *view,PartText *out){
    ProMdl active=NULL,window_model=NULL;
    ProError error=ProMdlCurrentGet(&active);
    if(!error)error=ProWindowCurrentGet(&view->window);
    if(!error)error=ProWindowMdlGet(view->window,&window_model);
    if(error)return error;
    if(active!=model||window_model!=model)return PRO_TK_BAD_CONTEXT;
    for(int i=0;i<4;i++)view->from_root[i][i]=1;
    if(model==cache->root&&!selector)return PRO_TK_NO_ERROR;

    if(selector){
        ProIdTable ids={0};
        const int depth=ax_path_parse(selector,ids);
        const int index=depth?ax_interference_index(cache->scan,ids,depth):-1;
        if(index>=0)view->item=&cache->scan->occurrences[index];
    }else{
        for(int i=0;i<cache->scan->count;i++){
            const AxOccurrence *item=&cache->scan->occurrences[i];
            if(item->model!=model||item->state!=PRO_FEAT_ACTIVE)continue;
            if(view->item){
                part_text_add(out,L"当前文件在总装里有多个实例。请用 AT ID:实例路径指定要复用的那一处。\n");
                return PRO_TK_BAD_CONTEXT;
            }
            view->item=item;
        }
    }
    if(!view->item||view->item->model!=model||view->item->state!=PRO_FEAT_ACTIVE){
        part_text_add(out,L"当前窗口与干涉结果中的组件实例不一致。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    ProAsmcomppath path;
    error=ax_interference_occurrence_path(cache->root,view->item,&path);
    return error?error:ProAsmcomppathTrfGet(&path,PRO_B_FALSE,view->from_root);
}

static int ax_interference_contains(const AxOccurrence *parent,const AxOccurrence *item){
    return !parent||(item->depth>=parent->depth&&
        !memcmp(parent->ids,item->ids,(size_t)parent->depth*sizeof(int)));
}

static int ax_interference_in_view(const AxInterferenceCache *cache,const AxInterferenceView *view,int index){
    const AxInterferencePair *pair=&cache->pairs[index];
    return ax_interference_contains(view->item,&cache->scan->occurrences[pair->index[0]])||
           ax_interference_contains(view->item,&cache->scan->occurrences[pair->index[1]]);
}
