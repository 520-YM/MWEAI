typedef enum {
    REQUEST_NATIVE,
    REQUEST_PLAN
} RequestRoute;

typedef struct {
    RequestRoute route;
    int readonly, execution_requested, continuation, compound, ambiguous;
} RequestAssessment;

typedef enum {
    REQUEST_DETAIL,
    REQUEST_READ,
    REQUEST_ACTION
} RequestClause;

static int request_starts(const wchar_t *text,const wchar_t *const *words,size_t count){
    for(size_t i=0;i<count;i++)if(!wcsncmp(text,words[i],wcslen(words[i])))return 1;
    return 0;
}

static int request_question(const wchar_t *text){
    static const wchar_t *const questions[]={L"为什么",L"怎么",L"如何",L"有没有",L"是不是",
        L"是否",L"什么",L"多少",L"有几个",L"有几项",L"第几"};
    return smart_has_text(text,questions,_countof(questions));
}

/* One clause may name the operation's object before its verb. The exact
   native command parser still has to accept the entire original request. */
static RequestClause request_clause(const wchar_t *text){
    static const wchar_t *const lead[]={L"想办法",L"麻烦你",L"麻烦",L"请你",L"请",L"你",L"那就",L"就",L"帮我",L"给我",L"替我",
        L"能不能帮我",L"能否帮我",L"可不可以帮我",L"可以帮我",L"能帮我",
        L"能不能给我",L"能否给我",L"可不可以给我",L"可以给我",L"能给我",
        L"可以先",L"先",L"接着",L"然后",L"接下来",L"继续",L"再",L"并",L"同时",L"另外",L"现在",L"直接",L"顺便"};
    static const wchar_t *const read[]={L"读取",L"查看",L"看看",L"查一下",L"核对",L"量一下",L"测量",L"检查",L"确认",
        L"找一下",L"搜索",L"列出",L"告诉我",L"说明",L"解释",L"分析",L"讨论",L"评估",L"给方案",L"介绍",L"建议",L"汇报"};
    static const wchar_t *const action[]={L"新建",L"另建",L"创建",L"建立",L"建模",L"生成",L"增加",L"加",L"装",L"插入",
        L"放入",L"紧固",L"定位",L"安装",L"画",L"绘制",L"做",L"建",L"重建",L"仿照",L"打开",L"调出",L"返回",
        L"切换",L"回到",L"放回",L"显示",L"改",L"更改",L"调整",L"换",L"重命名",L"复制",L"移动",L"写入",L"记录",L"记住",
        L"保存",L"存到",L"另存",L"导出",L"交付",L"删除",L"移除",L"补",L"修",L"动手",L"打",L"钻",L"开孔",L"开槽",
        L"加工",L"倒角",L"倒圆",L"拉伸",L"切除",L"旋转",L"阵列",L"镜像",L"折弯",L"抽壳",L"拔模",L"执行",L"完成"};
    static const wchar_t *const object_lead[]={L"把",L"将",L"在",L"对",L"按",L"照",L"用",L"以",L"沿"};
    static const wchar_t *const object_action[]={L"再做",L"做完",L"做好",L"做出来",L"画好",L"画出来",L"装上",L"装好",
        L"装到",L"装进去",L"装配到",L"装入",L"放入",L"安装",L"定位",L"打了",L"打好",L"改成",L"改为",L"改到",L"改好",L"修改",
        L"打",L"钻",L"开孔",L"加孔",L"切口",L"切除",L"添加",L"增加",L"补上",
        L"保存",L"另存",L"写入",L"创建",L"新建",L"建立",L"绘制",L"画",L"倒角",L"倒圆",L"拉伸",L"执行",L"补全",L"修好",
        L"删除",L"替换",L"重命名",L"改名",L"回到",L"打开",L"重生成",L"导出",L"阵列",L"镜像",L"折弯"};
    static const wchar_t *const capability[]={L"能不能",L"能否",L"可不可以",L"可以吗",L"能不能够"};
    for(;;){
        while(iswspace(*text))text++;
        size_t i;
        for(i=0;i<_countof(lead);i++){
            size_t n=wcslen(lead[i]);
            if(!wcsncmp(text,lead[i],n)){text+=n;break;}
        }
        if(i==_countof(lead))break;
    }
    if(request_question(text)||request_starts(text,capability,_countof(capability))||
       request_starts(text,read,_countof(read)))return REQUEST_READ;
    if(request_starts(text,action,_countof(action)))return REQUEST_ACTION;
    if(request_starts(text,object_lead,_countof(object_lead))){
        if(smart_has_text(text,read,_countof(read)))return REQUEST_READ;
        if(smart_has_text(text,object_action,_countof(object_action)))return REQUEST_ACTION;
    }
    return REQUEST_DETAIL;
}

/* Mask quoted descriptions and file paths so they cannot introduce another
   action, a negation, or a condition into the outer user instruction. */
static int request_plain_text(const wchar_t *source,wchar_t *plain){
    const wchar_t *opens=L"“‘「『\"《",*closes=L"”’」』\"》";
    wchar_t close=0;
    for(;*source;source++,plain++){
        const wchar_t *quote=wcschr(opens,*source);
        if(close){
            if(*source==close)close=0;
            *plain=L' ';
        }else if(quote){
            close=closes[quote-opens];
            *plain=L' ';
        }else *plain=*source;
    }
    *plain=0;
    return close==0;
}

static int request_extra_action(wchar_t *clause){
    static const wchar_t *const sequence[]={L"然后",L"接着",L"接下来",L"并且",L"并",L"再",L"同时",L"顺便",L"后再"};
    for(wchar_t *p=clause+1;*p;p++){
        for(size_t i=0;i<_countof(sequence);i++){
            size_t n=wcslen(sequence[i]);
            if(!wcsncmp(p,sequence[i],n)&&request_clause(p+n)!=REQUEST_DETAIL){
                /* The continuation is scanned independently, including its negations. */
                *p=L'\n';
                for(size_t j=1;j<n;j++)p[j]=L' ';
                return 1;
            }
        }
    }
    return 0;
}

static RequestAssessment request_assess(const wchar_t *original,int attachments){
    static const wchar_t *const condition[]={L"如果",L"假如",L"若是",L"要是",L"一旦",L"不行就",L"失败就",L"失败后",L"否则"};
    static const wchar_t *const continuation[]={L"刚才",L"刚刚",L"上次",L"上一轮",L"上一步",L"上一个",L"继续",L"接着",L"改成正确"};
    static const wchar_t *const limits[]={L"先别改",L"先别画",L"只回答",L"只需回答",L"只用回答",L"只给建议",
        L"仅给建议",L"不要实际操作",L"不用实际操作"};
    RequestAssessment assessment={0};
    wchar_t plain[6001];
    if(wcslen(original)>=_countof(plain)){
        assessment.readonly=smart_explicit_readonly(original);
        assessment.ambiguous=1;
        assessment.route=REQUEST_PLAN;
        return assessment;
    }
    const int complete_quotes=request_plain_text(original,plain);
    assessment.continuation=smart_has_text(plain,continuation,_countof(continuation));
    assessment.readonly=smart_explicit_readonly(plain);
    for(size_t i=0;i<_countof(limits);i++){
        const wchar_t *p=wcsstr(plain,limits[i]);
        if(p&&!smart_negated_limit(plain,p)&&
            (p-plain<2||wcsncmp(p-2,L"不要",2)))assessment.readonly=1;
    }
    assessment.ambiguous=!complete_quotes||smart_has_text(plain,condition,_countof(condition));
    const int explicit_limit=assessment.readonly;
    int reads=0,actions=0;
    for(wchar_t *p=plain;*p;){
        wchar_t *end=p+wcscspn(p,L"，,。；;！？?!\r\n");
        wchar_t separator=*end;
        *end=0;
        if(p<end&&request_extra_action(p)){
            *end=separator;
            end=p+wcscspn(p,L"\n");
            separator=*end;
            *end=0;
        }
        RequestClause clause=request_clause(p);
        if(clause==REQUEST_ACTION)actions++;
        if(clause==REQUEST_READ)reads++;
        *end=separator;
        p=separator?end+1:end;
    }
    if(!actions&&(smart_followup_execution_request(plain)||smart_resume_request(plain)))actions=1;
    assessment.compound=actions+reads>1&&actions>0;
    assessment.readonly=assessment.readonly||(!actions&&reads>0);
    assessment.execution_requested=!assessment.readonly&&actions>0;
    assessment.route=(attachments||explicit_limit||assessment.compound||assessment.ambiguous)?REQUEST_PLAN:REQUEST_NATIVE;
    return assessment;
}

static void request_assessment_prompt(FILE *out,const RequestAssessment *assessment){
    if(!assessment)return;
    fputws(L"\n<请求核对>\n先结合本轮完整请求、对话中已确认的决定和最新 Creo 回读核对目标、参照、限制及交付结果。",out);
    if(assessment->readonly)fputws(L"本轮仅作咨询、查看或核对。",out);
    if(assessment->continuation)fputws(L"本轮有承接线索，核对上一任务实际绑定的模型和已完成步骤。",out);
    if(assessment->compound)fputws(L"本轮有组合要求，按依赖顺序处理并核对各项结果。",out);
    if(assessment->ambiguous)fputws(L"先用现有信息验证条件和约束，再选择适用的做法。",out);
    fputws(L"遇到失败先读回实际状态，保留已验证进展，选择有依据的替代方法。仅凭已验证的回执报告成果。\n</请求核对>\n",out);
}
