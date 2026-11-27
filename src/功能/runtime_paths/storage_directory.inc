#ifndef BWTL_STORAGE_DIRECTORY_H
#define BWTL_STORAGE_DIRECTORY_H
#include <wctype.h>
static int storage_directory_on_drive(const wchar_t *directory,wchar_t drive){
    if(!directory||towupper(directory[0])!=drive||directory[1]!=L':')return 0;
    HANDLE handle=CreateFileW(directory,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(handle==INVALID_HANDLE_VALUE)return 0;
    wchar_t resolved[PRO_PATH_SIZE];
    DWORD length=GetFinalPathNameByHandleW(handle,resolved,_countof(resolved),VOLUME_NAME_DOS);
    CloseHandle(handle);
    return length>=7&&length<_countof(resolved)&&!wcsncmp(resolved,L"\\\\?\\",4)&&
        towupper(resolved[4])==drive&&resolved[5]==L':'&&resolved[6]==L'\\';
}

#endif
