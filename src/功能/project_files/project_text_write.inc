/* Local text output for explicitly selected project files. No shell or general
   filesystem access is exposed to the model. */
#define PROJECT_TEXT_LIMIT (128u*1024u)
enum { PROJECT_TEXT_CREATE=1,PROJECT_TEXT_APPEND,PROJECT_TEXT_REPLACE };
enum { TEXT_UTF8, TEXT_UTF8_BOM, TEXT_UTF16LE };

static void project_text_deslash(wchar_t *path){
    for(;*path;path++)if(*path==L'/')*path=L'\\';
}

static int project_text_protected(const wchar_t *path){
    wchar_t primary[MAX_PATH],root[MAX_PATH],*leaf;
    if(company_is_under(path,runtime_root)||company_is_under(path,runtime_data)||company_registered_path(path))return 1;
    if(modeling_rule_root(primary)&&(leaf=wcsrchr(primary,L'\\'))){
        *leaf=0;
        if(company_is_under(path,primary))return 1;
        if((leaf=wcsrchr(primary,L'\\'))!=NULL){
            *leaf=0;
            if(swprintf_s(root,MAX_PATH,L"%ls\\BWTL-Instances",primary)>=0&&company_is_under(path,root))return 1;
        }
    }
    for(const wchar_t *part=path+3;part&&*part;){
        static const wchar_t *const protected[]={L".git",L".codex",L".agents",L"AGENTS.md"};
        const wchar_t *next=wcschr(part,L'\\');
        size_t n=next?(size_t)(next-part):wcslen(part);
        for(size_t i=0;i<_countof(protected);i++)
            if(n==wcslen(protected[i])&&!_wcsnicmp(part,protected[i],n))return 1;
        part=next?next+1:NULL;
    }
    return 0;
}

static int project_text_path(const wchar_t *input,wchar_t path[MAX_PATH]){
    wchar_t drive=storage_output_drive();
    if(!drive)drive=towupper(runtime_data[0]);
    const size_t n=wcslen(input);
    if(n<5||n>=MAX_PATH-40||towupper(input[0])!=drive||input[1]!=L':'||
       (input[2]!=L'\\'&&input[2]!=L'/')||wcspbrk(input+3,L":\"<>*?|"))return 0;
    wcscpy_s(path,MAX_PATH,input);
    project_text_deslash(path);
    for(const wchar_t *start=path+3;*start;){
        const wchar_t *end=wcschr(start,L'\\');
        if(!end)end=start+wcslen(start);
        if(end==start||end[-1]==L'.'||end[-1]==L' ')return 0;
        for(const wchar_t *at=start;at<end;at++)if(*at<32)return 0;
        if(!*end)break;
        if(!end[1])return 0;
        start=end+1;
    }
    const wchar_t *leaf=wcsrchr(path,L'\\')+1,*ext=wcsrchr(leaf,L'.');
    static const wchar_t *const supported[]={L".txt",L".md",L".csv",L".tsv",L".log"};
    if(!ext||ext==leaf||project_text_protected(path))return 0;
    for(size_t i=0;i<_countof(supported);i++)if(!_wcsicmp(ext,supported[i]))return 1;
    return 0;
}

static int project_text_identity(HANDLE file,const wchar_t *path,int directory,
                                 BY_HANDLE_FILE_INFORMATION *info){
    wchar_t final[MAX_PATH+4];
    DWORD n=GetFinalPathNameByHandleW(file,final,_countof(final),VOLUME_NAME_DOS);
    return n>=7&&n<_countof(final)&&!wcsncmp(final,L"\\\\?\\",4)&&!_wcsicmp(final+4,path)&&
        GetFileType(file)==FILE_TYPE_DISK&&GetFileInformationByHandle(file,info)&&
        !(info->dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
        (!!(info->dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory)&&(directory||info->nNumberOfLinks==1);
}

static int project_text_pin_parents(const wchar_t *path,HANDLE pins[MAX_PATH/2]){
    wchar_t prefix[MAX_PATH];
    wcscpy_s(prefix,_countof(prefix),path);
    int count=0;
    for(wchar_t *slash=prefix+3;(slash=wcschr(slash,L'\\'))!=NULL;slash++){
        *slash=0;
        HANDLE handle=CreateFileW(prefix,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
            NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        BY_HANDLE_FILE_INFORMATION info;
        int ok=handle!=INVALID_HANDLE_VALUE&&project_text_identity(handle,prefix,1,&info);
        *slash=L'\\';
        if(!ok){
            if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
            while(count)CloseHandle(pins[--count]);
            return -1;
        }
        pins[count++]=handle;
    }
    return count;
}

static unsigned char *project_text_read(HANDLE file,DWORD *size){
    LARGE_INTEGER length,zero={0};
    if(!GetFileSizeEx(file,&length)||length.QuadPart<0||
       length.QuadPart>PROJECT_TEXT_LIMIT||!SetFilePointerEx(file,zero,NULL,FILE_BEGIN))return NULL;
    *size=(DWORD)length.QuadPart;
    unsigned char *bytes=malloc((size_t)*size+2);
    DWORD actual;
    if(bytes&&ReadFile(file,bytes,*size,&actual,NULL)&&actual==*size)return bytes;
    free(bytes);
    return NULL;
}

static wchar_t *project_text_decode(const unsigned char *bytes,DWORD n,int *encoding){
    DWORD bom=0;
    *encoding=TEXT_UTF8;
    if(n>=2&&bytes[0]==0xff&&bytes[1]==0xfe){
        if(n%2)return NULL;
        *encoding=TEXT_UTF16LE;
        bom=2;
    }else if(n>=3&&!memcmp(bytes,"\xef\xbb\xbf",3)){
        *encoding=TEXT_UTF8_BOM;
        bom=3;
    }
    int count=*encoding==TEXT_UTF16LE?(int)(n-bom)/2:
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)bytes+bom,(int)(n-bom),NULL,0);
    if(!count&&n>bom)return NULL;
    wchar_t *text=calloc((size_t)count+1,sizeof(*text));
    if(!text)return NULL;
    if(count&&*encoding==TEXT_UTF16LE)memcpy(text,bytes+bom,(size_t)count*sizeof(*text));
    else if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)bytes+bom,(int)(n-bom),text,count);
    if(wmemchr(text,0,count)||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,NULL,0,NULL,NULL)){
        free(text);
        return NULL;
    }
    return text;
}

static unsigned char *project_text_encode(const wchar_t *text,int encoding,DWORD *size){
    const int utf8=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,NULL,0,NULL,NULL);
    if(!utf8)return NULL;
    DWORD payload=encoding==TEXT_UTF16LE?(DWORD)(wcslen(text)*sizeof(*text)):(DWORD)(utf8-1);
    DWORD bom=encoding==TEXT_UTF16LE?2:encoding==TEXT_UTF8_BOM?3:0;
    *size=bom+payload;
    if(*size>PROJECT_TEXT_LIMIT)return NULL;
    unsigned char *bytes=malloc((size_t)*size+1);
    if(!bytes)return NULL;
    if(encoding==TEXT_UTF16LE){
        bytes[0]=0xff;bytes[1]=0xfe;
        memcpy(bytes+bom,text,payload);
    }else{
        if(bom)memcpy(bytes,"\xef\xbb\xbf",3);
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,(char *)bytes+bom,utf8,NULL,NULL);
    }
    return bytes;
}

static int project_text_matches(const wchar_t *path,const unsigned char *expected,DWORD bytes){
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    BY_HANDLE_FILE_INFORMATION identity;
    DWORD read=0;
    unsigned char *actual=project_text_identity(file,path,0,&identity)?project_text_read(file,&read):NULL;
    int same=actual&&read==bytes&&!memcmp(actual,expected,bytes);
    free(actual);
    CloseHandle(file);
    return same;
}

static int project_text_write(int mode,const wchar_t *path,const wchar_t *match,const wchar_t *body,
                              wchar_t *reply,size_t capacity){
    HANDLE pins[MAX_PATH/2],lease=INVALID_HANDLE_VALUE,source=INVALID_HANDLE_VALUE;
    HANDLE staged=INVALID_HANDLE_VALUE;
    wchar_t temp[MAX_PATH]=L"",backup[MAX_PATH]=L"",lock[MAX_PATH];
    wchar_t *prior=NULL,*updated=NULL;
    unsigned char *old_bytes=NULL,*new_bytes=NULL;
    DWORD old_size=0,new_size=0,written;
    int format=TEXT_UTF8,ok=0,staged_created=0;
    const wchar_t *failure=L"无法核对目标目录，或目标目录指向链接。";
    int pinned=project_text_pin_parents(path,pins);
    if(pinned<0)goto done;
    swprintf_s(lock,MAX_PATH,L"%ls.bwtl-write.lock",path);
    lease=CreateFileW(lock,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,CREATE_NEW,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if(lease==INVALID_HANDLE_VALUE){failure=L"当前文件正在被写入，或目录不可写。";goto done;}
    static LONG serial;
    const unsigned long nonce=GetTickCount()^(DWORD)InterlockedIncrement(&serial);
    swprintf_s(temp,MAX_PATH,L"%ls.bwtl-%08lx-%08lx.tmp",path,GetCurrentProcessId(),nonce);
    swprintf_s(backup,MAX_PATH,L"%ls.bwtl-%08lx-%08lx.bak",path,GetCurrentProcessId(),nonce);
    if(mode!=PROJECT_TEXT_CREATE){
        source=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        BY_HANDLE_FILE_INFORMATION identity;
        failure=L"现有文件无法读取，正在被编辑，或路径是链接。";
        if(source==INVALID_HANDLE_VALUE||!project_text_identity(source,path,0,&identity))goto done;
        old_bytes=project_text_read(source,&old_size);
        prior=old_bytes?project_text_decode(old_bytes,old_size,&format):NULL;
        if(!prior){failure=L"现有文件超过 128 KB，或编码不是 UTF-8／UTF-16LE。";goto done;}
    }else if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES){
        failure=L"目标文件已存在。需要加内容时可要求追加，需要改动时请说明要替换的原文。";
        goto done;
    }

    const wchar_t *position=mode==PROJECT_TEXT_REPLACE?wcsstr(prior,match):NULL;
    if(mode==PROJECT_TEXT_REPLACE){
        if(!position){failure=L"对不上要替换的原文。请先读取文件再定位改动。";goto done;}
        if(wcsstr(position+1,match)){failure=L"找到多处相同原文。请带上能区分位置的上下文。";goto done;}
    }
    size_t old_len=prior?wcslen(prior):0,body_len=wcslen(body);
    const wchar_t *separator=L"";
    if(mode==PROJECT_TEXT_APPEND&&old_len&&prior[old_len-1]!=L'\r'&&prior[old_len-1]!=L'\n'&&
       body[0]!=L'\r'&&body[0]!=L'\n')separator=wcsstr(prior,L"\r\n")?L"\r\n":L"\n";
    size_t total=old_len+body_len+wcslen(separator)+1;
    updated=calloc(total,sizeof(*updated));
    if(!updated){failure=L"无法准备写入内容。";goto done;}
    if(mode==PROJECT_TEXT_REPLACE)
        swprintf_s(updated,total,L"%.*ls%ls%ls",(int)(position-prior),prior,body,position+wcslen(match));
    else swprintf_s(updated,total,L"%ls%ls%ls",prior?prior:L"",separator,body);
    new_bytes=project_text_encode(updated,format,&new_size);
    if(!new_bytes){failure=L"写入后的内容超过 128 KB，或字符编码无效。";goto done;}
    if(old_bytes&&old_size==new_size&&!memcmp(old_bytes,new_bytes,new_size)){
        swprintf_s(reply,capacity,L"已核对文件与要求一致。本步无需再次修改。文件 %ls",path);
        ok=1;
        goto done;
    }
    staged=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(staged==INVALID_HANDLE_VALUE){failure=L"无法建立独立的写入暂存文件。";goto done;}
    staged_created=1;
    int flushed=WriteFile(staged,new_bytes,new_size,&written,NULL)&&written==new_size&&FlushFileBuffers(staged);
    CloseHandle(staged);
    staged=INVALID_HANDLE_VALUE;
    if(!flushed||!project_text_matches(temp,new_bytes,new_size)){
        failure=L"暂存文件写入或读回校验失败。";
        goto done;
    }
    if(mode==PROJECT_TEXT_CREATE){
        if(!MoveFileExW(temp,path,MOVEFILE_WRITE_THROUGH)){failure=L"无法创建文件，或同名文件已出现。";goto done;}
    }else{
        if(!project_text_matches(path,old_bytes,old_size)){
            failure=L"目标文件在提交前已变化。请重新核对当前内容。";
            goto done;
        }
        if(GetFileAttributesW(backup)!=INVALID_FILE_ATTRIBUTES){
            failure=L"备份文件名已占用。请重试。";
            goto done;
        }
        if(!ReplaceFileW(path,temp,backup,0,NULL,NULL)){
            if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES&&
               project_text_matches(backup,old_bytes,old_size))
                MoveFileExW(backup,path,MOVEFILE_WRITE_THROUGH);
            failure=L"Windows 未能提交文件更新。";
            goto done;
        }
        if(!project_text_matches(backup,old_bytes,old_size)){
            failure=L"提交时目标文件发生并发变化，保留了提交前的备份。请核对两份文件。";
            goto done;
        }
    }
    staged_created=0;
    if(!project_text_matches(path,new_bytes,new_size)){
        failure=L"文件已提交，但最终读回核对未通过。请查看文件状态。";
        goto done;
    }
    swprintf_s(reply,capacity,L"已%ls并从磁盘读回核对。文件 %ls%ls%ls",
        mode==PROJECT_TEXT_CREATE?L"创建":mode==PROJECT_TEXT_APPEND?L"追加":L"替换",
        path,mode==PROJECT_TEXT_CREATE?L"":L"\n原文件备份 ",mode==PROJECT_TEXT_CREATE?L"":backup);
    ok=1;
done:
    if(!ok){
        int have_backup=backup[0]&&GetFileAttributesW(backup)!=INVALID_FILE_ATTRIBUTES;
        swprintf_s(reply,capacity,L"%ls%ls%ls",failure,have_backup?L"\n请保留此备份 ":L"",have_backup?backup:L"");
    }
    if(staged!=INVALID_HANDLE_VALUE)CloseHandle(staged);
    if(staged_created)DeleteFileW(temp);
    if(source!=INVALID_HANDLE_VALUE)CloseHandle(source);
    if(lease!=INVALID_HANDLE_VALUE)CloseHandle(lease);
    while(pinned>0)CloseHandle(pins[--pinned]);
    free(old_bytes);free(new_bytes);free(prior);free(updated);
    return ok?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
}
