#define PRO_TK_E_NOT_EXIST PRO_TK_E_NOT_FOUND
#define user_initialize device_real_rename
#include "../src/assistant.c"
#undef user_initialize
#include <ProCore.h>
typedef struct {int plane[192],bore[192],planes,bores;} DeviceNativeRefs;
static ProError device_surface(ProSurface surface,ProError status,ProAppData data){
    if(status)return status;
    DeviceNativeRefs *r=data;
    ProSrftype type;
    int id;
    if(ProSurfaceTypeGet(surface,&type)||ProSurfaceIdGet(surface,&id))return 0;
    if(type==PRO_SRF_PLANE&&r->planes<192)r->plane[r->planes++]=id;
    if(type==PRO_SRF_CYL&&r->bores<192)r->bore[r->bores++]=id;
    return 0;
}
static int run_device_inventory(uiCmdCmdId id,uiCmdValue *v,void *data){
    FILE *out=_wfopen(L"company-device-inventory.log",L"wt, ccs=UTF-8");
    if(!out)return ProEngineerEnd();
    ProMdl model=NULL;
    ProPath source=L"E:\\BWTL-Data\\BWTL-55-Learning\\models\\2m0859-v1.prt.3";
    ProError error=ProMdlFiletypeLoad(source,PRO_MDLFILE_PART,PRO_B_FALSE,&model);
    double mm=0;
    if(!error)error=qa_mm_per_unit(model,&mm);
    DeviceNativeRefs r={0};
    if(!error)error=ProSolidSurfaceVisit((ProSolid)model,device_surface,NULL,&r);
    fwprintf(out,L"REAL_SOURCE=%d MM=%g PLANES=%d CYLINDERS=%d\n",error,mm,r.planes,r.bores);
    if(!error){
        AxComponent owner={.model=model};
        for(int i=0;i<r.planes;i++)for(int j=0;j<r.bores;j++){
            AxHardwareSite a={0};
            if(company_device_site(&owner,r.plane[i],r.bore[j],mm,&a))continue;
            fwprintf(out,L"FACE=%d HOLE=%d D=%.9g CENTER=%.9g %.9g %.9g NORMAL=%.9g %.9g %.9g\n",
                r.plane[i],r.bore[j],2*a.cylinder.radius*mm,a.contact[0]*mm,a.contact[1]*mm,a.contact[2]*mm,
                a.plane.direction[0],a.plane.direction[1],a.plane.direction[2]);
        }
    }
    fwprintf(out,L"DONE=1\n");
    fclose(out);
    return ProEngineerEnd();
}
__declspec(dllexport) int user_initialize(int n,char **a,char *v,char *b,wchar_t m[80]){
    if(!GetEnvironmentVariableW(L"BWTL_FOUNDATION_SELFTEST",NULL,0))return PRO_TK_BAD_CONTEXT;
    uiCmdCmdId id;
    ProError error=ProCmdActionAdd("CompanyDeviceInventory",run_device_inventory,uiProe2ndImmediate,NULL,PRO_B_TRUE,PRO_B_TRUE,&id);
    return error?error:ProMacroLoad(L"~ Command " L"\x60" L"CompanyDeviceInventory\x60;");
}
