static int markup_values(const MarkupRequest *request,double *values){
    if(request->count<1||request->count>MARKUP_MAX_MARKS)return 0;
    double mm;
    const wchar_t *text=markup_skip_space(request->text);
    const wchar_t *const all[]={L"所有标注的",L"全部标注的"};
    if((request->count==1||markup_take_phrase(&text,all,_countof(all)))&&markup_parse_diameter(text,&mm)){
        for(int i=0;i<request->count;i++)values[i]=mm;
        return 1;
    }
    wchar_t lines[256];
    wcscpy_s(lines,_countof(lines),request->text);
    wchar_t *context=NULL;
    unsigned seen=0;
    for(wchar_t *line=wcstok_s(lines,L"；;，,\r\n",&context);line;line=wcstok_s(NULL,L"；;，,\r\n",&context)){
        while(iswspace(*line))line++;
        int index=*line++-L'1';
        if(index<0||index>=request->count||*line++!=L'号'||(seen&(1u<<index)))return 0;
        if(!markup_parse_diameter(line,&values[index]))return 0;
        seen|=1u<<index;
    }
    return seen==(1u<<request->count)-1;
}

#include "markup_readback.inc"

typedef enum { MARKUP_DIAMETER, MARKUP_WIDTH, MARKUP_HEIGHT } MarkupMeasure;
typedef struct { int mark; MarkupMeasure measure; double value; } MarkupEdit;

#include "markup_assembly_actions.inc"
#include "markup_constraints.inc"
#include "markup_part_create.inc"
#include "markup_followup.inc"

static ProError markup_verify_sites(ProMdl model,const MarkupImage *image,double scale,const MarkupEdit *edits,const int *indices,
    const double *requested,int count,int after,int *failed){
    for(int i=0;i<count;i++){
        *failed=i;
        const int index=indices[i];
        if(edits[i].measure==MARKUP_DIAMETER){
            ProError error=markup_bore_read(model,&image->holes[index],
                after?requested[i]/scale:image->holes[index].diameter);
            if(error)return error;
            continue;
        }
        const MarkupOpening *opening=&image->openings[index];
        double size[]={opening->size[0],opening->size[1]};
        int first=1;
        for(int j=0;j<count;j++)if(edits[j].measure!=MARKUP_DIAMETER&&indices[j]==index){
            if(j<i)first=0;
            if(after)size[edits[j].measure-MARKUP_WIDTH]=requested[j]/scale;
        }
        if(first){
            ProError error=markup_opening_read(model,opening,size[0],size[1]);
            if(error)return error;
        }
    }
    return PRO_TK_NO_ERROR;
}
static ProError markup_apply_bound(const MarkupCapture *capture,const MarkupRequest *request,
    const MarkupEdit *edits,const int *indices,int count,wchar_t *reply,size_t capacity){
    if(count<1||count>MARKUP_MAX_EDITS)return PRO_TK_BAD_INPUTS;
    if(!capture->image||request->ticket!=capture->ticket||markup_check_capture(capture)){
        wcscpy_s(reply,capacity,L"模型、版本或工作目录已变化。请重新截图。");
        return PRO_TK_BAD_CONTEXT;
    }
    if(!storage_directory_on_drive(capture->directory,L'E')){
        wcscpy_s(reply,capacity,storage_output_message);
        return PRO_TK_CANT_WRITE;
    }
    ProMdl model=capture->model;
    double scale=capture->image->mm_per_native;
    AxComponent component={0};
    wchar_t authorization[400]=L"";
    if(capture->assembly){
        ProError proposed=markup_assembly_child(capture,request,edits,indices,count,&component,authorization,_countof(authorization));
        if(proposed){wcsncpy_s(reply,capacity,authorization,_TRUNCATE);return proposed;}
        model=component.model;
        scale=markup_mm_scale(model);
    }
    MarkupPreserved preserved;
    if(markup_prepare_preserved(capture,request,edits,indices,count,&preserved)){
        wcscpy_s(reply,capacity,L"未能唯一核对要保留的部位。请分别标出目标和保留处，本次尚未写入。");
        return PRO_TK_BAD_CONTEXT;
    }
    int driver[MARKUP_MAX_EDITS],owner[MARKUP_MAX_EDITS],feature[MARKUP_MAX_EDITS];
    double expected[MARKUP_MAX_EDITS];
    for(int i=0;i<count;i++){
        const MarkupEdit *edit=&edits[i];
        if(edit->mark<0||edit->mark>=request->count||edit->measure<MARKUP_DIAMETER||edit->measure>MARKUP_HEIGHT||
           !isfinite(edit->value)||edit->value<0.1||edit->value>10000)return PRO_TK_BAD_INPUTS;
        const int index=indices[i];
        if(edit->measure==MARKUP_DIAMETER){
            const MarkupHole *hole=&capture->image->holes[index];
            driver[i]=hole->dimension;owner[i]=hole->dimension_owner;feature[i]=hole->feature;expected[i]=hole->old_value;
        }else{
            const MarkupOpening *opening=&capture->image->openings[index];
            int axis=edit->measure-MARKUP_WIDTH;
            driver[i]=opening->dimension[axis];owner[i]=opening->owner;feature[i]=opening->feature;expected[i]=opening->size[axis];
        }
        if(driver[i]<0){
            swprintf_s(reply,capacity,L"%d 号目标没有可独立修改的驱动尺寸。本次未改图。",edit->mark+1);
            return PRO_TK_UNSUPPORTED;
        }
        for(int j=0;j<i;j++)if(driver[j]==driver[i]){
            swprintf_s(reply,capacity,L"%d 号与 %d 号对应同一个驱动尺寸。本次未改图。",edits[j].mark+1,edit->mark+1);
            return PRO_TK_BAD_INPUTS;
        }
    }
    PartSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    if(!snapshot)return PRO_TK_OUT_OF_MEMORY;
    ProError error=part_snapshot_take_options(model,snapshot,1,!capture->assembly);
    if(!error&&(!snapshot->stamp||!scale||snapshot->unhealthy||
       !part_close(snapshot->mm_per_native,scale)))error=PRO_TK_BAD_CONTEXT;
    if(!error&&capture->assembly){
        snapshot->window_model=capture->model;
        snapshot->in_assembly=1;
    }else if(!error&&(!ProVerstampEqual(snapshot->stamp,capture->stamp)||snapshot->window_model!=model||snapshot->in_assembly))
        error=PRO_TK_BAD_CONTEXT;
    ProDimension dimensions[MARKUP_MAX_EDITS];
    double old[MARKUP_MAX_EDITS],next[MARKUP_MAX_EDITS],mm_per_value[MARKUP_MAX_EDITS];
    for(int i=0;!error&&i<count;i++){
        const PartDimension *dimension=NULL;
        for(int j=0;j<snapshot->dimensions;j++){
            const PartDimension *candidate=&snapshot->dimension[j];
            int type_matches=edits[i].measure==MARKUP_DIAMETER?
                candidate->type==PRODIMTYPE_DIAMETER||candidate->type==PRODIMTYPE_RADIUS:candidate->type==PRODIMTYPE_LINEAR;
            if(candidate->handle.id==driver[i]&&candidate->feature==owner[i]&&type_matches&&
               part_close(candidate->value,expected[i])&&!candidate->related&&!candidate->negative){
                dimension=candidate;
                break;
            }
        }
        if(!dimension){
            error=PRO_TK_BAD_CONTEXT;
            break;
        }
        dimensions[i]=dimension->handle;
        old[i]=dimension->value;
        mm_per_value[i]=snapshot->mm_per_native*(dimension->type==PRODIMTYPE_RADIUS?2:1);
        next[i]=edits[i].value/mm_per_value[i];
    }
    double requested[MARKUP_MAX_EDITS];
    for(int i=0;i<count;i++)requested[i]=edits[i].value;
    int failed=0;
    const int hold_left=markup_keep_left(request->text);
    if(!error){
        error=markup_verify_sites(model,capture->image,scale,edits,indices,requested,count,0,&failed);
        if(error)swprintf_s(reply,capacity,L"%d 号标注的原生几何未与截图记录对上。本次未改图。",edits[failed].mark+1);
        else if((error=markup_verify_preserved(model,capture->image,&preserved)))
            wcscpy_s(reply,capacity,L"要保留的部位与截图记录对不上。请重截后核对，本次尚未写入。");
    }
    if(!error&&hold_left&&(error=markup_verify_opening_left(capture->image,model,edits,indices,requested,count,scale,0)))
        wcscpy_s(reply,capacity,L"未能核对截图里开口原来的左边位置，尚未写入。");
    if(!error)error=part_write_verified_dimensions(model,snapshot,dimensions,old,next,count,reply,capacity);
    if(!error){
        const ProError geometry=markup_verify_sites(model,capture->image,scale,edits,indices,requested,count,1,&failed);
        const ProError kept=markup_verify_preserved(model,capture->image,&preserved);
        const ProError shifted=hold_left?markup_verify_opening_left(capture->image,model,edits,indices,requested,count,scale,1):0;
        if(geometry||kept||shifted){
            wchar_t rollback[300]=L"";
            const int failed_mark=edits[failed].mark+1;
            int restored=!part_write_verified_dimensions(model,snapshot,dimensions,next,old,count,rollback,_countof(rollback))&&
                !markup_verify_sites(model,capture->image,scale,edits,indices,requested,count,0,&failed)&&
                !markup_verify_preserved(model,capture->image,&preserved)&&
                (!hold_left||!markup_verify_opening_left(capture->image,model,edits,indices,requested,count,scale,0));
            wchar_t failed_target[80];
            if(shifted)wcscpy_s(failed_target,_countof(failed_target),L"修改后开口左边位置发生变化");
            else if(kept)wcscpy_s(failed_target,_countof(failed_target),L"要保留的部位在再生后发生变化");
            else swprintf_s(failed_target,_countof(failed_target),L"%d 号标注修改后的原生几何未通过核对",failed_mark);
            swprintf_s(reply,capacity,L"%ls。%ls",failed_target,
                restored?L"本批旧尺寸和原有几何均已恢复。":L"恢复状态需要核对，请检查当前模型。");
            error=PRO_TK_BAD_CONTEXT;
        }
    }
    if(!error){
        int diameters=1;
        for(int i=0;i<count;i++)diameters&=edits[i].measure==MARKUP_DIAMETER;
        swprintf_s(reply,capacity,diameters?L"已修改 %d 处孔径。Creo 再生及逐处几何读回通过，未保存。":
            L"已修改 %d 个尺寸。Creo 再生及逐处几何读回通过，未保存。",count);
        for(int i=0;i<count;i++){
            const wchar_t *labels[]={L"孔径 Ø",L"草绘 X 向开口 ",L"草绘 Y 向开口 "};
            wchar_t item[140];
            double factor=mm_per_value[i];
            if(edits[i].measure==MARKUP_DIAMETER)
                swprintf_s(item,_countof(item),L"\n%d 号 · 特征 %d / 孔壁 %d · %ls%.8g → %.8g mm",
                    edits[i].mark+1,feature[i],capture->image->holes[indices[i]].surface,labels[0],old[i]*factor,next[i]*factor);
            else swprintf_s(item,_countof(item),L"\n%d 号 · 特征 %d · %ls%.8g → %.8g mm",
                edits[i].mark+1,feature[i],labels[edits[i].measure],old[i]*factor,next[i]*factor);
            if(wcslen(reply)+wcslen(item)<capacity)wcscat_s(reply,capacity,item);
        }
        markup_remember_holes(capture,request,edits,indices,count,&preserved);
        if(hold_left)wcscat_s(reply,capacity,L"\nCreo 已确认开口左边位置保持原样。");
        if(preserved.count){
            wchar_t check[110];
            swprintf_s(check,_countof(check),L"\n已用 Creo 确认另外 %d 处保留部位的几何保持原样。",preserved.count);
            if(wcslen(reply)+wcslen(check)<capacity)wcscat_s(reply,capacity,check);
            for(int i=0,reported=0;i<capture->image->count&&reported<4;i++)if(preserved.holes[i]){
                const MarkupHole *hole=&capture->image->holes[i];
                swprintf_s(check,_countof(check),L"\n保留孔壁 %d · Ø%.8g mm",hole->surface,hole->diameter*scale);
                if(wcslen(reply)+wcslen(check)<capacity)wcscat_s(reply,capacity,check);
                reported++;
            }
        }
        if(authorization[0]&&wcslen(reply)+wcslen(authorization)+24<capacity){
            wcscat_s(reply,capacity,L"\n");
            wcscat_s(reply,capacity,authorization);
            wcscat_s(reply,capacity,L"原装配已重生成。");
        }
    }else if(!reply[0])wcscpy_s(reply,capacity,L"未执行修改。请重新截图，并核对模型再生状态及驱动尺寸。");
    part_snapshot_clear(snapshot);
    free(snapshot);
    return error;
}

static ProError markup_apply_edits(const MarkupCapture *capture,const MarkupRequest *request,
    const MarkupEdit *edits,int count,wchar_t *reply,size_t capacity){
    if(count<1||count>MARKUP_MAX_EDITS||request->count<1||request->count>MARKUP_MAX_MARKS)
        return PRO_TK_BAD_INPUTS;
    if(!capture->image||request->ticket!=capture->ticket||markup_check_capture(capture)){
        wcscpy_s(reply,capacity,L"模型、版本或工作目录已变化。请重新截图。");
        return PRO_TK_BAD_CONTEXT;
    }
    int indices[MARKUP_MAX_EDITS];
    for(int i=0;i<count;i++){
        const MarkupEdit *edit=&edits[i];
        if(edit->mark<0||edit->mark>=request->count)return PRO_TK_BAD_INPUTS;
        const MarkupMark *mark=&request->marks[edit->mark];
        int index=edit->measure==MARKUP_DIAMETER?markup_match(capture->image,mark):markup_match_opening(capture->image,mark);
        if(index<0){
            swprintf_s(reply,capacity,L"%d 号标注%ls。请缩小标注范围或调整视角，本次未改图。",
                edit->mark+1,index==-2?L"包含多个候选":L"未定位到清楚可见的目标");
            return PRO_TK_BAD_INPUTS;
        }
        indices[i]=index;
    }
    return markup_apply_bound(capture,request,edits,indices,count,reply,capacity);
}

static ProError markup_apply_collective(const MarkupCapture *capture,const MarkupRequest *request,
    double value,int wanted,wchar_t *reply,size_t capacity){
    const MarkupImage *image=capture->image;
    if(!image||request->ticket!=capture->ticket||markup_check_capture(capture)){
        wcscpy_s(reply,capacity,L"模型、版本或工作目录已变化。请重新截图。");
        return PRO_TK_BAD_CONTEXT;
    }
    if(request->count<1||request->count>MARKUP_MAX_MARKS){
        wcscpy_s(reply,capacity,L"请先圈选或标注要改的孔。");
        return PRO_TK_BAD_INPUTS;
    }
    MarkupEdit edits[MARKUP_MAX_EDITS];
    int indices[MARKUP_MAX_EDITS],count=0;
    for(int i=0;i<request->count;i++){
        int hits[MARKUP_MAX_EDITS];
        const MarkupMark *mark=&request->marks[i];
        const int hit_count=markup_hole_hits(image,mark,hits,
            mark->tool==MARKUP_ARROW?1:MARKUP_MAX_EDITS);
        if(hit_count<1){
            const wchar_t *why=hit_count<0?L"候选重叠或超出本次可核对范围":L"未圈到可见圆孔";
            swprintf_s(reply,capacity,L"%d 号标注%ls。请缩小圈选或重截清晰孔位。本次未改图。",i+1,why);
            return PRO_TK_BAD_INPUTS;
        }
        for(int j=0;j<hit_count;j++){
            int k=0;
            while(k<count&&indices[k]!=hits[j])k++;
            if(k<count)continue;
            if(count==MARKUP_MAX_EDITS){
                swprintf_s(reply,capacity,L"本次最多核对 %d 处圆孔。请缩小选区，本次未改图。",MARKUP_MAX_EDITS);
                return PRO_TK_BAD_INPUTS;
            }
            indices[count]=hits[j];
            edits[count++]=(MarkupEdit){i,MARKUP_DIAMETER,value};
        }
    }
    if(wanted&&wanted!=count){
        swprintf_s(reply,capacity,L"你要求改 %d 个孔，标注中实际核对到 %d 个可见圆孔。本次未改图。",wanted,count);
        return PRO_TK_BAD_INPUTS;
    }
    return markup_apply_bound(capture,request,edits,indices,count,reply,capacity);
}
static ProError markup_apply_edge(const MarkupCapture *capture,const MarkupRequest *request,
    double length,wchar_t *reply,size_t capacity){
    if(!capture->image||request->ticket!=capture->ticket||markup_check_capture(capture)){
        wcscpy_s(reply,capacity,L"模型、版本或工作目录已变化。请重新截图。");
        return PRO_TK_BAD_CONTEXT;
    }
    if(request->count<1||request->count>MARKUP_MAX_MARKS){
        wcscpy_s(reply,capacity,L"请用箭头指在每条要修改的矩形开口边中间。");
        return PRO_TK_BAD_INPUTS;
    }
    MarkupEdit edits[MARKUP_MAX_MARKS];
    int indices[MARKUP_MAX_MARKS];
    for(int i=0;i<request->count;i++){
        int axis=0;
        indices[i]=markup_opening_edge(capture->image,&request->marks[i],&axis);
        if(indices[i]<0){
            swprintf_s(reply,capacity,L"%d 号未唯一定位到矩形贯穿开口的一条直边。请把箭头尖端指到边的中段，本次未改图。",i+1);
            return PRO_TK_BAD_INPUTS;
        }
        edits[i]=(MarkupEdit){i,axis?MARKUP_HEIGHT:MARKUP_WIDTH,length};
    }
    return markup_apply_bound(capture,request,edits,indices,request->count,reply,capacity);
}

static void markup_read_faces(const MarkupCapture *capture,const MarkupRequest *request,wchar_t *reply,size_t capacity){
    if(request->count<1||request->count>MARKUP_MAX_MARKS){
        wcscpy_s(reply,capacity,L"用箭头标注需要核对的面，可同时标多个位置。");
        return;
    }
    PartText out={reply,capacity,0};
    part_text_add(&out,L"Creo 原生面回读");
    for(int i=0;i<request->count;i++){
        const int h=markup_match(capture->image,&request->marks[i]);
        if(h>=0){
            const MarkupHole *hole=&capture->image->holes[h];
            if(!markup_bore_read(capture->model,hole,hole->diameter)){
                const double mm=capture->image->mm_per_native;
                part_text_add(&out,L"\n%d 号 · 圆孔 Ø%.8g mm · 孔壁 SURFACE:%d · 特征 %d",i+1,hole->diameter*mm,hole->surface,hole->feature);
                continue;
            }
        }
        MarkupFace face;
        ProError error=markup_face_at(capture,&request->marks[i],&face);
        if(error)part_text_add(&out,L"\n%d 号未命中可见实体面，请指向板面内部。",i+1);
        else{
            const wchar_t *type=face.type==PRO_SRF_PLANE?L"平面":face.type==PRO_SRF_CYL?L"圆柱面":
                face.type==PRO_SRF_CONE?L"圆锥面":L"曲面";
            part_text_add(&out,L"\n%d 号 · %ls SURFACE:%d · 特征 %d %ls",i+1,type,face.surface,face.feature,face.name);
        }
    }
}

static ProError markup_apply(const MarkupCapture *capture,const MarkupRequest *request,wchar_t *reply,size_t capacity){
    double requested[MARKUP_MAX_MARKS];
    MarkupEdit edits[MARKUP_MAX_MARKS];
    double collective_size;
    int collective_count;
    if(markup_collective_diameter(request->text,&collective_size,&collective_count))
        return markup_apply_collective(capture,request,collective_size,collective_count,reply,capacity);
    if(!markup_values(request,requested)){
        wcscpy_s(reply,capacity,L"请输入要改的孔径，或说明希望怎样调整。");
        return PRO_TK_BAD_INPUTS;
    }
    for(int i=0;i<request->count;i++)edits[i]=(MarkupEdit){i,MARKUP_DIAMETER,requested[i]};
    return markup_apply_edits(capture,request,edits,request->count,reply,capacity);
}

static void markup_deliver(unsigned long ticket,int success,int retake,const wchar_t *text){
    MarkupResult *result=malloc(sizeof(*result));
    if(result){
        result->ticket=ticket;
        result->success=success;
        result->retake=retake;
        size_t used=0;
        for(const wchar_t *p=text;*p&&used+2<_countof(result->text);p++){
            if(*p==L'\n'&&(p==text||p[-1]!=L'\r'))result->text[used++]=L'\r';
            result->text[used++]=*p;
        }
        result->text[used]=0;
        if(native_window&&PostMessageW(native_window,MARKUP_RESULT,0,(LPARAM)result))return;
        free(result);
    }
}

#include "markup_intent.inc"

static int markup_native_command(uiCmdCmdId id,uiCmdValue *value,void *data){
    int action=markup_pending;
    markup_pending=0;
    if(!native_window||!action)return 0;
    if(smart_active||ai_process||native_busy){
        if(action!=1)markup_deliver(markup_request.ticket,0,0,L"当前 MWEAI 有任务正在运行。完成后可重试。");
        else markup_deliver(0,0,0,L"当前 MWEAI 有任务正在运行。完成后可重新截图。");
        return 0;
    }
    if(action==1){
        markup_capture_clear();
        ProError error=markup_take(&markup_capture);
        if(error){
            markup_capture_clear();
            const wchar_t *message=error==PRO_TK_INVALID_TYPE?L"请先激活一个 Creo 零件或装配体。":
                L"无法截取当前 Creo 窗口。请确认窗口已展开，并检查 E 盘数据目录。";
            append_card(L"截图改图",message);
            markup_deliver(0,0,0,message);
            trace("Screenshot capture failed",error);
        }else{
            MarkupImage *copy=malloc(sizeof(*copy));
            if(copy){
                *copy=*markup_capture.image;
                copy->bitmap=(HBITMAP)CopyImage(markup_capture.image->bitmap,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
            }
            if(!copy||!copy->bitmap||!PostMessageW(native_window,MARKUP_READY,0,(LPARAM)copy)){
                markup_image_free(copy);
                markup_capture_clear();
                append_card(L"截图改图",L"无法打开截图标注窗口。");
                markup_deliver(0,0,0,L"无法打开截图标注窗口。");
            }
        }
    }else{
        wchar_t reply[2000]=L"";
        ProError error;
        if(action==3){
            if(markup_intent.proposal){markup_show_proposal();return 0;}
            error=markup_execute_intent(reply,_countof(reply));
        }else{
            if(!markup_capture.image||markup_request.ticket!=markup_capture.ticket||markup_check_capture(&markup_capture)){
                markup_finish_request(0,1,L"模型、版本或工作目录已变化。请重新截图。");
                return 0;
            }
            const int measure=markup_measure_request(markup_request.text);
            if(measure||markup_face_request(markup_request.text)||!wcscmp(markup_request.text,L"检查参照")){
                if(markup_capture.assembly)markup_assembly_read(&markup_capture,&markup_request,measure,reply,_countof(reply));
                else if(measure)markup_measure_part(&markup_capture,&markup_request,reply,_countof(reply));
                else markup_read_faces(&markup_capture,&markup_request,reply,_countof(reply));
                if(!reply[0])wcscpy_s(reply,_countof(reply),L"无法读取标注参照，请将标注放在可见的实体区域。");
                markup_finish_request(0,0,reply);
                return 0;
            }
            int confirmed=markup_confirm_proposal(&markup_request);
            if(confirmed)markup_intent=markup_proposal;
            markup_proposal=(MarkupIntent){0};
            MarkupRequest parsed=markup_request;
            const wchar_t *authorized=markup_capture.assembly?markup_shared_consent_clause(markup_request.text):NULL;
            if(authorized)parsed.text[authorized-markup_request.text]=0;
            double explicit_values[MARKUP_MAX_MARKS],collective_size,edge_length;
            int collective_count;
            const int collective=markup_collective_diameter(parsed.text,&collective_size,&collective_count);
            int opening=0;
            for(int i=0;i<markup_request.count&&!opening;i++)
                opening=markup_match_opening(markup_capture.image,&markup_request.marks[i])!=-1;
            MarkupCreation creation;
            MarkupEdit direct[MARKUP_MAX_EDITS];
            int direct_count=0;
            if(confirmed){
                error=markup_execute_intent(reply,_countof(reply));
            }else if(!smart_explicit_readonly(markup_request.text)&&markup_followup(&markup_capture,&markup_request,direct,&direct_count,reply,_countof(reply),&error)){
                if(!error)error=markup_apply_edits(&markup_capture,&markup_request,direct,direct_count,reply,_countof(reply));
            }else if(markup_capture.assembly&&!smart_explicit_readonly(markup_request.text)&&
                     markup_assembly_action(&markup_capture,&markup_request,reply,_countof(reply),&error)){
                /* The action returns its native receipt. */
            }else if(!smart_explicit_readonly(markup_request.text)&&markup_create_parse(parsed.text,&creation)){
                error=markup_apply_creation(&markup_capture,&markup_request,&creation,reply,_countof(reply));
            }else if(!smart_explicit_readonly(markup_request.text)&&markup_opening_adjustment(&markup_capture,&parsed,direct,reply,_countof(reply),&error)){
                if(!error)error=markup_apply_edits(&markup_capture,&markup_request,direct,1,reply,_countof(reply));
            }else if(!smart_explicit_readonly(markup_request.text)&&markup_direct_partial(&parsed,direct,&direct_count)){
                error=markup_apply_edits(&markup_capture,&markup_request,direct,direct_count,reply,_countof(reply));
            }else if(markup_edge_length_request(parsed.text,&edge_length)){
                error=markup_apply_edge(&markup_capture,&markup_request,edge_length,reply,_countof(reply));
            }else if(collective){
                error=markup_apply_collective(&markup_capture,&markup_request,collective_size,collective_count,reply,_countof(reply));
            }else if(!markup_values(&parsed,explicit_values)||
                     (opening&&!markup_named_hole(markup_request.text))){
                if(!markup_start_intent())markup_finish_request(0,0,
                    L"无法启动 Codex 分析。本次未修改模型，可以保留标注重试。");
                return 0;
            }else{
                for(int i=0;i<parsed.count;i++)direct[i]=(MarkupEdit){i,MARKUP_DIAMETER,explicit_values[i]};
                error=markup_apply_edits(&markup_capture,&markup_request,direct,parsed.count,reply,_countof(reply));
            }
        }
        trace("Apply screenshot annotation",error);
        if(!reply[0])wcscpy_s(reply,_countof(reply),L"修改未完成。请重试。");
        markup_finish_request(!error,error==PRO_TK_BAD_CONTEXT,reply);
    }
    return 0;
}

static void markup_queue(int action,MarkupRequest *request){
    if(!markup_pending&&!markup_ai.process&&!smart_active&&!ai_process&&!native_busy){
        if(request)markup_request=*request;
        markup_pending=action;
        if(!ProMacroLoad(L"~ Command " L"\x60" L"BWTLScreenshotMarkup" L"\x60;")){
            free(request);
            return;
        }
        markup_pending=0;
    }
    markup_deliver(request?request->ticket:0,0,0,L"当前无法提交到 Creo，请等待正在运行的任务结束后重试。");
    free(request);
}
