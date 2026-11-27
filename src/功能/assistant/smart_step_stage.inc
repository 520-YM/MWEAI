enum {
    SMART_STAGE_LOCATE,SMART_STAGE_READ,SMART_STAGE_INSERT,SMART_STAGE_PLACE,SMART_STAGE_WRITE,
    SMART_STAGE_COUNT
};
enum { SMART_MAX_DIAGNOSTIC_FAILURES=4 };

static int smart_stage_failures[SMART_STAGE_COUNT],smart_diagnostic_failures;

static const wchar_t *smart_stage_name(int stage){
    static const wchar_t *const names[]={L"查找库件",L"读取与核对",L"装入组件",L"定位与验收",L"建模写入"};
    return names[stage];
}

static int smart_step_stage(const PartCommand *cmd){
    if(!cmd)return SMART_STAGE_WRITE;
    if(smart_read_command(cmd))return SMART_STAGE_READ;
    if(cmd->verb!=PART_FOUNDATION)return SMART_STAGE_WRITE;
    switch(ax_kind(cmd->arg[0])){
    case AX_INSERT:case AX_INSERT_ASM:case AX_NEW_ASM:case AX_NEW_PART:
        return SMART_STAGE_INSERT;
    case AW_ALIGN:case AW_ALIGN_BATCH:case AX_CSYS:case AX_MATE:case AX_PARALLEL:case AX_OFFSET:
    case AX_COAX:case AX_THREE:case AX_FASTENER:case AX_PRESS_HARDWARE:case AX_FASTENER_STACK:
        return SMART_STAGE_PLACE;
    default:
        return SMART_STAGE_WRITE;
    }
}

static void smart_stage_failed(int stage){
    smart_stage_failures[stage]++;
    smart_total_failures++;
    if(stage==SMART_STAGE_LOCATE||stage==SMART_STAGE_READ)smart_diagnostic_failures++;
    else {
        smart_failures++;
        smart_failures_without_progress++;
    }
}

static int smart_diagnostic_budget_exhausted(void){
    return smart_diagnostic_failures>=SMART_MAX_DIAGNOSTIC_FAILURES;
}

static void smart_stage_summary(wchar_t *out,size_t capacity){
    swprintf_s(out,capacity,L"失败分布：找件 %d，读取与核对 %d，装入 %d，定位与验收 %d，其他写入 %d。连续未取得建模进展的写入失败 %d 次。",
        smart_stage_failures[SMART_STAGE_LOCATE],smart_stage_failures[SMART_STAGE_READ],smart_stage_failures[SMART_STAGE_INSERT],
        smart_stage_failures[SMART_STAGE_PLACE],smart_stage_failures[SMART_STAGE_WRITE],smart_failures_without_progress);
}

