
#define BWTL_REUSE_EXPANSION_PATTERN
#include "native_expansion_pattern.c"

static FILE *local_actions_log;
static ProMdl local_actions_model;
static ProMdlName local_actions_name;
static int local_actions_feature;
static wchar_t local_actions_reply[10000];

static int local_actions_label(const wchar_t *label,int pass){
    fwprintf(local_actions_log,L"%ls=%d\n",label,pass!=0);
    fflush(local_actions_log);
    return pass!=0;
}

static int local_actions_finish(int pass){
    local_actions_label(L"BWTL_LOCAL_ACTIONS_PASS",pass);
    fclose(local_actions_log);
    local_actions_log=NULL;
    ProEngineerEnd();
    return 0;
}

static int local_actions_model_paths(void){
    AxSessionSelector part={0},assembly={0},invalid={0};
    const int typed=local_control_model_selector(L"SAME_NAME.PRT",0,&part)&&
        local_control_model_selector(L"SAME_NAME.ASM",0,&assembly)&&
        !wcscmp(part.name,assembly.name)&&part.type==PRO_MDL_PART&&assembly.type==PRO_MDL_ASSEMBLY&&
        !local_control_model_selector(L"SAME_NAME",0,&invalid)&&
        !local_control_model_selector(L"SAME_NAME.PRT.7",0,&invalid)&&
        !local_control_model_selector(L"SAME_NAME.PRT.+7",1,&invalid)&&
        !local_control_model_selector(L"SAME_NAME.PRT.0",1,&invalid);
    if(!local_actions_label(L"LOCAL_MODEL_SELECTOR_PRESERVES_TYPE",typed))return 0;

    ProPath work,approved,file,outside,resolved;
    wchar_t canonical_work[MAX_PATH];
    if(ProDirectoryCurrentGet(work)||!or_real_path(work,1,canonical_work)||
       swprintf_s(approved,_countof(approved),L"%ls%lslocal-open-%llu",canonical_work,
           canonical_work[wcslen(canonical_work)-1]==L'\\'?L"":L"\\",GetTickCount64())<0||
       swprintf_s(file,_countof(file),L"%ls\\proof.prt.7",approved)<0||
       swprintf_s(outside,_countof(outside),L"%ls\\..\\proof.prt.7",approved)<0||
       !CreateDirectoryW(approved,NULL))return 0;
    HANDLE fixture_file=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    int pass=0;
    if(fixture_file!=INVALID_HANDLE_VALUE){
        DWORD written=0;
        const int created=WriteFile(fixture_file,"fixture",7,&written,NULL)&&written==7;
        CloseHandle(fixture_file);
        AxSessionSelector selected={0};
        pass=created&&!local_control_open_path(approved,L"proof.prt.7",resolved,&selected)&&
            selected.type==PRO_MDL_PART&&!wcscmp(selected.name,L"proof")&&
            !_wcsicmp(file,resolved)&&
            local_control_open_path(approved,outside,resolved,&selected)==PRO_TK_NO_PERMISSION&&
            local_control_open_path(approved,L"C:\\proof.prt.7",resolved,&selected)==PRO_TK_BAD_INPUTS&&
            local_control_open_path(approved,L"proof.prt.8",resolved,&selected)==PRO_TK_E_NOT_FOUND;
        if(!DeleteFileW(file))pass=0;
    }
    if(!RemoveDirectoryW(approved))pass=0;
    return local_actions_label(L"LOCAL_MODEL_OPEN_KEEPS_EXACT_REVISION_AND_DIRECTORY",pass);
}

static int local_actions_status(wchar_t context[72]){
    ProError error=local_control_dispatch("STATUS",L"",L"",local_actions_reply,_countof(local_actions_reply));
    fwprintf(local_actions_log,L"STATUS_CODE=%d\nSTATUS_REPLY=%ls\n",error,local_actions_reply);
    if(error){
        LocalControlContext seen;
        ProError native_error=local_control_collect(&seen);
        ProMdl owner_check=NULL;
        int window_check=-99;
        ProError model_error=ProMdlCurrentGet(&owner_check);
        ProError window_error=ProWindowCurrentGet(&window_check);
        ProMdl owner_at_window=NULL;
        ProError owner_error=window_error?window_error:ProWindowMdlGet(window_check,&owner_at_window);
        fwprintf(local_actions_log,L"COLLECT=%d DIR=%ls MODEL_CODE=%d WINDOW_CODE=%d OWNER_CODE=%d SAME_OWNER=%d SUBTYPE=%d TYPE=%d STAMP=%hs\n",native_error,seen.directory,model_error,window_error,owner_error,owner_check==owner_at_window,seen.subtype,seen.type,seen.stamp);
        return 0;
    }
    const wchar_t *key=wcsstr(local_actions_reply,L"\ncontext=");
    if(!key)return 0;
    key+=9;
    const wchar_t *end=wcschr(key,L'\n');
    return end&&end>key&&end-key<72&&!wcsncpy_s(context,72,key,end-key);
}

static int local_actions_readback(const ProDimension *source,double expected){
    ProDimension dim=*source;
    double measured;
    if(ProDimensionValueGet(&dim,&measured))return 0;
    fwprintf(local_actions_log,L"ACTUAL_NATIVE_DIAMETER_MM=%.9g\n",measured);
    return part_close(measured,expected);
}

static int local_actions_assembly(uiCmdCmdId cmd,uiCmdValue *unused,void *data){
    wchar_t context[72],read_tree[160];
    int pass=local_actions_status(context)&&wcsstr(local_actions_reply,L"\ntype=ASM\n");
    if(!local_actions_label(L"ASSEMBLY_WINDOW_AND_TYPE",pass))return local_actions_finish(0);
    LocalControlContext current;
    if(local_control_collect(&current)||current.model!=current.owner)return local_actions_finish(0);
    swprintf_s(read_tree,_countof(read_tree),L"CREO_OP ASM_TREE %ls",current.name);
    ProError error=local_control_dispatch("READ",context,read_tree,local_actions_reply,_countof(local_actions_reply));
    fwprintf(local_actions_log,L"ASSEMBLY_READ_CODE=%d\nASSEMBLY_READ_REPLY=%ls\n",error,local_actions_reply);
    return local_actions_finish(local_actions_label(L"ASSEMBLY_TREE_READ",!error));
}

static int local_actions_verify(uiCmdCmdId cmd,uiCmdValue *unused,void *data){
    wchar_t context[72],new_context[72];
    wchar_t protocol[256],invalid[512];
    int pass=local_actions_status(context)&&wcsstr(local_actions_reply,L"\ntype=PRT\n")&&
        wcsstr(local_actions_reply,local_actions_name)&&wcsstr(local_actions_reply,L"\nworkDir=E:")&&
        wcsstr(local_actions_reply,L"\nstamp=")&&wcsstr(local_actions_reply,L"\nwindow=");
    if(!local_actions_label(L"ACTUAL_PROCESS_WINDOW_AND_MODEL",pass))return local_actions_finish(0);

    ProFeature feature;
    ExpansionDiameter receipt={0};
    ProName symbol;
    ProError error=ProFeatureInit((ProSolid)local_actions_model,local_actions_feature,&feature);
    if(!error)error=ProFeatureDimensionVisit(&feature,expansion_diameter,NULL,&receipt);
    if(!error&&receipt.count==1)error=ProDimensionSymbolGet(&receipt.dimension,symbol);
    if(error||receipt.count!=1)return local_actions_finish(0);
    pass=!local_control_dispatch("READ",context,L"MODEL",local_actions_reply,_countof(local_actions_reply))&&
        wcsstr(local_actions_reply,symbol)&&wcsstr(local_actions_reply,L"VALUE=6");
    if(!local_actions_label(L"READ_NATIVE_FEATURE_AND_DRIVE_DIMENSION",pass))return local_actions_finish(0);

    swprintf_s(protocol,_countof(protocol),L"CREO_OP DIMS %ls %d %ls 6 9",
        local_actions_name,local_actions_feature,symbol);
    pass=local_control_dispatch("READ",context,protocol,local_actions_reply,_countof(local_actions_reply))==PRO_TK_UNSUPPORTED&&
        local_control_dispatch("EXEC",L"wrong",protocol,local_actions_reply,_countof(local_actions_reply))==PRO_TK_BAD_CONTEXT;
    swprintf_s(invalid,_countof(invalid),L"%ls\n%ls",protocol,protocol);
    pass=pass&&local_control_dispatch("EXEC",context,invalid,local_actions_reply,_countof(local_actions_reply))==PRO_TK_BAD_INPUTS;
    swprintf_s(invalid,_countof(invalid),L"CREO_OP SAVE %ls",local_actions_name);
    pass=pass&&local_control_dispatch("EXEC",context,invalid,local_actions_reply,_countof(local_actions_reply))==PRO_TK_UNSUPPORTED&&
        local_control_dispatch("SAVE",context,L"E:\\unbound.prt",local_actions_reply,_countof(local_actions_reply))==PRO_TK_BAD_INPUTS;
    if(!local_actions_label(L"WRONG_TOKEN_READ_WRITE_AND_RAW_SAVE_BLOCKED",pass&&local_actions_readback(&receipt.dimension,6)))return local_actions_finish(0);

    swprintf_s(invalid,_countof(invalid),L"CREO_OP DIMS wrong_model %d %ls 6 9",local_actions_feature,symbol);
    pass=local_control_dispatch("EXEC",context,invalid,local_actions_reply,_countof(local_actions_reply))==PRO_TK_BAD_CONTEXT;
    if(!local_actions_label(L"WRONG_MODEL_BLOCKED",pass))return local_actions_finish(0);

    error=local_control_dispatch("EXEC",context,protocol,local_actions_reply,_countof(local_actions_reply));
    fwprintf(local_actions_log,L"EDIT_CODE=%d\nEDIT_RESULT=%ls\n",error,local_actions_reply);
    if(!local_actions_label(L"BOUND_NATIVE_DIAMETER_EDIT",!error&&local_actions_readback(&receipt.dimension,9)))return local_actions_finish(0);
    pass=local_control_dispatch("EXEC",context,protocol,local_actions_reply,_countof(local_actions_reply))==PRO_TK_BAD_CONTEXT&&
        local_actions_status(new_context)&&wcscmp(context,new_context);
    if(!local_actions_label(L"STALE_BEFORE_WRITE_TOKEN_REJECTED",pass))return local_actions_finish(0);
    error=local_control_dispatch("READ",new_context,L"MODEL",local_actions_reply,_countof(local_actions_reply));
    if(!local_actions_label(L"READBACK_AFTER_EXTERNAL_WRITE",!error&&wcsstr(local_actions_reply,L"VALUE=9")))return local_actions_finish(0);

    ProPath original,new_directory;
    if(ProDirectoryCurrentGet(original)||swprintf_s(new_directory,_countof(new_directory),L"%ls\\changed",original)<0||
       !CreateDirectoryW(new_directory,NULL)||ProDirectoryChange(new_directory))return local_actions_finish(0);
    pass=local_control_dispatch("EXEC",new_context,protocol,local_actions_reply,_countof(local_actions_reply))==PRO_TK_BAD_CONTEXT;
    if(ProDirectoryChange(original))return local_actions_finish(0);
    if(!local_actions_label(L"WORK_DIRECTORY_SWITCH_BLOCKS_OBSERVED_TARGET",pass))return local_actions_finish(0);
    model_background_mode=0;
    const char *stage="new assembly";
    ProMdlName assembly_name;
    ProError assembly_error=model_create_empty(3,assembly_name,&stage);
    if(assembly_error){
        fwprintf(local_actions_log,L"ASSEMBLY_SETUP_CODE=%d STAGE=%hs\n",assembly_error,stage);
        return local_actions_finish(0);
    }
    return ProMacroLoad(L"~ Command \x60" L"BWTLLocalActionsAssembly\x60;");
}

static int local_actions_prepare(uiCmdCmdId cmd,uiCmdValue *unused,void *data){
    local_actions_log=_wfopen(L"local-control-actions.log",L"wt, ccs=UTF-8");
    if(!local_actions_log){ProEngineerEnd();return 0;}
    if(!local_actions_model_paths())return local_actions_finish(0);
    wchar_t no_model[72];
    if(!local_actions_label(L"EMPTY_SESSION_STATUS",local_actions_status(no_model)&&wcsstr(local_actions_reply,L"\ntype=NONE\n")))return local_actions_finish(0);
    model_background_mode=1;
    Fixture faces;
    ProError error=fixture(&local_actions_model,local_actions_name,&faces);
    if(!error){
        wchar_t create[256];
        swprintf_s(create,_countof(create),L"CREO_OP HOLE %ls TARGET SURFACE:%d SURFACE:%d 24 SURFACE:%d 24 6 THROUGH 0",
            local_actions_name,faces.top,faces.x,faces.y);
        error=expansion_build(local_actions_model,create,local_actions_log);
    }
    ProModelitem feature;
    if(!error)error=ProMdlMdlnameGet(local_actions_model,local_actions_name);
    if(!error)error=ProModelitemByNameInit(local_actions_model,PRO_FEATURE,L"TARGET",&feature);
    if(!error){local_actions_feature=feature.id;error=dv_display_new(local_actions_model);}
    if(error)return local_actions_finish(0);
    return ProMacroLoad(L"~ Command \x60" L"BWTLLocalActionsVerify\x60;");
}

__declspec(dllexport) int user_initialize(int argc,char **argv,char *version,char *build,wchar_t buffer[80]){
    uiCmdCmdId cmd;
    ProError error=expansion_pattern_initialize(argc,argv,version,build,buffer);
    if(!error)error=ProCmdActionAdd("BWTLLocalActionsPrepare",local_actions_prepare,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!error)error=ProCmdActionAdd("BWTLLocalActionsVerify",local_actions_verify,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    if(!error)error=ProCmdActionAdd("BWTLLocalActionsAssembly",local_actions_assembly,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&cmd);
    return error?error:ProMacroLoad(L"~ Command \x60" L"BWTLLocalActionsPrepare\x60;");
}
