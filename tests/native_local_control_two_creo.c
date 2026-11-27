
#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static FILE *local_probe_log;
static wchar_t local_test_project[PRO_PATH_SIZE];
static wchar_t local_probe_role[12];
static uiCmdCmdId local_probe_cmd;
static UINT_PTR local_probe_timer;
static ULONGLONG local_probe_deadline;
static int local_probe_finished,local_test_captured;
static ProMdl local_probe_model;
static ProMdlName local_probe_model_name;
static ProDimension local_test_diameter;

static int local_test_path(ProPath path,const wchar_t *leaf){
    return swprintf_s(path,PRO_PATH_SIZE,L"%ls\\%ls",local_test_project,leaf)>=0;
}

static int local_test_complete(int pass){
    if(local_probe_timer)KillTimer(native_backend_window,local_probe_timer);
    if(!local_probe_finished&&local_probe_log){
        local_probe_finished=1;
        fwprintf(local_probe_log,L"LOCAL_CONTROL_FIXTURE_PASS=%d\n",pass);
        fclose(local_probe_log);
    }
    ProEngineerEnd();
    return 0;
}

static int local_test_seed(void){
    Fixture stock;
    ProMdl target=NULL;
    ProMdlName title;
    ProError error=fixture(&target,title,&stock);
    wchar_t operation[500];
    if(!error){
        swprintf_s(operation,_countof(operation),L"CREO_OP HOLE %ls TEST_BORE SURFACE:%d SURFACE:%d 26 SURFACE:%d 24 8 THROUGH 0",
            title,stock.top,stock.x,stock.y);
        error=expansion_build(target,operation,local_probe_log);
    }
    if(!error&&!storage_prepare_output())error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlSave(target);
    ProPath stored,proof;
    if(!error)error=ax_native_source(local_test_project,title,PRO_MDL_PART,stored);
    if(!error&&!local_test_path(proof,L"seed-model.txt"))error=PRO_TK_BAD_CONTEXT;
    if(!error){
        FILE *seed=_wfopen(proof,L"wt, ccs=UTF-8");
        if(!seed)error=PRO_TK_CANT_WRITE;
        else{
            fwprintf(seed,L"%ls\n%ls\n",title,stored);
            if(fclose(seed))error=PRO_TK_CANT_WRITE;
        }
    }
    fwprintf(local_probe_log,L"SEED_SAVE_CODE=%d\n",error);
    return local_test_complete(!error);
}

static ProError local_test_find_diameter(ProDimension *dimension,ProError status,ProAppData data){
    if(status)return status;
    ProDimensiontype type;
    double current;
    if(!ProDimensionTypeGet(dimension,&type)&&type==PRODIMTYPE_DIAMETER&&
       !ProDimensionValueGet(dimension,&current)&&part_close(8,current))local_test_diameter=*dimension;
    return 0;
}

static void CALLBACK local_test_tick(HWND window,UINT message,UINT_PTR timer,DWORD time){
    if(local_probe_finished)return;
    if(GetTickCount64()>local_probe_deadline){
        local_test_complete(0);
        return;
    }
    ProPath stop,action;
    if(local_test_path(stop,L"stop.txt")&&GetFileAttributesW(stop)!=INVALID_FILE_ATTRIBUTES){
        local_test_complete(1);
        return;
    }
    if(!local_test_captured&&local_test_path(action,L"plugin-save-second.txt")&&
       !wcscmp(local_probe_role,L"B")&&GetFileAttributesW(action)!=INVALID_FILE_ATTRIBUTES){
        local_test_captured=1;
        ProMacroLoad(L"~ Command \x60" L"BWTLLocalProbePluginSave\x60;");
    }
}

static int local_test_plugin_save(uiCmdCmdId cmd,uiCmdValue *value,void *data){
    PartSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    wchar_t wire[512],reply[2000]=L"";
    swprintf_s(wire,_countof(wire),L"CREO_OP SAVE %ls",local_probe_model_name);
    PartCommand action;
    ProError error=snapshot?part_snapshot_take(local_probe_model,snapshot):PRO_TK_OUT_OF_MEMORY;
    if(!error&&part_command_parse(wire,&action)!=1)error=PRO_TK_BAD_INPUTS;
    if(!error)error=part_execute(&action,snapshot,reply,_countof(reply));
    fwprintf(local_probe_log,L"PLUGIN_SAVE_CONFLICT=%d CODE=%d\n%ls\n",
        error==PRO_TK_E_IN_USE&&wcsstr(reply,L"保存冲突")!=NULL,error,reply);
    fflush(local_probe_log);
    if(snapshot){part_snapshot_clear(snapshot);free(snapshot);}
    return 0;
}

static int local_test_open(uiCmdCmdId cmd,uiCmdValue *value,void *data){
    local_probe_log=_wfopen(L"local-control-fixture.log",L"wt, ccs=UTF-8");
    if(!local_probe_log)return local_test_complete(0);
    setvbuf(local_probe_log,NULL,_IONBF,0);
    model_background_mode=1;
    if(!storage_directory_on_drive(local_test_project,L'E')||ProDirectoryChange(local_test_project))
        return local_test_complete(0);
    if(!wcscmp(local_probe_role,L"SEED"))return local_test_seed();
    ProPath seed,stored,origin;
    if(!local_test_path(seed,L"seed-model.txt"))return local_test_complete(0);
    FILE *info=_wfopen(seed,L"rt, ccs=UTF-8");
    if(!info)return local_test_complete(0);
    int ok=fgetws(local_probe_model_name,_countof(local_probe_model_name),info)&&fgetws(stored,_countof(stored),info);
    fclose(info);
    if(!ok)return local_test_complete(0);
    local_probe_model_name[wcscspn(local_probe_model_name,L"\r\n")]=0;
    stored[wcscspn(stored,L"\r\n")]=0;
    ProError error=ProMdlFiletypeLoad(stored,PRO_MDLFILE_PART,PRO_B_FALSE,&local_probe_model);
    ProFeature bore;
    if(!error)error=ux_named_feature(local_probe_model,L"TEST_BORE",&bore);
    if(!error)error=ProFeatureDimensionVisit(&bore,local_test_find_diameter,NULL,NULL);
    if(!error&&!local_test_diameter.owner)error=PRO_TK_E_NOT_FOUND;
    ProName symbol;
    if(!error)error=ProDimensionSymbolGet(&local_test_diameter,symbol);
    if(!error)error=dv_display_new(local_probe_model);
    if(!error)error=ProMdlOriginGet(local_probe_model,origin);
    if(error){
        fwprintf(local_probe_log,L"PROBE_SETUP_CODE=%d\n",error);
        return local_test_complete(0);
    }
    bwtl_save_observe(local_probe_model);
    local_probe_deadline=GetTickCount64()+300000;
    local_probe_timer=SetTimer(native_backend_window,7240,200,local_test_tick);
    fwprintf(local_probe_log,L"FIXTURE_READY=%d\nMODEL=%ls\nFEATURE=%d\nSYMBOL=%ls\nORIGIN=%ls\n",
        !!local_probe_timer,local_probe_model_name,bore.id,symbol,origin);
    if(!local_probe_timer)return local_test_complete(0);
    return 0;
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    DWORD p=GetEnvironmentVariableW(L"BWTL_TEST_LOCAL_PROJECT",local_test_project,_countof(local_test_project));
    DWORD r=GetEnvironmentVariableW(L"BWTL_TEST_LOCAL_ROLE",local_probe_role,_countof(local_probe_role));
    if(!p||p>=_countof(local_test_project)||!r||r>=_countof(local_probe_role)||
       (wcscmp(local_probe_role,L"SEED")&&wcscmp(local_probe_role,L"A")&&wcscmp(local_probe_role,L"B")))
        return PRO_TK_BAD_CONTEXT;
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    if(!error&&!bwtl_save_watches)bwtl_save_watch_start();
    if(!error)error=ProCmdActionAdd("BWTLLocalProbeOpen",local_test_open,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&local_probe_cmd);
    if(!error)error=ProCmdActionAdd("BWTLLocalProbePluginSave",local_test_plugin_save,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&local_probe_cmd);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLLocalProbeOpen\x60;");
}
