static ProError pressfit_delivery_pdf_attachment(wchar_t path[MAX_PATH],PartText *out){
    NativeFile files[NATIVE_ATTACHMENT_MAX];
    const int count=native_files_snapshot(files);
    int pdf=-1;
    for(int i=0;i<count;i++){
        if(files[i].kind!=ATTACH_PDF)continue;
        if(pdf!=-1){
            part_text_add(out,L"请给这次孔位核对留一份 PDF 附件。\n");
            return PRO_TK_BAD_INPUTS;
        }
        pdf=i;
    }
    if(pdf<0){
        part_text_add(out,L"请通过「+ 附件」添加要核对的孔位表 PDF。\n");
        return PRO_TK_E_NOT_FOUND;
    }
    return wcscpy_s(path,MAX_PATH,files[pdf].path)?PRO_TK_OUT_OF_RANGE:PRO_TK_NO_ERROR;
}

static ProError pressfit_delivery_pdf_process(const wchar_t *input,wchar_t result[MAX_PATH]){
    wchar_t pdf[MAX_PATH],attachments[MAX_PATH],script[MAX_PATH],powershell[MAX_PATH];
    if(!or_real_path(input,0,pdf)||!or_real_path(native_attachment_root,1,attachments)||
       !or_below(pdf,attachments)||!storage_directory_on_drive(attachments,L'E')||
       !storage_directory_on_drive(ai_dir,L'E')||!storage_output_directory(ai_dir)||
       swprintf_s(result,MAX_PATH,L"%ls\\pressfit-pdf-%lu-%ld.tsv",ai_dir,GetCurrentProcessId(),
        InterlockedIncrement(&pressfit_delivery_serial))<0||
       swprintf_s(script,MAX_PATH,L"%ls\\scripts\\Read-BWTLPressfitPdf.ps1",runtime_root)<0||
       GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES)return PRO_TK_CANT_OPEN;
    UINT length=GetSystemDirectoryW(powershell,MAX_PATH);
    if(!length||length>=MAX_PATH||wcscat_s(powershell,MAX_PATH,L"\\WindowsPowerShell\\v1.0\\powershell.exe"))
        return PRO_TK_CANT_OPEN;
    wchar_t command[MAX_PATH*5+128];
    if(swprintf_s(command,_countof(command),
        L"\"%ls\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\" -Pdf \"%ls\" -Exchange \"%ls\" -Result \"%ls\"",
        powershell,script,pdf,ai_dir,result)<0)return PRO_TK_OUT_OF_RANGE;
    STARTUPINFOW startup={sizeof(startup)};
    PROCESS_INFORMATION child={0};
    if(!CreateProcessW(powershell,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,ai_dir,&startup,&child))
        return PRO_TK_CANT_OPEN;
    CloseHandle(child.hThread);
    const DWORD state=WaitForSingleObject(child.hProcess,38000);
    DWORD exit_code=1;
    if(state==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&exit_code);
    else TerminateProcess(child.hProcess,1);
    CloseHandle(child.hProcess);
    return !exit_code?PRO_TK_NO_ERROR:state==WAIT_TIMEOUT?PRO_TK_E_IN_USE:PRO_TK_CANT_OPEN;
}

static const wchar_t *pressfit_delivery_pdf_reason(const wchar_t *status){
    if(!wcscmp(status,L"PAGE_LIMIT"))return L"PDF 超过四页，请将有孔位表的页面单独存成 PDF。";
    if(!wcscmp(status,L"OCR_UNAVAILABLE"))return L"本机没有可用的 Windows OCR 语言，无法读出 PDF 孔位表。";
    if(!wcscmp(status,L"ROW_LIMIT"))return L"孔位表超过 128 行，请分成几份核对。";
    if(!wcscmp(status,L"NO_HEADER"))return L"未识别到孔位表表头。可用 PANEL X Y Z D GROUP SPEC，Z 列可以省略，尺寸单位为 mm。";
    if(!wcscmp(status,L"NO_ROWS"))return L"识别到了表头，未读到孔位数据行。";
    return L"PDF 页面没有成功读取。请核对文件是否加密、损坏或表格是否清晰。";
}

static int pressfit_pdf_fields(wchar_t *row,wchar_t *values[],int capacity){
    row[wcscspn(row,L"\r\n")]=0;
    int n=0;
    while(n<capacity){
        values[n++]=row;
        wchar_t *tab=wcschr(row,L'\t');
        if(!tab)return n;
        *tab=0;
        row=tab+1;
    }
    return -1;
}

static int pressfit_delivery_pdf_int(const wchar_t *text,int max){
    if(!*text||wcsspn(text,L"0123456789")!=wcslen(text))return 0;
    const long value=wcstol(text,NULL,10);
    return value>0&&value<=max?(int)value:0;
}

static int pressfit_delivery_pdf_number(const wchar_t *text,double *value){
    wchar_t *end=NULL;
    *value=wcstod(text,&end);
    return end!=text&&!*end&&isfinite(*value)&&fabs(*value)<1e7;
}

static int pressfit_delivery_pdf_row(wchar_t *const *field,const PressfitDeliveryRow *rows,int count,
    unsigned char *matched,PartText *out){
    const int page=pressfit_delivery_pdf_int(field[1],4);
    const int line=pressfit_delivery_pdf_int(field[2],10000);
    const int panel=pressfit_delivery_pdf_int(field[3],INT_MAX);
    double x,y,z=0,diameter;
    const int has_z=wcscmp(field[6],L"-")!=0;
    PressfitSelector spec={0};
    if(!page||!line||!panel||!pressfit_delivery_pdf_number(field[4],&x)||
       !pressfit_delivery_pdf_number(field[5],&y)||(has_z&&!pressfit_delivery_pdf_number(field[6],&z))||
       !pressfit_delivery_pdf_number(field[7],&diameter)||diameter<=0||
       !or_safe_id(field[8])||!pressfit_selector(field[9],&spec)||!spec.instance_key[0]){
        part_text_add(out,L"PDF 第 %d 页文字行 %d 有不可核对的字段。\n",page,line);
        return 0;
    }
    int index=-1,hits=0;
    for(int i=0;i<count;i++){
        const PressfitDeliveryRow *row=&rows[i];
        if(row->panel_id==panel&&fabs(row->center[0]-x)<=0.01&&fabs(row->center[1]-y)<=0.01&&
           (!has_z||fabs(row->center[2]-z)<=0.01)){index=i;hits++;}
    }
    if(hits!=1){
        part_text_add(out,L"PDF 第 %d 页文字行 %d 在板件 ID:%d 的 (%.6g, %.6g) 找到 %d 个孔，无法唯一核对。\n",
            page,line,panel,x,y,hits);
        return 0;
    }
    if(matched[index]){
        part_text_add(out,L"PDF 第 %d 页文字行 %d 重复指定板件 ID:%d 的 %d 号孔。\n",page,line,panel,rows[index].number);
        return 0;
    }
    matched[index]=1;
    const PressfitDeliveryRow *row=&rows[index];
    const wchar_t *existing=pressfit_delivery_text(row->spec),*group=pressfit_delivery_text(row->group);
    if(fabs(row->diameter-diameter)>0.01||(*existing&&!pressfit_delivery_same_spec(existing,field[9]))||
       (*group&&_wcsicmp(group,field[8]))){
        part_text_add(out,L"PDF 第 %d 页文字行 %d 与板件 ID:%d 的 %d 号孔不符。PDF Ø%.6g、%ls、%ls；模型 Ø%.6g、%ls、%ls。\n",
            page,line,panel,row->number,diameter,field[8],field[9],row->diameter,group,existing);
        return 0;
    }
    part_text_add(out,L"PDF 第 %d 页文字行 %d 对应板件 ID:%d 的 %d 号孔，实测 Ø%.6g mm，表格要求 %ls 装 %ls。\n",
        page,line,panel,row->number,row->diameter,field[8],field[9]);
    return 1;
}

static ProError pressfit_delivery_pdf_read(FILE *file,const PressfitDeliveryRow *rows,int count,PartText *out){
    wchar_t line[512];
    if(!pressfit_record_line(file,line,_countof(line))||wcscmp(line,L"BWTL-PRESSFIT-PDF-1"))return PRO_TK_CANT_OPEN;
    unsigned char matched[PRESSFIT_DELIVERY_MAX]={0};
    int total=0,verified=0;
    while(pressfit_record_line(file,line,_countof(line))){
        wchar_t *field[10];
        const int n=pressfit_pdf_fields(line,field,_countof(field));
        if(n==2&&!wcscmp(field[0],L"ERROR")){
            part_text_add(out,L"%ls\n",pressfit_delivery_pdf_reason(field[1]));
            return PRO_TK_CANT_OPEN;
        }
        if(++total>128)return PRO_TK_OUT_OF_RANGE;
        if(n==3&&!wcscmp(field[0],L"BAD"))
            part_text_add(out,L"PDF 第 %ls 页文字行 %ls 的字符或列无法识别，请对照原表。\n",field[1],field[2]);
        else if(n==10&&!wcscmp(field[0],L"ROW"))
            verified+=pressfit_delivery_pdf_row(field,rows,count,matched,out);
        else return PRO_TK_CANT_OPEN;
    }
    if(!feof(file)||ferror(file)||!total)return PRO_TK_CANT_OPEN;
    part_text_add(out,L"共读到 %d 行，%d 行与 Creo 原生孔位和尺寸对应。此次只核对 PDF 与模型。\n",total,verified);
    return verified==total?PRO_TK_NO_ERROR:PRO_TK_INCOMPLETE;
}

static ProError pressfit_delivery_pdf_compare(const wchar_t *attached_pdf,const PressfitDeliveryRow *rows,
    int count,PartText *out){
    if(!attached_pdf||!rows||count<1||count>PRESSFIT_DELIVERY_MAX)return PRO_TK_BAD_INPUTS;
    wchar_t result[MAX_PATH]=L"";
    ProError error=pressfit_delivery_pdf_process(attached_pdf,result);
    if(error)part_text_add(out,L"本机 PDF 读取未完成，请检查当前 PDF 附件和 E 盘会话目录（Creo %d）。\n",error);
    else{
        FILE *file=ax_learning_open(result);
        if(!file)error=PRO_TK_CANT_OPEN;
        else{
            error=pressfit_delivery_pdf_read(file,rows,count,out);
            fclose(file);
        }
    }
    if(result[0])DeleteFileW(result);
    return error;
}