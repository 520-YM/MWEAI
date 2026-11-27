#include "storage_directory.inc"

static wchar_t storage_output_drive(void){
    wchar_t drive[2];
    DWORD length=GetEnvironmentVariableW(L"BWTL_OUTPUT_DRIVE",drive,_countof(drive));
    if(!length)return 0;
    wchar_t letter=length==1?towupper(drive[0]):0;
    return letter>=L'A'&&letter<=L'Z'?letter:L'?';
}

static int storage_output_directory(const wchar_t *directory){
    wchar_t drive=storage_output_drive();
    return !drive||storage_directory_on_drive(directory,drive);
}

static int storage_method_directory(wchar_t path[MAX_PATH],const wchar_t *library,int create){
    if(!storage_directory_on_drive(runtime_data,towupper(runtime_data[0]))||
       !storage_output_directory(runtime_data)||
       swprintf_s(path,MAX_PATH,L"%ls\\%ls",runtime_data,library)<0)return 0;
    if(create&&!CreateDirectoryW(path,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    DWORD attributes=GetFileAttributesW(path);
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&
        !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}

static int storage_prepare_output(void){
    if(!storage_output_drive())return 1;
    ProPath directory;
    return !ProDirectoryCurrentGet(directory)&&storage_output_directory(directory)&&
        !ProConfigoptSet(L"override_store_back",L"yes");
}

static const wchar_t storage_output_message[]=L"MWEAI 的文件只写入 E 盘。请在「工作目录」中选择 E 盘上的目录，再执行操作。";
