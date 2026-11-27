enum { BSO_FIND=1,BSO_SAVE,BSO_PLACE };
typedef struct {int action,length,expected;} FastenerBsoRequest;

static int fastener_bso_spec(const wchar_t **input,int *length,int *branded){
    wchar_t key[32];
    size_t n=0;
    const wchar_t *p=fastener_chat_separator(*input);
    wchar_t close=*p==L'「'?L'」':*p==L'“'?L'”':*p==L'"'?L'"':0;
    if(close)p++;
    /* BSO_M3X5 is a different, exact stock instance. It stays in the generic catalog. */
    if(!_wcsnicmp(p,L"BSO_M",5)||!_wcsnicmp(p,L"BSO-M",5))return 0;
    for(;*p;p++){
        wchar_t c=towupper(*p);
        if(c==L' '||c==L'\t'||c==L'-'||c==L'_'||c==L'.')continue;
        if(c==L'×'||c==L'＊'||c==L'*')c=L'X';
        if(!wcschr(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",c))break;
        if(n+1>=_countof(key))return 0;
        key[n++]=c;
    }
    if(close&&*p++!=close)return 0;
    key[n]=0;
    const wchar_t *spec=key;
    *branded=!wcsncmp(spec,L"BSO",3);
    if(*branded){
        spec+=3;
        if(!wcsncmp(spec,L"35",2))spec+=2;
    }
    if(!wcscmp(spec,L"M3X5"))*length=5;
    else if(!wcscmp(spec,L"M3X6"))*length=6;
    else return 0;
    *input=p;
    return 1;
}

static int fastener_bso_count(const wchar_t **input){
    const wchar_t *p=fastener_chat_separator(*input);
    static const wchar_t *const numbers[]={L"十二",L"十一",L"十",L"九",L"八",L"七",L"六",L"五",L"四",L"三",L"两",L"二",L"一"};
    static const int counts[]={12,11,10,9,8,7,6,5,4,3,2,2,1};
    int count=0;
    for(size_t i=0;i<_countof(numbers);i++)if(assembly_take(&p,numbers[i])){count=counts[i];break;}
    if(!count&&*p>=L'1'&&*p<=L'9'){
        wchar_t *end;
        long number=wcstol(p,&end,10);
        if(number<1||number>12)return 0;
        count=(int)number;
        p=end;
    }
    if(count)*input=fastener_chat_separator(p);
    return count;
}

static int fastener_bso_hole_count(const wchar_t **input){
    const wchar_t *p=fastener_chat_separator(*input);
    assembly_take(&p,L"选中的");
    assembly_take(&p,L"这");
    int count=fastener_bso_count(&p);
    if(!count)return 0;
    assembly_take(&p,L"个");
    if(!assembly_take(&p,L"孔位")&&!assembly_take(&p,L"孔"))return 0;
    *input=p;
    return count;
}

static const wchar_t *fastener_bso_action_start(const wchar_t *text){
    const wchar_t *p=fastener_chat_separator(text);
    if(!assembly_take(&p,L"那么"))assembly_take(&p,L"那");
    assembly_take(&p,L"你");
    assembly_take(&p,L"先");
    return model_request_action_start(p);
}

static int fastener_bso_parse(const wchar_t *text,FastenerBsoRequest *request,int allow_prior);

static int fastener_bso_prior(const wchar_t *latest,FastenerBsoRequest *prior){
    const wchar_t *lines[7];
    size_t count=0;
    for(const wchar_t *p=conversation;(p=wcsstr(p,L"\n用户："))!=NULL;){
        p+=4;
        lines[count++%_countof(lines)]=p;
    }
    size_t remaining=count<_countof(lines)?count:_countof(lines);
    while(remaining--){
        const wchar_t *line=lines[--count%_countof(lines)];
        size_t n=wcscspn(line,L"\r\n");
        wchar_t raw[6001];
        if(!n||n>=_countof(raw)||wcslen(latest)==n&&!wcsncmp(latest,line,n))continue;
        if(!wcsncpy_s(raw,_countof(raw),line,n)&&fastener_bso_parse(raw,prior,0))return 1;
    }
    return 0;
}

static int fastener_bso_confirmed_end(const wchar_t *p,int length){
    if(fastener_chat_end(p))return 1;
    if(!assembly_take(&p,L"。记住是")&&!assembly_take(&p,L"，记住是"))return 0;
    int repeated=0,branded=0;
    if(!fastener_bso_spec(&p,&repeated,&branded)||repeated!=length)return 0;
    if(fastener_chat_end(p))return 1;
    return assembly_take(&p,L"。一定要成功，自己想办法")&&fastener_chat_end(p);
}

static int fastener_bso_parse(const wchar_t *text,FastenerBsoRequest *request,int allow_prior){
    const wchar_t *p=fastener_bso_action_start(text);
    if(wcspbrk(p,L"?？\r\n"))return 0;
    FastenerBsoRequest result={BSO_FIND,0,0};
    int place=assembly_take(&p,L"装配")||assembly_take(&p,L"安装")||assembly_take(&p,L"装上");
    if(place)result.action=BSO_PLACE;
    else if(assembly_take(&p,L"打开并保存")||assembly_take(&p,L"读取并另存")||assembly_take(&p,L"我要")||
        assembly_take(&p,L"调出")||assembly_take(&p,L"打开")||assembly_take(&p,L"另存"))result.action=BSO_SAVE;
    else if(!assembly_take(&p,L"查找")&&!assembly_take(&p,L"搜索"))assembly_take(&p,L"读取");
    p=fastener_chat_separator(p);
    if(place){
        const wchar_t *quantity=p;
        int count=fastener_bso_count(&quantity);
        if(count&&(assembly_take(&quantity,L"个")||assembly_take(&quantity,L"件"))){
            p=fastener_chat_separator(quantity);
            result.expected=count;
        }
    }
    int object_first=assembly_take(&p,L"把")||assembly_take(&p,L"给");
    if(object_first){
        result.expected=fastener_bso_hole_count(&p);
        if(result.expected){
            assembly_take(&p,L"都");
            if(!assembly_take(&p,L"装上")&&!assembly_take(&p,L"装配")&&!assembly_take(&p,L"安装"))return 0;
            result.action=BSO_PLACE;
        }
    }
    p=fastener_chat_separator(p);
    fastener_chat_scope(&p);
    int branded=0;
    int exact=fastener_bso_spec(&p,&result.length,&branded);
    int reference=!exact&&object_first&&assembly_take(&p,L"它");
    if(!exact&&!reference)return 0;
    p=fastener_chat_separator(p);
    assembly_take(&p,L"的");
    if(assembly_take(&p,L"装配到")||assembly_take(&p,L"装到")||assembly_take(&p,L"安装到"))result.action=BSO_PLACE;
    else if(assembly_take(&p,L"调出来")||assembly_take(&p,L"调出")||assembly_take(&p,L"另存到")||
        assembly_take(&p,L"并另存到")||assembly_take(&p,L"并保存到"))result.action=BSO_SAVE;
    p=fastener_chat_separator(p);
    if(result.action==BSO_PLACE){
        assembly_take(&p,L"到");
        if(!result.expected)result.expected=fastener_bso_hole_count(&p);
        if(!assembly_take(&p,L"选中的孔位")&&!assembly_take(&p,L"选中的孔")&&
            !assembly_take(&p,L"对应位置上")&&!assembly_take(&p,L"对应位置"))assembly_take(&p,L"这几个孔");
    }
    if(result.action==BSO_SAVE){
        if(!assembly_take(&p,L"我的工作目录")&&!assembly_take(&p,L"当前工作目录"))assembly_take(&p,L"工作目录");
        assembly_take(&p,L"里面");
    }
    if(!branded){
        FastenerBsoRequest prior={0};
        if(!allow_prior||!fastener_bso_prior(text,&prior))return 0;
        if(reference)result.length=prior.length;
        if(!result.expected&&prior.action==BSO_PLACE&&result.action==BSO_PLACE)result.expected=prior.expected;
    }
    if(!fastener_bso_confirmed_end(p,result.length))return 0;
    *request=result;
    return 1;
}

static int fastener_bso_request(const wchar_t *text){
    FastenerBsoRequest result;
    return fastener_bso_parse(text,&result,1)?result.action:0;
}

static int fastener_bso_contextual_request(const wchar_t *text){
    FastenerBsoRequest result;
    return fastener_bso_parse(text,&result,1);
}


static int fastener_bso_chat(const wchar_t *text){
    FastenerBsoRequest request;
    if(!fastener_bso_parse(text,&request,1))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成。");return 1;}
    const int action=request.action;
    smart_begin(text);
    smart_local_plan=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    wchar_t report[3500]=L"",library[MAX_PATH]=L"",saved[MAX_PATH]=L"";
    PartText out={report,_countof(report),0};
    ProMdl assembly=NULL,source=NULL,project=NULL;
    BsoSite sites[12];
    int count=0;
    double mm=0;
    ProWVerstamp version=NULL,after=NULL;
    ProPath directory,current_directory;
    int window=0,current_window=0;
    ProError error=0;
    if(action==BSO_PLACE){
        ProMdlType type;
        if(ProMdlCurrentGet(&assembly)||ProMdlTypeGet(assembly,&type)||type!=PRO_MDL_ASSEMBLY){
            part_text_add(&out,L"请先打开准备安装螺母柱的装配。");
            error=PRO_TK_BAD_CONTEXT;
        }
        if(!error)error=ProMdlVerstampGet(assembly,&version);
        if(!error)error=ProWindowCurrentGet(&window);
        if(!error)error=ProDirectoryCurrentGet(directory);
        if(!error)error=qa_mm_per_unit(assembly,&mm);
        if(!error)error=fastener_bso_resolve_sites(assembly,mm,request.expected,sites,&count,&out);
    }
    if(!error){
        wchar_t result[1800]=L"";
        PartText detail={result,_countof(result),0};
        error=fastener_bso_read_exact(request.length,&source,library,&detail);
        if(result[0])part_text_add(&out,L"%ls\n",result);
    }
    if(!error&&action!=BSO_FIND){
        wchar_t result[1800]=L"";
        PartText detail={result,_countof(result),0};
        error=fastener_bso_copy_exact(request.length,source,&project,saved,&detail);
        if(!error)part_text_add(&out,L"工作目录中的原生零件：%ls\n",saved);
        else if(result[0])part_text_add(&out,L"%ls\n",result);
    }
    if(!error&&action==BSO_PLACE){
        ProMdl active=NULL;
        if(ProMdlCurrentGet(&active)||active!=assembly||ProWindowCurrentGet(&current_window)||current_window!=window||
           ProDirectoryCurrentGet(current_directory)||_wcsicmp(current_directory,directory)||
           ProMdlVerstampGet(assembly,&after)||!ProVerstampEqual(version,after)){
            part_text_add(&out,L"装配、窗口、版本或工作目录发生变化，请重新选定实际孔位。");
            error=PRO_TK_BAD_CONTEXT;
        }else error=fastener_bso_place(assembly,project,sites,count,mm,&out);
    }
    if(!error&&action==BSO_FIND)part_text_add(&out,L"族表来源：%ls\n",library);
    if(error)part_text_add(&out,L"\n%ls未完成（Creo %d）。",action==BSO_PLACE?L"装配":action==BSO_SAVE?L"查找或另存":L"读取",error);
    if(after)ProVerstampFree(&after);
    if(version)ProVerstampFree(&version);
    smart_record_tool(report);
    smart_reply(report);
    return 1;
}
