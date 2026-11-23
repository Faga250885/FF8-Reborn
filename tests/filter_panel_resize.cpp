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
        {1280,720,L"720p / 800p · 1280×720",L"Auto · 240 lines"}, {1280,800,L"720p / 800p · 1280×800",L"Auto · 265 lines"},
        {1920,1080,L"1080p · 1920×1080",L"Auto · 320 lines"}, {2560,1440,L"1440p (2K) · 2560×1440",L"Auto · 320 lines"},
        // Same panel dimensions and tier: only the window width changes.
        {3440,1440,L"1440p (2K) · 3440×1440",L"Auto · 320 lines"}, {3840,2160,L"2160p (4K) · 3840×2160",L"Auto · 320 lines"},
        {1280,800,L"720p / 800p · 1280×800",L"Auto · 265 lines"},
        {1280,800,L"720p / 800p · 1280×800",L"265 lines",true},
        {2560,1440,L"1440p (2K) · 2560×1440",L"265 lines",true}
    };
    for(const auto& example:examples){
        display_settings.retro.lines=265;
        display_settings.retro.auto_lines=!example.manual;
        demand(SetWindowPos(window,nullptr,0,0,example.width,example.height,
                            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize window");
        int w=0,h=0;
        for(auto page:{retro_panel::Section::Display,retro_panel::Section::ScreenFilter}){
            retro_panel::page=page;
            const bool filter=page==retro_panel::Section::ScreenFilter;
            retro_panel::row=filter?retro_panel::Profile:retro_panel::InstalledVersion;
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
            }else{
                check_label(L"Aspect ratio",ui::left,ui::first_y,125,ui::first_y+ui::display_row,ui::mint);
                const int y=ui::row_top(retro_panel::ModUpdate,false);
                check_label(L"Check for updates",ui::left,y,right,y+ui::display_row,ui::mint);
                const int vy=ui::row_top(retro_panel::InstalledVersion,false);
                demand(vy-y==ui::display_row,"version spacing must match the other rows");
                check_label(L"Installed version",ui::left,vy,122,vy+ui::display_row,ui::mint);
                auto tag=mod_update::wide(update_version::tag);
                check_label(tag.c_str(),125,vy,right,vy+ui::display_row,ui::ink,true);
            }
            const int selected_y=ui::row_top(retro_panel::row,example.manual)+((filter?ui::filter_row:ui::display_row)-16)/2;
            expected.cursor(5,selected_y);
            for(int cy=0;cy<16;++cy)for(int cx=0;cx<16;++cx){
                if(ui::hand[cy][cx]=='0')continue;
                const auto offset=4*(size_t(panel.h-1-selected_y-cy)*panel.w+5+cx);
                demand(memcmp(expected.pixels+offset,frame.data()+offset,4)==0,"left option missing cursor");
            }
            expected.window(mw,ui::main_y,ui::side_width,47);
            for(int section=0;section<2;++section){
                const int y=ui::main_y+6+section*17;
                const bool active=(section==1)==filter;
                check_label(section?L"Screen filter":L"Display",mw+10,y,mw+ui::side_width-5,y+16,active?ui::section_blue:ui::ink);
            }
            for(int y=ui::main_y+5;y<ui::main_y+41;++y){
                const auto offset=4*(size_t(panel.h-1-y)*panel.w+mw+3);
                demand(memcmp(expected.pixels+offset,frame.data()+offset,4*(ui::side_width-6))==0,"section text or cursor outside the option list");
            }
            const int footer=ui::footer_top(page,example.manual);
            check_label(L"Close",mw+9,ui::main_y+47+6+4*27+12,mw+ui::side_width-5,ui::main_y+47+6+4*27+24,ui::ink);
            const int counter=fps_counter::tile_h*fps_counter::scale(3,w,h);
            const auto place=ui::place(page,example.manual,w,h,counter);
            demand(place.y>=0&&place.y+place.h<=h-counter&&place.x+place.w<=w,"panel outside drawable");
            demand(panel.h==footer+ui::footer_height+2,"wrong section height");
            demand(frame[4*(size_t(panel.h-1)*panel.w+panel.w-1)+3]==0,"background between windows must be clear");
            if(!filter&&!example.manual&&example.height==800){
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
                    reference.window(0,footer,mw,ui::footer_height);
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
    puts("Display/filter views: left cursor, blue section, aligned version rows, tag changes, progress and resize OK");
}
