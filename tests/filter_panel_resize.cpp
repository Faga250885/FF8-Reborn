#define GEO_TEST
#ifndef GEO_NATIVE_CAPTURE
#define GEO_NATIVE_CAPTURE
#endif
#include "../src/geometry.cpp"

using namespace geo;
namespace ui=retro_panel_draw;

struct OutputExample { int width,height;const wchar_t* label;const wchar_t* lines;bool manual=false; };

int main(){
    HWND window=CreateWindowA("STATIC","Filter output",WS_POPUP,0,0,1280,800,
                              nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR format{sizeof(format),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    demand(SetPixelFormat(dc,ChoosePixelFormat(dc,&format),&format)&&
           wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl(),"OpenGL initialization");
    display_settings.fps_counter=3; // Allow space for the largest counter.
    display_settings.retro=RetroSettings::recommended();
    retro_panel::open=true;
    const OutputExample examples[]={
        {1280,720,L"1280×720",L"Auto · 240 lines"}, {1280,800,L"1280×800",L"Auto · 265 lines"},
        {1920,1080,L"1920×1080",L"Auto · 320 lines"}, {2560,1440,L"2560×1440",L"Auto · 320 lines"},
        // Same panel dimensions and tier: only the window width changes.
        {3440,1440,L"3440×1440",L"Auto · 320 lines"}, {3840,2160,L"3840×2160",L"Auto · 320 lines"},
        {1280,800,L"1280×800",L"Auto · 265 lines"},
        {1280,800,L"1280×800",L"265 lines",true},
        {2560,1440,L"2560×1440",L"265 lines",true}
    };
    for(const auto& example:examples){
        display_settings.retro.lines=265;
        display_settings.retro.auto_lines=!example.manual;
        demand(SetWindowPos(window,nullptr,0,0,example.width,example.height,
                            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize window");
        int w=0,h=0;
        for(auto page:{retro_panel::Section::Display,retro_panel::Section::ScreenFilter,retro_panel::Section::MenuSettings}){
            retro_panel::page=page;
            const bool filter=page==retro_panel::Section::ScreenFilter;
            const bool display=page==retro_panel::Section::Display;
            retro_panel::row=filter?retro_panel::Profile:display?retro_panel::InstalledVersion:retro_panel::MenuSize;
            demand(begin_retro_frame(dc,w,h)&&w==example.width&&h==example.height,"drawable dimensions");
            const auto& panel=ui::panel;
            const int mw=ui::main_width(page),right=mw-9;
            ui::Canvas expected(panel.w,panel.h,panel.w);
            demand(expected.pixels!=nullptr,"panel reference");
            std::vector<unsigned char> frame(size_t(panel.w)*panel.h*4);
            auto read_frame=[&](){
                pBindFramebuffer(GL_READ_FRAMEBUFFER,panel.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
                glReadPixels(0,0,panel.w,panel.h,GL_BGRA,GL_UNSIGNED_BYTE,frame.data());
            };
            read_frame();
            auto check_label=[&](const wchar_t* label,int x,int y,int end,int bottom,COLORREF colour,bool align_right=false){
                demand(ui::text_width(label)<=end-x,"label clipped");
                expected.label(label,x,y,end,bottom,0,colour,align_right);
                int matching_ink=0;
                for(int cy=y;cy<bottom;++cy)for(int cx=x;cx<end;++cx){
                    const auto offset=4*(size_t(panel.h-1-cy)*panel.w+cx);
                    const auto* ref=expected.pixels+offset;
                    if(ref[3]&&RGB(ref[2],ref[1],ref[0])==colour){
                        demand(memcmp(ref,frame.data()+offset,4)==0,"incorrect or stale menu label");
                        ++matching_ink;
                    }
                }
                demand(matching_ink>0,"empty label region");
            };
            if(filter){
                check_label(example.label,110,ui::tuning_top,right,ui::tuning_top+ui::output_height,ui::secondary,true);
                const int y=ui::row_top(retro_panel::Lines,example.manual);
                check_label(example.lines,140,y,right,y+ui::filter_row,ui::ink,true);
            }else if(display){
                check_label(L"Aspect ratio",ui::left,ui::first_y,125,ui::first_y+ui::display_row,ui::ink);
                const int y=ui::row_top(retro_panel::ModUpdate,false);
                check_label(L"Check for updates",ui::left,y,right,y+ui::display_row,ui::ink);
                const int vy=ui::row_top(retro_panel::InstalledVersion,false);
                demand(vy-y==ui::display_row,"version spacing must match the other rows");
                check_label(L"Installed version",ui::left,vy,122,vy+ui::display_row,ui::ink);
                auto tag=mod_update::wide(update_version::tag);
                check_label(tag.c_str(),125,vy,right,vy+ui::display_row,ui::ink,true);
            }else{
                check_label(L"Menu size",ui::left,ui::first_y,125,ui::first_y+ui::display_row,ui::ink);
                check_label(L"100%",125,ui::first_y,right,ui::first_y+ui::display_row,ui::ink,true);
                const auto check_shortcut=[&](int id,const wchar_t* title,unsigned mask){
                    const int y=ui::row_top(id,false);
                    check_label(title,ui::left,y,125,y+ui::display_row,ui::ink);
                    const auto value=retro_panel::shortcut_label(mask);
                    check_label(value.c_str(),125,y,right,y+ui::display_row,ui::ink,true);
                };
                check_shortcut(retro_panel::MenuShortcut,L"Open menu",display_settings.menu_shortcut);
                check_shortcut(retro_panel::PreviousFilterShortcut,L"Previous filter",display_settings.previous_filter_shortcut);
                check_shortcut(retro_panel::NextFilterShortcut,L"Next filter",display_settings.next_filter_shortcut);
            }
            const int selected_y=ui::row_top(retro_panel::row,example.manual)+((filter?ui::filter_row:ui::display_row)-16)/2;
            expected.cursor(5,selected_y);
            for(int cy=0;cy<16;++cy)for(int cx=0;cx<16;++cx){
                if(ui::hand[cy][cx]=='0')continue;
                const auto offset=4*(size_t(panel.h-1-selected_y-cy)*panel.w+5+cx);
                demand(memcmp(expected.pixels+offset,frame.data()+offset,4)==0,"left option missing cursor");
            }
            const int tab_width=(ui::design_width(page)-2*ui::menu_gap)/3;
            for(int section=0;section<3;++section){
                const int x=section*(tab_width+ui::menu_gap);
                const bool active=section==int(page);
                expected.window(x,0,tab_width,ui::section_height,active,false);
                const int title=x+(tab_width-ui::text_width(ui::section_names[section]))/2;
                check_label(ui::section_names[section],title,0,x+tab_width,ui::section_height,ui::ink);
            }
            const int footer=ui::footer_top(page,example.manual);
            const int side=mw+ui::menu_gap;
            check_label(L"Close",side+64,ui::main_y+6+4*20,side+ui::side_width-5,ui::main_y+6+4*20+14,ui::ink);
            for(int button=3;button<=4;++button){
                const int x=side+8,y=ui::main_y+6+button*20;
                int min_x=14,max_x=-1,min_y=14,max_y=-1;
                for(int by=2;by<12;++by)for(int bx=2;bx<12;++bx){
                    const auto offset=4*(size_t(panel.h-1-y-by)*panel.w+x+bx);
                    if(RGB(frame[offset+2],frame[offset+1],frame[offset])==ui::ink){
                        min_x=std::min(min_x,bx);max_x=std::max(max_x,bx);
                        min_y=std::min(min_y,by);max_y=std::max(max_y,by);
                    }
                }
                demand(max_x>=0&&std::abs(min_x+max_x+1-14)<=2&&std::abs(min_y+max_y+1-14)<=2,
                       "controller button letter is not centered");
            }
            for(int button=0;button<2;++button){
                const int x=side+6+button*22,y=ui::main_y+6;
                int min_x=20,max_x=-1,min_y=14,max_y=-1;
                for(int by=2;by<12;++by)for(int bx=2;bx<18;++bx){
                    const auto offset=4*(size_t(panel.h-1-y-by)*panel.w+x+bx);
                    if(RGB(frame[offset+2],frame[offset+1],frame[offset])==ui::ink){
                        min_x=std::min(min_x,bx);max_x=std::max(max_x,bx);
                        min_y=std::min(min_y,by);max_y=std::max(max_y,by);
                    }
                }
                demand(max_x>=0&&std::abs(min_x+max_x+1-20)<=2&&std::abs(min_y+max_y+1-14)<=2,
                       "shoulder button label is not centered");
            }
            const int counter=fps_counter::tile_h*fps_counter::scale(3,w,h);
            const auto place=ui::place(page,example.manual,w,h,counter);
            demand(place.y>=0&&place.y+place.h<=h-counter&&place.x+place.w<=w,"panel outside drawable");
            demand(panel.h==ui::design_height(page,example.manual)&&panel.h>=footer+ui::footer_height+2,"wrong section height");
            demand(frame[4*(size_t(panel.h-1)*panel.w+panel.w-1)+3]==0,"background between windows must be clear");
            if(display&&!example.manual&&example.height==800){
                retro_panel::update_status=[](){return retro_panel::update_downloading;};
                retro_panel::update_bytes=[](unsigned& done,unsigned& total){done=50;total=100;};
                demand(ui::draw(display_settings.retro,retro_panel::ModUpdate,0,w,h),"update draw");
                pBindFramebuffer(GL_READ_FRAMEBUFFER,ui::panel.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
                const int y=ui::row_top(retro_panel::InstalledVersion,false)-2;
                for(int quarter:{1,3}){
                    const int x=ui::left+(right-ui::left)*quarter/4;
                    BYTE bgra[4]{};
                    glReadPixels(x,panel.h-1-y,1,1,GL_BGRA,GL_UNSIGNED_BYTE,bgra);
                    demand(RGB(bgra[2],bgra[1],bgra[0])==(quarter==1?ui::mint:ui::track),"update progress not visible");
                }
                retro_panel::update_status=mod_update::status;
                for(auto tag:{L"v1.4.0-deck.12",L"v1.4.0-deck.13",L"v4294967295.4294967295.0-deck.13"}){
                    lstrcpynW(mod_update::available_tag,tag,33);
                    mod_update::say(retro_panel::update_available,L"");
                    demand(ui::draw(display_settings.retro,retro_panel::InstalledVersion,0,w,h),"available version draw");
                    read_frame();
                    const int top=ui::row_top(retro_panel::ModUpdate,false);
                    const auto* value=ui::text_width(tag)+ui::text_width(L"Download update")+5<=right-ui::left?
                        tag:L"Update available";
                    check_label(value,right-ui::text_width(value),top,right,top+ui::display_row,ui::ink,true);
                    ui::Canvas reference(panel.w,panel.h,panel.w);
                    reference.window(0,footer,mw,ui::footer_height,false,false);
                    reference.paragraph(L"Available version: "+std::wstring(tag),9,footer+6,mw-9,2,ui::ink);
                    for(int py=footer+5;py<footer+33;++py){
                        const auto offset=4*(size_t(panel.h-1-py)*panel.w+5);
                        demand(memcmp(reference.pixels+offset,frame.data()+offset,4*(mw-10))==0,"stale or clipped available version");
                    }
                    expected.box(ui::left,top,right,top+ui::display_row,ui::track); // clear previous reference text
                }
                mod_update::available_tag[0]=0;mod_update::say(retro_panel::update_idle,L"");
                retro_panel::update_status=nullptr;retro_panel::update_bytes=nullptr;
            }
            demand(SwapBuffers(dc),"present");
            retro_display::end_window(true,false,w,h);
        }
    }
    // The largest counter must fit above the menu at every Deck zoom level.
    using namespace retro_panel;
    for(int height:{720,800}){
        const int width=1280,counter=fps_counter::tile_h*fps_counter::scale(3,width,height);
        int previous_width=0;
        page=Section::ScreenFilter;
        for(int percent=70;percent<=150;percent+=10){
            display_settings.menu_scale=percent;
            const auto layout=ui::place(page,true,width,height,counter);
            demand(layout.w>previous_width&&layout.y>=0&&layout.y+layout.h<=height-counter&&
                layout.x+layout.w<=width,"actual size increases at every stop and fits");
            previous_width=layout.w;
            const int footer=std::min(ui::footer_top(page,true),layout.canvas_height-ui::footer_height-2);
            int first_offset=-1;
            for(auto id:filter_order){
                const auto view=ui::filter_page(id,true,footer);
                const int y=ui::row_top(id,true)-view.offset;
                demand(y>=ui::first_y&&y+ui::filter_row<=footer-5,"selected row outside visible filter page");
                if(id==Profile)first_offset=view.offset;
                if(id==Lines)demand(view.offset==first_offset,"selection moves within a stable page");
            }
            if(height==800&&percent==150){
                demand(ui::filter_page(Profile,true,footer).more&&ui::filter_page(SaveCustom,true,footer).offset>0,
                       "large filter view pages its rows");
                auto edited=RetroSettings::recommended();edited.pixelation=90;
                dirty=true;confirm_close=false;
                auto contains_hand=[&](int x_start,int top){
                    auto& panel=ui::panel;
                    pBindFramebuffer(GL_READ_FRAMEBUFFER,panel.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
                    BYTE pixels[16][16][4]{};
                    glReadPixels(x_start,panel.h-top-16,16,16,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
                    for(int y=0;y<16;++y)for(int x=0;x<16;++x){
                        int i=ui::hand[y][x]-'0';
                        if(i&&RGB(pixels[15-y][x][2],pixels[15-y][x][1],pixels[15-y][x][0])!=ui::hand_palette[i])return false;
                    }
                    return true;
                };
                row=SaveCustom;
                auto redraw=[&]{demand(ui::draw(edited,row,0,width,height),"draw focused menu");};
                redraw();
                const int scroll=ui::filter_page(SaveCustom,true,footer).offset;
                const int last_y=ui::row_top(SaveCustom,true)-scroll;
                demand(contains_hand(5,last_y),"last action visible in enlarged menu");
                change_section(1);
                const auto pending_hand=[&](Section section){
                    const int tab=(ui::design_width(page)-2*ui::menu_gap)/3;
                    return contains_hand(int(section)*(tab+ui::menu_gap)+4,3);
                };
                redraw();
                demand(!contains_hand(5,last_y)&&pending_hand(Section::MenuSettings),"cursor moves to pending section");
                handle_key(VK_RIGHT,L"",edited);redraw();
                demand(pending_hand(Section::Display)&&!pending_hand(Section::MenuSettings)&&!contains_hand(5,last_y),
                       "moving within chooser repaints only its cursor");
                handle_key(VK_F8,L"",edited);
                for(auto choice:{CloseChoice::Save,CloseChoice::Discard,CloseChoice::KeepEditing}){
                    close_choice=choice;
                    redraw();
                    demand(contains_hand(5,ui::first_y+int(choice)*ui::display_row+(ui::display_row-16)/2)&&
                           !pending_hand(Section::Display),"only the confirmation cursor is visible");
                }
                handle_key(VK_RETURN,L"",edited);redraw();
                demand(pending_hand(Section::Display)&&!contains_hand(5,last_y),"keep editing returns to the chooser");
                handle_key(VK_ESCAPE,L"",edited);redraw();
                demand(contains_hand(5,last_y)&&!pending_hand(Section::Display),"cancel restores the scrolled left row");
                change_section(1);handle_key(VK_RETURN,L"",edited);redraw();
                demand(page==Section::MenuSettings&&contains_hand(5,ui::first_y+(ui::display_row-16)/2)&&
                       !pending_hand(Section::MenuSettings),"committing a section focuses its first option");
                dirty=false;
                for(auto target:{MenuShortcut,PreviousFilterShortcut,NextFilterShortcut}){
                    row=target;
                    handle_key(VK_RETURN,L"",edited);
                    demand(recording_shortcut&&shortcut_target==target,"opening the selected shortcut recorder");
                    shortcut_candidate=display_settings.menu_shortcut;shortcut_progress=5;
                    redraw();
                    auto bar_point=[&](int quarter){
                        auto& panel=ui::panel;
                        pBindFramebuffer(GL_READ_FRAMEBUFFER,panel.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
                        BYTE data[4];
                        const int x=10+(ui::main_width(page)-20)*quarter/4;
                        glReadPixels(x,panel.h-1-(ui::first_y+69),1,1,GL_BGRA,GL_UNSIGNED_BYTE,data);
                        return RGB(data[2],data[1],data[0]);
                    };
                    demand(bar_point(1)==ui::mint&&bar_point(3)==ui::track,"hold progress visible");
                    shortcut_progress=9;redraw();
                    demand(bar_point(3)==ui::mint,"hold progress repaints while row is unchanged");
                    shortcut_conflict=true;redraw();demand(bar_point(1)==ui::track,"conflicting shortcut pauses progress");
                    shortcut_conflict=false;shortcut_too_many=true;redraw();
                    demand(bar_point(1)==ui::track,"too many buttons pauses progress");
                    handle_key(VK_ESCAPE,L"",edited);redraw();
                    demand(!recording_shortcut&&contains_hand(5,ui::row_top(target,false)+(ui::display_row-16)/2),
                           "cancel returns to the shortcut row");
                }
                focus_section(Section::Display);
                update_choice_open=true;close_update_now=false;redraw();
                const int update_first=ui::first_y+41+(ui::display_row-16)/2;
                demand(contains_hand(5,update_first+ui::display_row)&&!contains_hand(5,update_first),
                       "Later is selected in the update dialog");
                handle_key(VK_UP,L"",edited);redraw();
                demand(contains_hand(5,update_first)&&!contains_hand(5,update_first+ui::display_row),
                       "update dialog moves the single cursor to close");
                handle_key(VK_ESCAPE,L"",edited);redraw();
                demand(!update_choice_open&&contains_hand(5,ui::first_y+(ui::display_row-16)/2),
                       "deferring update returns to display settings");
            }
        }
    }
    puts("Menu sections, zoom, focus, unsaved changes, three shortcut recorders and update dialog: visual checks passed");
}
