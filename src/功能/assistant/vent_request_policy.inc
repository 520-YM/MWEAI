/* Direct instructions with a named hole layout can skip the intent-only
   Codex turn. Native model, reference and write checks still run. */
static int smart_direct_vent_request(const wchar_t *text){
    if(wcspbrk(text,L"?？")||smart_readonly_override(text)||model_request_explicit_new(text))return 0;
    const wchar_t *p=model_request_action_start(text);
    const wchar_t *targets[]={L"在当前钣金零件",L"在当前钣金件",L"在当前模型",L"在当前零件",L"在这块板"};
    for(size_t i=0;i<_countof(targets);i++){
        size_t length=wcslen(targets[i]);
        if(wcsncmp(p,targets[i],length))continue;
        p+=length;
        if(*p&&wcschr(L"上内里",*p))p++;
        p=model_request_action_start(p);
        break;
    }
    const wchar_t *discussion[]={L"开发",L"打开",L"打算",L"报告",L"会失败",L"能不能",L"不能",
        L"什么命令",L"用哪个",L"参数格式",L"我自己",L"只告诉",L"先别"};
    if(smart_has_text(p,discussion,_countof(discussion)))return 0;
    if(!wcsstr(p,L"VENT_ROUND")&&!(wcsstr(p,L"行")&&wcsstr(p,L"列")))return 0;
    const wchar_t *commands[]={L"打",L"开",L"加工",L"布置"};
    const wchar_t *layouts[]={L"VENT_ROUND",L"圆孔网格",L"圆孔阵列",L"圆孔散热区"};
    for(size_t i=0;i<_countof(commands);i++){
        if(!wcsncmp(p,commands[i],wcslen(commands[i])))return smart_has_text(p,layouts,_countof(layouts));
    }
    return 0;
}
