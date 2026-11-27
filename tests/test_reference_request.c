#include <assert.h>
#include <stdio.h>

#include "../src/model_plan.h"
#include "../src/part_command.h"

/* The parser only uses these Creo SDK buffer types. Use Creo 10 capacities
   so the suite can compile without starting Creo or linking its libraries. */
#define PRO_PATH_SIZE 260
#define PRO_NAME_SIZE 32
typedef wchar_t ProMdlName[180];
typedef wchar_t ProName[PRO_NAME_SIZE];

/* company_pair_revision is in a native translation unit. */
static int company_pair_revision(const wchar_t *value){
    return wcslen(value)==3&&towupper(value[0])==L'V'&&
        value[1]>=L'0'&&value[1]<=L'9'&&value[2]>=L'0'&&value[2]<=L'9';
}

#include "../src/功能/reference_design/reference_request.inc"

int main(void){
    ReferenceDesign request;
    assert(reference_request(L"REPLAY frame.prt.2 M26A1481001V00",&request));
    assert(!request.redraw&&request.axis==-1&&!request.instance&&!request.flat&&!request.save&&!request.background);
    assert(!wcscmp(request.source,L"frame.prt.2")&&!wcscmp(request.number,L"M26A1481001V00"));

    assert(reference_request(L"redraw\tplate.prt.1\tM26A1481002v01 size y 350.25 FLAT",&request));
    assert(request.redraw&&request.axis==1&&request.size==350.25&&request.flat&&!request.save);
    assert(!request.skeleton[0]&&!request.parent[0]);

    assert(reference_request(L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6 42 FIT skeleton.prt.7 X 0.5 SAVE BACKGROUND",&request));
    assert(!request.redraw&&request.axis==0&&request.clearance==0.5&&request.size==0);
    assert(!wcscmp(request.skeleton,L"skeleton.prt.7")&&!wcscmp(request.parent,L"source.asm.6"));
    assert(request.instance==42&&request.save&&request.background&&!request.flat);

    assert(reference_request(L"REDRAW plate.prt.1 NEWV00 FIT skeleton.prt.7 z 0 PLACE source.asm.6 2147483647",&request));
    assert(request.redraw&&request.axis==2&&request.clearance==0&&request.instance==2147483647);

    assert(reference_request(L"REPLAY frame.prt.2 NEWV00 SIZE X 0.1",&request));
    assert(request.axis==0&&request.size==0.1);
    assert(reference_request(L"REPLAY frame.prt.2 NEWV00 SIZE Z 10000",&request));
    assert(request.axis==2&&request.size==10000);
    assert(reference_request(L"REDRAW plate.prt.1 NEWV00 FITCURRENT X 2 SAVE",&request));
    assert(request.current_skeleton&&request.axis==0&&request.clearance==2&&request.save);
    assert(reference_request(L"REDRAW plate.prt.1 NEWV00 FITPLANES LEFT_RIGHT INLET_LIMIT Y 1.5",&request));
    assert(request.current_skeleton&&request.axis==1&&request.clearance==1.5);
    assert(!wcscmp(request.skeleton_planes[0],L"LEFT_RIGHT")&&!wcscmp(request.skeleton_planes[1],L"INLET_LIMIT"));

    const wchar_t *const rejected[]={
        NULL, L"", L"  ", L"COPY frame.prt.2 NEWV00", L"REPLAY", L"REDRAW frame.prt.2",
        L"REPLAY frame.prt.2 NEW", L"REPLAY frame.prt.2 V00", L"REPLAY frame.prt.2 NEWV0",
        L"REPLAY frame.prt.2 NEWVA1", L"REPLAY frame.prt.2 NEWV001",
        L"REPLAY frame.prt.2 NEWV00.asm", L"REPLAY frame.prt.2 ..\\NEWV00",
        L"REPLAY frame.prt.2 NEW!V00", L"REPLAY frame.prt.2 12345678901234567890123456789V00",
        L"REPLAY frame.prt.2 NEWV00\nSAVE", L"REPLAY frame.prt.2 NEWV00\rSAVE",
        L"REPLAY frame.prt.2 NEWV00 UNKNOWN", L"REPLAY frame.prt.2 NEWV00 SIZE XY 6",
        L"REPLAY frame.prt.2 NEWV00 SIZE X", L"REPLAY frame.prt.2 NEWV00 SIZE X -1",
        L"REPLAY frame.prt.2 NEWV00 SIZE X 0.09", L"REPLAY frame.prt.2 NEWV00 SIZE X 10001",
        L"REPLAY frame.prt.2 NEWV00 SIZE X nan", L"REPLAY frame.prt.2 NEWV00 SIZE X inf",
        L"REPLAY frame.prt.2 NEWV00 SIZE X 25mm", L"REPLAY frame.prt.2 NEWV00 SIZE X 1 SIZE Z 2",
        L"REPLAY frame.prt.2 NEWV00 FIT skeleton.prt.7 X -0.1", L"REPLAY frame.prt.2 NEWV00 FIT X 2",
        L"REPLAY frame.prt.2 NEWV00 FIT skeleton.prt.7 X", L"REPLAY frame.prt.2 NEWV00 FIT skeleton.prt.7 Y 2 FIT skeleton.prt.7 Z 1",
        L"REPLAY frame.prt.2 NEWV00 SIZE X 10 FIT skeleton.prt.7 X 1",
        L"REPLAY frame.prt.2 NEWV00 FIT skeleton.prt.7 X 1 SIZE X 10",
        L"REPLAY frame.prt.2 NEWV00 FITCURRENT Z -1", L"REPLAY frame.prt.2 NEWV00 FITCURRENT X",
        L"REPLAY frame.prt.2 NEWV00 FITPLANES TOP top Z 2", L"REPLAY frame.prt.2 NEWV00 FITPLANES TOP Z 2",
        L"REPLAY frame.prt.2 NEWV00 FITPLANES C:\\A TOP X 2", L"REPLAY frame.prt.2 NEWV00 FITCURRENT X 2 SIZE X 4",
        L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6", L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6 0",
        L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6 -1", L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6 2.5",
        L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6 2147483648",
        L"REPLAY frame.prt.2 NEWV00 PLACE source.asm.6 42 PLACE other.asm.1 7",
        L"REPLAY frame.prt.2 NEWV00 SAVE SAVE", L"REPLAY frame.prt.2 NEWV00 FLAT FLAT",
        L"REPLAY frame.prt.2 NEWV00 BACKGROUND BACKGROUND",
    };
    for(size_t i=0;i<_countof(rejected);i++){
        if(reference_request(rejected[i],&request)){
            fwprintf(stderr,L"Accepted invalid reference request at case %zu: %ls\n",i,rejected[i]);
            return 1;
        }
    }

    assert(wcslen(reference_help)>0);
    puts("REFERENCE REQUEST PASS: replay, redraw, sizing, skeleton fitting, placement, and invalid requests.");
    return 0;
}
