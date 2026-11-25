// The settings UI is rasterized once at its native pixel grid, then composited
// over the game. Only changes to rows, drawable tuning or update progress repaint it.
namespace retro_panel_draw {
#include "ffvi_menu_canvas.h"
static Surface panel;
static std::array<int,52> last_state{};
static unsigned last_update_revision=0;
static bool painted=false;
constexpr int display_width=256,filter_width=256,side_width=126;
constexpr int main_y=26,first_y=32,filter_row=17,display_row=17;
constexpr int left=24,output_height=15,footer_height=40;
constexpr int section_height=22,keys_height=112,menu_gap=4;
static constexpr const wchar_t* section_names[]={L"Display",L"Screen filter",L"Menu settings"};
static int main_width(retro_panel::Section page){
    return page==retro_panel::Section::ScreenFilter?filter_width:display_width;
}
static int design_width(retro_panel::Section page){return main_width(page)+menu_gap+side_width;}
static int row_top(int id,bool restore_visible){
    using namespace retro_panel;
    const auto m=std::find(std::begin(menu_order),std::end(menu_order),id);
    if(m!=std::end(menu_order))return first_y+int(m-menu_order)*display_row;
    const auto d=std::find(std::begin(display_order),std::end(display_order),id);
    if(d!=std::end(display_order))return first_y+int(d-display_order)*display_row;
    const auto f=std::find(std::begin(filter_order),std::end(filter_order),id);
    const int index=int(f-filter_order)-(!restore_visible&&id==SaveCustom?1:0);
    return first_y+index*filter_row+(id==Profile?0:output_height);
}
constexpr int tuning_top=first_y+filter_row;
static int footer_top(retro_panel::Section page,bool restore_visible){
    using namespace retro_panel;
    if(page==Section::MenuSettings)return first_y+int(std::size(menu_order))*display_row+5;
    return page==Section::Display?row_top(InstalledVersion,false)+display_row+5:
                                  row_top(SaveCustom,restore_visible)+filter_row+5;
}
static int design_height(retro_panel::Section page,bool restore,bool bypass=false){
    const int side_bottom=main_y+keys_height+(bypass?20:0);
    return std::max(footer_top(page,restore)+footer_height,side_bottom)+2;
}
struct Placement {int x,y,w,h,canvas_height;};
static Placement place(retro_panel::Section page,bool restore,int w,int h,int counter,bool bypass=false){
    // At 100% the complete menu retains its original whole-pixel scale.
    // Increasing the text size can page the filter list; its footer and the
    // navigation legend stay in view. All sections use the same magnification.
    const int available_height=h-counter;
    const double width_fit=w/double(design_width(retro_panel::Section::ScreenFilter)+16);
    const double fit=std::min(width_fit,
        available_height/double(design_height(retro_panel::Section::ScreenFilter,true)+16));
    const double whole=std::max(1.,std::round(h/400.));
    const double normal=fit>=1.?std::min(whole,std::floor(fit)):fit;
    const int sidebar=main_y+keys_height+(bypass?20:0)+2;
    const double scale=std::min({normal*(display_settings.menu_scale/100.),width_fit,available_height/double(sidebar+16)});
    const int visible_height=std::min(design_height(page,restore,bypass),int(std::floor(available_height/scale+1e-6))-16);
    const int margin=int(std::lround(8*scale));
    const int pw=int(std::lround(design_width(page)*scale)),ph=int(std::lround(visible_height*scale));
    return {margin,h-counter-margin-ph,pw,ph,visible_height};
}
struct FilterPage {int offset;bool more;};
static FilterPage filter_page(int selected,bool restore,int footer){
    using namespace retro_panel;
    const int last=row_top(SaveCustom,restore)+filter_row;
    if(last<=footer-5)return {0,false};
    const int target=row_top(selected,restore);
    int offset=0;
    for(Row id:filter_order){
        if(id==Restore&&!restore)continue;
        const int top=row_top(id,restore);
        if(top-offset+filter_row>footer-5){
            if(target<top)return {offset,true};
            offset=top-first_y;
        }
    }
    return {offset,false};
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
static const UpdateText english_update_text[]={
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
static UpdateText update_message(retro_panel::UpdateState update){
    const auto& message=english_update_text[update];
    return {menu_locale::text(message.action),menu_locale::text(message.status),menu_locale::text(message.hint)};
}
static void row_label(Canvas& c,int id,int selected,int y,int height,const wchar_t* title,
                      const wchar_t* value,int right,COLORREF title_colour=ink,COLORREF value_colour=ink,int indent=0){
    if(id==selected&&(!retro_panel::choosing_section||retro_panel::confirm_close||retro_panel::update_choice_open))
        c.cursor(5,y+(height-16)/2);
    int end=right;
    if(value&&*value){
        const int value_start=right-text_width(value);
        end=value_start-5;
        c.label(value,std::max(left,value_start),y,right,y+height,0,value_colour,true);
    }
    c.label(title,left+indent,y,end,y+height,0,title_colour);
}
static void draw_update(Canvas& c,int selected,retro_panel::UpdateState update,UpdateMeter meter,const std::wstring& available){
    using namespace retro_panel;
    const auto message=update_message(update);
    const int y=row_top(ModUpdate,false),right=display_width-9;
    wchar_t progress[32]{};
    if(update==update_downloading){
        if(meter.percent>=0)swprintf(progress,std::size(progress),L"%d%%",meter.percent);
        else if(meter.transferred_kib<1024)swprintf(progress,std::size(progress),L"%d KiB",meter.transferred_kib);
        else swprintf(progress,std::size(progress),L"%.1f MiB",meter.transferred_kib/1024.);
    }
    const bool tag_fits=update==update_available&&!available.empty()&&
        text_width(message.action)+5+text_width(available.c_str())<=right-left;
    row_label(c,ModUpdate,selected,y,display_row,message.action,tag_fits?available.c_str():progress[0]?progress:message.status,
              right,update_actionable(update)?ink:muted,update==update_error?warning:ink);
    static const std::wstring installed(update_version::tag,update_version::tag+sizeof(update_version::tag)-1);
    row_label(c,InstalledVersion,selected,row_top(InstalledVersion,false),display_row,menu_locale::text(L"Installed version"),installed.c_str(),right);
    if(update==update_downloading||update==update_verifying||update==update_saving||update==update_ready){
        const int top=y+display_row-3;
        c.box(left,top,right,top+3,track);
        if(meter.percent>=0)c.box(left,top,left+(right-left)*meter.percent/100,top+3,mint);
        else{
            const int pos=left+(right-left-32)*meter.sweep/12;
            c.box(pos,top,pos+32,top+3,mint);
        }
    }
}
static void draw_display(Canvas& c,int selected,retro_panel::UpdateState update,UpdateMeter meter,const std::wstring& available){
    using namespace retro_panel;
    const wchar_t* counter_sizes[]={menu_locale::text(L"Off"),menu_locale::text(L"Small"),menu_locale::text(L"Medium"),menu_locale::text(L"Large")};
    wchar_t automatic_fps[32];
    swprintf(automatic_fps,std::size(automatic_fps),menu_locale::text(L"Auto · %d FPS"),display_settings.fps);
    const std::wstring fps=display_settings.fps_setting==DisplaySettings::auto_fps?automatic_fps:
        unlimited_rendering()?menu_locale::text(L"Unlimited"):display_settings.fps?std::to_wstring(display_settings.fps)+L" FPS":menu_locale::text(L"Original");
    const wchar_t* labels[]={menu_locale::text(L"Full-screen map"),menu_locale::text(L"Aspect ratio"),menu_locale::text(L"Frame rate"),menu_locale::text(L"Blurred sidebars"),menu_locale::text(L"FPS counter")};
    const wchar_t* values[]={display_settings.atlas?L"Reborn":menu_locale::text(L"Original"),display_settings.wide?menu_locale::text(L"Widescreen"):menu_locale::text(L"Original (4:3)"),
        fps.c_str(),display_settings.field_sides?menu_locale::text(L"On"):menu_locale::text(L"Off"),counter_sizes[display_settings.fps_counter]};
    for(int i=Map;i<=Counter;++i)
        row_label(c,i,selected,row_top(i,false),display_row,labels[i-Map],values[i-Map],display_width-9);
    draw_update(c,selected,update,meter,available);
}
static void draw_filter(Canvas& c,const RetroSettings& s,int selected,int w,int h,int footer){
    using namespace retro_panel;
    const bool restore=can_restore(s);
    const wchar_t* labels[]={menu_locale::text(L"Profile"),menu_locale::text(L"Vertical resolution"),menu_locale::text(L"Pixelation"),menu_locale::text(L"Scanlines"),menu_locale::text(L"RGB mask"),menu_locale::text(L"Glow"),
        menu_locale::text(L"PS1 dithering"),menu_locale::text(L"Edge smoothing"),menu_locale::text(L"Motion blur")};
    const wchar_t* screens[]={L"",menu_locale::text(L"Trinitron grille"),menu_locale::text(L"RCA slot mask"),menu_locale::text(L"RCA dot mask")};
    const int values[]={0,s.effective_lines(w,h,base_profile()),s.pixelation,s.scanlines,s.mask,s.glow,s.dither,s.edge_softness,s.motion_blur};
    const int right=filter_width-9;
    wchar_t output[72];
    swprintf(output,std::size(output),L"%d×%d",w,h);
    const auto view=filter_page(selected,restore,footer);
    if(!view.offset){
        c.label(menu_locale::text(L"Output tuning"),left,tuning_top,right-text_width(output)-5,tuning_top+output_height,0,muted);
        c.label(output,110,tuning_top,right,tuning_top+output_height,0,secondary,true);
    }
    if(view.offset||view.more)
        c.label(view.offset?(view.more?L"↑ ↓":L"↑"):L"↓",right-30,main_y+1,right,first_y,0,ink,true);
    for(Row id:filter_order){
        if(id==Restore&&!restore)continue;
        const int y=row_top(id,restore)-view.offset;
        if(y<first_y||y+filter_row>footer-5)continue;
        const bool active=editable(s,id);
        const wchar_t* title=nullptr;
        const wchar_t* value=nullptr;
        wchar_t formatted[32];
        int percentage=-1;
        if(id<=MotionBlur){
            title=labels[id];
            if(id==Profile)value=dirty?menu_locale::text(L"Custom (preview)"):profile_name(active_profile);
            else if(id==Lines){
                swprintf(formatted,std::size(formatted),s.auto_lines?menu_locale::text(L"Auto · %d lines"):menu_locale::text(L"%d lines"),values[id]);value=formatted;
            }else percentage=values[id];
        }else if(id==TVSimulation){title=menu_locale::text(L"TV simulation");value=s.tv_active()?menu_locale::text(L"On"):menu_locale::text(L"Off");}
        else if(id==TVScreen){title=menu_locale::text(L"Screen pattern");value=screens[s.selected_tube()];}
        else if(id==PhosphorStrength){title=menu_locale::text(L"Pattern strength");percentage=s.phosphor_strength;}
        else if(id==PhosphorSharpness){title=menu_locale::text(L"Pattern sharpness");percentage=s.phosphor_sharpness;}
        else if(id==Restore)title=menu_locale::text(L"Restore filter defaults");
        else title=active_profile==custom?menu_locale::text(L"Save my changes"):custom_exists?menu_locale::text(L"Save to my custom profile"):menu_locale::text(L"Create a custom copy");
        if(!active)value=menu_locale::text(L"Off");
        else if(percentage>=0){
            if(id==MotionBlur&&!percentage)value=menu_locale::text(L"Off");
            else{swprintf(formatted,std::size(formatted),L"%d%%",percentage);value=formatted;}
        }
        const int indent=id==TVScreen||id==PhosphorStrength||id==PhosphorSharpness?6:0;
        row_label(c,id,selected,y,filter_row,title,value,right,active?ink:muted,active?ink:muted,indent);
    }
}
static const wchar_t* shortcut_action(int id){
    if(id==retro_panel::PreviousFilterShortcut)return menu_locale::text(L"Previous filter");
    if(id==retro_panel::NextFilterShortcut)return menu_locale::text(L"Next filter");
    return menu_locale::text(L"Open menu");
}
static void draw_menu_settings(Canvas& c,int selected){
    using namespace retro_panel;
    const std::wstring value=std::to_wstring(display_settings.menu_scale)+L"%";
    row_label(c,MenuSize,selected,first_y,display_row,menu_locale::text(L"Menu size"),value.c_str(),display_width-9);
    const auto bind=[&](Row id,unsigned buttons){
        row_label(c,id,selected,row_top(id,false),display_row,shortcut_action(id),shortcut_label(buttons).c_str(),display_width-9);
    };
    bind(MenuShortcut,display_settings.menu_shortcut);
    bind(PreviousFilterShortcut,display_settings.previous_filter_shortcut);
    bind(NextFilterShortcut,display_settings.next_filter_shortcut);
}
static void draw_shortcut_capture(Canvas& c,int mw){
    using namespace retro_panel;
    const int right=mw-10;
    c.label(shortcut_action(shortcut_target),10,first_y,right,first_y+16,0,mint);
    c.paragraph(menu_locale::text(L"Hold one or two buttons together for 1 second."),10,first_y+18,right,2,ink);
    const auto candidate=shortcut_candidate_label();
    c.label(candidate.empty()?menu_locale::text(L"Waiting for input…"):candidate.c_str(),10,first_y+47,right,first_y+66,0,mint);
    const int bar_y=first_y+69;
    c.box(10,bar_y,right,bar_y+4,track);
    if(shortcut_progress>0&&!shortcut_too_many&&!shortcut_conflict)c.box(10,bar_y,10+(right-10)*shortcut_progress/10,bar_y+4,mint);
}
static const wchar_t* draw_close_prompt(Canvas& c,int right){
    using namespace retro_panel;
    const wchar_t* choices[]={custom_exists?menu_locale::text(L"Replace saved Custom"):menu_locale::text(L"Save as Custom"),menu_locale::text(L"Discard changes"),menu_locale::text(L"Keep editing")};
    for(int i=0;i<3;++i)
        row_label(c,i,int(close_choice),first_y+i*display_row,display_row,choices[i],nullptr,right);
    if(save_failed())return menu_locale::text(L"Couldn't save Custom. Try again or keep editing.");
    switch(close_choice){
    case CloseChoice::Save:return custom_exists?menu_locale::text(L"Replace your saved custom filter and close."):menu_locale::text(L"Save these settings as your custom filter and close.");
    case CloseChoice::Discard:return menu_locale::text(L"Close using your last saved filter settings.");
    default:return menu_locale::text(L"Return to the menu with your preview.");
    }
}
static void draw_update_prompt(Canvas& c,int right){
    using namespace retro_panel;
    c.paragraph(menu_locale::text(L"Save your game before closing. The update applies at the next launch."),10,first_y,right,3,ink);
    const int first=first_y+41;
    const int selected=close_update_now?0:1;
    row_label(c,0,selected,first,display_row,menu_locale::text(L"Close game now"),nullptr,right);
    row_label(c,1,selected,first+display_row,display_row,menu_locale::text(L"Later"),nullptr,right);
}
static std::wstring help(int selected,const RetroSettings& s,retro_panel::UpdateState update,bool failed,const std::wstring& available){
    using namespace retro_panel;
    if(failed)return menu_locale::text(L"Couldn’t save your changes. Try again.");
    if(selected==ModUpdate){
        wchar_t detail[96]{};
        if(update_detail)update_detail(detail,std::size(detail));
        detail[std::size(detail)-1]=0;
        return detail[0]?detail:update_message(update).hint;
    }
    if(selected==InstalledVersion)return available.empty()?menu_locale::text(L"Version of FF8 Reborn currently installed."):menu_locale::format(L"Available version: {}",available);
    switch(selected){
        case MenuSize:return menu_locale::text(L"Adjust the menu size. Enter / A resets it to 100%.");
        case MenuShortcut:case PreviousFilterShortcut:case NextFilterShortcut:
            return menu_locale::text(L"Enter / A records a shortcut. Left restores its default.");
        case Aspect:return menu_locale::text(L"Set the game screen format.");
        case FPS:return menu_locale::text(unlimited_rendering()?L"Generate frames as rendering time allows.":
            deck_renderer?L"Set the presentation frame rate.":L"Auto selects a target for the current display.");
        case Map:return menu_locale::text(L"Choose the artwork for the full-screen map.");
        case Sides:return menu_locale::text(L"Blend the sides of pre-rendered scenes.");
        case Counter:return menu_locale::text(L"Set the size of the FPS counter.");
        case Lines:return menu_locale::text(L"Adjust by 5 lines. Enter / A enables Auto.");
        case TVSimulation:return menu_locale::text(L"Show the phosphor pattern of a CRT television.");
        case TVScreen:return menu_locale::text(L"Choose the pattern of the TV's red, green and blue phosphors.");
        case PhosphorStrength:return menu_locale::text(L"Set how visible the phosphor pattern is.");
        case PhosphorSharpness:return menu_locale::text(L"Adjust how sharply the phosphor edges appear.");
        case Restore:return menu_locale::text(L"Preview the original settings for this filter.");
        case SaveCustom:return custom_exists?menu_locale::text(L"Replace your saved custom filter."):menu_locale::text(L"Save a custom copy of this filter.");
        case Profile:
            if(dirty)return menu_locale::text(L"Changing profiles discards your preview.");
            if(!s.filter_enabled())return menu_locale::text(L"Screen filter effects are off. Choose a profile to enable them.");
            if(active_profile==custom)return menu_locale::format(L"Based on {}",profile_name(base_profile()));
            return menu_locale::text(L"Choose a profile or edit it to create a copy.");
        default:
            return menu_locale::text(L"Save a custom copy to keep your changes.");
    }
}
static void centered_icon_label(Canvas& c,const wchar_t* text,int x,int y,int width,int height){
    int left=INT_MAX,right=-1,top=12,bottom=-1,pen=0;
    for(const wchar_t* p=text;*p;++p){
        const Glyph& g=glyph(*p);
        for(int gy=0;gy<int(std::size(g.rows));++gy)for(int gx=0;gx<g.width;++gx){
            if(g.rows[gy]&(1u<<(g.width-1-gx))){
                left=std::min(left,pen+gx);right=std::max(right,pen+gx);
                top=std::min(top,gy);bottom=std::max(bottom,gy);
            }
        }
        pen+=g.advance;
    }
    if(right<left||bottom<top)return;
    const int draw_x=x+(width-(right-left+1))/2-left;
    const int draw_y=y+(height-(bottom-top+1))/2-top;
    c.label(text,draw_x,draw_y,draw_x+pen,draw_y+11,0,ink);
}
static void render(Canvas& c,const RetroSettings& s,int selected,bool bypass,bool failed,
                   retro_panel::UpdateState update,UpdateMeter meter,int w,int h){
    using namespace retro_panel;
    const auto page=section_view();
    const bool filter=page==Section::ScreenFilter;
    const bool record=recording_shortcut&&!confirm_close;
    const bool update_prompt=update_choice_open&&!confirm_close&&!record;
    const bool choose=choosing_section&&!confirm_close&&!record&&!update_prompt;
    const int mw=main_width(page);
    const int footer=record||update_prompt?first_y+41+2*display_row+menu_gap+5:confirm_close?first_y+3*display_row+5:
        std::min(footer_top(page,can_restore(s)),int(std::lround(c.height/c.scale))-footer_height-2);
    c.window(0,main_y,mw,footer-main_y-menu_gap,false,false);
    const int tab_width=(design_width(page)-2*menu_gap)/3;
    for(int section=0;section<int(std::size(section_names));++section){
        const int x=section*(tab_width+menu_gap);
        c.window(x,0,tab_width,section_height,section==int(page),false);
        const wchar_t* title=menu_locale::text(section_names[section]);
        const int text_x=x+(tab_width-text_width(title))/2;
        c.label(title,text_x,0,x+tab_width,section_height,0,ink);
        if(choose&&section==int(pending_section))c.cursor(x+4,3);
    }
    std::wstring available;
#ifdef GEO_NATIVE_CAPTURE
    if(page==Section::Display&&!confirm_close)available=mod_update::available_version();
#endif
    std::wstring hint;
    if(confirm_close)hint=draw_close_prompt(c,mw-9);
    else if(update_prompt){
        draw_update_prompt(c,mw-9);
        hint=update_close_failed?menu_locale::text(L"Close FFVIII normally to apply the update."):
             close_update_now?menu_locale::text(L"Close Final Fantasy VIII Remastered."):menu_locale::text(L"Keep playing and finish the update later.");
    }
    else if(record){
        draw_shortcut_capture(c,mw);
        hint=shortcut_too_many?menu_locale::text(L"Use at most two buttons."):shortcut_conflict?menu_locale::text(L"Already used by another shortcut."):
             failed?menu_locale::text(L"Couldn't save the shortcut. Try again."):menu_locale::text(L"Tap B, Esc or F8 to keep your current shortcut.");
    }
    else{
        if(filter)draw_filter(c,s,selected,w,h,footer);
        else if(page==Section::MenuSettings)draw_menu_settings(c,selected);
        else draw_display(c,selected,update,meter,available);
        hint=choose?menu_locale::format(L"Enter / A opens {}. Esc / B goes back.",
                                        menu_locale::text(section_names[int(pending_section)])):
                    help(selected,s,update,failed,available);
    }
    c.window(0,footer,mw,footer_height,false,false);
    c.paragraph(hint,9,footer+6,mw-9,2,
                !choose&&(failed||(update_prompt&&update_close_failed)||(record&&(shortcut_too_many||shortcut_conflict))||(selected==ModUpdate&&update==update_error))?warning:ink);

    const wchar_t* keys[][2]={{L"L1 R1",menu_locale::text(L"Section")},{L"↑ ↓",menu_locale::text(L"Navigate")},{L"← →",menu_locale::text(L"Adjust")},{L"A",menu_locale::text(L"Select")},{L"B",menu_locale::text(L"Close")}};
    const int sx=mw+menu_gap,ky=main_y;
    const wchar_t* prompt_keys[][2]={{L"↑ ↓",menu_locale::text(L"Navigate")},{L"A",menu_locale::text(L"Select")},{L"B",menu_locale::text(L"Back")}};
    const wchar_t* chooser_keys[][2]={{L"← →",menu_locale::text(L"Section")},{L"A",menu_locale::text(L"Select")},{L"B",menu_locale::text(L"Back")}};
    const wchar_t* record_keys[][2]={{menu_locale::text(L"1 sec"),menu_locale::text(L"Save")},{L"B",menu_locale::text(L"Cancel")}};
    const wchar_t* update_keys[][2]={{L"↑ ↓",menu_locale::text(L"Navigate")},{L"A",menu_locale::text(L"Select")},{L"B",menu_locale::text(L"Later")}};
    const auto draw_keys=[&](const auto& actions){
        const int count=int(std::size(actions)),height=12+count*20;
        c.window(sx,ky,side_width,height,false,false);
        for(int i=0;i<count;++i){
            const int y=ky+6+i*20;
            const wchar_t* key=actions[i][0];
            if(key[0]==L'↑'||key[0]==L'←'){
                const int x=sx+8;
                c.box(x+4,y,x+8,y+1,ink);c.box(x+4,y+11,x+8,y+12,ink);
                c.box(x+4,y+1,x+5,y+4,ink);c.box(x+7,y+1,x+8,y+4,ink);
                c.box(x+4,y+8,x+5,y+11,ink);c.box(x+7,y+8,x+8,y+11,ink);
                c.box(x,y+4,x+1,y+8,ink);c.box(x+11,y+4,x+12,y+8,ink);
                c.box(x+1,y+4,x+4,y+5,ink);c.box(x+1,y+7,x+4,y+8,ink);
                c.box(x+8,y+4,x+11,y+5,ink);c.box(x+8,y+7,x+11,y+8,ink);
                const bool vertical=key[0]==L'↑';
                constexpr unsigned fade[]={220,145,70};
                for(int j=0;j<3;++j){
                    if(vertical){c.blend_box(x+5,y+1+j,x+7,y+2+j,mint,fade[j]);c.blend_box(x+5,y+10-j,x+7,y+11-j,mint,fade[j]);}
                    else{c.blend_box(x+1+j,y+5,x+2+j,y+7,mint,fade[j]);c.blend_box(x+10-j,y+5,x+11-j,y+7,mint,fade[j]);}
                }
            }else if((key[0]==L'A'||key[0]==L'B')&&!key[1]){
                const int x=sx+8;
                c.stepped_box(x,y,14,14,ink);
                c.stepped_box(x+1,y+1,12,12,track);
                centered_icon_label(c,key,x,y,14,14);
                c.label(key[0]==L'A'?menu_locale::text(L"Enter"):menu_locale::text(L"Esc"),sx+24,y,sx+63,y+14,0,mint);
            }else if(!wcscmp(key,L"L1 R1")){
                for(int button=0;button<2;++button){
                    const int x=sx+6+button*22;
                    const wchar_t* title=button?L"R1":L"L1";
                    c.stepped_box(x,y,20,14,ink);c.stepped_box(x+1,y+1,18,12,track);
                    centered_icon_label(c,title,x,y,20,14);
                }
            }else{
                c.label(key,sx+8,y,sx+45,y+14,0,mint);
            }
            c.label(actions[i][1],sx+64,y,sx+side_width-2,y+14,0,ink);
        }
        return height;
    };
    const int key_box_height=confirm_close?draw_keys(prompt_keys):record?draw_keys(record_keys):update_prompt?draw_keys(update_keys):
                             choose?draw_keys(chooser_keys):draw_keys(keys);
    if(bypass){
        c.window(sx,ky+key_box_height+menu_gap,side_width,20,false,false);
        c.label(menu_locale::text(L"Original view"),sx+9,ky+key_box_height+menu_gap+2,sx+side_width-4,ky+key_box_height+menu_gap+18,0,mint);
    }
}
static bool draw(const RetroSettings& settings,int selected,GLuint target,int w,int h,bool bypass=false){
    if(w<24||h<24)return false;
    const auto page=retro_panel::section_view();
    const bool restore=retro_panel::can_restore(settings);
    const int counter=fps_counter::tile_h*fps_counter::scale(display_settings.fps_counter,w,h);
    if(h-counter<24)return false;
    const auto position=place(page,restore,w,h,counter,bypass);
    const int pw=design_width(page),ph=position.canvas_height;
    const bool failed=retro_panel::save_failed();
    const auto update=page==retro_panel::Section::Display&&retro_panel::update_status?
        retro_panel::update_status():retro_panel::update_idle;
    const decltype(last_state) state={pw,ph,selected,bypass,failed,settings.mode,settings.lines,settings.pixelation,
                                    settings.scanlines,settings.mask,settings.glow,settings.dither,settings.edge_softness,settings.ps1_raster,retro_panel::active_profile,retro_panel::dirty,settings.phosphors,display_settings.fps,display_settings.atlas,display_settings.wide,display_settings.field_sides,retro_panel::custom_exists,retro_panel::custom_base,settings.tv_screen,settings.phosphor_strength,settings.phosphor_sharpness,display_settings.fps_counter,update,
                                    settings.motion_blur,w,h,settings.auto_lines,int(page),settings.tv_enabled,display_settings.menu_scale,retro_panel::confirm_close,int(retro_panel::close_choice),retro_panel::choosing_section,int(retro_panel::pending_section),
                                    int(display_settings.menu_shortcut),int(display_settings.previous_filter_shortcut),int(display_settings.next_filter_shortcut),retro_panel::recording_shortcut,retro_panel::shortcut_target,int(retro_panel::shortcut_candidate),retro_panel::shortcut_progress,retro_panel::shortcut_too_many,retro_panel::shortcut_conflict,retro_panel::update_choice_open,retro_panel::close_update_now,retro_panel::update_close_failed,display_settings.fps_setting};
    UpdateMeter meter=last_meter;
    unsigned update_revision=0;
#ifdef GEO_NATIVE_CAPTURE
    if(page==retro_panel::Section::Display)update_revision=mod_update::revision.load(std::memory_order_acquire);
#endif
    const bool changed=!painted||last_state!=state||last_update_revision!=update_revision;
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
        last_state=state;last_meter=meter;last_update_revision=update_revision;painted=true;
    }
    return blit(panel,target,position.x,position.y,position.w,position.h,1.f);
}
static void forget(){panel={};painted=false;}
}
