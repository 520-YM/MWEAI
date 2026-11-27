enum { PRICE_READ=36, PRICE_EXPORT=37, PRICE_OPEN_FOLDER=38, PRICE_EXPORT_RESULT=WM_APP+318, PRICE_MODEL=WM_APP+317, PRICE_FIELD=801, PRICE_SPRAY=820, PRICE_READ_BUTTON, PRICE_RUN, PRICE_BACK, PRICE_EXPORT_BUTTON, PRICE_FOLDER_BUTTON };
enum { COST_NAME,COST_LEAD,COST_W,COST_D,COST_H,COST_NET,COST_MATERIAL,COST_QTY,COST_FIELD_COUNT };
static HWND price_panel,price_title,price_output,price_note,price_spray,price_fields[COST_FIELD_COUNT];
static HWND price_labels[COST_FIELD_COUNT],price_buttons[5];
static HBRUSH price_brush;
static SRWLOCK price_export_lock=SRWLOCK_INIT;
static unsigned char price_export_data[8192];
static size_t price_export_length;
static ProPath price_last_export_directory;

static void price_invalidate_export(void){
    AcquireSRWLockExclusive(&price_export_lock);
    price_export_length=0;
    ReleaseSRWLockExclusive(&price_export_lock);
    if(price_buttons[3])EnableWindow(price_buttons[3],FALSE);
}

static void price_export(void){
    if(!price_panel)return;
    wchar_t message[PRO_PATH_SIZE+48]=L"";
    ProPath directory,output;
    unsigned char snapshot[sizeof(price_export_data)];
    AcquireSRWLockShared(&price_export_lock);
    const size_t length=price_export_length;
    if(length)memcpy(snapshot,price_export_data,length);
    ReleaseSRWLockShared(&price_export_lock);
    if(!length)wcscpy_s(message,_countof(message),L"参数已更改，请重新计算后导出。");
    else if(ProDirectoryCurrentGet(directory)||!storage_directory_on_drive(directory,L'E'))
        wcscpy_s(message,_countof(message),L"请先将 Creo 工作目录切换到 E 盘，再导出。");
    else{
        SYSTEMTIME now;GetLocalTime(&now);
        static LONG sequence;
        const LONG serial=InterlockedIncrement(&sequence);
        if(swprintf_s(output,_countof(output),L"%ls%lsMWEAI-钣金价格-%04u%02u%02u-%02u%02u%02u-%lu-%ld.csv",
            directory,directory[wcslen(directory)-1]==L'\\'?L"":L"\\",
            now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),serial)<0)
            wcscpy_s(message,_countof(message),L"工作目录路径太长，无法导出。");
        else{
            HANDLE file=CreateFileW(output,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            if(file==INVALID_HANDLE_VALUE)wcscpy_s(message,_countof(message),L"无法创建导出文件，请检查工作目录。");
            else{
                DWORD written=0;
                BOOL ok=WriteFile(file,snapshot,(DWORD)length,&written,NULL)&&written==length;
                BOOL closed=CloseHandle(file);
                if(ok&&closed){
                    wcscpy_s(price_last_export_directory,_countof(price_last_export_directory),directory);
                    swprintf_s(message,_countof(message),L"已导出数据  %ls",output);
                }
                else{DeleteFileW(output);wcscpy_s(message,_countof(message),L"写入失败，未生成完整的导出文件。");}
            }
        }
    }
    if(price_panel)SendMessageW(price_panel,PRICE_EXPORT_RESULT,0,(LPARAM)message);
}
static const wchar_t *price_materials[]={L"SUS304",L"SUS316",L"SUS201",L"SPCC",L"SECC",L"SGCC",L"AL5052",L"AL6061"};
typedef struct {wchar_t name[128],material[48];double size[3];} PriceModel;

static int price_resolve_folder(ProPath directory){
    if(price_last_export_directory[0]&&storage_directory_on_drive(price_last_export_directory,L'E')){
        wcscpy_s(directory,PRO_PATH_SIZE,price_last_export_directory);
        return 1;
    }
    return !ProDirectoryCurrentGet(directory)&&storage_directory_on_drive(directory,L'E');
}

static void price_open_folder(void){
    if(!price_panel)return;
    ProPath directory;
    const wchar_t *message=L"请先将 Creo 工作目录切换到可用的 E 盘目录。";
    if(price_resolve_folder(directory))
        message=(INT_PTR)ShellExecuteW(price_panel,L"open",directory,NULL,NULL,SW_SHOWNORMAL)>32?
            L"已打开数据文件夹。":L"无法打开数据文件夹，请检查目录是否可访问。";
    if(price_panel)SendMessageW(price_panel,PRICE_EXPORT_RESULT,0,(LPARAM)message);
}
static void price_read_model(void){
    if(!price_panel)return;
    int window=-1;
    ProMdl model=NULL;
    ProMdlType type;
    double mm;
    ProOutline bounds;
    PriceModel update={0};
    ProError error=ProWindowCurrentGet(&window);
    if(!error)error=ProWindowMdlGet(window,&model);
    if(!error)error=model?ProMdlTypeGet(model,&type):PRO_TK_BAD_CONTEXT;
    if(!error&&type!=PRO_MDL_PART&&type!=PRO_MDL_ASSEMBLY)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=qa_mm_per_unit(model,&mm);
    if(!error)error=or_outline(model,bounds);
    if(!error){
        ProMdlName name;
        if(!ProMdlMdlnameGet(model,name))wcsncpy_s(update.name,_countof(update.name),name,_TRUNCATE);
        for(int i=0;i<3;i++)update.size[i]=(bounds[1][i]-bounds[0][i])*mm;
        if(type==PRO_MDL_PART){
            ProMaterial material;
            if(!ProMaterialCurrentGet((ProSolid)model,&material)){
                for(int i=0;i<_countof(price_materials);i++)
                    if(!_wcsicmp(material.matl_name,price_materials[i]))wcscpy_s(update.material,_countof(update.material),price_materials[i]);
            }
        }
    }
    if(price_panel)SendMessageW(price_panel,PRICE_MODEL,(WPARAM)error,(LPARAM)&update);
}

static void price_estimate(void){
    price_invalidate_export();
    SetWindowTextW(price_output,L"");
    wchar_t fields[COST_FIELD_COUNT][160];
    for(int i=0;i<COST_FIELD_COUNT;i++){
        GetWindowTextW(price_fields[i],fields[i],_countof(fields[i]));
        if(!fields[i][wcsspn(fields[i],L" \t")]){
            SetWindowTextW(price_note,L"请填写订单、负责人、尺寸、净重、材料和数量。");
            SetFocus(price_fields[i]);return;
        }
    }
    ProPath path_file,python,script,input,output,csv;
    if(!storage_directory_on_drive(runtime_data,L'E')||
       swprintf_s(path_file,_countof(path_file),L"%ls\\resources\\sheet-metal-price\\python-path.txt",runtime_root)<0||
       swprintf_s(script,_countof(script),L"%ls\\resources\\sheet-metal-price\\price_preview.py",runtime_root)<0){
        SetWindowTextW(price_note,L"无法访问 E 盘估算文件。");return;
    }
    FILE *path=_wfopen(path_file,L"rt, ccs=UTF-16LE");
    int ready=path&&fgetws(python,_countof(python),path);
    if(path)fclose(path);
    if(ready)python[wcscspn(python,L"\r\n")]=0;
    if(!ready||GetFileAttributesW(python)==INVALID_FILE_ATTRIBUTES){
        SetWindowTextW(price_note,L"未找到已配置的 Python 运行环境。");return;
    }
    static LONG sequence;
    LONG serial=InterlockedIncrement(&sequence);
    if(swprintf_s(input,_countof(input),L"%ls\\price-%lu-%ld-input.txt",runtime_data,GetCurrentProcessId(),serial)<0||
       swprintf_s(output,_countof(output),L"%ls\\price-%lu-%ld-result.txt",runtime_data,GetCurrentProcessId(),serial)<0||
       swprintf_s(csv,_countof(csv),L"%ls\\price-%lu-%ld-result.csv",runtime_data,GetCurrentProcessId(),serial)<0)return;
    wchar_t cmd[1500];
    if(swprintf_s(cmd,_countof(cmd),L"\"%ls\" -E -s -B \"%ls\" \"%ls\" \"%ls\" \"%ls\"",python,script,input,output,csv)<0)return;
    FILE *file=_wfopen(input,L"wt, ccs=UTF-16LE");
    if(!file){SetWindowTextW(price_note,L"无法写入 E 盘临时输入。");return;}
    int good=1;
    for(int i=0;i<COST_FIELD_COUNT;i++)if(fwprintf(file,L"%ls\n",fields[i])<0)good=0;
    if(fwprintf(file,L"%d\n",SendMessageW(price_spray,BM_GETCHECK,0,0)==BST_CHECKED)<0)good=0;
    if(fclose(file))good=0;
    STARTUPINFOW startup={sizeof(startup)};
    PROCESS_INFORMATION process={0};
    if(good&&CreateProcessW(python,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,runtime_data,&startup,&process)){
        DWORD code=1;
        DWORD status=WaitForSingleObject(process.hProcess,5000);
        if(status!=WAIT_OBJECT_0){
            TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,1000);
        }
        good=status==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==0;
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }else good=0;
    wchar_t result[6000]=L"";
    if(good){
        FILE *out=_wfopen(output,L"rt, ccs=UTF-16LE");
        if(out){
            wchar_t line[600];
            while(fgetws(line,_countof(line)-1,out)){
                size_t length=wcslen(line);
                if(length&&line[length-1]==L'\n'&&(length<2||line[length-2]!=L'\r')){
                    line[length-1]=L'\r';line[length]=L'\n';line[length+1]=0;
                }
                if(wcslen(result)+wcslen(line)+1>=_countof(result))break;
                wcscat_s(result,_countof(result),line+(line[0]==0xfeff));
            }
            fclose(out);
        }
    }
    if(good&&result[0]){
        FILE *table=_wfopen(csv,L"rb");
        if(table){
            AcquireSRWLockExclusive(&price_export_lock);
            size_t bytes=fread(price_export_data,1,sizeof(price_export_data),table);
            price_export_length=!ferror(table)&&feof(table)?bytes:0;
            ReleaseSRWLockExclusive(&price_export_lock);
            fclose(table);
            EnableWindow(price_buttons[3],price_export_length>0);
        }
    }
    DeleteFileW(input);DeleteFileW(output);DeleteFileW(csv);
    if(result[0]){
        SetWindowTextW(price_output,result);
        SetWindowTextW(price_note,!wcsncmp(result,L"输入未通过校验",7)?L"请修正输入后重新计算。":L"预估明细  ·  仅供内部核算");
    }else SetWindowTextW(price_note,L"便携估算脚本执行失败，请检查 Python 运行环境。");
}

static void price_layout(int width,int height){
    int left=max(245,min(330,width/3)),x=left+23;
    MoveWindow(price_title,12,14,width-344,24,TRUE);
    MoveWindow(price_buttons[4],width-323,8,123,29,TRUE);
    MoveWindow(price_buttons[3],width-194,8,88,29,TRUE);
    MoveWindow(price_buttons[2],width-100,8,88,29,TRUE);
    for(int i=0;i<COST_FIELD_COUNT;i++){
        int y=55+i*37;
        MoveWindow(price_labels[i],12,y+4,114,23,TRUE);
        MoveWindow(price_fields[i],126,y,left-126,i==COST_MATERIAL?250:27,TRUE);
    }
    MoveWindow(price_spray,126,351,left-126,25,TRUE);
    MoveWindow(price_buttons[0],12,391,(left-18)/2,29,TRUE);
    MoveWindow(price_buttons[1],left/2+10,391,(left-18)/2,29,TRUE);
    MoveWindow(price_output,x,55,max(130,width-x-12),max(120,height-93),TRUE);
    MoveWindow(price_note,12,height-29,width-24,23,TRUE);
}

static LRESULT CALLBACK price_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        price_brush=CreateSolidBrush(CHAT_CANVAS_COLOR);
        price_title=clash_control(window,L"STATIC",L"钣金价格    ·    玛威尔钣金外壳估算",0,0);
        price_output=clash_control(window,L"EDIT",L"填写左侧参数后，点击「计算价格」查看材料、工序、喷涂及订单总价。",
            WS_BORDER|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,0);
        price_note=clash_control(window,L"STATIC",L"模型尺寸按 X / Y / Z 预填。请核对产品朝向并填写钣金外壳净重。",SS_ENDELLIPSIS,0);
        const wchar_t *labels[]={L"订单名称",L"结构负责人",L"宽 W（mm）",L"深 D（mm）",L"高 H（mm）",L"净重（kg）",L"材料牌号",L"数量"};
        for(int i=0;i<COST_FIELD_COUNT;i++){
            price_labels[i]=clash_control(window,L"STATIC",labels[i],0,0);
            price_fields[i]=clash_control(window,i==COST_MATERIAL?L"COMBOBOX":L"EDIT",L"",
                WS_TABSTOP|(i==COST_MATERIAL?CBS_DROPDOWNLIST|WS_VSCROLL:WS_BORDER|ES_AUTOHSCROLL),PRICE_FIELD+i);
        }
        for(int i=0;i<_countof(price_materials);i++)SendMessageW(price_fields[COST_MATERIAL],CB_ADDSTRING,0,(LPARAM)price_materials[i]);
        price_spray=clash_control(window,L"BUTTON",L"计入喷涂",BS_AUTOCHECKBOX|WS_TABSTOP,PRICE_SPRAY);
        SendMessageW(price_spray,BM_SETCHECK,BST_CHECKED,0);
        price_buttons[0]=clash_control(window,L"BUTTON",L"读取当前模型",BS_OWNERDRAW|WS_TABSTOP,PRICE_READ_BUTTON);
        price_buttons[1]=clash_control(window,L"BUTTON",L"计算价格",BS_OWNERDRAW|WS_TABSTOP,PRICE_RUN);
        price_buttons[2]=clash_control(window,L"BUTTON",L"返回聊天",BS_OWNERDRAW|WS_TABSTOP,PRICE_BACK);
        price_buttons[3]=clash_control(window,L"BUTTON",L"导出数据",BS_OWNERDRAW|WS_TABSTOP,PRICE_EXPORT_BUTTON);
        price_buttons[4]=clash_control(window,L"BUTTON",L"打开数据文件夹",BS_OWNERDRAW|WS_TABSTOP,PRICE_FOLDER_BUTTON);
        price_invalidate_export();
        return 0;
    }
    case WM_SIZE:price_layout(LOWORD(l),HIWORD(l));return 0;
    case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect((HDC)w,&r,price_brush);return 1;}
    case WM_CTLCOLORSTATIC:
        if((HWND)l==price_output){SetBkColor((HDC)w,RGB(255,255,255));return (LRESULT)GetStockObject(WHITE_BRUSH);}
        SetBkColor((HDC)w,CHAT_CANVAS_COLOR);return (LRESULT)price_brush;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT button=*(DRAWITEMSTRUCT *)l;
        if(button.CtlID==PRICE_RUN)button.CtlID=CHAT_SEND;
        native_theme_button(&button);return TRUE;
    }
    case PRICE_MODEL:
        if((ProError)w)SetWindowTextW(price_note,L"无法读取当前模型，请激活 Creo 零件或装配，也可手动录入。");
        else{
            const PriceModel *update=(const PriceModel *)l;
            if(!GetWindowTextLengthW(price_fields[COST_NAME]))SetWindowTextW(price_fields[COST_NAME],update->name);
            for(int i=0;i<3;i++){
                wchar_t size[32];swprintf_s(size,_countof(size),L"%.6g",update->size[i]);
                SetWindowTextW(price_fields[COST_W+i],size);
            }
            int material=update->material[0]?(int)SendMessageW(price_fields[COST_MATERIAL],CB_FINDSTRINGEXACT,-1,(LPARAM)update->material):-1;
            SendMessageW(price_fields[COST_MATERIAL],CB_SETCURSEL,material,0);
            SetWindowTextW(price_fields[COST_NET],L"");
            SetWindowTextW(price_note,L"已读取包络尺寸。请确认产品朝向、材料和钣金外壳净重后再计算。");
        }
        return 0;
    case PRICE_EXPORT_RESULT:
        SetWindowTextW(price_note,(const wchar_t *)l);
        return 0;
    case WM_COMMAND:
        if((LOWORD(w)>=PRICE_FIELD&&LOWORD(w)<PRICE_FIELD+COST_FIELD_COUNT&&
           (HIWORD(w)==EN_CHANGE||HIWORD(w)==CBN_SELCHANGE))||LOWORD(w)==PRICE_SPRAY)
            price_invalidate_export();
        switch(LOWORD(w)){
        case PRICE_READ_BUTTON:
            if(native_busy||!native_backend_window||!PostMessageW(native_backend_window,PACKAGING_REQUEST,PRICE_READ,0))
                SetWindowTextW(price_note,L"Creo 正忙或命令通道未连接。");
            break;
        case PRICE_RUN:price_estimate();break;
        case PRICE_EXPORT_BUTTON:
            if(!native_backend_window||native_busy||!PostMessageW(native_backend_window,PACKAGING_REQUEST,PRICE_EXPORT,0))
                SetWindowTextW(price_note,L"Creo 正忙，请稍后重新导出。");
            break;
        case PRICE_FOLDER_BUTTON:
            if(!native_backend_window||native_busy||!PostMessageW(native_backend_window,PACKAGING_REQUEST,PRICE_OPEN_FOLDER,0))
                SetWindowTextW(price_note,L"Creo 正忙，请稍后打开数据文件夹。");
            break;
        case PRICE_BACK:packaging_switch(0);break;
        }
        return 0;
    case WM_DESTROY:price_invalidate_export();price_panel=NULL;DeleteObject(price_brush);price_brush=NULL;return 0;
    }
    return DefWindowProcW(window,message,w,l);
}

static void price_open(HWND parent){
    if(price_panel)return;
    const wchar_t *name=L"MweaiSheetMetalPrice";
    WNDCLASSW cls={0};
    cls.lpfnWndProc=price_proc;cls.hInstance=GetModuleHandleW(NULL);cls.lpszClassName=name;
    cls.hCursor=LoadCursor(NULL,IDC_ARROW);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    price_panel=CreateWindowW(name,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,0,0,parent,NULL,cls.hInstance,NULL);
}
