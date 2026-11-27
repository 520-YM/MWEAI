/* One explicit company preference per nominal diameter and trial panel thickness. */
#define PRESSFIT_PREFERENCE_HEADER L"BWTL_PRESSFIT_PREFERENCE 1\n"

static HANDLE pressfit_preference_path(double hole,double sheet,int create,wchar_t path[MAX_PATH]){
    if(!isfinite(hole)||!isfinite(sheet)||hole<=0.0||sheet<=0.0||hole>1000.0||sheet>1000.0)
        return INVALID_HANDLE_VALUE;
    wchar_t directory[MAX_PATH];
    HANDLE folder=experience_directory(directory,create);
    if(folder==INVALID_HANDLE_VALUE)return folder;
    if(!storage_output_directory(directory)||
       swprintf_s(path,MAX_PATH,L"%ls\\BWTL-PressfitPreferred-%ld-%ld.bwtl",
           directory,lround(1000*hole),lround(1000*sheet))<0){
        CloseHandle(folder);
        return INVALID_HANDLE_VALUE;
    }
    return folder;
}

static const PressfitAcceptance *pressfit_preference_parse(const wchar_t *record,double hole,double sheet){
    const size_t header=wcslen(PRESSFIT_PREFERENCE_HEADER);
    if(wcsncmp(record,PRESSFIT_PREFERENCE_HEADER,header))return NULL;
    const wchar_t *body=record+header;
    const wchar_t *newline=wcschr(body,L'\n');
    wchar_t wanted[MAX_PATH];
    if(!newline||wcsncpy_s(wanted,_countof(wanted),body,newline-body)||
       wcslen(newline+1)!=65||newline[65]!=L'\n')return NULL;
    for(size_t i=0;i+1<_countof(pressfit_acceptance_rows);i++){
        const PressfitAcceptance *row=pressfit_acceptance_rows+i;
        wchar_t expected[MAX_PATH];
        if(pressfit_acceptance_equal(hole,row->library_hole_mm)&&
           pressfit_acceptance_equal(sheet,row->trial_thickness_mm)&&
           pressfit_acceptance_selector(row,expected)&&!_wcsicmp(wanted,expected)&&
           !_wcsnicmp(newline+1,row->sha256,64))return row;
    }
    return NULL;
}

static const PressfitAcceptance *pressfit_preference_read(double hole,double sheet){
    wchar_t path[MAX_PATH],record[512];
    HANDLE folder=pressfit_preference_path(hole,sheet,0,path);
    if(folder==INVALID_HANDLE_VALUE)return NULL;
    const PressfitAcceptance *result=experience_read(path,record,_countof(record))?
        pressfit_preference_parse(record,hole,sheet):NULL;
    CloseHandle(folder);
    return result;
}

static ProError pressfit_preference_save(ProMdl model,const wchar_t *source,PartText *out){
    const PressfitAcceptance *row=pressfit_acceptance_lookup(model,source);
    const double hole=row?pressfit_acceptance_nominal(row,source):0.0;
    if(!row||hole<=0.0){
        part_text_add(out,L"需要源文件、公司目录底孔和 Creo 试装三者都能核对，才能保存为自动选型偏好。\n");
        return PRO_TK_BAD_CONTEXT;
    }
    wchar_t selector[MAX_PATH],path[MAX_PATH],pending[MAX_PATH]=L"",record[512],readback[512];
    const double sheet=row->trial_thickness_mm;
    if(!pressfit_acceptance_selector(row,selector))return PRO_TK_BAD_INPUTS;
    HANDLE folder=pressfit_preference_path(hole,sheet,1,path);
    if(folder==INVALID_HANDLE_VALUE)return PRO_TK_CANT_WRITE;
    ProError error=PRO_TK_CANT_WRITE;
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES){
        if(!experience_read(path,readback,_countof(readback))||
           !pressfit_preference_parse(readback,hole,sheet)){
            part_text_add(out,L"既有选型偏好文件读回不一致，已保留原文件。\n");
            CloseHandle(folder);
            return PRO_TK_CANT_WRITE;
        }
    }
    static LONG serial;
    if(swprintf_s(record,_countof(record),L"%ls%ls\n%ls\n",PRESSFIT_PREFERENCE_HEADER,selector,row->sha256)>=0&&
       swprintf_s(pending,MAX_PATH,L"%ls.%lu.%ld.pending",path,GetCurrentProcessId(),InterlockedIncrement(&serial))>=0){
        char bytes[1024];
        const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,record,-1,bytes,sizeof(bytes),NULL,NULL);
        HANDLE file=size>1?CreateFileW(pending,GENERIC_WRITE,0,NULL,CREATE_NEW,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
        if(file!=INVALID_HANDLE_VALUE){
            DWORD written;
            BY_HANDLE_FILE_INFORMATION info;
            const int safe=modeling_rule_file_matches(file,pending,0,&info)&&
                WriteFile(file,bytes,size-1,&written,NULL)&&written==(DWORD)size-1&&FlushFileBuffers(file);
            CloseHandle(file);
            if(safe&&experience_read(pending,readback,_countof(readback))&&!wcscmp(record,readback)&&
               MoveFileExW(pending,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)&&
               experience_read(path,readback,_countof(readback))&&pressfit_preference_parse(readback,hole,sheet)==row)
                error=PRO_TK_NO_ERROR;
            if(error)DeleteFileW(pending);
        }
    }
    CloseHandle(folder);
    if(!error)part_text_add(out,L"已保存并读回选型 %ls；适用于实测孔径 Ø%.6g mm、板厚 %.6g mm。"
        L"换库版本、板厚或孔径时重新匹配。\n保存位置：%ls\n",selector,hole,sheet,path);
    return error;
}