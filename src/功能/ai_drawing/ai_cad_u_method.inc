/* Only the stock rectangular U method is executable automatically. The
   original is inspected read-only. A recipe contains dimensions and roles,
   while source feature IDs stay in the human-readable evidence. */
static const char cad_u_recipe_marker[]=METHOD_U_RECIPE_MARKER;

static int cad_u_face_axis(const double direction[3]){
    for(int i=0;i<3;i++)if(fabs(direction[i])>=1-1e-7)return i;
    return -1;
}

static ProError cad_u_rectangular_section(ProFeature *feature){
    ProSection section=NULL;
    int *ids=NULL,count=0,lines=0;
    ProError error=ProFeatureSectionCopy(feature,0,&section);
    if(!error)error=ProSectionEntityIdsGet(section,&ids,&count);
    Pro2dLinedef sides[4]={0};
    for(int i=0;!error&&i<count;i++){
        ProBoolean projected,construction;
        error=ProSectionEntityIsProjection(section,ids[i],&projected);
        if(!error)error=ProSectionEntityIsConstruction(section,ids[i],&construction);
        if(error||projected||construction)continue;
        Pro2dEntdef *entity=NULL;
        error=ProSectionEntityGet(section,ids[i],&entity);
        if(!error&&(lines==4||!entity||entity->type!=PRO_2D_LINE))error=PRO_TK_UNSUPPORTED;
        if(!error)sides[lines++]=*(Pro2dLinedef *)entity;
    }
    if(ids)ProArrayFree((ProArray *)&ids);
    if(section)ProSectionFree(&section);
    if(error||lines!=4)return error?error:PRO_TK_UNSUPPORTED;
    for(int i=0;i<4;i++){
        int connects[2]={0};
        const double *ends[]={sides[i].end1,sides[i].end2};
        const double x=ends[1][0]-ends[0][0],y=ends[1][1]-ends[0][1];
        const double length=hypot(x,y);
        if(length<1e-7)return PRO_TK_BAD_CONTEXT;
        for(int j=0;j<4;j++)if(i!=j){
            const double *other[]={sides[j].end1,sides[j].end2};
            for(int a=0;a<2;a++)for(int b=0;b<2;b++){
                if(hypot(ends[a][0]-other[b][0],ends[a][1]-other[b][1])>1e-6)continue;
                double ox=other[1][0]-other[0][0],oy=other[1][1]-other[0][1];
                if(fabs(x*ox+y*oy)>1e-7*length*hypot(ox,oy))return PRO_TK_UNSUPPORTED;
                connects[a]++;
            }
        }
        if(connects[0]!=1||connects[1]!=1)return PRO_TK_UNSUPPORTED;
    }
    return 0;
}

static ProError cad_u_source_wall(ProFeature *feature,double mm,double *radius,
    double *height,CabinetFace *face){
    ProElement tree=NULL,angle=NULL,inner_radius=NULL;
    ProSection section=NULL;
    double actual_angle=0,from=0,to=0;
    ProError error=ProFeatureElemtreeExtract(feature,NULL,PRO_FEAT_EXTRACT_NO_OPTS,&tree);
    if(!error)error=cm_bend_elements(feature,tree,&angle,&inner_radius);
    if(!error)error=ProElementDoubleGet(angle,NULL,&actual_angle);
    if(!error)error=ProElementDoubleGet(inner_radius,NULL,radius);
    if(!error)error=fd_section_from_tree(tree,&section);
    if(!error&&!replay_stock_wall_profile(section,&from,&to,height))error=PRO_TK_UNSUPPORTED;
    if(tree)ProFeatureElemtreeFree(feature,tree);
    if(!error&&(fabs(actual_angle-90)>0.002||!isfinite(*radius)||*radius<=0||
               fabs(from*mm)>0.002||fabs(to*mm)>0.002))error=PRO_TK_UNSUPPORTED;
    if(!error)error=cm_bend_verified(feature,90,*radius,1/mm);
    if(!error)error=cm_wall_face(feature,face);
    *radius*=mm;
    *height*=mm;
    return error;
}

static ProError cad_u_learn_recipe(ProMdl source,PartText *out){
    ProMdlsubtype subtype;
    ProError error=ProMdlSubtypeGet(source,&subtype);
    if(error||subtype!=PROMDLSTYPE_PART_SHEETMETAL)return error?error:PRO_TK_INVALID_TYPE;
    ReplayOrder order={.read_candidate=1};
    error=part_health(source);
    if(!error)error=ProSolidFeatVisit((ProSolid)source,replay_read_order,NULL,&order);
    ProError cleanup=replay_pattern_source_close(&order.patterns);
    if(!error)error=cleanup;
    if(error)return error;
    int base_index=order.count==4?1:0;
    if(order.count!=base_index+3||order.derived||
       (base_index&&order.step[0].type!=PRO_FEAT_CURVE)||
       order.step[base_index].type!=PRO_FEAT_DATUM_SURF||
       order.step[base_index].conversion||
       order.step[base_index+1].type!=PRO_FEAT_WALL||order.step[base_index+2].type!=PRO_FEAT_WALL)
        return PRO_TK_UNSUPPORTED;
    ProFeature features[4]={0};
    for(int i=0;!error&&i<order.count;i++){
        if(order.step[i].group||order.step[i].members)return PRO_TK_UNSUPPORTED;
        error=ProFeatureInit((ProSolid)source,order.step[i].id,&features[i]);
    }
    if(!error)error=cad_u_rectangular_section(&features[0]);
    double mm=0,gauge=0,radii[2]={0},heights[2]={0};
    ProOutline bounds;
    CabinetFace base={0},wall[2]={0};
    if(!error)error=fab_thickness(source,&mm,&gauge);
    if(!error)error=or_outline(source,bounds);
    if(!error)error=cm_wall_face(&features[base_index],&base);
    for(int i=0;!error&&i<2;i++)
        error=cad_u_source_wall(&features[base_index+1+i],mm,&radii[i],&heights[i],&wall[i]);
    if(error)return error;
    int up=cad_u_face_axis(base.normal),across=cad_u_face_axis(wall[0].normal);
    if(up<0||across<0||up==across||across!=cad_u_face_axis(wall[1].normal)||
       fabs(radii[0]-radii[1])>0.002||fabs(heights[0]-heights[1])>0.002)
        return PRO_TK_UNSUPPORTED;
    int along=3-up-across;
    double length=(bounds[1][along]-bounds[0][along])*mm;
    double width=(bounds[1][across]-bounds[0][across])*mm;
    double height=(bounds[1][up]-bounds[0][up])*mm;
    double gap=fabs(wall[0].point[across]-wall[1].point[across])*mm;
    if(!isfinite(length)||!isfinite(width)||!isfinite(height)||!isfinite(gauge)||
       length<=0||gauge<=0||width<=2*(gauge+radii[0])||height<=gauge+radii[0]||
       fabs(height-heights[0])>0.002||gap<width-2*gauge-0.002||gap>width+0.002)
        return PRO_TK_UNSUPPORTED;

    ProVector longitudinal={0},vertical={0};
    longitudinal[along]=vertical[up]=1;
    int sign=0;
    for(int i=0;i<2;i++){
        CabinetSpan ends,side;
        error=cm_face_span(source,wall[i].id,longitudinal,&ends);
        if(!error)error=cm_face_span(source,wall[i].id,vertical,&side);
        if(error)return error;
        double lower=(side.min-base.point[up])*mm,upper=(side.max-base.point[up])*mm;
        int actual=upper>gauge+0.002&&lower>=-0.002?1:lower<-(gauge+0.002)&&upper<=0.002?-1:0;
        if(fabs((ends.max-ends.min)*mm-length)>0.002||!actual||(i&&actual!=sign))
            return PRO_TK_UNSUPPORTED;
        sign=actual;
    }

    ModelingLesson *lesson=calloc(1,sizeof(*lesson));
    if(!lesson)return PRO_TK_OUT_OF_MEMORY;
    lesson->method.kind=L'M';
    if(base_index)method_add_operation(&lesson->method,L'S');
    method_add_operation(&lesson->method,L'W');
    method_add_operation(&lesson->method,L'F');
    method_add_operation(&lesson->method,L'F');
    ProMdlName name=L"";
    error=ProMdlMdlnameGet(source,name);
    if(!error)wcsncpy_s(lesson->source,_countof(lesson->source),name,_TRUNCATE);
    PartText evidence={lesson->evidence,_countof(lesson->evidence),0};
    part_text_add(&evidence,L"%hs %.12g %.12g %.12g %.12g %.12g\n"
        L"源钣金件 %ls。以只读方式核对实际外形、板厚、两道折弯角度和半径。\n"
        L"原生建模顺序：矩形草绘 → 平面首壁 → 沿两条相对全长边分别折弯 90°。\n"
        L"原件特征编号供核对：首壁 %d，侧壁 %d、%d。\n"
        L"复用时使用空白模板；外形和板厚按新输入，两条长边按新首壁的实际几何重找。"
        L"样件内 R 与板厚一致时，默认按新板厚取内 R；否则用样件实测内 R。可指定内 R 覆盖。\n",
        cad_u_recipe_marker,length,width,height,gauge,radii[0],name,features[base_index].id,
        features[base_index+1].id,features[base_index+2].id);
    if(!error)error=method_save(lesson,out);
    if(!error)part_text_add(out,L"样件实测 %.8g × %.8g × %.8g mm，板厚 %.8g mm，内 R%.8g mm。\n",
        length,width,height,gauge,radii[0]);
    free(lesson);
    return error;
}

static int cad_u_recipe_read(const wchar_t *file,double *gauge,double *radius){
    ModelingMethod method={0};
    if(!method_header_read(file,&method)||method.kind!=L'M'||
       (wcscmp(method.sequence,L"WFF")&&wcscmp(method.sequence,L"SWFF")))return 0;
    HANDLE input=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(input==INVALID_HANDLE_VALUE)return 0;
    BY_HANDLE_FILE_INFORMATION info;
    char bytes[512];
    DWORD size=0;
    int ok=GetFileInformationByHandle(input,&info)&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        ReadFile(input,bytes,sizeof(bytes)-1,&size,NULL);
    CloseHandle(input);
    if(!ok||size<sizeof(cad_u_recipe_marker))return 0;
    bytes[size]=0;
    char *line=strchr(bytes,'\n');
    if(!line)return 0;
    while(*line=='\r'||*line=='\n')line++;
    if(strncmp(line,cad_u_recipe_marker,sizeof(cad_u_recipe_marker)-1)||
       line[sizeof(cad_u_recipe_marker)-1]!=' ')return 0;
    char *end=strchr(line,'\n');
    if(!end)return 0;
    if(end>line&&end[-1]=='\r')end--;
    *end=0;
    double length,width,height;
    int chars=0;
    if(sscanf(line+sizeof(cad_u_recipe_marker),"%lf %lf %lf %lf %lf%n",
        &length,&width,&height,gauge,radius,&chars)!=5||!chars||
       line[sizeof(cad_u_recipe_marker)+chars]||!isfinite(length)||!isfinite(width)||
       !isfinite(height)||!isfinite(*gauge)||!isfinite(*radius)||length<=0||*gauge<=0||*radius<=0||
       width<=2*(*gauge+*radius)||height<=*gauge+*radius)return 0;
    return 1;
}

static int cad_u_find_recipe(double *gauge,double *radius,wchar_t path[MAX_PATH]){
    wchar_t folder[MAX_PATH],glob[MAX_PATH];
    if(!method_directory(folder,0)||swprintf_s(glob,MAX_PATH,L"%ls\\%ls",folder,METHOD_FILE_GLOB)<0)return 0;
    WIN32_FIND_DATAW entry;
    HANDLE search=FindFirstFileW(glob,&entry);
    if(search==INVALID_HANDLE_VALUE)return 0;
    FILETIME newest={0};
    do{
        if((entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
           (path[0]&&CompareFileTime(&entry.ftLastWriteTime,&newest)<0))continue;
        wchar_t file[MAX_PATH];
        double t,r;
        if(swprintf_s(file,MAX_PATH,L"%ls\\%ls",folder,entry.cFileName)<0||
           !cad_u_recipe_read(file,&t,&r))continue;
        if(path[0]&&!CompareFileTime(&entry.ftLastWriteTime,&newest)&&wcscmp(file,path)<=0)continue;
        newest=entry.ftLastWriteTime;
        wcscpy_s(path,MAX_PATH,file);
        *gauge=t;
        *radius=r;
    }while(FindNextFileW(search,&entry));
    FindClose(search);
    return path[0]!=0;
}
