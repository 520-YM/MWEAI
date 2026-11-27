/* Reference workflow arguments. All model names include their Creo type. */
typedef struct {
    wchar_t source[PRO_PATH_SIZE], parent[PRO_PATH_SIZE], skeleton[PRO_PATH_SIZE];
    ProName skeleton_planes[2];
    ProMdlName number;
    int redraw, axis, instance, flat, save, background, current_skeleton;
    double size, clearance;
} ReferenceDesign;

static const wchar_t reference_help[] =
    L"参考建模与装配 REPLAY 或 REDRAW 源件.prt.版本 新图号V00 "
    L"[SIZE X/Y/Z 毫米 | FIT 骨架.prt.版本 X/Y/Z 总间隙毫米 | "
    L"FITCURRENT X/Y/Z 总间隙毫米 | FITPLANES 基准面A 基准面B X/Y/Z 总间隙毫米] "
    L"[PLACE 定位源.asm.版本 实例ID] [FLAT] [SAVE] [BACKGROUND]。"
    L"REPLAY 从空模板按原树建，REDRAW 按轮廓新画单平板。PLACE 只装入当前总装，按源实例的原生定位参照复核。";

static int reference_copy_argument(wchar_t *output,size_t capacity,const wchar_t *value){
    return value&&*value&&!wcscpy_s(output,capacity,value);
}

static int reference_axis(const wchar_t *value){
    if(!value||!value[0]||value[1])return -1;
    const wchar_t axis=towupper(value[0]);
    return axis>=L'X'&&axis<=L'Z'?axis-L'X':-1;
}

static int reference_request(const wchar_t *text,ReferenceDesign *plan){
    wchar_t input[1800],*next;
    if(!text||wcspbrk(text,L"\r\n")||wcscpy_s(input,_countof(input),text))return 0;
    *plan=(ReferenceDesign){.axis=-1};
    wchar_t *verb=wcstok_s(input,L" \t",&next);
    if(!verb)return 0;
    plan->redraw=!_wcsicmp(verb,L"REDRAW");
    if(!plan->redraw&&_wcsicmp(verb,L"REPLAY"))return 0;
    if(!reference_copy_argument(plan->source,_countof(plan->source),wcstok_s(NULL,L" \t",&next))||
       !reference_copy_argument(plan->number,_countof(plan->number),wcstok_s(NULL,L" \t",&next)))return 0;
    const size_t number_length=wcslen(plan->number);
    if(number_length<=3||number_length>31||!company_pair_revision(plan->number+number_length-3)||
       !model_assembly_name_valid(plan->number)||!dv_safe_leaf(plan->number))return 0;
    for(wchar_t *option=wcstok_s(NULL,L" \t",&next);option;option=wcstok_s(NULL,L" \t",&next)){
        if(!_wcsicmp(option,L"PLACE")&&!plan->instance){
            if(!reference_copy_argument(plan->parent,_countof(plan->parent),wcstok_s(NULL,L" \t",&next)))return 0;
            const wchar_t *id=wcstok_s(NULL,L" \t",&next);
            if(!id||!part_integer(id,&plan->instance)||plan->instance<=0)return 0;
        }else if(plan->axis<0&&(!_wcsicmp(option,L"SIZE")||!_wcsicmp(option,L"FIT")||
                                !_wcsicmp(option,L"FITCURRENT")||!_wcsicmp(option,L"FITPLANES"))){
            const int fitted=_wcsicmp(option,L"SIZE")!=0;
            if(!_wcsicmp(option,L"FIT")){
                if(!reference_copy_argument(plan->skeleton,_countof(plan->skeleton),
                                            wcstok_s(NULL,L" \t",&next)))return 0;
            }else if(fitted){
                plan->current_skeleton=1;
                if(!_wcsicmp(option,L"FITPLANES")){
                    for(int i=0;i<2;i++){
                        const wchar_t *name=wcstok_s(NULL,L" \t",&next);
                        if(!name||wcspbrk(name,L"\\/:*?\"<>|")||
                           !reference_copy_argument(plan->skeleton_planes[i],PRO_NAME_SIZE,name))return 0;
                    }
                    if(!_wcsicmp(plan->skeleton_planes[0],plan->skeleton_planes[1]))return 0;
                }
            }
            plan->axis=reference_axis(wcstok_s(NULL,L" \t",&next));
            const wchar_t *number=wcstok_s(NULL,L" \t",&next);
            double value;
            if(plan->axis<0||!number||!part_number(number,&value)||value>10000||value<0||
               (!fitted&&value<0.1))return 0;
            if(fitted)plan->clearance=value;
            else plan->size=value;
        }else if(!_wcsicmp(option,L"FLAT")&&!plan->flat)plan->flat=1;
        else if(!_wcsicmp(option,L"SAVE")&&!plan->save)plan->save=1;
        else if(!_wcsicmp(option,L"BACKGROUND")&&!plan->background)plan->background=1;
        else return 0;
    }
    return 1;
}
