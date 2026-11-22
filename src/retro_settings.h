// Optional output filter. Integer controls are shared with the installer.
struct RetroSettings {
    int mode=0, lines=240, pixelation=85, scanlines=35, mask=20, glow=10, dither=20, edge_softness=0;
    bool ps1_raster=false, hd_blend=false;
    enum Tube { standard, aperture_grille, slot_mask, dot_triad };
    Tube phosphors=standard;
    int tv_screen=0,phosphor_strength=100,phosphor_sharpness=70,motion_blur=0;
    static constexpr int hd_profile=7;
    static constexpr int min_lines=120,max_lines=720,line_step=5;
    int visible_lines(int active_rows)const{
        return ps1_raster?(lines*active_rows+120)/240:lines;
    }
    static constexpr RetroSettings with_blur(RetroSettings filter,int strength){
        filter.motion_blur=strength;
        return filter;
    }
    static constexpr RetroSettings recommended(){return with_blur({2,320,100,20,20,8,70,40},20);}

    bool active() const {
        return (mode==1 || mode==2) &&
            (pixelation || dither || edge_softness || (mode==2 && (scanlines || mask || glow)));
    }
    void load(const wchar_t* path,const wchar_t* section=L"Retro",int base_profile=-1) {
        wchar_t configured[32]={};
        const bool has_mode=GetPrivateProfileStringW(section,L"Mode",L"",configured,32,path)!=0;
        const RetroSettings defaults=has_mode?RetroSettings{}:recommended();
        auto read=[&](const wchar_t* key,int fallback,int low,int high) {
            wchar_t text[32]={};
            if(!GetPrivateProfileStringW(section,key,L"",text,32,path))return fallback;
            wchar_t* end=nullptr;
            const long value=wcstol(text,&end,10);
            if(end==text || *end)return fallback;
            return int(std::clamp(value,long(low),long(high)));
        };
        mode=read(L"Mode",defaults.mode,INT_MIN,INT_MAX);
        if(mode!=1 && mode!=2)mode=0;
        const int saved_lines=read(L"Lines",defaults.lines,min_lines,max_lines);
        lines=((saved_lines+line_step/2)/line_step)*line_step;
        pixelation=read(L"Pixelation",defaults.pixelation,0,100);
        scanlines=read(L"Scanlines",defaults.scanlines,0,100);
        mask=read(L"Mask",defaults.mask,0,100);
        glow=read(L"Glow",defaults.glow,0,100);
        dither=read(L"Dither",defaults.dither,0,100);
        edge_softness=read(L"EdgeSoftness",defaults.edge_softness,0,100);
        int old_blur=0;
        if(!wcscmp(section,L"Retro")&&!GetPrivateProfileStringW(section,L"MotionBlur",L"",configured,32,path)){
            for(const wchar_t* key:{L"MotionBlur3D",L"MotionBlurBackground",L"CRTPersistence"})
                old_blur=std::max(old_blur,std::clamp(int(GetPrivateProfileIntW(L"Display",key,0,path)),0,100));
        }
        motion_blur=((read(L"MotionBlur",old_blur,0,100)+5)/10)*10;
        ps1_raster=read(L"PS1Raster",0,0,1)!=0;
        phosphors=Tube(read(L"Phosphors",standard,standard,dot_triad));
        const bool legacy=!wcscmp(section,L"Retro") && !GetPrivateProfileStringW(section,L"TVScreen",L"",configured,32,path);
        const wchar_t* screen_section=legacy?L"Display":section;
        tv_screen=std::clamp(int(GetPrivateProfileIntW(screen_section,L"TVScreen",0,path)),0,3);
        phosphor_strength=std::clamp(int(GetPrivateProfileIntW(screen_section,L"PhosphorStrength",100,path)),0,100);
        phosphor_sharpness=std::clamp(int(GetPrivateProfileIntW(screen_section,L"PhosphorSharpness",70,path)),0,100);
        if(base_profile<0){
            base_profile=read(L"Selected",-1,-1,10);
            if(base_profile==9)base_profile=read(L"CustomBase",8,0,8);
        }
        // Legacy INIs without a selected slot identified factory presets by their values.
        hd_blend=base_profile==hd_profile||(base_profile==-1&&mode==1&&!ps1_raster&&
            lines==480&&pixelation==32&&dither==6&&edge_softness==70);
    }
};
