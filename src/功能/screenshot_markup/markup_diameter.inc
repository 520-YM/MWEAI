static const wchar_t *markup_skip_space(const wchar_t *text){
    while(iswspace(*text))text++;
    return text;
}

static int markup_take_phrase(const wchar_t **text,const wchar_t *const *phrases,size_t count){
    for(size_t i=0;i<count;i++){
        size_t length=wcslen(phrases[i]);
        if(!wcsncmp(*text,phrases[i],length)){
            *text=markup_skip_space(*text+length);
            return 1;
        }
    }
    return 0;
}

/* Accept one absolute target diameter, including common variations in Chinese. */
static int markup_parse_diameter(const wchar_t *text,double *diameter){
    static const wchar_t *const subjects[]={L"这个孔",L"该孔",L"孔"};
    static const wchar_t *const diameter_words[]={L"直径",L"孔径"};
    static const wchar_t *const edit_words[]={L"修改为",L"修改成",L"修改到",L"改成",L"改为",L"改到",L"设为",L"调到",L"改"};
    text=markup_skip_space(text);
    if(*text==L'把'||*text==L'将')text=markup_skip_space(text+1);
    if(wcsncmp(text,L"孔径",2)&&markup_take_phrase(&text,subjects,_countof(subjects))&&*text==L'的')
        text=markup_skip_space(text+1);
    int named=markup_take_phrase(&text,diameter_words,_countof(diameter_words));
    int edit=markup_take_phrase(&text,edit_words,_countof(edit_words));
    while(iswspace(*text)||*text==L':'||*text==L'：')text++;
    int symbol=*text==L'Ø'||*text==L'ø'||*text==L'Φ'||*text==L'φ'||*text==L'⌀';
    if(symbol)text=markup_skip_space(text+1);
    if(!(named||edit||symbol)||*text<L'0'||*text>L'9')return 0;
    double number=0;
    while(*text>=L'0'&&*text<=L'9'){
        number=number*10+(*text++-L'0');
        if(number>10000)return 0;
    }
    if(*text==L'.'){
        text++;
        if(*text<L'0'||*text>L'9')return 0;
        double place=0.1;
        while(*text>=L'0'&&*text<=L'9'){
            number+=(*text++-L'0')*place;
            place*=0.1;
        }
    }
    while(iswspace(*text))text++;
    if(!_wcsnicmp(text,L"mm",2)||!wcsncmp(text,L"毫米",2))text+=2;
    while(iswspace(*text)||*text==L'。'||*text==L'！'||*text==L'!')text++;
    if(*text||number<0.1||number>10000)return 0;
    *diameter=number;
    return 1;
}

static int markup_named_hole(const wchar_t *text){
    return wcsstr(text,L"孔径")||wcsstr(text,L"直径")||wcsstr(text,L"这个孔")||wcsstr(text,L"该孔")||
        wcspbrk(text,L"ØøΦφ⌀");
}


/* An explicit area-group request can target several independently editable bores.
   Other marked requests keep the original one-object interpretation. */
static int markup_collective_diameter(const wchar_t *text,double *value,int *wanted){
    static const wchar_t *const group[]={L"所有标注的",L"全部标注的",L"所有圈出的",L"我圈出来的",
        L"圈出来的",L"圈起来的",L"我圈出的",L"我圈的",L"圈出的",L"圈中的",L"框选的",L"标注的",L"选中的",L"这"};
    static const wchar_t *const all[]={L"全部都",L"全部",L"全都",L"都",L"统一"};
    static const wchar_t *const num[]={L"一",L"两",L"三",L"四",L"五",L"六",L"七",L"八",L"九",L"十"};
    static const wchar_t *const object[]={L"圆孔",L"孔"};
    text=markup_skip_space(text);
    if(*text==L'把'||*text==L'将')text=markup_skip_space(text+1);
    if(!markup_take_phrase(&text,group,_countof(group)))return 0;
    int specific=0;
    if(*text>=L'1'&&*text<=L'9'){
        while(*text>=L'0'&&*text<=L'9'){
            specific=specific*10+(*text++-L'0');
            if(specific>MARKUP_MAX_EDITS)return 0;
        }
    }else if(*text==L'二'){
        specific=2;
        text++;
    }else if(*text==L'俩'){
        specific=2;
        text++;
    }else for(int i=0;i<_countof(num);i++)if(*text==num[i][0]){
        specific=i+1;
        text++;
        break;
    }
    text=markup_skip_space(text);
    if(specific&&(*text==L'个'||*text==L'处'))text=markup_skip_space(text+1);
    if(wcsncmp(text,L"孔径",2)){
        if(!markup_take_phrase(&text,object,_countof(object)))return 0;
        if(*text==L'的')text=markup_skip_space(text+1);
    }
    if(wcsncmp(text,L"孔径",2)&&wcsncmp(text,L"直径",2)){
        (void)markup_take_phrase(&text,all,_countof(all));
    }
    if(!markup_parse_diameter(text,value))return 0;
    *wanted=specific;
    return 1;
}

static int markup_edge_length_request(const wchar_t *text,double *value){
    static const wchar_t *const target[]={L"箭头指的这条开口边",L"这条开口边",L"箭头指的开口边",L"标注的开口边",L"开口边"};
    text=markup_skip_space(text);
    if(*text==L'把'||*text==L'将')text=markup_skip_space(text+1);
    if(!markup_take_phrase(&text,target,_countof(target)))return 0;
    if(*text==L'的')text=markup_skip_space(text+1);
    if(!wcsncmp(text,L"长度",2))text=markup_skip_space(text+2);
    return !wcspbrk(text,L"ØøΦφ⌀")&&markup_parse_diameter(text,value);
}

static int markup_face_request(const wchar_t *text){
    static const wchar_t *const asks[]={L"读取标注的面",L"读取标注的板面",L"查看标注的面",
        L"识别标注的面",L"标注的是什么面",L"这几个标注是什么面"};
    text=markup_skip_space(text);
    return markup_take_phrase(&text,asks,_countof(asks))&&text[wcsspn(text,L"。！!？? \t")]==0;
}

static int markup_neighbor_clause(const wchar_t *text){
    const wchar_t *const phrases[]={L"旁边的孔别动",L"旁边的孔不动",L"旁边的孔保持不变",
        L"其他孔别动",L"其他孔不动",L"其余孔保持不变",L"邻孔保持原样"};
    for(int i=0;i<_countof(phrases);i++){
        const wchar_t *p=markup_skip_space(text);
        size_t n=wcslen(phrases[i]);
        if(wcsncmp(p,phrases[i],n))continue;
        p=markup_skip_space(p+n);
        if(*p==L'。'||*p==L'！'||*p==L'!')p=markup_skip_space(p+1);
        if(!*p)return 1;
    }
    return 0;
}

static int markup_display_diameter(const wchar_t *text,double *value){
    if(markup_parse_diameter(text,value))return 1;
    const wchar_t *comma=wcspbrk(text,L"，,");
    if(!comma||!markup_neighbor_clause(comma+1))return 0;
    wchar_t clause[256];
    if(comma-text>=_countof(clause))return 0;
    wcsncpy_s(clause,_countof(clause),text,comma-text);
    return markup_parse_diameter(clause,value);
}

static int markup_read_size(const wchar_t **text,double *size){
    const wchar_t *p=markup_skip_space(*text);
    if(wcschr(L"ØøΦφ⌀",*p)&&*p)p=markup_skip_space(p+1);
    if(*p<L'0'||*p>L'9')return 0;
    const wchar_t *end=p+wcsspn(p,L"0123456789.");
    wchar_t *read=NULL;
    double value=wcstod(p,&read);
    if(read!=end||!isfinite(value)||value<0.1||value>10000)return 0;
    *text=markup_skip_space(end);
    *size=value;
    return 1;
}

static void markup_read_mm(const wchar_t **text){
    if(!_wcsnicmp(*text,L"mm",2)||!wcsncmp(*text,L"毫米",2))*text=markup_skip_space(*text+2);
}

static int markup_followup_parse(const wchar_t *input,int *side,double *delta){
    const wchar_t *text=markup_skip_space(input);
    const wchar_t *const right[]={L"右边那个孔",L"右边的孔",L"右边那个"};
    const wchar_t *const left[]={L"左边那个孔",L"左边的孔",L"左边那个"};
    const wchar_t *const recent[]={L"刚才那个孔",L"刚才这个孔",L"这个孔",L"那个孔"};
    *side=0;
    if(markup_take_phrase(&text,right,_countof(right)))*side=1;
    else if(markup_take_phrase(&text,left,_countof(left)))*side=-1;
    else if(!markup_take_phrase(&text,recent,_countof(recent)))return 0;
    const wchar_t *const enlarge[]={L"再放大",L"再增大",L"再扩大",L"再大"};
    const wchar_t *const reduce[]={L"再缩小",L"再减小",L"再小"};
    int plus=markup_take_phrase(&text,enlarge,_countof(enlarge));
    if(!plus&&!markup_take_phrase(&text,reduce,_countof(reduce)))return 0;
    double size;
    if(!markup_read_size(&text,&size))return 0;
    markup_read_mm(&text);
    while(iswspace(*text)||*text==L'。'||*text==L'！')text++;
    if(*text)return 0;
    *delta=plus?size:-size;
    return 1;
}

static int markup_followup_hint(const wchar_t *text){
    int side;
    double delta;
    return markup_followup_parse(text,&side,&delta);
}

