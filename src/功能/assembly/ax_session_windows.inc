/* List objects in this Creo session and activate exact live model handles. */
typedef struct {int id;ProMdl model;} AxSessionWindow;
typedef struct {
    AxSessionWindow *row;
    size_t count,capacity;
} AxSessionWindowList;

static ProError ax_session_window_visit(int id,ProAppData data){
    AxSessionWindowList *windows=data;
    ProMdl model=NULL;
    ProError error=ProWindowMdlGet(id,&model);
    if(error==PRO_TK_E_NOT_FOUND||error==PRO_TK_BAD_INPUTS||(!error&&!model))return PRO_TK_NO_ERROR;
    if(error)return error;
    if(windows->count==windows->capacity){
        size_t capacity=windows->capacity?2*windows->capacity:16;
        AxSessionWindow *next=realloc(windows->row,capacity*sizeof(*next));
        if(!next)return PRO_TK_OUT_OF_MEMORY;
        windows->row=next;
        windows->capacity=capacity;
    }
    windows->row[windows->count++]=(AxSessionWindow){id,model};
    return PRO_TK_NO_ERROR;
}

static const wchar_t *ax_session_model_suffix(ProMdlType type){
    return type==PRO_MDL_ASSEMBLY?L"ASM":L"PRT";
}

static ProError ax_session_inventory(wchar_t **text){
    const ProMdlType types[]={PRO_MDL_ASSEMBLY,PRO_MDL_PART};
    ProMdl *models[2]={0},active=NULL;
    int count[2]={0},current=PRO_VALUE_UNUSED;
    AxSessionWindowList windows={0};
    *text=NULL;
    ProError error=ProWindowsVisit(ax_session_window_visit,&windows);
    if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    if(!error)error=ProWindowCurrentGet(&current);
    if(!error)error=ProMdlCurrentGet(&active);
    if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    for(int k=0;!error&&k<_countof(types);k++){
        error=ProSessionMdlList(types[k],&models[k],&count[k]);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    }
    if(!error){
        const size_t lines=(size_t)count[0]+(size_t)count[1]+windows.count;
        const size_t size=512+lines*(sizeof(ProMdlName)/sizeof(wchar_t)+92);
        *text=calloc(size,sizeof(wchar_t));
        if(!*text)error=PRO_TK_OUT_OF_MEMORY;
        else{
            PartText out={*text,size,0};
            part_text_add(&out,L"Creo 当前会话：%d 个装配，%d 个零件。\n",count[0],count[1]);
            for(int k=0;!error&&k<_countof(types);k++)for(int i=0;!error&&i<count[k];i++){
                ProMdlName name;
                ProBoolean dirty=PRO_B_FALSE;
                ProMdl model=models[k][i];
                error=ProMdlMdlnameGet(model,name);
                if(error)break;
                const wchar_t *state=ProMdlModificationVerify(model,&dirty)?L"保存状态未知":
                    dirty?L"有未保存改动":L"无未保存改动";
                int shown=0;
                for(size_t j=0;j<windows.count;j++)if(windows.row[j].model==model){
                    const int id=windows.row[j].id;
                    const wchar_t *status=active==model&&id==current?L"活动":L"已打开";
                    part_text_add(&out,L"%ls.%ls | 窗口 ID %d | %ls | %ls\n",name,
                        ax_session_model_suffix(types[k]),id,status,state);
                    shown=1;
                }
                if(!shown)part_text_add(&out,L"%ls.%ls | 已加载，无独立窗口 | %ls\n",name,
                    ax_session_model_suffix(types[k]),state);
            }
            if(out.truncated)error=PRO_TK_OUT_OF_RANGE;
        }
    }
    for(int k=0;k<_countof(types);k++)if(models[k])ProArrayFree((ProArray *)&models[k]);
    free(windows.row);
    if(error){free(*text);*text=NULL;}
    return error;
}

typedef struct {
    ProMdlName name;
    ProMdlType type;
    int window_id,has_window_id;
} AxSessionSelector;

typedef struct {
    ProMdl model;
    ProMdlName name;
    ProMdlType type;
    ProWVerstamp stamp;
    ProBoolean modified;
    int window;
} AxSessionIdentity;

typedef struct {
    AxSessionIdentity source,target;
    ProPath directory;
} AxSessionActivation;

static AxSessionActivation ax_session_pending;

static void ax_session_activation_clear(AxSessionActivation *activation){
    if(activation->source.stamp)ProVerstampFree(&activation->source.stamp);
    if(activation->target.stamp)ProVerstampFree(&activation->target.stamp);
    memset(activation,0,sizeof(*activation));
}

static int ax_session_activation_waiting(void){
    return ax_session_pending.target.model!=NULL;
}

static ProError ax_session_identity_take(ProMdl model,AxSessionIdentity *identity){
    identity->model=model;
    ProError error=ProMdlMdlnameGet(model,identity->name);
    if(!error)error=ProMdlTypeGet(model,&identity->type);
    if(!error)error=ProMdlVerstampGet(model,&identity->stamp);
    if(!error)error=ProMdlModificationVerify(model,&identity->modified);
    return error;
}

static ProError ax_session_identity_verify(const AxSessionIdentity *identity,int current){
    ProMdl owner=NULL;
    int window=PRO_VALUE_UNUSED;
    if(current){
        ProError error=ProMdlCurrentGet(&owner);
        if(!identity->model){
            if(owner||(error&&error!=PRO_TK_E_NOT_FOUND&&error!=PRO_TK_BAD_CONTEXT))return PRO_TK_BAD_CONTEXT;
            return !ProWindowCurrentGet(&window)&&window==identity->window?PRO_TK_NO_ERROR:PRO_TK_BAD_CONTEXT;
        }
        if(error||owner!=identity->model||ProWindowCurrentGet(&window)||window!=identity->window)
            return PRO_TK_BAD_CONTEXT;
    }
    if(!identity->model)return PRO_TK_NO_ERROR;
    ProMdlName name;
    ProMdlType type;
    ProBoolean modified;
    ProWVerstamp stamp=NULL;
    ProError error=ProWindowMdlGet(identity->window,&owner);
    if(!error&&owner!=identity->model)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlMdlnameGet(owner,name);
    if(!error)error=ProMdlTypeGet(owner,&type);
    if(!error)error=ProMdlModificationVerify(owner,&modified);
    if(!error)error=ProMdlVerstampGet(owner,&stamp);
    if(!error&&(type!=identity->type||_wcsicmp(name,identity->name)||
       modified!=identity->modified||!ProVerstampEqual(identity->stamp,stamp)))error=PRO_TK_BAD_CONTEXT;
    if(stamp)ProVerstampFree(&stamp);
    return error;
}

static ProError ax_session_resolve(AxSessionSelector *selector,AxSessionIdentity *target,PartText *out){
    ProMdl model=NULL;
    ProError error=PRO_TK_NO_ERROR;
    if(selector->has_window_id){
        error=ProWindowMdlGet(selector->window_id,&model);
        if(!error&&!model)error=PRO_TK_E_NOT_FOUND;
        if(!error)target->window=selector->window_id;
    }else if(selector->type==PRO_MDL_ASSEMBLY||selector->type==PRO_MDL_PART){
        error=ProMdlnameInit(selector->name,selector->type==PRO_MDL_ASSEMBLY?
            PRO_MDLFILE_ASSEMBLY:PRO_MDLFILE_PART,&model);
    }else{
        ProMdl part=NULL,assembly=NULL;
        ProError a=ProMdlnameInit(selector->name,PRO_MDLFILE_ASSEMBLY,&assembly);
        ProError p=ProMdlnameInit(selector->name,PRO_MDLFILE_PART,&part);
        if(a&&a!=PRO_TK_E_NOT_FOUND)return a;
        if(p&&p!=PRO_TK_E_NOT_FOUND)return p;
        if(part&&assembly){
            part_text_add(out,L"会话里同时有 %ls.ASM 和 %ls.PRT。请带上后缀，或用窗口 ID。\n",
                selector->name,selector->name);
            return PRO_TK_E_FOUND;
        }
        model=assembly?assembly:part;
    }
    if(error==PRO_TK_E_NOT_FOUND||(!error&&!model)){
        if(selector->has_window_id)part_text_add(out,L"窗口 ID %d 没有可激活的会话模型。\n",selector->window_id);
        else if(selector->type==PRO_MDL_UNUSED)part_text_add(out,L"当前 Creo 会话未加载 %ls。\n",selector->name);
        else part_text_add(out,L"当前 Creo 会话未加载 %ls.%ls。\n",selector->name,ax_session_model_suffix(selector->type));
        return PRO_TK_E_NOT_FOUND;
    }
    if(!error)error=ax_session_identity_take(model,target);
    if(!error&&((target->type!=PRO_MDL_ASSEMBLY&&target->type!=PRO_MDL_PART)||
       (selector->type!=PRO_MDL_UNUSED&&target->type!=selector->type)||
       (selector->name[0]&&_wcsicmp(selector->name,target->name))))error=PRO_TK_BAD_CONTEXT;
    return error;
}

/* An unrelated part has no parent to infer. The sole displayed assembly is
   a valid destination for an unqualified assembly command. */
static ProError ax_session_unique_open_assembly(AxSessionSelector *selector,PartText *out){
    AxSessionWindowList windows={0};
    ProError error=ProWindowsVisit(ax_session_window_visit,&windows);
    if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
    *selector=(AxSessionSelector){.type=PRO_MDL_ASSEMBLY};
    int count=0;
    for(size_t i=0;!error&&i<windows.count;i++){
        const AxSessionWindow *window=&windows.row[i];
        ProMdlType type;
        error=ProMdlTypeGet(window->model,&type);
        if(error||type!=PRO_MDL_ASSEMBLY)continue;
        int duplicate=0;
        for(size_t j=0;j<i;j++)if(windows.row[j].model==window->model){duplicate=1;break;}
        if(duplicate)continue;
        ProMdlName name;
        error=ProMdlMdlnameGet(window->model,name);
        if(error)break;
        if(++count==1){
            selector->has_window_id=1;
            selector->window_id=window->id;
            wcscpy_s(selector->name,_countof(selector->name),name);
        }else{
            if(count==2)part_text_add(out,L"有多个已打开的装配，当前零件没有找到父装配。请指定下面的名称或窗口 ID。\n%ls.ASM | 窗口 ID %d\n",
                selector->name,selector->window_id);
            part_text_add(out,L"%ls.ASM | 窗口 ID %d\n",name,window->id);
        }
    }
    free(windows.row);
    if(error)return error;
    if(count>1)return PRO_TK_E_FOUND;
    if(!count){
        part_text_add(out,L"没有已打开的装配窗口。装配已加载时，可以指定名称激活，或发送「读取会话模型与窗口清单」核对。\n");
        return PRO_TK_E_NOT_FOUND;
    }
    return PRO_TK_NO_ERROR;
}

static ProError ax_session_activate(AxSessionSelector *selector,PartText *out){
    if(!selector||!out||(!selector->name[0]&&!selector->has_window_id))return PRO_TK_BAD_INPUTS;
    if(ax_session_activation_waiting()||ax_return_pending.link.root||ax_open_pending.target)
        return PRO_TK_E_IN_USE;
    AxSessionActivation next={0};
    ProMdl before=NULL,owner=NULL;
    ProError error=ProDirectoryCurrentGet(next.directory);
    if(!error)error=ProWindowCurrentGet(&next.source.window);
    if(!error)error=ProMdlCurrentGet(&before);
    if(!before&&(error==PRO_TK_E_NOT_FOUND||error==PRO_TK_BAD_CONTEXT))error=PRO_TK_NO_ERROR;
    if(!error&&before)error=ax_session_identity_take(before,&next.source);
    if(!error)error=ax_session_identity_verify(&next.source,1);
    if(error){
        part_text_add(out,L"切换前未能核对原活动窗口、模型身份或版本（Creo %d）。\n",error);
        goto done;
    }
    error=ax_session_resolve(selector,&next.target,out);
    if(error)goto done;

    int create=0;
    if(!selector->has_window_id){
        error=ProMdlWindowGet(next.target.model,&next.target.window);
        create=error==PRO_TK_E_NOT_FOUND;
    }
    if(create)error=ProObjectwindowMdlnameCreate(next.target.name,
        next.target.type==PRO_MDL_ASSEMBLY?PRO_ASSEMBLY:PRO_PART,&next.target.window);
    if(!error&&before&&before!=next.target.model&&next.target.window==next.source.window)
        error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProWindowCurrentSet(next.target.window);
    if(!error&&create)error=ProMdlDisplay(next.target.model);
    if(!error)error=ProWindowMdlGet(next.target.window,&owner);
    if(!error&&owner!=next.target.model)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProWindowActivate(next.target.window);
    if(error){
        if(next.source.window!=PRO_VALUE_UNUSED)ProWindowCurrentSet(next.source.window);
        part_text_add(out,L"Creo 未能激活 %ls.%ls 的会话窗口（Creo %d）。\n",
            next.target.name,ax_session_model_suffix(next.target.type),error);
        goto done;
    }

    ax_session_pending=next;
    return PRO_TK_NO_ERROR;
done:
    ax_session_activation_clear(&next);
    return error;
}

static ProError ax_session_activate_readback(ProMdl *active,ProPath directory,PartText *out){
    *active=NULL;
    if(!ax_session_activation_waiting())return PRO_TK_E_NOT_FOUND;
    AxSessionActivation *pending=&ax_session_pending;
    ProError error=ProDirectoryCurrentGet(directory);
    if(!error&&_wcsicmp(pending->directory,directory))error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ax_session_identity_verify(&pending->target,1);
    if(!error)error=ax_session_identity_verify(&pending->source,0);
    if(error)part_text_add(out,L"未能核对目标活动窗口、原模型状态或工作目录（Creo %d）。已暂停后续操作。\n",error);
    else{
        *active=pending->target.model;
        part_text_add(out,L"已激活 %ls.%ls，窗口 ID %d。",pending->target.name,
            ax_session_model_suffix(pending->target.type),pending->target.window);
    }
    ax_session_activation_clear(pending);
    return error;
}
