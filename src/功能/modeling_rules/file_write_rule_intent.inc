/* A general note records a confirmed user preference. Native rule settings remain separate. */
#define FILE_WRITE_RULE_TAG L"[BWTL_RULE_NOTE_V1]\n"
enum { FILE_WRITE_RULE_LIMIT=1300, FILE_WRITE_RULE_REQUEST_LIMIT=6000 };

typedef struct {
    const wchar_t *literal,*followup;
    size_t length,followup_length;
    int local;
} FileWriteRuleIntent;

static const wchar_t *file_write_rule_space(const wchar_t *text){
    while(*text&&iswspace(*text))text++;
    return text;
}

static wchar_t file_write_rule_quote_end(wchar_t opening){
    switch(opening){
    case L'「':return L'」';
    case L'『':return L'』';
    case L'“':return L'”';
    case L'"':return L'"';
    case L'`':return L'`';
    default:return 0;
    }
}

/* A quoted or fenced example is one span and cannot supply write authority. */
static const wchar_t *file_write_rule_markup_end(const wchar_t *p){
    const wchar_t *fence=!wcsncmp(p,L"\x60\x60\x60",3)?L"\x60\x60\x60":
        !wcsncmp(p,L"~~~",3)?L"~~~":NULL;
    if(fence){
        const wchar_t *end=wcsstr(p+3,fence);
        return end?end+3:p+wcslen(p);
    }
    const wchar_t close=file_write_rule_quote_end(*p);
    if(!close)return p;
    const wchar_t *end=wcschr(p+1,close);
    return end?end+1:p+wcslen(p);
}

static const wchar_t *file_write_rule_boundary(const wchar_t *p,const wchar_t *separators){
    while(*p&&!wcschr(separators,*p)){
        const wchar_t *next=file_write_rule_markup_end(p);
        p=next==p?p+1:next;
    }
    return p;
}

static const wchar_t *file_write_rule_action(const wchar_t *text){
    const wchar_t *p=model_request_action_start(text);
    if(!wcsncmp(p,L"但是",2)||!wcsncmp(p,L"不过",2))p=model_request_action_start(p+2);
    else if(*p==L'但')p=model_request_action_start(p+1);
    if(*p==L'你')p=model_request_action_start(p+1);
    if(*p==L'就'||*p==L'先')p=file_write_rule_space(p+1);
    return p;
}

static int file_write_rule_target(const wchar_t **input){
    static const wchar_t *const suffixes[]={L"文件里面",L"文件中",L"文件里",L"文件",L"里面去",L"里面",L"里去",L"里",L"中",L"内"};
    const wchar_t *p=file_write_rule_space(*input);
    wchar_t close=file_write_rule_quote_end(*p);
    if(close)p=file_write_rule_space(p+1);
    size_t branded=!_wcsnicmp(p,L"MWEAI",5)?5:
        ((!_wcsnicmp(p,L"BWTL",4)||!_wcsnicmp(p,L"BETL",4))?4:0);
    if(branded){
        p=file_write_rule_space(p+branded);
        if(*p==L'的')p=file_write_rule_space(p+1);
    }
    if(!assembly_take(&p,L"建模规则")&&(branded||!assembly_take(&p,L"规则")))return 0;
    p=file_write_rule_space(p);
    if(close){if(*p!=close)return 0;p=file_write_rule_space(p+1);}
    if(modeling_part_take_any(&p,suffixes,_countof(suffixes)))p=file_write_rule_space(p);
    *input=p;
    return 1;
}

static int file_write_rule_write(const wchar_t *verb,const wchar_t *end,FileWriteRuleIntent *intent){
    static const wchar_t *const verbs[]={
        L"追加",L"补充",L"记录",L"登记",L"收录",L"录入",L"记入",L"写入",L"写进",L"写到",
        L"保存",L"存入",L"存到",L"加入",L"添加",L"新增",L"增加",L"加"
    };
    static const wchar_t *const qualifiers[]={L"下面这条",L"以下这条",L"一条",L"一项",L"到",L"进",L"至"};
    const wchar_t *p=verb;
    if(!modeling_part_take_any(&p,verbs,_countof(verbs)))return 0;
    p=file_write_rule_space(p);
    if(modeling_part_take_any(&p,qualifiers,_countof(qualifiers)))p=file_write_rule_space(p);
    if(!file_write_rule_target(&p))return 0;
    if(p==end)return 1;
    if(p<end&&(*p==L':'||*p==L'：')&&!intent->literal){
        const wchar_t *body=file_write_rule_space(p+1);
        if(!*body)return 0;
        intent->literal=body;
        intent->length=wcslen(body);
        return 1;
    }
    return 0;
}

static int file_write_rule_command(const wchar_t *p,const wchar_t *end,FileWriteRuleIntent *intent){
    if(p>=end)return 0;
    if(!assembly_take(&p,L"把")&&!assembly_take(&p,L"将"))return file_write_rule_write(p,end,intent);
    p=file_write_rule_space(p);
    wchar_t close=file_write_rule_quote_end(*p);
    if(close){
        const wchar_t *quoted_end=wcschr(p+1,close);
        if(!quoted_end||quoted_end>=end||quoted_end==p+1)return 0;
        intent->literal=p+1;
        intent->length=(size_t)(quoted_end-p-1);
        return file_write_rule_write(file_write_rule_space(quoted_end+1),end,intent);
    }
    static const wchar_t *const negated[]={L"不要",L"不用",L"不必",L"不许",L"不能写",L"不得写",L"不写",L"不存",L"别写",L"别保存",L"勿写",L"禁止写"};
    const wchar_t *object=p;
    for(;p<end;){
        const wchar_t *check=p;
        if(modeling_part_take_any(&check,negated,_countof(negated)))return 0;
        if(p>object&&file_write_rule_write(p,end,intent))return 1;
        const wchar_t *next=file_write_rule_markup_end(p);
        p=next==p?p+1:next;
    }
    return 0;
}

static int file_write_rule_clause(const wchar_t *text,const wchar_t *end,FileWriteRuleIntent *intent){
    static const wchar_t *const denies[]={
        L"只分析",L"仅分析",L"只解释",L"只说明",L"只回答",L"只查看",L"仅查看",L"只讨论",L"仅讨论",L"只看方案",
        L"别写",L"不要写",L"不用写",L"不许写",L"不写",L"不要保存",L"别保存",L"不用保存",L"不保存",
        L"不要落盘",L"别落盘",L"不落盘",L"不要修改规则",L"不要改规则",L"别改建模规则"
    };
    static const wchar_t *const examples[]={
        L"例如",L"比如",L"举例",L"举个例子",L"示例",L"假设",L"如果",L"要是",L"如何",L"怎么",L"能否",
        L"解释",L"演示",L"复述",L"翻译",L"引用",L"原文",L"附件里",L"截图里",L"他说",L"她说",L"我说的是"
    };
    int found=0,example=0;
    for(const wchar_t *p=text;p<end;){
        const wchar_t *tail=file_write_rule_boundary(p,L"，,。！？!?；;\r\n");
        const wchar_t *action=file_write_rule_action(p);
        FileWriteRuleIntent candidate={0};
        if(action<tail&&modeling_part_take_any(&action,denies,_countof(denies)))return -1;
        if(action<tail&&modeling_part_take_any(&action,examples,_countof(examples)))example=1;
        if(!example&&*end!=L'?'&&*end!=L'？'&&file_write_rule_command(action,tail,&candidate)){
            if(found)return -1;
            *intent=candidate;
            found=1;
            if(candidate.literal&&candidate.literal+candidate.length>tail)break;
        }
        p=tail<end?tail+1:end;
    }
    return found;
}


static int file_write_rule_policy(const wchar_t *text){
    static const wchar_t *const starts[]={L"遇到",L"出现",L"以后",L"今后",L"每次",L"每条",L"每个",L"本公司",L"所有"};
    const wchar_t *start=file_write_rule_space(text);
    return modeling_part_take_any(&start,starts,_countof(starts));
}

static int file_write_rule_find(const wchar_t *request,FileWriteRuleIntent *intent){
    if(!request||wcslen(request)>FILE_WRITE_RULE_REQUEST_LIMIT)return 0;
    *intent=(FileWriteRuleIntent){0};
    const wchar_t *previous=NULL,*previous_end=NULL;
    int found=0,earlier=0;
    for(const wchar_t *p=request;*p;){
        const wchar_t *end=file_write_rule_boundary(p,L"。！？!?；;\r\n");
        const wchar_t *start=model_request_action_start(p);
        FileWriteRuleIntent part={0};
        if(start<end){
            int command=file_write_rule_clause(start,end,&part);
            if(command<0)return 0;
            if(command){
                if(found)return 0;
                found=1;
                *intent=part;
                intent->local=!earlier||(earlier==1&&file_write_rule_policy(previous));
                if(!intent->literal&&previous&&file_write_rule_policy(previous)&&
                   *previous_end!=L'?'&&*previous_end!=L'？'){
                    intent->literal=previous;
                    intent->length=(size_t)(previous_end-previous);
                }
            }else if(found&&intent->literal&&file_write_rule_policy(start)&&
                     !intent->followup&&*end!=L'?'&&*end!=L'？'&&
                     (size_t)(end-start)>=wcslen(L"建模规则")){
                const wchar_t *reference=wcsstr(start,L"建模规则");
                if(reference&&reference<end&&start>intent->literal+intent->length){
                    intent->followup=start;
                    intent->followup_length=(size_t)(end-start);
                }else if(start>=intent->literal+intent->length)intent->local=0;
            }else if(found&&(!intent->literal||start>=intent->literal+intent->length))intent->local=0;
            earlier++;
            previous=start;
            previous_end=end;
        }
        p=*end?end+1:end;
    }
    return found;
}

static int file_write_rule_requested(const wchar_t *request){
    FileWriteRuleIntent intent;
    return file_write_rule_find(request,&intent);
}

static int file_write_rule_content(FileWriteRuleIntent *intent,const wchar_t *proposed,wchar_t *text,size_t capacity){
    if(!intent->literal&&!proposed)return 0;
    const wchar_t *source=intent->literal?intent->literal:file_write_rule_space(proposed);
    size_t length=intent->literal?intent->length:wcslen(source);
    while(length&&iswspace(source[length-1]))length--;
    if(!length||length>FILE_WRITE_RULE_LIMIT||wcsncpy_s(text,capacity,source,length))return 0;
    if(intent->followup_length){
        if(length+3+intent->followup_length>=capacity)return 0;
        if(wcscat_s(text,capacity,L"。\n")||
           wcsncat_s(text,capacity,intent->followup,intent->followup_length))return 0;
    }
    return 1;
}

static const wchar_t *file_write_rule_stored_body(const wchar_t *record){
    const wchar_t *body=wcsstr(record,L"\n\n");
    if(!body||wcsncmp(body+2,FILE_WRITE_RULE_TAG,wcslen(FILE_WRITE_RULE_TAG)))return NULL;
    return body+2+wcslen(FILE_WRITE_RULE_TAG);
}

static int file_write_rule_apply(const wchar_t *request,const wchar_t *content,wchar_t *reply,size_t capacity){
    FileWriteRuleIntent intent;
    wchar_t body[FILE_WRITE_RULE_LIMIT+1],tagged[FILE_WRITE_RULE_LIMIT+_countof(FILE_WRITE_RULE_TAG)];
    wchar_t path[MAX_PATH];
    if(!file_write_rule_find(request,&intent)){
        wcscpy_s(reply,capacity,L"本条消息没有明确要求写入 MWEAI 建模规则。");
        return PRO_TK_BAD_INPUTS;
    }
    if(!file_write_rule_content(&intent,content,body,_countof(body))){
        wcscpy_s(reply,capacity,L"请明确要写入的规则内容，规则正文最多 1300 字。");
        return PRO_TK_BAD_INPUTS;
    }
    ModelingRuleChange structured;
    if(modeling_rule_parse_clauses(body,&structured)){
        wcscpy_s(reply,capacity,L"这条内容是默认流程或图号版本。请使用「写入建模规则：默认流程 公司」或「写入建模规则：默认版本 V07」。");
        return PRO_TK_BAD_INPUTS;
    }
    wcscpy_s(tagged,_countof(tagged),FILE_WRITE_RULE_TAG);
    wcscat_s(tagged,_countof(tagged),body);
    wchar_t *record=calloc(EXPERIENCE_TEXT_MAX,sizeof(wchar_t));
    if(!record)return PRO_TK_OUT_OF_MEMORY;
    const wchar_t *actual=NULL;
    wchar_t paths[12][MAX_PATH],directory[MAX_PATH];
    unsigned total;
    int count=experience_recent(paths,directory,&total);
    for(int i=0;i<count;i++){
        if(experience_read(paths[i],record,EXPERIENCE_TEXT_MAX)&&(actual=file_write_rule_stored_body(record))&&
           !wcscmp(actual,body)){
            swprintf_s(reply,capacity,L"已核对同一条建模规则。文件 %ls",paths[i]);
            free(record);
            return PRO_TK_NO_ERROR;
        }
    }
    int saved=experience_save(tagged,path);
    int checked=saved&&experience_read(path,record,EXPERIENCE_TEXT_MAX)&&
        (actual=file_write_rule_stored_body(record))&&!wcscmp(actual,body);
    free(record);
    if(checked)swprintf_s(reply,capacity,L"建模规则已保存并读回核对。文件 %ls",path);
    else if(saved)swprintf_s(reply,capacity,L"规则已写到 %ls，未通过最后的读回核对。",path);
    else wcscpy_s(reply,capacity,L"建模规则写入失败，请核对 MWEAI 数据目录权限。");
    return checked?PRO_TK_NO_ERROR:PRO_TK_GENERAL_ERROR;
}

static int file_write_rule_chat(const wchar_t *text){
    FileWriteRuleIntent intent;
    wchar_t body[FILE_WRITE_RULE_LIMIT+1];
    ModelingRuleChange structured;
    if(!file_write_rule_find(text,&intent)||!intent.local||!intent.literal||
       !file_write_rule_content(&intent,NULL,body,_countof(body)))return 0;
    if(modeling_rule_parse_clauses(body,&structured)||modeling_rule_company_sentence(body))return 0;
    if(native_busy||smart_active||ai_process){append(L"请等待当前任务完成后再写入规则。");return 1;}
    wchar_t reply[900]=L"";
    file_write_rule_apply(text,NULL,reply,_countof(reply));
    experience_managing=1;
    append_card(L"你",text);
    save_turn(L"用户",text);
    native_prompt_set(L"");
    if(!reply[0])wcscpy_s(reply,_countof(reply),L"建模规则处理失败，无法生成保存回执。");
    append_card(L"AI",reply);
    save_turn(L"助手",reply);
    experience_managing=0;
    return 1;
}

static void file_write_rule_prompt(FILE *prompt){
    wchar_t paths[12][MAX_PATH],directory[MAX_PATH];
    unsigned total;
    int count=experience_recent(paths,directory,&total);
    if(!count)return;
    wchar_t *record=calloc(EXPERIENCE_TEXT_MAX,sizeof(wchar_t));
    if(!record)return;
    int printed=0;
    for(int i=0;i<count;i++){
        if(!experience_read(paths[i],record,EXPERIENCE_TEXT_MAX))continue;
        const wchar_t *body=file_write_rule_stored_body(record);
        if(!body)continue;
        if(!printed++)fputws(L"\n<BWTL附加建模规则>\n以下是本机用户保存的建模偏好，用于本轮规划。授权以最新用户消息为准，仍须核对当前模型及原生工具的写入边界。\n",prompt);
        fputws(L"规则 ",prompt);
        for(size_t n=0;body[n]&&n<FILE_WRITE_RULE_LIMIT;n++){
            wchar_t c=body[n];
            if(c==L'\r')continue;
            if(c==L'\n')fputws(L"\n  ",prompt);
            else fputwc(c==L'<'?L'＜':c==L'>'?L'＞':c,prompt);
        }
        fputwc(L'\n',prompt);
    }
    if(printed)fputws(L"</BWTL附加建模规则>\n",prompt);
    free(record);
}
