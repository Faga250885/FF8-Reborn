// Rasterize text at the drawable resolution only when the panel changes.
// A normal presentation of the open panel is one GPU-to-GPU rectangle blit.
namespace retro_panel_draw {
static Surface panel;
static std::array<int,29> last_state{};
static bool painted=false;
constexpr int update_space=34;
constexpr int row_height=29,section_gap=32;
constexpr int design_w=404,design_h=746+update_space+row_height;
static int row_top(int id,bool restore_visible){
    if(id==retro_panel::ModUpdate)return 54;
    int index=retro_panel::visual_index(id);
    if(!restore_visible&&index>retro_panel::visual_index(retro_panel::Restore))--index;
    return 78+update_space+(index-1)*row_height+
           (index>=retro_panel::visual_index(retro_panel::Profile)?section_gap:0);
}

struct Canvas {
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ old_bitmap=nullptr,old_font=nullptr;
    HFONT fonts[3]={};
    unsigned char* pixels=nullptr;
    double scale;
    int width,height;
    int px(double v) const{return int(std::lround(v*scale));}
    Canvas(int w,int h):scale(double(w)/design_w),width(w),height(h){
        dc=CreateCompatibleDC(nullptr);
        if(!dc)return;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        // Positive height gives BGRA rows in the same bottom-to-top order as GL.
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap||!bits)return;
        old_bitmap=SelectObject(dc,bitmap);
        if(!old_bitmap||old_bitmap==HGDI_ERROR)return;
        old_font=GetCurrentObject(dc,OBJ_FONT);
        const int heights[]={14,17,24},weights[]={FW_SEMIBOLD,FW_NORMAL,FW_SEMIBOLD};
        for(int i=0;i<3;i++)fonts[i]=CreateFontW(-std::max(1,px(heights[i])),0,0,0,weights[i],
            FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        SetBkMode(dc,TRANSPARENT);pixels=static_cast<unsigned char*>(bits);
    }
    ~Canvas(){
        if(old_font)SelectObject(dc,old_font);
        if(old_bitmap&&old_bitmap!=HGDI_ERROR)SelectObject(dc,old_bitmap);
        for(HFONT f:fonts)if(f)DeleteObject(f);
        if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);
    }
    void box(int x,int y,int r,int b,COLORREF colour){
        RECT rect{px(x),px(y),px(r),px(b)};
        SetDCBrushColor(dc,colour);FillRect(dc,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
    }
    void meter(int top,int value,COLORREF colour){
        box(259,top+13,315,top+16,RGB(56,73,76));
        const int end=259+int(std::lround(56*value/100.0));
        if(end>259)box(259,top+13,end,top+16,colour);
    }
    void label(const wchar_t* str,int x,int y,int r,int b,int font,COLORREF colour,bool right=false){
        RECT rect{px(x),px(y),px(r),px(b)};
        if(fonts[font])SelectObject(dc,fonts[font]);
        SetTextColor(dc,colour);
        DrawTextW(dc,str,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(right?DT_RIGHT:DT_LEFT));
    }
};

static void render(Canvas& c,const RetroSettings& s,int selected,bool bypass,bool failed,
                   retro_panel::UpdateState update=retro_panel::update_idle){
    const COLORREF body=RGB(23,31,35),border=RGB(96,119,120),accent=RGB(125,226,200);
    const COLORREF text=RGB(238,243,243),secondary=RGB(181,201,199),muted=RGB(130,146,148);
    c.box(0,0,design_w,design_h,border);c.box(1,1,design_w-1,design_h-1,body);
    c.label(L"FF8 REBORN",17,5,160,26,0,accent);
    c.label(L"Settings",16,23,270,56,2,text);
    if(bypass)c.label(L"Original view",243,18,387,47,0,secondary,true);
    using namespace retro_panel;
    const int update_top=row_top(ModUpdate,true);
    if(selected==ModUpdate){
        c.box(8,update_top,396,update_top+28,RGB(43,65,67));c.box(8,update_top,11,update_top+28,accent);
    }
    struct UpdateText {const wchar_t* action;const wchar_t* status;const wchar_t* hint;};
    static constexpr UpdateText update_text[]={
        {L"Check for updates",L"",L"Check GitHub for a newer version of FF8 Reborn."},
        {L"Checking for updates…",L"",L"You can keep playing while FF8 Reborn checks."},
        {L"Check for updates",L"Up to date",L"You have the latest version for this platform."},
        {L"Download update",L"Update available",L"Download now and use it the next time you play."},
        {L"Downloading update…",L"",L"You can keep playing during the download."},
        {L"Update ready",L"Restart to apply",L"Keep playing. The update takes effect at next launch."},
        {L"Check for updates again",L"Update failed",L"Try again or use the installer from GitHub."},
    };
    const auto& message=update_text[update];
    c.label(message.action,18,update_top,250,update_top+28,1,update_actionable(update)?text:secondary);
    c.label(message.status,249,update_top,385,update_top+28,0,update==update_error?RGB(255,185,134):accent,true);
    c.label(L"Display settings",17,52+update_space,260,76+update_space,0,accent);
    c.label(L"Auto-save",246,52+update_space,387,76+update_space,0,secondary,true);
    const int filter_top=row_top(Profile,true)-section_gap;
    c.box(16,filter_top+1,388,filter_top+2,RGB(61,77,80));
    c.label(L"Screen filter",17,filter_top+3,388,filter_top+29,0,accent);
    const bool restore_visible=can_restore(s);
    if(dirty||active_profile==custom){
        std::wstring base=std::wstring(L"Based on ")+(base_profile()==8?L"FF8 Reborn":names[base_profile()]);
        c.label(base.c_str(),122,filter_top+3,387,filter_top+29,0,secondary,true);
    }
    const wchar_t* labels[]={L"Profile",L"Vertical resolution",L"Pixelation",L"Scanlines",
                              L"RGB mask",L"Glow",L"PS1 dithering",L"Edge smoothing",L"Motion blur"};
    const int values[]={0,s.lines,s.pixelation,s.scanlines,s.mask,s.glow,s.dither,s.edge_softness,s.motion_blur};
    for(int i=Profile;i<=MotionBlur;i++){
        const int top=row_top(i,restore_visible);
        const bool current=selected==i;
        const bool editable=retro_panel::editable(s.mode,i);
        if(current){c.box(8,top,396,top+28,RGB(43,65,67));c.box(8,top,11,top+28,accent);}
        c.label(labels[i],18,top,226,top+28,1,editable?text:muted);
        if(!i){
            const int profile=retro_panel::dirty?retro_panel::custom:retro_panel::active_profile;
            c.label(retro_panel::names[profile],111,top,385,top+28,1,accent,true);
        }else{
            wchar_t value[30];
            if(i==MotionBlur&&values[i]==0)wcscpy(value,L"Off");
            else swprintf(value,30,i==Lines?L"%d lines":L"%d %%",values[i]);
            if(i!=1)c.meter(top,values[i],editable?accent:muted);
            c.label(value,318-(i==1?42:0),top,385,top+28,1,editable?secondary:muted,true);
        }
    }
    const wchar_t* extra[]={L"Filter type",L"Full-screen map",L"Aspect ratio",L"Frame rate",L"Blurred sidebars",L"FPS counter",L"TV phosphors",L"Phosphor strength",L"Phosphor sharpness",
                            active_profile==custom?L"Save my changes":custom_exists?L"Save to my custom profile":L"Create a custom copy",
                            L"Restore filter defaults"};
    const wchar_t* counter_sizes[]={L"Off",L"Small",L"Medium",L"Large"};
    const wchar_t* screens[]={L"Use filter profile",L"Trinitron grille",L"RCA slot mask",L"RCA dot mask"};
    std::wstring strength=std::to_wstring(s.phosphor_strength)+L" %",sharpness=std::to_wstring(s.phosphor_sharpness)+L" %";
    const std::wstring fps=display_settings.fps?std::to_wstring(display_settings.fps):L"Original";
    const wchar_t* value[]={s.mode==0?L"Off":s.mode==1?L"Pixelated":L"CRT",display_settings.atlas?L"Reborn":L"Original",
        display_settings.wide?L"Widescreen":L"Original (4:3)",fps.c_str(),
        display_settings.field_sides?L"On":L"Off",counter_sizes[display_settings.fps_counter],screens[s.tv_screen],strength.c_str(),sharpness.c_str()};
    for(int i=FilterType;i<=Restore;i++){
        if(i==Restore&&!restore_visible)continue;
        const int top=row_top(i,restore_visible);
        if(selected==i){c.box(8,top,396,top+28,RGB(43,65,67));c.box(8,top,11,top+28,accent);}
        c.label(extra[i-FilterType],18,top,385,top+28,1,text);
        if(i<=PhosphorSharpness)c.label(value[i-FilterType],220,top,385,top+28,1,accent,true);
    }
    const int footer=row_top(SaveCustom,restore_visible)+32;
    c.box(16,footer,388,footer+1,RGB(61,77,80));
    wchar_t detail[96]={};
    if(selected==ModUpdate&&update_detail){
        update_detail(detail,sizeof(detail)/sizeof(detail[0]));
        detail[sizeof(detail)/sizeof(detail[0])-1]=0;
    }
    std::wstring state=selected==ModUpdate?(detail[0]?detail:message.hint):
        failed?L"Couldn’t save your changes. Try again.":
        selected==MotionBlur?L"Softens movement across gameplay in 10% steps.":
        selected==Restore?L"Restore the original settings for this filter.":
        selected==SaveCustom&&active_profile!=custom&&custom_exists?L"This will replace your saved custom profile.":
        needs_choice?L"Save your changes or press Esc / B to discard.":
        active_profile==custom?L"Changes are saved to your custom profile.":
        L"Use this filter as the starting point for your copy.";
    const bool warning=selected==ModUpdate?update==update_error:failed;
    c.label(state.c_str(),13,footer+4,393,footer+27,0,warning?RGB(255,185,134):secondary);
    c.label(L"Arrows / D-pad: adjust · Enter / A: select",13,footer+29,393,footer+51,0,text);
    c.label(L"F8 / Select + Start: close · Esc / B: cancel",13,footer+51,393,footer+75,0,text);
}
static bool draw(const RetroSettings& settings,int selected,GLuint target,int w,int h,bool bypass=false){
    if(w<24||h<24)return false;
    const int height=design_h-(retro_panel::can_restore(settings)?0:row_height);
    const int counter_height=fps_counter::tile_h*fps_counter::scale(display_settings.fps_counter,w,h);
    if(h-counter_height<24)return false;
    const double scale=std::min({std::clamp(h/900.,.8,3.0),
                                 double(w)/(design_w+32),double(h-counter_height)/(design_h+32)});
    const int pw=int(std::lround(design_w*scale)),ph=int(std::lround(height*scale));
    const int margin=int(std::lround(16*scale));
    const bool failed=retro_panel::save_failed();
    const auto update=retro_panel::update_status?retro_panel::update_status():retro_panel::update_idle;
    const decltype(last_state) state={pw,ph,selected,bypass,failed,settings.mode,settings.lines,settings.pixelation,
                                    settings.scanlines,settings.mask,settings.glow,settings.dither,settings.edge_softness,settings.ps1_raster,retro_panel::active_profile,retro_panel::dirty,retro_panel::needs_choice,display_settings.fps,display_settings.atlas,display_settings.wide,display_settings.field_sides,retro_panel::custom_exists,retro_panel::custom_base,settings.tv_screen,settings.phosphor_strength,settings.phosphor_sharpness,display_settings.fps_counter,update,
                                    settings.motion_blur};
    if(!painted||last_state!=state){
        Canvas canvas(pw,ph);
        if(!canvas.pixels)return false;
        render(canvas,settings,selected,bypass,failed,update);
        GdiFlush();
        for(size_t at=3;at<size_t(pw)*ph*4;at+=4)canvas.pixels[at]=255;
        try{retro_display::allocate(panel,pw,ph);}
        catch(const std::exception&){return false;}
        PixelTransferGuard transfer;
        GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glBindTexture(GL_TEXTURE_2D,panel.color);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,pw,ph,GL_BGRA,GL_UNSIGNED_BYTE,canvas.pixels);
        glBindTexture(GL_TEXTURE_2D,texture);
        last_state=state;painted=true;
    }
    retro_display::inspect_context();
    retro_display::TargetState state_guard(target,false);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,panel.fbo);
    pBlitFramebuffer(0,0,pw,ph,margin,h-counter_height-margin-ph,margin+pw,h-counter_height-margin,
                     GL_COLOR_BUFFER_BIT,GL_NEAREST);
    return true;
}
static void forget(){panel={};painted=false;}
}
