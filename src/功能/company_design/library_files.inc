#define COMPANY_PATH_ROOTS 8
#define COMPANY_SCAN_LIMIT 16000
#define COMPANY_LIST_LIMIT 72

static wchar_t company_roots[COMPANY_PATH_ROOTS][MAX_PATH];
static int company_root_count;

static int company_match(const wchar_t *text,const wchar_t *word){
    size_t count=wcslen(word);
    for(;*text;text++)if(!_wcsnicmp(text,word,count))return 1;
    return !count;
}

static int fastener_bso_contextual_request(const wchar_t *text);

typedef enum {
    COMPANY_SCOPE_NONE, COMPANY_SCOPE_FASTENERS, COMPANY_SCOPE_REGISTERED
} CompanyScope;

static int company_metric_fastener(const wchar_t *text){
    for(const wchar_t *p=text;*p;p++){
        if((*p!=L'M'&&*p!=L'm'&&*p!=L'Ｍ'&&*p!=L'ｍ')||
           (p>text&&p[-1]<=127&&iswalnum(p[-1])))continue;
        const wchar_t *number=p+1;
        while(*number==L' '||*number==L'\t')number++;
        if(*number<L'1'||*number>L'9')continue;
        const wchar_t *end=number+1;
        while(*end>=L'0'&&*end<=L'9')end++;
        if(end-number<=2&&(!*end||*end>127||!iswalnum(*end)||*end==L'x'||*end==L'X'))return 1;
    }
    return 0;
}

static CompanyScope company_named_scope(const wchar_t *text){
    const wchar_t *const registered[]={
        L"公司标准",L"标准文件",L"公司资料",L"技术文件",L"公司库",L"标准库",L"资料库",
        L"标准件",L"电子件",L"库里",L"库中",L"设计规范",L"标准样件",L"库模型"
    };
    const wchar_t *const fastener[]={
        L"紧固件",L"压铆",L"螺母",L"螺柱",L"铆柱",L"螺丝",L"螺钉",L"螺栓",L"垫圈",L"螺母柱"
    };
    const wchar_t *const actions[]={
        L"装",L"查",L"找",L"读",L"选",L"用",L"换",L"打开",L"调出",L"拿",L"改成",L"什么规格",L"能不能"
    };
    if(smart_has_text(text,registered,_countof(registered)))return COMPANY_SCOPE_REGISTERED;
    if(smart_has_text(text,fastener,_countof(fastener))||company_match(text,L"BSO")||
       (company_metric_fastener(text)&&smart_has_text(text,actions,_countof(actions))))
        return COMPANY_SCOPE_FASTENERS;
    return COMPANY_SCOPE_NONE;
}

static CompanyScope company_request_scope(void){
    const wchar_t *const denied[]={
        L"不要读取公司库",L"不用读取公司库",L"不要查公司库",L"别查公司库",L"不准查公司库",
        L"不要访问公司库",L"不要读取紧固件库",L"不要查紧固件库",L"别查紧固件库"
    };
    if(smart_has_text(smart_latest_request,denied,_countof(denied)))return COMPANY_SCOPE_NONE;
    CompanyScope scope=company_named_scope(smart_latest_request);
    if(scope)return scope;
    if(smart_prior_goal[0]&&smart_short_reply(smart_latest_request)&&
       company_named_scope(smart_prior_goal)==COMPANY_SCOPE_FASTENERS)
        return COMPANY_SCOPE_FASTENERS;
    return fastener_bso_contextual_request(smart_latest_request)?COMPANY_SCOPE_FASTENERS:COMPANY_SCOPE_NONE;
}
static int company_path_in_request(const wchar_t *path){
    size_t length=wcslen(path);
    if(length<4)return 0;
    for(const wchar_t *p=smart_latest_request;*p;p++){
        if(p!=smart_latest_request&&((p[-1]<=127&&iswalnum(p[-1]))||wcschr(L"/\\:",p[-1])))continue;
        size_t n=0;
        while(n<length&&p[n]&&
            (towupper(p[n])==towupper(path[n])||
            ((p[n]==L'/'||p[n]==L'\\')&&(path[n]==L'/'||path[n]==L'\\'))))n++;
        if(n!=length)continue;
        const wchar_t *after=p+n;
        if(*after==L'\\'||*after==L'/')after++;
        while(*after==L' '||*after==L'\t')after++;
        if(!*after||wcschr(L"\r\n\"'“”‘’「」【】，。；、！？）",*after))return 1;
    }
    return 0;
}

static void company_trim_path(wchar_t *path){
    size_t length=wcslen(path);
    while(length>3&&(path[length-1]==L'\\'||path[length-1]==L'/'))path[--length]=0;
}

static int company_is_under(const wchar_t *path,const wchar_t *root){
    size_t length=wcslen(root);
    return length&&!_wcsnicmp(path,root,length)&&(!path[length]||path[length]==L'\\');
}

typedef enum {
    COMPANY_ACCESS_OK,COMPANY_ACCESS_SYNTAX,COMPANY_ACCESS_SCOPE,COMPANY_ACCESS_MISSING,
    COMPANY_ACCESS_DENIED,COMPANY_ACCESS_BUSY,COMPANY_ACCESS_LINK,COMPANY_ACCESS_KIND,COMPANY_ACCESS_IO
} CompanyAccessStage;

typedef struct {
    CompanyAccessStage stage;
    DWORD win32;
    int directory;
    wchar_t path[MAX_PATH];
} CompanyAccess;

static void company_access_set(CompanyAccess *access,CompanyAccessStage stage,DWORD code,const wchar_t *path){
    if(!access)return;
    access->stage=stage;
    access->win32=code;
    wcsncpy_s(access->path,_countof(access->path),path?path:L"",_TRUNCATE);
}

static CompanyAccessStage company_access_win32(DWORD code){
    switch(code){
    case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:case ERROR_INVALID_DRIVE:
        return COMPANY_ACCESS_MISSING;
    case ERROR_ACCESS_DENIED:case ERROR_NETWORK_ACCESS_DENIED:
        return COMPANY_ACCESS_DENIED;
    case ERROR_SHARING_VIOLATION:case ERROR_LOCK_VIOLATION:
        return COMPANY_ACCESS_BUSY;
    default:return COMPANY_ACCESS_IO;
    }
}

static void company_access_report(PartText *out,const CompanyAccess *access){
    const int directory=access->directory;
    const wchar_t *reason;
    switch(access->stage){
    case COMPANY_ACCESS_SYNTAX:reason=L"路径格式不适用，需要数据盘上的完整路径";break;
    case COMPANY_ACCESS_SCOPE:reason=L"MWEAI 授权范围未通过，尚未打开资料";break;
    case COMPANY_ACCESS_MISSING:reason=directory?L"目录不存在或数据盘不可用":L"文件不存在或数据盘不可用";break;
    case COMPANY_ACCESS_DENIED:reason=L"Windows 拒绝读取，需核对当前 Creo 进程的访问权限";break;
    case COMPANY_ACCESS_BUSY:reason=L"资料被占用，Windows 无法以只读方式打开";break;
    case COMPANY_ACCESS_LINK:reason=L"实际路径与登记路径不同，或路径包含链接";break;
    case COMPANY_ACCESS_KIND:reason=directory?L"该位置不是普通目录":L"该位置不是普通文件";break;
    default:reason=L"Windows 无法读取资料";break;
    }
    part_text_add(out,L"资料访问失败｜%ls。",reason);
    if(access->path[0])part_text_add(out,L"\n核对位置：%ls",access->path);
    if(access->win32)part_text_add(out,L"（Windows %lu）",access->win32);
    part_text_add(out,L"\n");
}

static int company_normalize_path(const wchar_t *input,wchar_t path[MAX_PATH]){
    const wchar_t drive=storage_output_drive()?storage_output_drive():towupper(runtime_data[0]);
    size_t length=wcslen(input);
    if(length<4||length>=MAX_PATH||towupper(input[0])!=drive||input[1]!=L':'||
       (input[2]!=L'\\'&&input[2]!=L'/')||wcspbrk(input+2,L":*?|\r\n"))return 0;
    DWORD normalized=GetFullPathNameW(input,MAX_PATH,path,NULL);
    if(normalized<4||normalized>=MAX_PATH)return 0;
    company_trim_path(path);
    return 1;
}

static HANDLE company_open_path_checked(const wchar_t *input,int directory,wchar_t path[MAX_PATH],CompanyAccess *access){
    if(access)access->directory=directory;
    if(!company_normalize_path(input,path)){
        company_access_set(access,COMPANY_ACCESS_SYNTAX,0,input);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle=CreateFileW(path,directory?FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,
        NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),NULL);
    if(handle==INVALID_HANDLE_VALUE){
        DWORD code=GetLastError();
        company_access_set(access,company_access_win32(code),code,path);
        return handle;
    }
    BY_HANDLE_FILE_INFORMATION info;
    wchar_t final_path[MAX_PATH+4];
    DWORD final_length=GetFinalPathNameByHandleW(handle,final_path,_countof(final_path),VOLUME_NAME_DOS);
    DWORD code=0;
    CompanyAccessStage stage=COMPANY_ACCESS_OK;
    if(!final_length||!GetFileInformationByHandle(handle,&info)){
        code=GetLastError();
        stage=company_access_win32(code);
    }else if(final_length>=_countof(final_path)||wcsncmp(final_path,L"\\\\?\\",4)||
       _wcsicmp(final_path+4,path)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
        stage=COMPANY_ACCESS_LINK;
    }else if(GetFileType(handle)!=FILE_TYPE_DISK||
       (!!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=!!directory)){
        stage=COMPANY_ACCESS_KIND;
    }
    company_access_set(access,stage,code,path);
    if(stage!=COMPANY_ACCESS_OK){CloseHandle(handle);return INVALID_HANDLE_VALUE;}
    return handle;
}

static HANDLE company_open_path(const wchar_t *input,int directory,wchar_t path[MAX_PATH]){
    return company_open_path_checked(input,directory,path,NULL);
}

static int company_default_root(wchar_t root[MAX_PATH]){
    wchar_t parent[MAX_PATH];
    wcscpy_s(parent,_countof(parent),runtime_data);
    company_trim_path(parent);
    wchar_t *leaf=wcsrchr(parent,L'\\');
    if(leaf&&!_wcsicmp(leaf+1,L"data"))*leaf=0;
    return swprintf_s(root,MAX_PATH,L"%ls\\公司资料",parent)>=0;
}

static int company_fastener_primary_root(wchar_t root[MAX_PATH]);

typedef enum {
    COMPANY_ROOT_PATH=-1, COMPANY_ROOT_ALL, COMPANY_ROOT_STANDARDS, COMPANY_ROOT_DOCUMENTS,
    COMPANY_ROOT_STANDARD_MODELS, COMPANY_ROOT_FASTENERS, COMPANY_ROOT_PRESSFIT
} CompanyRoot;

static const wchar_t *const company_root_sections[]={
    L"",L"公司标准",L"技术文件",L"公司标准\\公司标准件",L"公司标准\\紧固件"
};

static CompanyRoot company_alias_root(const wchar_t *input){
    wchar_t alias[32];
    while(iswspace(*input))input++;
    size_t length=wcslen(input);
    while(length&&iswspace(input[length-1]))length--;
    if(length>=_countof(alias)||wcsncpy_s(alias,_countof(alias),input,length))return COMPANY_ROOT_PATH;
    if(!*alias||!wcscmp(alias,L"公司库")||!wcscmp(alias,L"公司标准库")||!wcscmp(alias,L"标准库")||
       !wcscmp(alias,L"资料库")||!wcscmp(alias,L"公司资料")||!wcscmp(alias,L"默认公司库"))return COMPANY_ROOT_ALL;
    if(!wcscmp(alias,L"公司标准")||!wcscmp(alias,L"标准文件"))return COMPANY_ROOT_STANDARDS;
    if(!wcscmp(alias,L"技术文件")||!wcscmp(alias,L"公司技术文件"))return COMPANY_ROOT_DOCUMENTS;
    if(!wcscmp(alias,L"公司标准件")||!wcscmp(alias,L"已验收标准件"))return COMPANY_ROOT_STANDARD_MODELS;
    if(!wcscmp(alias,L"紧固件")||!wcscmp(alias,L"紧固件库")||!wcscmp(alias,L"公司紧固件库"))return COMPANY_ROOT_FASTENERS;
    if(!wcscmp(alias,L"压铆件")||!wcscmp(alias,L"压铆件库"))return COMPANY_ROOT_PRESSFIT;
    return COMPANY_ROOT_PATH;
}

static int company_registered_root_path(CompanyRoot section,int primary,wchar_t path[MAX_PATH]){
    wchar_t base[MAX_PATH];
    if(!(primary?company_fastener_primary_root(base):company_default_root(base)))return 0;
    if(section==COMPANY_ROOT_ALL)
        return !wcscpy_s(path,MAX_PATH,base);
    if(section==COMPANY_ROOT_PRESSFIT)
        return swprintf_s(path,MAX_PATH,L"%ls\\%ls\\%ls",base,company_root_sections[COMPANY_ROOT_FASTENERS],
            native_company_fasteners[4].folder)>=0;
    return swprintf_s(path,MAX_PATH,L"%ls\\%ls",base,company_root_sections[section])>=0;
}

static int company_registered_section_path(const wchar_t *path,CompanyRoot section){
    wchar_t root[MAX_PATH];
    for(int i=0;i<2;i++)
        if(company_registered_root_path(section,i,root)&&company_is_under(path,root))return 1;
    return 0;
}

static int company_registered_path(const wchar_t *path){
    return company_registered_section_path(path,COMPANY_ROOT_ALL);
}

static int company_scope_contains(const wchar_t *input,const wchar_t *normalized){
    if(company_path_in_request(input))return 1;
    CompanyScope scope=company_request_scope();
    if(scope&&company_registered_section_path(normalized,
        scope==COMPANY_SCOPE_FASTENERS?COMPANY_ROOT_FASTENERS:COMPANY_ROOT_ALL))return 1;
    for(int i=0;i<company_root_count;i++)if(company_is_under(normalized,company_roots[i]))return 1;
    return 0;
}

static CompanyRoot company_alias_for_scope(CompanyRoot section,CompanyScope scope){
    if(section==COMPANY_ROOT_ALL&&scope==COMPANY_SCOPE_FASTENERS)return COMPANY_ROOT_FASTENERS;
    return section;
}

static HANDLE company_open_root_checked(const wchar_t *input,wchar_t path[MAX_PATH],CompanyAccess *access){
    CompanyAccess local={0};
    if(!access)access=&local;
    access->directory=1;
    HANDLE handle=INVALID_HANDLE_VALUE;
    CompanyRoot section=company_alias_root(input);
    if(section!=COMPANY_ROOT_PATH){
        CompanyScope scope=company_request_scope();
        section=company_alias_for_scope(section,scope);
        if(!scope||(scope==COMPANY_SCOPE_FASTENERS&&section<COMPANY_ROOT_FASTENERS)){
            company_access_set(access,COMPANY_ACCESS_SCOPE,0,NULL);
            return INVALID_HANDLE_VALUE;
        }
        /* General catalog aliases use the primary shared collection. Narrow
           categories try the managed instance's own registered directory. */
        const int primary_first=section<COMPANY_ROOT_FASTENERS;
        wchar_t root[MAX_PATH];
        for(int i=0;i<2;i++){
            const int primary=i? !primary_first:primary_first;
            if(!company_registered_root_path(section,primary,root))continue;
            handle=company_open_path_checked(root,1,path,access);
            if(handle!=INVALID_HANDLE_VALUE||access->stage!=COMPANY_ACCESS_MISSING)break;
        }
    }else{
        wchar_t normalized[MAX_PATH];
        if(!company_normalize_path(input,normalized)){
            company_access_set(access,COMPANY_ACCESS_SYNTAX,0,input);
            return INVALID_HANDLE_VALUE;
        }
        if(!company_scope_contains(input,normalized)){
            company_access_set(access,COMPANY_ACCESS_SCOPE,0,NULL);
            return INVALID_HANDLE_VALUE;
        }
        handle=company_open_path_checked(normalized,1,path,access);
    }
    if(handle==INVALID_HANDLE_VALUE)return handle;
    for(int i=0;i<company_root_count;i++)if(company_is_under(path,company_roots[i]))return handle;
    if(company_root_count<COMPANY_PATH_ROOTS)wcscpy_s(company_roots[company_root_count++],MAX_PATH,path);
    return handle;
}
static HANDLE company_open_root(const wchar_t *input,wchar_t path[MAX_PATH]){
    return company_open_root_checked(input,path,NULL);
}

static HANDLE company_open_document_checked(const wchar_t *input,const wchar_t *root,wchar_t path[MAX_PATH],CompanyAccess *access){
    wchar_t scope[MAX_PATH]=L"";
    if(*root){
        HANDLE directory=company_open_root_checked(root,scope,access);
        if(directory==INVALID_HANDLE_VALUE)return directory;
        CloseHandle(directory);
    }
    if(access)access->directory=0;
    wchar_t normalized[MAX_PATH];
    if(!company_normalize_path(input,normalized)){
        company_access_set(access,COMPANY_ACCESS_SYNTAX,0,input);
        return INVALID_HANDLE_VALUE;
    }
    CompanyRoot section=company_alias_for_scope(company_alias_root(root),company_request_scope());
    if((scope[0]&&!company_is_under(normalized,scope)&&
       (section==COMPANY_ROOT_PATH||!company_registered_section_path(normalized,section)))||
       !company_scope_contains(input,normalized)){
        company_access_set(access,COMPANY_ACCESS_SCOPE,0,NULL);
        return INVALID_HANDLE_VALUE;
    }
    return company_open_path_checked(normalized,0,path,access);
}
static HANDLE company_open_document(const wchar_t *input,const wchar_t *root,wchar_t path[MAX_PATH]){
    return company_open_document_checked(input,root,path,NULL);
}

typedef struct {
    unsigned visited,shown,skipped;
    int bounded,recursive;
    const wchar_t *query;
    PartText *out;
} CompanyScan;

static int company_known_file(const wchar_t *name){
    const wchar_t *suffix=native_file_suffix(name);
    if(native_file_kind(suffix)||native_file_is_image(suffix))return 1;
    return suffix&&(!_wcsicmp(suffix,L".docx")||!_wcsicmp(suffix,L".xlsx")||
                    !_wcsicmp(suffix,L".doc")||!_wcsicmp(suffix,L".xls"));
}

static void company_scan_dir(const wchar_t *folder,CompanyScan *scan,int depth){
    wchar_t pattern[MAX_PATH],path[MAX_PATH];
    if(swprintf_s(pattern,_countof(pattern),L"%ls\\*",folder)<0){scan->skipped++;return;}
    WIN32_FIND_DATAW entry;
    HANDLE search=FindFirstFileW(pattern,&entry);
    if(search==INVALID_HANDLE_VALUE){
        if(GetLastError()!=ERROR_FILE_NOT_FOUND)scan->skipped++;
        return;
    }
    do{
        if(scan->visited++>=COMPANY_SCAN_LIMIT||scan->shown>=COMPANY_LIST_LIMIT||scan->out->truncated){
            scan->bounded=1;break;
        }
        const wchar_t *name=entry.cFileName;
        if(!wcscmp(name,L".")||!wcscmp(name,L".."))continue;
        if(entry.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_SYSTEM)){scan->skipped++;continue;}
        if(swprintf_s(path,_countof(path),L"%ls\\%ls",folder,name)<0){scan->skipped++;continue;}
        int is_dir=(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        if((is_dir||company_known_file(name))&&company_match(name,scan->query)){
            part_text_add(scan->out,L"%ls %ls\n",is_dir?L"[目录]":L"[文件]",path);
            scan->shown++;
        }
        if(is_dir&&scan->recursive&&depth<8){
            wchar_t resolved[MAX_PATH];
            HANDLE child=company_open_path(path,1,resolved);
            if(child==INVALID_HANDLE_VALUE)scan->skipped++;
            else{company_scan_dir(resolved,scan,depth+1);CloseHandle(child);}
        }else if(is_dir&&scan->recursive&&depth==8)scan->bounded=1;
        if(scan->visited>=COMPANY_SCAN_LIMIT||scan->shown>=COMPANY_LIST_LIMIT||scan->out->truncated){
            scan->bounded=1;break;
        }
    }while(FindNextFileW(search,&entry));
    if(!scan->bounded&&GetLastError()!=ERROR_NO_MORE_FILES)scan->skipped++;
    FindClose(search);
}

static ProError company_library_check(const wchar_t *root,PartText *out){
    wchar_t path[MAX_PATH],pattern[MAX_PATH];
    CompanyAccess access={0};
    HANDLE directory=company_open_root_checked(root,path,&access);
    if(directory==INVALID_HANDLE_VALUE){company_access_report(out,&access);return PRO_TK_CANT_OPEN;}
    if(swprintf_s(pattern,_countof(pattern),L"%ls\\*",path)<0){
        CloseHandle(directory);
        return PRO_TK_BAD_INPUTS;
    }
    WIN32_FIND_DATAW entry;
    HANDLE found=FindFirstFileW(pattern,&entry);
    DWORD code=found==INVALID_HANDLE_VALUE?GetLastError():0;
    if(found!=INVALID_HANDLE_VALUE)FindClose(found);
    CloseHandle(directory);
    if(code&&code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_NO_MORE_FILES){
        company_access_set(&access,company_access_win32(code),code,path);
        company_access_report(out,&access);
        return PRO_TK_CANT_OPEN;
    }
    part_text_add(out,L"资料访问通过｜本轮授权、Windows 打开和目录枚举均已核对。\n实际资料目录：%ls\n",path);
    return PRO_TK_NO_ERROR;
}

static ProError company_list(const wchar_t *root,const wchar_t *query,int recursive,PartText *out){
    wchar_t path[MAX_PATH];
    if(wcslen(query)>120||(recursive&&!*query)){
        part_text_add(out,L"递归搜索需要明确的文件名、型号或目录名关键词（最多 120 字）。\n");
        return PRO_TK_BAD_INPUTS;
    }
    CompanyAccess access={0};
    HANDLE directory=company_open_root_checked(root,path,&access);
    if(directory==INVALID_HANDLE_VALUE){company_access_report(out,&access);return PRO_TK_CANT_OPEN;}
    part_text_add(out,L"资料目录：%ls\n",path);
    CompanyScan scan={0};
    scan.out=out;scan.query=query;scan.recursive=recursive;
    company_scan_dir(path,&scan,0);
    CloseHandle(directory);
    part_text_add(out,L"显示 %u 项。%ls%ls\n",scan.shown,
        scan.bounded?L"达到读取范围，请缩小目录或关键词。":L"",
        scan.skipped?L"链接、路径过长或无访问权限的条目已略过。":L"");
    return PRO_TK_NO_ERROR;
}

static ProError company_document_stage(HANDLE file,const wchar_t *source,PartText *out){
    const wchar_t *suffix=wcsrchr(source,L'.');
    static LONG serial;
    LARGE_INTEGER size;
    if(!suffix||!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>40LL*1024*1024){
        part_text_add(out,L"资料为空或超过单份 40 MB 限制。\n");
        return PRO_TK_BAD_INPUTS;
    }
    const AttachmentKind kind=native_file_kind(suffix);
    if(kind!=ATTACH_TEXT&&kind!=ATTACH_PDF&&_wcsicmp(suffix,L".docx")&&_wcsicmp(suffix,L".xlsx")){
        part_text_add(out,L"资料正文支持 PDF、DOCX、XLSX 和常见文本。Creo 原生模型请用模型读取。旧版 DOC/XLS 可另存为 DOCX/XLSX 或 PDF。\n");
        return PRO_TK_UNSUPPORTED;
    }
    wchar_t leaf[80],destination[MAX_PATH];
    if(!storage_output_directory(ai_dir)||
       swprintf_s(leaf,_countof(leaf),L"company-doc-%lu-%ld%ls",resident_sequence,InterlockedIncrement(&serial),suffix)<0||
       !resident_path(destination,leaf))return PRO_TK_CANT_WRITE;
    HANDLE copy=CreateFileW(destination,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(copy==INVALID_HANDLE_VALUE)return PRO_TK_CANT_WRITE;
    char bytes[16384];
    LONGLONG remaining=size.QuadPart;
    int ok=1;
    while(remaining){
        DWORD got=0,wrote=0;
        const DWORD request=remaining>sizeof(bytes)?sizeof(bytes):(DWORD)remaining;
        if(!ReadFile(file,bytes,request,&got,NULL)||!got||!WriteFile(copy,bytes,got,&wrote,NULL)||wrote!=got){
            ok=0;break;
        }
        remaining-=got;
    }
    CloseHandle(copy);
    if(!ok){DeleteFileW(destination);return PRO_TK_CANT_WRITE;}
    part_text_add(out,L"BWTL_LIBRARY_DOCUMENT\n%ls\n%ls",leaf,source);
    return PRO_TK_NO_ERROR;
}
