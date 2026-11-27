/* Share the primary company library read-only across managed BWTL profiles. */
static int company_fastener_primary_root(wchar_t root[MAX_PATH]){
    wchar_t parent[MAX_PATH];
    if(wcscpy_s(parent,_countof(parent),runtime_data))return 0;
    company_trim_path(parent);
    wchar_t *leaf=wcsrchr(parent,L'\\');
    if(!leaf||_wcsicmp(leaf+1,L"data"))return 0;
    *leaf=0;
    leaf=wcsrchr(parent,L'\\');
    if(!leaf||wcslen(leaf+1)!=1||leaf[1]<L'2'||leaf[1]>L'8')return 0;
    *leaf=0;
    leaf=wcsrchr(parent,L'\\');
    if(!leaf||_wcsicmp(leaf+1,L"BWTL-Instances"))return 0;
    *leaf=0;
    return swprintf_s(root,MAX_PATH,L"%ls\\BWTL\\公司资料",parent)>=0;
}

static int company_fastener_resolve_report(UINT action,wchar_t path[MAX_PATH],PartText *out){
    if(action<COMPANY_OPEN_FASTENER_FIRST||
       action-COMPANY_OPEN_FASTENER_FIRST>=_countof(native_company_fasteners))return 0;
    const wchar_t *folder=native_company_fasteners[action-COMPANY_OPEN_FASTENER_FIRST].folder;
    wchar_t roots[2][MAX_PATH];
    CompanyAccess failures[2]={{0}};
    int count=0;
    if(company_default_root(roots[count]))count++;
    if(company_fastener_primary_root(roots[count]))count++;
    for(int i=0;i<count;i++){
        wchar_t expected[MAX_PATH];
        if(swprintf_s(expected,_countof(expected),L"%ls\\公司标准\\紧固件\\%ls",roots[i],folder)<0){
            company_access_set(&failures[i],COMPANY_ACCESS_SYNTAX,0,roots[i]);
            continue;
        }
        HANDLE directory=company_open_path_checked(expected,1,path,&failures[i]);
        if(directory!=INVALID_HANDLE_VALUE){CloseHandle(directory);return 1;}
    }
    if(out){
        part_text_add(out,L"已核对登记的紧固件分类「%ls」。\n",native_company_fasteners[action-COMPANY_OPEN_FASTENER_FIRST].label);
        for(int i=0;i<count;i++)company_access_report(out,&failures[i]);
    }
    return 0;
}

static int company_fastener_resolve(UINT action,wchar_t path[MAX_PATH]){
    return company_fastener_resolve_report(action,path,NULL);
}

