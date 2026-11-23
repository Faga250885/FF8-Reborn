// The settings UI is rasterized once at its native pixel grid, then composited
// over the game. Only changes to rows, drawable tuning or update progress repaint it.
namespace retro_panel_draw {
#include "ffvi_menu_canvas.h"
static Surface panel;
static std::array<int,33> last_state{};
static bool painted=false;
constexpr int display_width=241,filter_width=262,side_width=104;
constexpr int main_y=12,first_y=24,filter_row=17,display_row=20;
constexpr int left=24,output_height=15,version_height=31,footer_height=40;
static int main_width(retro_panel::Section page){
    return page==retro_panel::Section::Display?display_width:filter_width;
}
static int design_width(retro_panel::Section page){return main_width(page)+side_width+1;}
static int row_top(int id,bool restore_visible){
    using namespace retro_panel;
    const auto d=std::find(std::begin(display_order),std::end(display_order),id);
    if(d!=std::end(display_order))return first_y+int(d-display_order)*display_row;
    const auto f=std::find(std::begin(filter_order),std::end(filter_order),id);
    const int index=int(f-filter_order)-(!restore_visible&&id==SaveCustom?1:0);
    return first_y+index*filter_row+(id==Profile?0:output_height);
}
constexpr int tuning_top=first_y+filter_row;
static int footer_top(retro_panel::Section page,bool restore_visible){
    using namespace retro_panel;
    return page==Section::Display?row_top(ModUpdate,false)+display_row+version_height+5:
                                  row_top(SaveCustom,restore_visible)+filter_row+5;
}
static int design_height(retro_panel::Section page,bool restore){
    return footer_top(page,restore)+footer_height+2;
}
struct Placement {int x,y,w,h;};
static Placement place(retro_panel::Section page,bool restore,int w,int h,int counter){
    // Use whole output pixels wherever the screen fits them. A small window
    // can still show the whole menu. Both sections share the same pixel scale.
    const int available_height=h-counter;
    const double fit=std::min(w/double(design_width(retro_panel::Section::ScreenFilter)+16),
        available_height/double(design_height(retro_panel::Section::ScreenFilter,true)+16));
    const double whole=std::max(1.,std::round(h/400.));
    const double scale=fit>=1.?std::min(whole,std::floor(fit)):fit;
    const int margin=int(std::lround(8*scale));
    const int pw=int(std::lround(design_width(page)*scale)),ph=int(std::lround(design_height(page,restore)*scale));
    return {margin,h-counter-margin-ph,pw,ph};
}
struct UpdateMeter {
    int percent=-1, transferred_kib=0, sweep=0;
    bool operator==(const UpdateMeter& v)const{
        return percent==v.percent&&transferred_kib==v.transferred_kib&&sweep==v.sweep;
    }
};
static UpdateMeter last_meter;
static ULONGLONG last_meter_sample=0;
static UpdateMeter update_meter(retro_panel::UpdateState update,ULONGLONG tick){
    using namespace retro_panel;
    UpdateMeter meter;
    if(update==update_ready){meter.percent=100;return meter;}
    if(update==update_downloading){
        unsigned received=0,total=0;
        if(update_bytes)update_bytes(received,total);
        if(total){
            meter.percent=int(std::min<uint64_t>(100,uint64_t(received)*100/total));
            return meter;
        }
        meter.transferred_kib=int(received/1024);
    }else if(update!=update_verifying&&update!=update_saving)return meter;
    const int phase=int((tick/150)%24);
    meter.sweep=phase<=12?phase:24-phase;
    return meter;
}
struct UpdateText {const wchar_t* action;const wchar_t* status;const wchar_t* hint;};
static constexpr UpdateText update_text[]={
    {L"Check for updates",L"",L"Check GitHub for a newer version."},
    {L"Checking for updates…",L"",L"You can keep playing while the check runs."},
    {L"Check for updates",L"Up to date",L"You have the latest version for this platform."},
    {L"Download update",L"Update available",L"Download now to update at the next launch."},
    {L"Downloading update…",L"",L"You can keep playing during the download."},
    {L"Update ready",L"Restart to apply",L"The update takes effect at the next launch."},
    {L"Check for updates again",L"Update failed",L"Try again or use the installer from GitHub."},
    {L"Verifying update…",L"",L"Checking the downloaded file and its checksum."},
    {L"Saving update…",L"",L"Preparing the verified update for the next launch."}
};
static void row_label(Canvas& c,int id,int selected,int y,int height,const wchar_t* title,
                      const wchar_t* value,int right,COLORREF title_colour=mint,COLORREF value_colour=ink){
    if(id==selected)c.cursor(5,y+(height-16)/2);
    int end=right;
    if(value&&*value){
        const int value_start=right-text_width(value);
        end=value_start-5;
        c.label(value,std::max(left,value_start),y,right,y+height,0,value_colour,true);
    }
    c.label(title,left,y,end,y+height,0,title_colour);
}
static void draw_update(Canvas& c,int selected,retro_panel::UpdateState update,UpdateMeter meter){
    using namespace retro_panel;
    const auto& message=update_text[update];
    const int y=row_top(ModUpdate,false),right=display_width-9;
    wchar_t progress[32]{};
    if(update==update_downloading){
        if(meter.percent>=0)swprintf(progress,std::size(progress),L"%d%%",meter.percent);
        else if(meter.transferred_kib<1024)swprintf(progress,std::size(progress),L"%d KiB",meter.transferred_kib);
        else swprintf(progress,std::size(progress),L"%.1f MiB",meter.transferred_kib/1024.);
    }
    row_label(c,ModUpdate,selected,y,display_row,message.action,progress[0]?progress:message.status,
              right,update_actionable(update)?mint:muted,update==update_error?warning:ink);
#ifdef GEO_NATIVE_CAPTURE
    const auto versions=mod_update::versions();
#else
    const std::wstring versions=L"Installed "+std::wstring(update_version::tag,update_version::tag+sizeof(update_version::tag)-1);
#endif
    c.paragraph(versions,9,y+display_row,right,2,secondary);
    if(update==update_downloading||update==update_verifying||update==update_saving||update==update_ready){
        const int top=y+display_row+version_height-3;
        c.box(9,top,right,top+3,track);
        if(meter.percent>=0)c.box(9,top,9+(right-9)*meter.percent/100,top+3,mint);
        else{
            const int pos=9+(right-9-32)*meter.sweep/12;
            c.box(pos,top,pos+32,top+3,mint);
        }
    }
}
static void draw_display(Canvas& c,int selected,retro_panel::UpdateState update,UpdateMeter meter){
    using namespace retro_panel;
    static constexpr const wchar_t* counter_sizes[]={L"Off",L"Small",L"Medium",L"Large"};
    const std::wstring fps=display_settings.fps?std::to_wstring(display_settings.fps)+L" FPS":L"Original";
    const wchar_t* labels[]={L"Full-screen map",L"Aspect ratio",L"Frame rate",L"Blurred sidebars",L"FPS counter"};
    const wchar_t* values[]={display_settings.atlas?L"Reborn":L"Original",display_settings.wide?L"Widescreen":L"Original (4:3)",
        fps.c_str(),display_settings.field_sides?L"On":L"Off",counter_sizes[display_settings.fps_counter]};
    for(int i=Map;i<=Counter;++i)
        row_label(c,i,selected,row_top(i,false),display_row,labels[i-Map],values[i-Map],display_width-9);
    draw_update(c,selected,update,meter);
}
static void draw_filter(Canvas& c,const RetroSettings& s,int selected,int w,int h){
    using namespace retro_panel;
    const bool restore=can_restore(s);
    const wchar_t* labels[]={L"Profile",L"Vertical resolution",L"Pixelation",L"Scanlines",L"RGB mask",L"Glow",
        L"PS1 dithering",L"Edge smoothing",L"Motion blur",L"Filter type"};
    static constexpr const wchar_t* screens[]={L"Use filter profile",L"Trinitron grille",L"RCA slot mask",L"RCA dot mask"};
    const int values[]={0,s.effective_lines(w,h,base_profile()),s.pixelation,s.scanlines,s.mask,s.glow,s.dither,s.edge_softness,s.motion_blur};
    const int right=filter_width-9;
    wchar_t output[72];
    swprintf(output,std::size(output),L"%ls · %d×%d",filter_resolution::label(filter_resolution::detect(w,h)),w,h);
    c.label(L"Output tuning",left,tuning_top,108,tuning_top+output_height,0,muted);
    c.label(output,110,tuning_top,right,tuning_top+output_height,0,secondary,true);
    for(Row id:filter_order){
        if(id==Restore&&!restore)continue;
        const int y=row_top(id,restore);
        const bool active=id>MotionBlur||editable(s.mode,id);
        const wchar_t* title=nullptr;
        const wchar_t* value=nullptr;
        wchar_t formatted[32];
        int percentage=-1;
        if(id<=FilterType){
            title=labels[id];
            if(id==Profile)value=dirty?L"Custom (preview)":names[active_profile];
            else if(id==FilterType)value=s.mode==2?L"CRT":s.mode==1?L"Pixelated":L"Off";
            else if(id==Lines){
                swprintf(formatted,std::size(formatted),s.auto_lines?L"Auto · %d lines":L"%d lines",values[id]);value=formatted;
            }else percentage=values[id];
        }else if(id==TVScreen){title=L"TV phosphors";value=screens[s.tv_screen];}
        else if(id==PhosphorStrength){title=L"Phosphor strength";percentage=s.phosphor_strength;}
        else if(id==PhosphorSharpness){title=L"Phosphor sharpness";percentage=s.phosphor_sharpness;}
        else if(id==Restore)title=L"Restore filter defaults";
        else title=active_profile==custom?L"Save my changes":custom_exists?L"Save to my custom profile":L"Create a custom copy";
        if(percentage>=0){
            if(id==MotionBlur&&!percentage)value=L"Off";
            else{swprintf(formatted,std::size(formatted),L"%d%%",percentage);value=formatted;}
        }
        row_label(c,id,selected,y,filter_row,title,value,right,active?mint:muted,active?ink:muted);
    }
}
static std::wstring help(int selected,const RetroSettings& s,retro_panel::UpdateState update,bool failed){
    using namespace retro_panel;
    if(failed)return L"Couldn’t save your changes. Try again.";
    if(selected==ModUpdate){
        wchar_t detail[96]{};
        if(update_detail)update_detail(detail,std::size(detail));
        detail[std::size(detail)-1]=0;
        return detail[0]?detail:update_text[update].hint;
    }
    if(selected==Tabs)return L"← → Choose a section. Enter / A opens it.";
    switch(selected){
        case Aspect:return L"Set the game screen format.";
        case FPS:return L"Set the presentation frame rate.";
        case Map:return L"Choose the artwork for the full-screen map.";
        case Sides:return L"Blend the sides of pre-rendered scenes.";
        case Counter:return L"Set the size of the FPS counter.";
        case Lines:return L"Adjust by 5 lines. Enter / A enables Auto.";
        case Restore:return L"Preview the original settings for this filter.";
        case SaveCustom:return custom_exists?L"Replace your saved custom filter.":L"Save a custom copy of this filter.";
        case Profile:
            if(dirty)return L"Changing profiles discards your preview.";
            if(active_profile==custom)return std::wstring(L"Based on ")+names[base_profile()];
            return L"Choose a profile or edit it to create a copy.";
        default:
            if(selected<=MotionBlur&&!editable(s.mode,selected))return L"This control is inactive for the current filter type.";
            return L"Save a custom copy to keep your changes.";
    }
}
static void render(Canvas& c,const RetroSettings& s,int selected,bool bypass,bool failed,
                   retro_panel::UpdateState update,UpdateMeter meter,int w,int h){
    using namespace retro_panel;
    const auto page=section_view();
    const bool filter=page==Section::ScreenFilter;
    const int mw=main_width(page),footer=footer_top(page,can_restore(s));
    const wchar_t* heading=filter?L"Screen filter":L"Display";
    c.window(0,main_y,mw,footer-main_y);
    c.window(6,0,text_width(heading)+18,21);
    c.label(heading,15,4,mw-9,17,0,ink);
    if(filter)draw_filter(c,s,selected,w,h);else draw_display(c,selected,update,meter);
    c.window(0,footer,mw,footer_height);
    c.paragraph(help(selected,s,update,failed),9,footer+6,mw-9,2,
                failed||(selected==ModUpdate&&update==update_error)?warning:ink);

    c.window(mw,main_y,side_width,47);
    for(int section=0;section<2;++section){
        const bool active=(section==1)==filter;
        const int top=main_y+6+section*17;
        if(selected==Tabs&&active)c.cursor(mw+4,top);
        c.label(section?L"Screen filter":L"Display",mw+(selected==Tabs?24:10),top,mw+side_width-5,top+16,0,active?mint:ink);
    }
    static constexpr const wchar_t* keys[][2]={{L"Tab / LB / RB",L"Section"},{L"↑ ↓",L"Navigate"},
        {L"← →",L"Adjust"},{L"Enter / A",L"Select"},{L"Esc / B",L"Close"}};
    const int ky=main_y+47;
    c.window(mw,ky,side_width,140);
    for(int i=0;i<5;++i){
        const int y=ky+6+i*27;
        c.label(keys[i][0],mw+9,y,mw+side_width-5,y+12,0,mint);
        c.label(keys[i][1],mw+9,y+12,mw+side_width-5,y+24,0,ink);
    }
    if(bypass){
        c.window(mw,ky+140,side_width,20);
        c.label(L"Original view",mw+9,ky+142,mw+side_width-4,ky+158,0,mint);
    }
}
static bool draw(const RetroSettings& settings,int selected,GLuint target,int w,int h,bool bypass=false){
    if(w<24||h<24)return false;
    const auto page=retro_panel::section_view();
    const bool restore=retro_panel::can_restore(settings);
    const int pw=design_width(page),ph=design_height(page,restore);
    const int counter=fps_counter::tile_h*fps_counter::scale(display_settings.fps_counter,w,h);
    if(h-counter<24)return false;
    const auto position=place(page,restore,w,h,counter);
    const bool failed=retro_panel::save_failed();
    const auto update=page==retro_panel::Section::Display&&retro_panel::update_status?
        retro_panel::update_status():retro_panel::update_idle;
    const decltype(last_state) state={pw,ph,selected,bypass,failed,settings.mode,settings.lines,settings.pixelation,
                                    settings.scanlines,settings.mask,settings.glow,settings.dither,settings.edge_softness,settings.ps1_raster,retro_panel::active_profile,retro_panel::dirty,settings.phosphors,display_settings.fps,display_settings.atlas,display_settings.wide,display_settings.field_sides,retro_panel::custom_exists,retro_panel::custom_base,settings.tv_screen,settings.phosphor_strength,settings.phosphor_sharpness,display_settings.fps_counter,update,
                                    settings.motion_blur,w,h,settings.auto_lines,int(page)};
    UpdateMeter meter=last_meter;
    const bool changed=!painted||last_state!=state;
    const bool active_update=page==retro_panel::Section::Display&&(update==retro_panel::update_downloading||
                             update==retro_panel::update_verifying||update==retro_panel::update_saving);
    if(changed||active_update){
        const auto tick=GetTickCount64();
        if(changed||tick-last_meter_sample>=150){
            meter=update_meter(update,tick);
            last_meter_sample=tick;
        }
    }
    if(changed||!(last_meter==meter)){

        Canvas canvas(pw,ph,pw);
        if(!canvas.pixels)return false;
        render(canvas,settings,selected,bypass,failed,update,meter,w,h);
        if(!upload(panel,canvas))return false;
        last_state=state;last_meter=meter;painted=true;
    }
    return blit(panel,target,position.x,position.y,position.w,position.h,1.f);
}
static void forget(){panel={};painted=false;}
}
