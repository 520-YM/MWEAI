#include <ProLayer.h>
#include "../runtime_paths/storage_directory.inc"
#include "ax_interference_cache.inc"

typedef struct {
    ProMdlName drawing;
    ProLine cname;
} AxInterferenceIdentity;

static int ax_interference_row_visible(const AxInterferenceCache *cache,const AxInterferenceView *view,
                                        int index,int visible,int *unknown){
    if(unknown)*unknown=0;
    if(!ax_interference_in_view(cache,view,index))return 0;
    if(!visible)return 1;
    for(int end=0;end<2;end++){
        const AxOccurrence *item=&cache->scan->occurrences[cache->pairs[index].index[end]];
        ProMdl owner=cache->root;
        for(int i=0;i<item->depth;i++){
            ProAsmcomp feature;
            ProBoolean hidden=PRO_B_FALSE;
            if(ax_component_feature(owner,item->ids[i],&feature)){
                if(unknown)*unknown=1;
                break;
            }
            if(ProModelitemIsHidden(&feature,&hidden)&&unknown)*unknown=1;
            if(hidden)return 0;
            if(i+1<item->depth&&ProAsmcompMdlGet(&feature,&owner)){
                if(unknown)*unknown=1;
                break;
            }
        }
    }
    return 1;
}

#include "ax_interference_view.inc"

static void ax_interference_display_name(ProLine name){
    for(wchar_t *c=name;*c;c++)if(*c<L' ') *c=L' ';
}

static int ax_interference_csv_row(FILE *file,int index,const AxInterferenceIdentity row[2]){
    return fwprintf(file,L"%d,",index)>=0&&dv_csv_text(file,row[0].drawing)&&
        fputwc(L',',file)!=WEOF&&dv_csv_text(file,row[0].cname)&&
        fputwc(L',',file)!=WEOF&&dv_csv_text(file,row[1].drawing)&&
        fputwc(L',',file)!=WEOF&&dv_csv_text(file,row[1].cname)&&fputwc(L'\n',file)!=WEOF;
}

static ProError ax_interference_format(const AxInterferenceCache *cache,const AxInterferenceView *view,
                                        const wchar_t *folder,PartText *out,int reused){
    static const wchar_t *const fields[]={L"CNAME"};
    DeliveryOutput output={0};
    FILE *file=NULL;
    ProPath csv=L"";
    int reported=0,filtered=0,unknown=0,shown=0;
    ProError error=PRO_TK_NO_ERROR;
    if(folder){
        error=dv_stage_begin(folder,&output);
        if(error==PRO_TK_E_FOUND)part_text_add(out,L"报告目录 %ls 已存在，请指定新目录名。\n",folder);
        if(error)goto done;
        if(swprintf_s(csv,_countof(csv),L"%ls\\interference.csv",output.stage)<0){
            error=PRO_TK_BAD_INPUTS;
            goto done;
        }
        file=_wfopen(csv,L"wt, ccs=UTF-8");
        if(!file||fwprintf(file,L"序号,图号A,CNAME A,图号B,CNAME B\n")<0){
            error=PRO_TK_CANT_WRITE;
            goto done;
        }
    }
    part_text_add(out,L"%ls · %ls\n工作目录 %ls\n",cache->name,
        reused?L"复用已核对的总装精确实体干涉":L"当前装配精确实体干涉",cache->directory);
    if(view->item){
        ProLine path=L"";
        ax_path_label(view->item->ids,view->item->depth,path,_countof(path));
        part_text_add(out,L"仅列出涉及当前窗口组件 %ls（%ls）的干涉。\n",view->item->name,path);
    }
    part_text_add(out,L"序号\t图号A\tCNAME A\t图号B\tCNAME B\n");
    for(int i=0;i<cache->count;i++){
        if(!ax_interference_in_view(cache,view,i))continue;
        int unverified;
        if(!ax_interference_row_visible(cache,view,i,cache->visible,&unverified)){
            filtered++;
            continue;
        }
        unknown+=unverified;
        AxInterferenceIdentity row[2]={0};
        for(int end=0;end<2;end++){
            const AxOccurrence *item=&cache->scan->occurrences[cache->pairs[i].index[end]];
            error=ProMdlMdlnameGet(item->model,row[end].drawing);
            if(error)goto done;
            ax_parameter_text(item->model,fields,_countof(fields),row[end].cname);
        }
        reported++;
        if(file&&!ax_interference_csv_row(file,i+1,row)){
            error=PRO_TK_CANT_WRITE;
            goto done;
        }
        const size_t length=wcslen(row[0].drawing)+wcslen(row[0].cname)+
            wcslen(row[1].drawing)+wcslen(row[1].cname)+14;
        if(shown==reported-1&&shown<50&&wcslen(out->text)+length+600<out->size){
            ax_interference_display_name(row[0].cname);
            ax_interference_display_name(row[1].cname);
            part_text_add(out,L"%d\t%ls\t%ls\t%ls\t%ls\n",i+1,
                row[0].drawing,row[0].cname,row[1].drawing,row[1].cname);
            shown++;
        }
    }
    part_text_add(out,L"已检查 %d 个活动零件实例，Creo 检出 %d 对，列出 %d 对。\n",
        cache->scan->parts,cache->count,reported);
    if(cache->visible)part_text_add(out,L"按组件或上级组件的「隐藏」状态过滤 %d 对，无法核实隐藏状态的 %d 对仍列出。\n",
        filtered,unknown);
    if(shown<reported)part_text_add(out,L"聊天显示前 %d 对。完整表可从总装窗口用 EXPORT 导出。\n",shown);
    if(reported)part_text_add(out,L"可发送「干涉红框 行号」「下一处干涉」或「清除干涉红框」。\n");
done:
    if(file&&fclose(file)&&!error)error=PRO_TK_CANT_WRITE;
    if(output.owned){
        if(!error&&!dv_file_nonempty(csv))error=PRO_TK_CANT_WRITE;
        error=dv_stage_finish(&output,error,out);
        if(!error)part_text_add(out,L"已保存完整清单 %ls\\interference.csv\n",output.target);
    }
    return error;
}

static ProError ax_interference_directory(ProPath directory,PartText *out){
    ProError error=ProDirectoryCurrentGet(directory);
    if(error)return error;
    if(storage_directory_on_drive(directory,L'E'))return PRO_TK_NO_ERROR;
    part_text_add(out,L"请先将 Creo 工作目录设在 E 盘项目中。\n");
    return PRO_TK_BAD_CONTEXT;
}

static ProError ax_interference_report(ProMdl assembly,const PartCommand *cmd,PartText *out){
    const wchar_t *folder=cmd->count>=5?cmd->arg[cmd->count-1]:NULL;
    AxInterferenceCache result={0};
    AxInterferenceView view={0};
    ProPath directory;
    ProMdlType type;
    ProError error=ax_interference_directory(directory,out);
    if(!error)error=ProMdlTypeGet(assembly,&type);
    if(error)return error;
    if(type!=PRO_MDL_ASSEMBLY){
        part_text_add(out,L"请在总装窗口执行整机检查。打开组件后可以复用已有检查结果。\n");
        return PRO_TK_INVALID_TYPE;
    }
    result.root=assembly;
    error=ax_interference_view(&result,assembly,NULL,&view,out);
    if(error)return error;
    result.visible=cmd->count>=4&&!wcscmp(cmd->arg[3],L"VISIBLE");
    result.selected=-1;
    error=ax_interference_compute(assembly,directory,&result);
    if(error)part_text_add(out,L"Creo 未返回可核对的精确实体干涉及组件实例（Creo %d）。\n",error);
    else{
        const ProError captured=ax_interference_capture(&result);
        error=ax_interference_format(&result,&view,folder,out,0);
        if(!error&&!captured){
            ax_interference_close();
            ax_interference_cache=result;
            result=(AxInterferenceCache){0};
        }else if(!error){
            part_text_add(out,L"本次清单已计算，但组件版本或位姿快照未完成（Creo %d），暂不能复用或显示红框。\n",captured);
        }
    }
    ax_interference_result_free(&result);
    return error;
}

static ProError ax_interference_review(ProMdl model,const PartCommand *cmd,PartText *out){
    AxInterferenceCache *cache=&ax_interference_cache;
    if(!wcscmp(cmd->arg[2],L"CLEAR")){
        ProError error=ax_interference_clear_overlay();
        if(!error){
            cache->selected=-1;
            part_text_add(out,L"已清除 MWEAI 干涉红框。\n");
        }
        return error;
    }
    ProPath directory;
    ProError error=ax_interference_directory(directory,out);
    if(error)return error;
    error=ax_interference_validate(cache,directory);
    if(error){
        (void)ax_interference_clear_overlay();
        part_text_add(out,error==PRO_TK_E_NOT_FOUND?
            L"此进程尚无可复用的干涉清单。请在总装窗口先执行「查看整机干涉」。\n":
            L"原检查的模型、组件位置或工作目录已变化。请回总装窗口重新检查干涉。\n");
        return error;
    }
    const int has_selector=cmd->count>=5&&!wcscmp(cmd->arg[cmd->count-2],L"AT");
    const int reuse=!wcscmp(cmd->arg[2],L"REUSE");
    AxInterferenceView view={0};
    error=ax_interference_view(cache,model,has_selector?cmd->arg[cmd->count-1]:NULL,&view,out);
    if(error)return error;
    if(!reuse)return ax_interference_show(model,cache,&view,cmd->arg[3],out);
    if(cmd->count-(has_selector?2:0)==4)cache->visible=!wcscmp(cmd->arg[3],L"VISIBLE");
    error=ax_interference_clear_overlay();
    if(error)return error;
    cache->selected=-1;
    return ax_interference_format(cache,&view,NULL,out,1);
}
