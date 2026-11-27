#define DRAWING_NUMBER_BATCH_LIMIT 8
#define DRAWING_NUMBER_AUDIT_PAGE 16

typedef struct {
    ProMdl model;
    ProMdlType type;
    int first, occurrences, manufacture, serial, rename_attempted;
    wchar_t path[PART_TOKEN_MAX];
    ProMdlName original, target;
    ProLine label;
    unsigned char attributes_present[2];
    ProLine attributes[2];
    char stamp[240];
} DrawingNumberEntry;

typedef struct {
    LocalControlContext context;
    ProPath directory;
    wchar_t prefix[9], token[72];
    int count;
    DrawingNumberEntry entries[DRAWING_NUMBER_BATCH_LIMIT];
    AxScan *tree;
} DrawingNumberPreview;

typedef struct {ProMatrix pose; int constraints;} DrawingNumberPlacement;

static DrawingNumberPreview drawing_number_preview;
static unsigned int drawing_number_sequence;
static const wchar_t *drawing_number_keys[]={L"CNAME",L"CINDEX"};

static void drawing_number_clear(void){
    free(drawing_number_preview.tree);
    drawing_number_preview=(DrawingNumberPreview){0};
}

static int drawing_number_index(const DrawingNumberPreview *preview,ProMdl model){
    for(int i=0;i<preview->count;i++)if(preview->entries[i].model==model)return i;
    return -1;
}

static ProError drawing_number_attribute(ProMdl model,const wchar_t *name,DeliveryAttribute *attribute){
    ProParameter parameter;
    *attribute=(DeliveryAttribute){0};
    attribute->name=name;
    ProError error=ProMdlToModelitem(model,&attribute->owner);
    if(error)return error;
    error=dv_attribute_read(attribute,&parameter,&attribute->original);
    if(error==PRO_TK_E_NOT_FOUND)return PRO_TK_NO_ERROR;
    if(!error)attribute->existed=1;
    return error;
}

static ProError drawing_number_audit(ProMdl root,int offset,PartText *out){
    AxScan *tree=NULL;
    ProError error=ax_scan(root,0,&tree);
    if(error)return error;
    ProMdlName assembly;
    error=ProMdlMdlnameGet(root,assembly);
    if(!error)part_text_add(out,L"%ls.ASM 的组件图号与属性。按 Creo 特征树顺序，实例路径保留。\n",assembly);
    for(int i=offset;!error&&i<tree->count&&i-offset<DRAWING_NUMBER_AUDIT_PAGE;i++){
        const AxOccurrence *item=&tree->occurrences[i];
        wchar_t path[PART_TOKEN_MAX];
        if(!ax_path_label(item->ids,item->depth,path,_countof(path))){error=PRO_TK_BAD_CONTEXT;break;}
        if(!item->model){
            part_text_add(out,L"%ls %ls · %ls。\n",path,item->name,ax_state_label(item->state));
            continue;
        }
        DeliveryAttribute attrs[2];
        const wchar_t *values[2];
        for(int j=0;j<2;j++){
            ProError read=drawing_number_attribute(item->model,drawing_number_keys[j],&attrs[j]);
            values[j]=read?L"无法读取":!attrs[j].existed?L"未填写":
                attrs[j].original.type!=PRO_PARAM_STRING?L"非文字参数":attrs[j].original.value.s_val;
        }
        part_text_add(out,L"%ls %ls.%ls · CNAME=%ls · CINDEX=%ls。\n",path,item->name,
            item->type==PRO_MDL_ASSEMBLY?L"ASM":L"PRT",values[0],values[1]);
    }
    if(!error)part_text_add(out,L"%d 个实例，%d 个抑制，%d 个无法读取。%ls\n",tree->count,tree->suppressed,
        tree->unavailable,tree->limited?L"达到扫描上限。":L"");
    if(!error&&offset+DRAWING_NUMBER_AUDIT_PAGE<tree->count)
        part_text_add(out,L"下一页用 DRAWING_NUMBERS AUDIT %d。\n",offset+DRAWING_NUMBER_AUDIT_PAGE);
    free(tree);
    return error;
}

static ProError drawing_number_eligible(const DrawingNumberEntry *entry,const wchar_t *directory,PartText *out){
    ProBoolean restricted=PRO_B_FALSE,modifiable=PRO_B_FALSE;
    ProMdl generic=NULL;
    ProError error=ProMdlIsSkeleton(entry->model,&restricted);
    if(!error&&restricted)error=PRO_TK_BAD_CONTEXT;
    if(!error)error=ProMdlIsModifiable(entry->model,PRO_B_FALSE,&modifiable);
    if(!error&&!modifiable)error=PRO_TK_CANT_MODIFY;
    if(!error){
        error=ProFaminstanceGenericGet(entry->model,PRO_B_TRUE,&generic);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
        if(!error&&generic&&generic!=entry->model)error=PRO_TK_E_IN_USE;
    }
    if(!error)error=dv_no_external_dependencies(entry->model);
    if(!error)error=company_pair_unsaved(entry->model,directory,entry->original,
        entry->type==PRO_MDL_ASSEMBLY?L"asm":L"prt");
    ProPath origin;
    if(!error)error=ProMdlOriginGet(entry->model,origin);
    if(!error&&origin[0]){
        DWORD attributes=GetFileAttributesW(origin);
        if(attributes!=INVALID_FILE_ATTRIBUTES)error=PRO_TK_E_IN_USE;
        else{
            DWORD reason=GetLastError();
            if(reason!=ERROR_FILE_NOT_FOUND&&reason!=ERROR_PATH_NOT_FOUND)error=PRO_TK_CANT_OPEN;
        }
    }
    if(error)part_text_add(out,L"%ls %ls 未能通过写入预检。仅处理可编辑、未保存、无外部父参照的非骨架、非族表自制件（Creo %d）。\n",
        entry->path,entry->original,error);
    if(!error&&wcslen(entry->original)==14&&towupper(entry->original[0])==L'M'&&
       company_pair_digits(entry->original+1,2)&&company_pair_digits(entry->original+4,7)&&
       company_pair_revision(entry->original+11)){
        int occupied=0;
        ProMdl sibling=NULL;
        const int part=entry->type==PRO_MDL_PART;
        ProMdlName name;
        wcscpy_s(name,_countof(name),entry->original);
        error=ProMdlnameInit(name,part?PRO_MDL_ASSEMBLY:PRO_MDL_PART,&sibling);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
        if(!error)error=model_named_disk(directory,name,part?L"asm":L"prt",NULL,&occupied);
        if(!error&&(sibling||occupied)){
            part_text_add(out,L"%ls 还有同图号装配或零件，请用配套图号功能一起改名。\n",name);
            error=PRO_TK_E_IN_USE;
        }
    }
    return error;
}

static ProError drawing_number_selection(DrawingNumberPreview *preview,const PartCommand *command,PartText *out){
    ProError error=PRO_TK_NO_ERROR;
    for(int i=4;i<command->count;i+=3){
        const wchar_t *path=command->arg[i],*role=command->arg[i+1],*label=command->arg[i+2];
        const int manufacture=!_wcsicmp(role,L"MAKE");
        if((!manufacture&&(_wcsicmp(role,L"KEEP")||wcscmp(label,L"-")))||
           (manufacture&&(!label[0]||!wcscmp(label,L"-")||wcslen(label)>=PRO_LINE_SIZE)))return PRO_TK_BAD_INPUTS;
        AxComponent component;
        error=ax_component_resolve(preview->context.model,path,&component);
        if(error)return error;
        int index=drawing_number_index(preview,component.model);
        if(index>=0){
            const DrawingNumberEntry *previous=&preview->entries[index];
            if(previous->manufacture!=manufacture||(manufacture&&wcscmp(previous->label,label))){
                part_text_add(out,L"%ls 与 %ls 指向同一个模型，请为它使用同一种处理方式和名称。\n",path,previous->path);
                return PRO_TK_BAD_INPUTS;
            }
            continue;
        }
        DrawingNumberEntry *entry=&preview->entries[preview->count++];
        entry->model=component.model;
        entry->manufacture=manufacture;
        entry->first=-1;
        for(int j=0;j<preview->tree->count;j++)if(preview->tree->occurrences[j].model==entry->model){
            if(entry->first<0)entry->first=j;
            entry->occurrences++;
        }
        if(entry->first<0||entry->model==preview->context.model)return PRO_TK_BAD_CONTEXT;
        const AxOccurrence *first=&preview->tree->occurrences[entry->first];
        if(!ax_path_label(first->ids,first->depth,entry->path,_countof(entry->path)))return PRO_TK_BAD_CONTEXT;
        if((error=ProMdlTypeGet(entry->model,&entry->type)))return error;
        if(entry->type!=PRO_MDL_PART&&entry->type!=PRO_MDL_ASSEMBLY)return PRO_TK_BAD_CONTEXT;
        if((error=ProMdlMdlnameGet(entry->model,entry->original)))return error;
        if(manufacture)wcscpy_s(entry->label,_countof(entry->label),label);
    }
    return PRO_TK_NO_ERROR;
}

static int drawing_number_order(const void *left,const void *right){
    return ((const DrawingNumberEntry *)left)->first-((const DrawingNumberEntry *)right)->first;
}

static ProError drawing_number_plan(const LocalControlContext *context,const PartCommand *command,PartText *out){
    int serial=0,made=0,ready=1;
    const wchar_t *prefix=command->arg[1],*revision=command->arg[2];
    if(command->count<7||command->count>4+3*DRAWING_NUMBER_BATCH_LIMIT||(command->count-4)%3||
       wcslen(prefix)!=8||towupper(prefix[0])!=L'M'||!company_pair_digits(prefix+1,2)||
       towupper(prefix[3])<L'A'||towupper(prefix[3])>L'Z'||!company_pair_digits(prefix+4,4)||
       !company_pair_revision(revision)||!part_integer(command->arg[3],&serial)||serial<1||serial>999)
        return PRO_TK_BAD_INPUTS;
    DrawingNumberPreview *preview=&drawing_number_preview;
    drawing_number_clear();
    preview->context=*context;
    if(!or_real_path(context->directory,1,preview->directory)||!storage_directory_on_drive(preview->directory,L'E'))
        return PRO_TK_CANT_WRITE;
    for(int i=0;i<8;i++)preview->prefix[i]=towupper(prefix[i]);
    ProError error=ax_master(context->model);
    if(!error)error=ax_scan(context->model,1,&preview->tree);
    if(!error&&preview->tree->suppressed)error=PRO_TK_INCOMPLETE;
    if(!error)error=drawing_number_selection(preview,command,out);
    if(error)goto finish;
    qsort(preview->entries,preview->count,sizeof(preview->entries[0]),drawing_number_order);
    unsigned char used[1000]={0};
    error=company_pair_scan_names(preview->directory,preview->prefix,used);
    if(error)goto finish;
    for(int i=0;i<preview->count;i++){
        DrawingNumberEntry *entry=&preview->entries[i];
        if(!entry->manufacture){
            part_text_add(out,L"%ls %ls · 明确保留，%d 个实例原样。\n",entry->path,entry->original,entry->occurrences);
            continue;
        }
        while(serial<=999&&used[serial])serial++;
        if(serial>999){error=PRO_TK_OUT_OF_RANGE;goto finish;}
        entry->serial=serial++;
        swprintf_s(entry->target,_countof(entry->target),L"%ls%03dV%ls",preview->prefix,entry->serial,revision+1);
        if((error=ax_study_verstamp(entry->model,entry->stamp)))goto finish;
        part_text_add(out,L"%ls %ls → %ls · CNAME=%ls，CINDEX=%ls。统一用于同一模型的 %d 个实例。\n",
            entry->path,entry->original,entry->target,entry->label,entry->target,entry->occurrences);
        if(drawing_number_eligible(entry,preview->directory,out))ready=0;
        for(int j=0;j<2;j++){
            DeliveryAttribute attribute;
            error=drawing_number_attribute(entry->model,drawing_number_keys[j],&attribute);
            if(error)goto finish;
            entry->attributes_present[j]=(unsigned char)attribute.existed;
            if(attribute.existed&&attribute.original.type!=PRO_PARAM_STRING){
                part_text_add(out,L"%ls 的 %ls 不是文字参数。\n",entry->original,drawing_number_keys[j]);
                ready=0;
            }else if(attribute.existed){
                wcscpy_s(entry->attributes[j],_countof(entry->attributes[j]),attribute.original.value.s_val);
                part_text_add(out,L"  现有 %ls=%ls。\n",drawing_number_keys[j],entry->attributes[j]);
            }
        }
        made++;
    }
    if(!made||!ready){error=PRO_TK_BAD_CONTEXT;goto finish;}
    swprintf_s(preview->token,_countof(preview->token),L"DN-%08lX-%016llX-%08X",GetCurrentProcessId(),
        (unsigned long long)GetTickCount64(),++drawing_number_sequence);
    part_text_add(out,L"预览完成，未改模型。其余组件保持原样。核对后发送「应用批量图号 %ls」。\n",preview->token);
finish:
    if(error){
        part_text_add(out,L"未签发可应用的批次，Creo %d。\n",error);
        free(preview->tree);
        preview->tree=NULL;
    }
    return error;
}

static ProError drawing_number_outside_refs(const DrawingNumberPreview *preview,PartText *out){
    const ProMdlType types[]={PRO_MDL_ASSEMBLY,PRO_MDL_DRAWING,PRO_MDL_PART};
    for(int kind=0;kind<_countof(types);kind++){
        ProMdl *models=NULL;
        int count=0;
        ProError error=ProSessionMdlList(types[kind],&models,&count);
        if(error==PRO_TK_E_NOT_FOUND)error=PRO_TK_NO_ERROR;
        for(int i=0;!error&&i<count;i++){
            const ProMdl model=models[i];
            if(model==preview->context.model)continue;
            if(types[kind]==PRO_MDL_ASSEMBLY){
                int in_tree=0;
                for(int j=0;j<preview->tree->count;j++)if(preview->tree->occurrences[j].model==model){in_tree=1;break;}
                if(in_tree)continue;
            }
            for(int j=0;!error&&j<preview->count;j++){
                const DrawingNumberEntry *entry=&preview->entries[j];
                if(!entry->manufacture||model==entry->model)continue;
                int found=0;
                error=company_pair_dependency(model,types[kind],entry->model,entry->model,entry->original,&found);
                if(!error&&found){
                    ProMdlName dependent=L"";
                    ProMdlMdlnameGet(model,dependent);
                    part_text_add(out,L"%ls 还被 %ls 引用，需要先处理该引用。\n",entry->original,dependent);
                    error=PRO_TK_E_IN_USE;
                }
            }
        }
        if(models)ProArrayFree((ProArray *)&models);
        if(error)return error;
    }
    return PRO_TK_NO_ERROR;
}

static ProError drawing_number_tree_readback(const DrawingNumberPreview *preview,int renamed,AxScan **result){
    *result=NULL;
    AxScan *current=NULL;
    ProError error=ax_master(preview->context.model);
    if(!error)error=ax_scan(preview->context.model,1,&current);
    if(!error&&(current->count!=preview->tree->count||current->suppressed))error=PRO_TK_BAD_CONTEXT;
    for(int i=0;!error&&i<current->count;i++){
        const AxOccurrence *before=&preview->tree->occurrences[i],*now=&current->occurrences[i];
        const int entry=drawing_number_index(preview,before->model);
        const wchar_t *name=renamed&&entry>=0&&preview->entries[entry].manufacture?preview->entries[entry].target:before->name;
        if(now->depth!=before->depth||memcmp(now->ids,before->ids,before->depth*sizeof(now->ids[0]))||
           now->model!=before->model||now->state!=before->state||now->type!=before->type||_wcsicmp(now->name,name))
            error=PRO_TK_BAD_CONTEXT;
    }
    if(error)free(current);
    else *result=current;
    return error;
}

static ProError drawing_number_pose_readback(ProMdl root,const AxScan *tree,DrawingNumberPlacement *placements,int compare){
    double scale;
    ProError error=qa_mm_per_unit(root,&scale);
    for(int i=0;!error&&i<tree->count;i++){
        ProAsmcomppath path;
        ProAsmcomp feature;
        ProMatrix pose;
        ProIdTable ids={0};
        int constraints;
        const AxOccurrence *item=&tree->occurrences[i];
        memcpy(ids,item->ids,item->depth*sizeof(ids[0]));
        ProMdl owner=root;
        if(item->depth>1){
            error=ProAsmcomppathInit((ProSolid)root,ids,item->depth-1,&path);
            if(!error)error=ProAsmcomppathMdlGet(&path,&owner);
        }
        if(!error)error=ax_component_feature(owner,ids[item->depth-1],&feature);
        if(!error)error=ax_constraint_count(&feature,&constraints);
        if(!error)error=ProAsmcomppathInit((ProSolid)root,ids,item->depth,&path);
        if(!error)error=ProAsmcomppathTrfGet(&path,PRO_B_TRUE,pose);
        if(error)break;
        if(compare){
            if(constraints!=placements[i].constraints||!ax_same_pose(placements[i].pose,pose,scale))error=PRO_TK_BAD_CONTEXT;
        }else{
            placements[i].constraints=constraints;
            memcpy(placements[i].pose,pose,sizeof(pose));
        }
    }
    return error;
}

static ProError drawing_number_prepare_attributes(DrawingNumberEntry *entry,DeliveryAttribute attrs[2]){
    for(int i=0;i<2;i++){
        ProError error=drawing_number_attribute(entry->model,drawing_number_keys[i],&attrs[i]);
        if(error)return error;
        DeliveryAttribute *attribute=&attrs[i];
        attribute->value=i?entry->target:entry->label;
        if(attribute->existed!=entry->attributes_present[i])return PRO_TK_BAD_CONTEXT;
        if(!attribute->existed)continue;
        if(attribute->original.type!=PRO_PARAM_STRING||wcscmp(attribute->original.value.s_val,entry->attributes[i]))
            return PRO_TK_BAD_CONTEXT;
        if(!wcscmp(attribute->original.value.s_val,attribute->value)){attribute->unchanged=1;continue;}
        ProParameter parameter;
        ProLockstatus lock;
        error=ProParameterInit(&attribute->owner,(wchar_t *)attribute->name,&parameter);
        if(!error)error=ProParameterLockstatusGet(&parameter,&lock);
        if(error)return error;
        if(lock!=PRO_PARAMLOCKSTATUS_UNLOCKED)return PRO_TK_CANT_MODIFY;
    }
    return PRO_TK_NO_ERROR;
}

static ProError drawing_number_write_attribute(DeliveryAttribute *attribute){
    if(attribute->unchanged)return PRO_TK_NO_ERROR;
    ProParameter parameter;
    ProParamvalue value={0},readback;
    value.type=PRO_PARAM_STRING;
    wcscpy_s(value.value.s_val,_countof(value.value.s_val),attribute->value);
    attribute->attempted=1;
    ProError error;
    if(attribute->existed){
        error=ProParameterInit(&attribute->owner,(wchar_t *)attribute->name,&parameter);
        if(!error)error=ProParameterValueWithUnitsSet(&parameter,&value,NULL);
    }else error=ProParameterWithUnitsCreate(&attribute->owner,(wchar_t *)attribute->name,&value,NULL,&parameter);
    if(!error)error=dv_attribute_read(attribute,&parameter,&readback);
    return !error&&(readback.type!=PRO_PARAM_STRING||wcscmp(readback.value.s_val,attribute->value))?PRO_TK_GENERAL_ERROR:error;
}

static ProError drawing_number_apply(const LocalControlContext *context,const wchar_t *token,PartText *out){
    DrawingNumberPreview *preview=&drawing_number_preview;
    if(!preview->token[0]||wcscmp(token,preview->token)){
        part_text_add(out,L"预览编号不存在、已应用或已被替换，请重新预览。\n");
        return PRO_TK_BAD_INPUTS;
    }
    preview->token[0]=0;
    ProError error=PRO_TK_BAD_CONTEXT;
    AxScan *readback=NULL;
    DrawingNumberPlacement *before=NULL;
    DeliveryAttribute attrs[DRAWING_NUMBER_BATCH_LIMIT][2]={0};
    HANDLE claims[DRAWING_NUMBER_BATCH_LIMIT]={0};
    int make_count=0,mutated=0;
    if(ax_recovery_uncertain||!local_control_same_context(&preview->context,context))goto finish;
    if((error=drawing_number_tree_readback(preview,0,&readback)))goto finish;
    free(readback);readback=NULL;
    before=calloc(preview->tree->count,sizeof(*before));
    if(!before){error=PRO_TK_OUT_OF_MEMORY;goto finish;}
    if((error=drawing_number_pose_readback(context->model,preview->tree,before,0)))goto finish;
    if((error=drawing_number_outside_refs(preview,out)))goto finish;
    unsigned char used[1000]={0};
    if((error=company_pair_scan_names(preview->directory,preview->prefix,used)))goto finish;
    for(int i=0;i<preview->count;i++){
        DrawingNumberEntry *entry=&preview->entries[i];
        if(!entry->manufacture)continue;
        if(used[entry->serial]){error=PRO_TK_E_FOUND;goto finish;}
        char stamp[240];
        if((error=ax_study_verstamp(entry->model,stamp)))goto finish;
        if(strcmp(stamp,entry->stamp)){error=PRO_TK_BAD_CONTEXT;goto finish;}
        if((error=drawing_number_eligible(entry,preview->directory,out)))goto finish;
        if((error=drawing_number_prepare_attributes(entry,attrs[i])))goto finish;
        if(company_pair_reservations+make_count>=_countof(company_pair_reserved)){error=PRO_TK_OUT_OF_RANGE;goto finish;}
        DWORD reason=0;
        HANDLE claim=company_pair_claim(preview->directory,entry->target,&reason);
        if(claim==INVALID_HANDLE_VALUE){
            part_text_add(out,L"无法预留 %ls（Windows %lu）。\n",entry->target,reason);
            error=PRO_TK_E_IN_USE;goto finish;
        }
        claims[i]=claim;
        make_count++;
    }
    memset(used,0,sizeof(used));
    if((error=company_pair_scan_names(preview->directory,preview->prefix,used)))goto finish;
    for(int i=0;i<preview->count;i++)if(claims[i]&&used[preview->entries[i].serial]){error=PRO_TK_E_FOUND;goto finish;}
    for(int i=0;!error&&i<preview->count;i++){
        DrawingNumberEntry *entry=&preview->entries[i];
        if(!entry->manufacture)continue;
        entry->rename_attempted=1;
        mutated=1;
        error=ProMdlnameRename(entry->model,entry->target);
        if(!error&&!company_pair_model_name(entry->model,entry->type,entry->target))error=PRO_TK_GENERAL_ERROR;
        for(int j=0;!error&&j<2;j++)error=drawing_number_write_attribute(&attrs[i][j]);
    }
    if(!error)error=part_regenerate(context->model);
    if(!error)error=drawing_number_tree_readback(preview,1,&readback);
    if(!error)error=drawing_number_pose_readback(context->model,readback,before,1);
    for(int i=0;!error&&i<preview->count;i++)if(preview->entries[i].manufacture){
        for(int j=0;!error&&j<2;j++){
            DeliveryAttribute actual;
            error=drawing_number_attribute(preview->entries[i].model,drawing_number_keys[j],&actual);
            if(!error&&(!actual.existed||actual.original.type!=PRO_PARAM_STRING||
               wcscmp(actual.original.value.s_val,attrs[i][j].value)))error=PRO_TK_BAD_CONTEXT;
        }
    }
    if(!error){
        for(int i=0;i<preview->count;i++)if(claims[i]){
            DrawingNumberEntry *entry=&preview->entries[i];
            if(!company_pair_remember_claim(claims[i],preview->directory,entry->target))claims[i]=NULL;
            assembly_rename_refresh(entry->model);
            part_text_add(out,L"%ls → %ls，回读 %d 个型号实例。\n",entry->original,entry->target,entry->occurrences);
        }
        part_text_add(out,L"已核对 %d 个自制模型的图号、CNAME、CINDEX，及 %d 个实例的路径、位姿和约束数。修改留在 Creo，会话内未自动保存。\n",
            make_count,preview->tree->count);
    }
finish:
    if(error&&mutated){
        int restored=1;
        free(readback);readback=NULL;
        for(int i=preview->count-1;i>=0;i--){
            DrawingNumberEntry *entry=&preview->entries[i];
            for(int j=1;j>=0;j--)if(attrs[i][j].attempted&&!dv_attribute_restore(&attrs[i][j]))restored=0;
            if(entry->rename_attempted&&!company_pair_restore_name(entry->model,entry->type,entry->original,entry->target))restored=0;
        }
        if(part_regenerate(context->model)||drawing_number_tree_readback(preview,0,&readback)||
           drawing_number_pose_readback(context->model,readback,before,1))restored=0;
        if(restored)part_text_add(out,L"改名未通过，已回读原图号、原参数和原装配位置。\n");
        else{
            ax_uncertain(out);
            for(int i=0;i<preview->count;i++)if(claims[i]&&
                !company_pair_remember_claim(claims[i],preview->directory,preview->entries[i].target))claims[i]=NULL;
        }
    }
    if(error)part_text_add(out,L"批次未完成（Creo %d）。重新预览再应用。\n",error);
    if(mutated){local_control_invalidate_context();bwtl_live_refresh_model();}
    for(int i=0;i<preview->count;i++)if(claims[i])CloseHandle(claims[i]);
    free(readback);
    free(before);
    free(preview->tree);
    preview->tree=NULL;
    return error;
}

static ProError drawing_number_dispatch(const LocalControlContext *context,const wchar_t *payload,int execute,wchar_t *result,size_t capacity){
    if(context->type!=PRO_MDL_ASSEMBLY||!context->model||context->owner!=context->model)
        return local_control_error(PRO_TK_BAD_CONTEXT,result,capacity,L"请先激活要处理的 Creo 装配窗口。");
    PartCommand command={0};
    PartText out={result,capacity,0};
    size_t length=wcsnlen(payload,4097);
    if(length<=4096&&!wcspbrk(payload,L"\r\n")&&part_tokens(payload,length,&command)&&command.count){
        int offset=0;
        ProError error=PRO_TK_BAD_INPUTS;
        if(!execute&&!_wcsicmp(command.arg[0],L"AUDIT")&&(command.count==1||
           (command.count==2&&part_integer(command.arg[1],&offset)&&offset>=0&&offset<AX_MAX_OCCURRENCES)))
            error=drawing_number_audit(context->model,offset,&out);
        if(!execute&&!_wcsicmp(command.arg[0],L"PREVIEW"))error=drawing_number_plan(context,&command,&out);
        if(execute&&!_wcsicmp(command.arg[0],L"APPLY")&&command.count==2)error=drawing_number_apply(context,command.arg[1],&out);
        if(error!=PRO_TK_BAD_INPUTS||result[0]){
            if(error&&!result[0])part_text_add(&out,L"图号操作未完成（Creo %d）。\n",error);
            return error;
        }
    }
    return local_control_error(PRO_TK_BAD_INPUTS,result,capacity,L"用法：AUDIT [偏移]，PREVIEW M26A1481 V00 1 ID:42 MAKE \"支架\" ID:43 KEEP -，APPLY 预览编号。");
}

static int drawing_number_chat(const wchar_t *request){
    const wchar_t *operation=NULL,*arguments=L"";
    if(!wcscmp(request,L"查看装配图号"))operation=L"AUDIT";
    else if(!wcsncmp(request,L"查看装配图号 ",7)){operation=L"AUDIT";arguments=request+7;}
    else if(!wcsncmp(request,L"预览批量图号 ",7)){operation=L"PREVIEW";arguments=request+7;}
    else if(!wcsncmp(request,L"应用批量图号 ",7)){operation=L"APPLY";arguments=request+7;}
    if(!operation)return 0;
    if(native_busy||smart_active||ai_process||imitation_pending){append(L"请等待当前任务完成。");return 1;}
    wchar_t result[PART_TEXT_MAX]=L"",payload[4097];
    LocalControlContext current;
    ProError error=local_control_collect(&current);
    if(!error&&swprintf_s(payload,_countof(payload),L"%ls %ls",operation,arguments)<0)error=PRO_TK_BAD_INPUTS;
    if(!error)error=drawing_number_dispatch(&current,payload,!wcscmp(operation,L"APPLY"),result,_countof(result));
    if(error&&!result[0])swprintf_s(result,_countof(result),L"图号操作未完成（Creo %d）。",error);
    append_card(L"你",request);
    save_turn(L"用户",request);
    append_card(L"AI",result);
    save_turn(L"助手",result);
    native_prompt_set(L"");
    return 1;
}
