/* Verify the exact family row and its native geometry before saving a project part. */
static ProError fastener_bso_verify_copy(ProMdl source,ProMdl copy,PartText *out);

static const wchar_t *fastener_bso_instance_name(int length){
    if(length==5)return L"BSO-3_5M3X5";
    if(length==6)return L"BSO_3_5M3X6";
    return NULL;
}

static ProError fastener_bso_measure_length(ProMdl model,int length,PartText *out){
    ProMdlType kind;
    ProOutline outline;
    double mm;
    ProError error=ProMdlTypeGet(model,&kind);
    if(!error&&kind!=PRO_MDL_PART)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=part_health(model);
    if(!error)error=qa_mm_per_unit(model,&mm);
    if(!error)error=ProSolidOutlineGet((ProSolid)model,outline);
    if(error){
        part_text_add(out,L"型号核对｜Creo 未取得可验证的紧固件实体外形（%d）。\n",error);
        return error;
    }
    double axial=fabs(outline[1][2]-outline[0][2])*mm;
    if(!part_close(axial,(double)length)){
        part_text_add(out,L"型号核对｜规格长度为 %d mm，实际沿模型轴线量得 %.8g mm，已停止选件。\n",length,axial);
        return PRO_TK_BAD_CONTEXT;
    }
    return PRO_TK_NO_ERROR;
}

static ProError fastener_bso_report_conflict(ProMdl model,const wchar_t *label,
    const wchar_t *expected,const wchar_t *source,PartText *out){
    ProPath origin=L"";
    ProBoolean modified=PRO_B_TRUE;
    ProError read_state=ProMdlModificationVerify(model,&modified);
    (void)ProMdlOriginGet(model,origin);
    part_text_add(out,L"%ls｜Creo 已加载同名模型 %ls。状态：%ls；内存来源：%ls；指定公司库：%ls。\n"
        L"当前模型的未保存状态和窗口保留。可在独立 Creo 会话读取正确的库件，并从已验收的精确实例另存项目件。\n",
        label,expected,read_state?L"保存状态无法核对":modified?L"有未保存修改":L"库件来源或族表身份不一致",
        origin[0]?origin:L"无可读取的保存路径",source);
    return PRO_TK_E_IN_USE;
}

static ProError fastener_bso_read_exact(int length,ProMdl *model,wchar_t path[MAX_PATH],PartText *out){
    const UINT category=COMPANY_OPEN_FASTENER_FIRST+4;
    const wchar_t *const generic_name=L"BSO_3_5M3XX";
    const wchar_t *requested_name=fastener_bso_instance_name(length);
    wchar_t folder[MAX_PATH],verified[MAX_PATH];
    *model=NULL;
    path[0]=0;
    if(!requested_name)return PRO_TK_BAD_INPUTS;
    if(!company_fastener_resolve_report(category,folder,out))return PRO_TK_CANT_OPEN;

    ProPath candidate;
    ProError error=ax_native_source(folder,generic_name,PRO_MDL_PART,candidate);
    if(error){
        part_text_add(out,L"库件查找｜压铆件目录可读，未找到可读取的 %ls.PRT（Creo %d）。\n",generic_name,error);
        return error;
    }
    HANDLE file=fastener_chat_open_source(candidate,verified);
    if(file==INVALID_HANDLE_VALUE){
        part_text_add(out,L"库件来源核对｜找到 %ls，未能核对实际文件访问和库范围。\n",candidate);
        return PRO_TK_CANT_OPEN;
    }
    NativeFile source={0};
    HANDLE lock=company_library_source(verified,&source,out);
    if(lock==INVALID_HANDLE_VALUE){CloseHandle(file);return PRO_TK_CANT_OPEN;}

    ProMdl generic=NULL,found=NULL,owner=NULL;
    ProFamtable table;
    ProFaminstance instance;
    ProName requested;
    wcscpy_s(requested,_countof(requested),requested_name);
    int loaded=0,already_in_session=0,locked=0;
    wchar_t native_report[900]=L"";
    PartText detail={native_report,_countof(native_report),0};
    error=attachment_open_native(&source,&generic,&loaded,&detail);
    if(error==PRO_TK_E_IN_USE&&generic&&!loaded){
        error=fastener_bso_report_conflict(generic,L"库件来源核对",generic_name,verified,out);
        goto done;
    }
    if(error){
        part_text_add(out,L"库件读取｜Creo 无法打开指定母件（%d）。%ls\n",error,native_report);
        goto done;
    }
    error=ProFamtableInit(generic,&table);
    if(!error)error=ProFaminstanceInit(requested,&table,&instance);
    if(!error)error=ProFaminstanceCheck(&instance,&locked);
    if(error){
        part_text_add(out,L"族表实例核对｜库件 %ls 无法确认含有准确的 %ls 行（Creo %d）。\n",verified,requested,error);
        goto done;
    }
    error=ProMdlnameInit(requested,PRO_MDL_PART,&found);
    if(error==PRO_TK_E_NOT_FOUND)error=ProFaminstanceMdlGet(&instance,&found);
    if(error==PRO_TK_E_NOT_FOUND)error=ProFaminstanceRetrieve(&instance,&found);
    else already_in_session=!error;
    if(error||!found){
        part_text_add(out,L"族表实例读取｜没有取得 %ls 的 Creo 实体（%d）。\n",requested,error);
        if(!error)error=PRO_TK_BAD_CONTEXT;
        goto done;
    }

    ProMdlName actual;
    error=ProFaminstanceGenericGet(found,PRO_B_TRUE,&owner);
    if(!error)error=ProMdlMdlnameGet(found,actual);
    if(!error&&(owner!=generic||_wcsicmp(actual,requested)))error=PRO_TK_BAD_CONTEXT;
    if(already_in_session){
        ProBoolean modified=PRO_B_TRUE;
        if(error||ProMdlModificationVerify(found,&modified)||modified){
            error=fastener_bso_report_conflict(found,L"族表实例核对",requested,verified,out);
            goto done;
        }
    }
    if(error){
        part_text_add(out,L"族表实例核对｜读取结果的实际名称或所属母件与请求规格不一致（Creo %d）。\n",error);
        goto done;
    }
    error=fastener_bso_measure_length(found,length,out);
    if(!error){
        *model=found;
        wcscpy_s(path,MAX_PATH,verified);
        part_text_add(out,L"已从公司库核对 BSO M3×%d，Creo 实例 %ls，实测轴向长度 %d mm。\n",length,requested,length);
    }
done:
    CloseHandle(lock);
    CloseHandle(file);
    return error;
}

static ProError fastener_bso_read(ProMdl *model,wchar_t path[MAX_PATH],PartText *out){
    return fastener_bso_read_exact(6,model,path,out);
}

/* The staged copy is an independent part, with a separate name from its family
   instance. A pre-existing project part may be reused only after shape readback. */
typedef ProError (*FastenerCopyVerify)(ProMdl source,ProMdl copy,PartText *out);

static int fastener_project_bytes_match(const FastenerSavedView *known,const wchar_t *project){
    if(!known||!company_is_under(known->path,known->stage))return 0;
    wchar_t path[MAX_PATH];
    HANDLE staged=company_open_path(known->path,0,path);
    if(staged==INVALID_HANDLE_VALUE)return 0;
    BY_HANDLE_FILE_INFORMATION now;
    const int same=GetFileInformationByHandle(staged,&now)&&company_source_matches(&now,&known->source)&&
        fastener_equal_files(path,project);
    CloseHandle(staged);
    return same;
}

static int fastener_project_from_stage(ProMdl model,const wchar_t *project){
    /* A native assembly operation can mark its exact component handle as
       modified. Only the version verified synchronously by that operation
       is eligible for reuse in addition to normally unchanged models. */
    const FastenerSavedView *known=fastener_saved_model(model);
    ProBoolean modified=PRO_B_TRUE;
    return known&&!ProMdlModificationVerify(model,&modified)&&
        (!modified||known->verified_placement_stamp)&&fastener_project_bytes_match(known,project);
}
static ProError fastener_bso_reuse_project(ProMdl source,const wchar_t *existing,ProMdl *copy,
    FastenerCopyVerify verify,PartText *out){
    NativeFile file={0};
    HANDLE lock=company_library_source(existing,&file,out);
    if(lock==INVALID_HANDLE_VALUE)return PRO_TK_CANT_OPEN;
    int loaded=0;
    ProError error=attachment_open_native(&file,copy,&loaded,out);
    if(error==PRO_TK_E_IN_USE&&*copy&&!loaded&&fastener_project_from_stage(*copy,existing)){
        out->text[0]=0;
        out->truncated=0;
        error=PRO_TK_NO_ERROR;
    }
    if(!error)error=verify(source,*copy,out);
    if(!error&&!fastener_saved_model(*copy)){
        wchar_t directory[MAX_PATH];
        const wchar_t *leaf=wcsrchr(existing,L'\\');
        if(leaf&&!wcsncpy_s(directory,_countof(directory),existing,leaf-existing))
            fastener_saved_record_at(existing,lock,*copy,directory,1);
    }
    CloseHandle(lock);
    if(error){
        if(loaded&&*copy)ProMdlErase(*copy);
        *copy=NULL;
    }
    return error;
}

static ProError fastener_copy_project_named(ProMdl source,const wchar_t *stem,ProMdl *copy,
    wchar_t saved[MAX_PATH],FastenerCopyVerify verify,PartText *out){
    ProPath directory,existing;
    ProMdlName project_name;
    *copy=NULL;
    saved[0]=0;
    if(ProDirectoryCurrentGet(directory))return PRO_TK_BAD_CONTEXT;
    company_trim_path(directory);
    if(!storage_output_directory(directory)){
        part_text_add(out,L"项目件保存｜当前 Creo 工作目录不可写，请切换到数据盘项目目录。");
        return PRO_TK_BAD_CONTEXT;
    }
    ProError error;
    int slot;
    for(slot=1;slot<=20;slot++){
        if(slot==1)wcscpy_s(project_name,_countof(project_name),stem);
        else swprintf_s(project_name,_countof(project_name),L"%ls_%d",stem,slot);
        error=ax_native_source(directory,project_name,PRO_MDL_PART,existing);
        if(!error){
            wchar_t detail_text[800]=L"";
            PartText detail={detail_text,_countof(detail_text),0};
            if(!fastener_bso_reuse_project(source,existing,copy,verify,&detail)){
                wcscpy_s(saved,MAX_PATH,existing);
                return PRO_TK_NO_ERROR;
            }
            continue;
        }
        if(error!=PRO_TK_E_NOT_FOUND)return error;
        error=ax_target_collision(project_name,PRO_MDL_PART);
        if(!error)break;
        if(error!=PRO_TK_E_FOUND)return error;
    }
    if(slot>20){
        part_text_add(out,L"项目件保存｜没有找到空闲或已验证相同的项目件名称，现有模型均保留。\n");
        return PRO_TK_E_FOUND;
    }
    static LONG serial;
    wchar_t stage[MAX_PATH];
    if(!storage_output_directory(ai_dir)||
       swprintf_s(stage,_countof(stage),L"%ls\\fastener-%lu",ai_dir,InterlockedIncrement(&serial))<0||
       !CreateDirectoryW(stage,NULL))return PRO_TK_CANT_WRITE;
    error=ProDirectoryChange(stage);
    if(error)return error;
    error=ProMdlnameCopy(source,project_name,copy);
    ProError restored=ProDirectoryChange(directory);
    if(restored){
        part_text_add(out,L"项目件保存｜Creo 恢复原工作目录失败（%d），已停止后续定位。\n",restored);
        return restored;
    }
    if(!error)error=verify(source,*copy,out);
    if(!error)error=ax_native_source(stage,project_name,PRO_MDL_PART,existing);
    if(!error&&swprintf_s(saved,MAX_PATH,L"%ls\\%ls",directory,wcsrchr(existing,L'\\')+1)<0)error=PRO_TK_BAD_INPUTS;
    if(!error&&(!CopyFileW(existing,saved,TRUE)&&!fastener_equal_files(existing,saved)))error=PRO_TK_CANT_WRITE;
    BwtlSaveFile published={0};
    if(!error)error=bwtl_save_read_file(saved,&published);
    if(!error&&!fastener_equal_files(existing,saved))error=PRO_TK_CANT_WRITE;
    if(!error)error=bwtl_save_note_published_copy(*copy,directory,&published);
    if(!error){
        HANDLE staged=CreateFileW(existing,FILE_READ_ATTRIBUTES,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        if(staged!=INVALID_HANDLE_VALUE){
            wcscpy_s(fastener_last_stage,MAX_PATH,stage);
            fastener_saved_record(existing,staged,*copy);
            CloseHandle(staged);
        }
        if(slot>1)part_text_add(out,L"已有同名项目件或会话模型，独立另存为 %ls，保留现有模型。\n",project_name);
    }else if(*copy){ProMdlErase(*copy);*copy=NULL;}
    return error;
}

static ProError fastener_bso_copy_exact(int length,ProMdl source,ProMdl *copy,wchar_t saved[MAX_PATH],PartText *out){
    *copy=NULL;
    saved[0]=0;
    if(!fastener_bso_instance_name(length))return PRO_TK_BAD_INPUTS;
    ProError error=fastener_bso_measure_length(source,length,out);
    if(error)return error;
    ProName name;
    swprintf_s(name,_countof(name),L"BSO_3_5M3X%d_BWTL",length);
    return fastener_copy_project_named(source,name,copy,saved,fastener_bso_verify_copy,out);
}

static ProError fastener_bso_copy(ProMdl source,ProMdl *copy,wchar_t saved[MAX_PATH],PartText *out){
    return fastener_bso_copy_exact(6,source,copy,saved,out);
}

