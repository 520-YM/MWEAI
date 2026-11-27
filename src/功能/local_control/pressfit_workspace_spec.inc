/* An explicit catalog selector never falls through to a guessed BSO family.
   The native resolver verifies the exact family row or standalone PRT. */
static int workspace_pressfit_exact_spec(const wchar_t **text,wchar_t spec[64]){
    const wchar_t *p=sh_plate_spacing(*text);
    if(!sh_plate_take(&p,L"型号"))return 0;
    if(*p==L':'||*p==L'：')p=sh_plate_spacing(p+1);
    const size_t length=wcsspn(p,L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.:");
    if(!length||length>=64||(p[length]&&!iswspace(p[length])&&!wcschr(L"，,；;。！!",p[length])))
        return -1;
    wchar_t proposed[64];
    PressfitSelector selector={0};
    if(wcsncpy_s(proposed,_countof(proposed),p,length)||!pressfit_selector(proposed,&selector))
        return -1;
    wcscpy_s(spec,64,proposed);
    *text=sh_plate_spacing(p+length);
    return 1;
}
