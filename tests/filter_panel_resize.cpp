#define GEO_TEST
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
            retro_panel::row=retro_panel::Tabs;
            const bool filter=page==retro_panel::Section::ScreenFilter;
            demand(begin_retro_frame(dc,w,h)&&w==example.width&&h==example.height,"drawable dimensions");
            const auto& panel=ui::panel;
            const int mw=ui::main_width(page),right=mw-9;
            ui::Canvas expected(panel.w,panel.h,panel.w);
            demand(expected.pixels!=nullptr,"panel reference");
            pBindFramebuffer(GL_READ_FRAMEBUFFER,panel.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
            std::vector<unsigned char> frame(size_t(panel.w)*panel.h*4);
            glReadPixels(0,0,panel.w,panel.h,GL_BGRA,GL_UNSIGNED_BYTE,frame.data());
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
            }
            const int tab_y=ui::main_y+6+(filter?17:0);
            check_label(filter?L"Screen filter":L"Display",mw+24,tab_y,mw+ui::side_width-5,tab_y+16,ui::mint);
            expected.cursor(mw+4,tab_y);
            for(int cy=0;cy<16;++cy)for(int cx=0;cx<16;++cx){
                if(ui::hand[cy][cx]=='0')continue;
                const auto offset=4*(size_t(panel.h-1-tab_y-cy)*panel.w+mw+4+cx);
                demand(memcmp(expected.pixels+offset,frame.data()+offset,4)==0,"wrong cursor on focused section");
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
                const int y=ui::row_top(retro_panel::ModUpdate,false)+ui::display_row+ui::version_height-2;
                for(int quarter:{1,3}){
                    const int x=9+(right-9)*quarter/4;
                    BYTE bgra[4]{};
                    glReadPixels(x,panel.h-1-y,1,1,GL_BGRA,GL_UNSIGNED_BYTE,bgra);
                    demand(RGB(bgra[2],bgra[1],bgra[0])==(quarter==1?ui::mint:ui::track),"update progress not visible");
                }
                retro_panel::update_status=nullptr;retro_panel::update_bytes=nullptr;
            }
            demand(SwapBuffers(dc),"present");
            retro_display::end_window(true,false,w,h);
        }
    }
    puts("Display/filter tabs: labels, active tab, resize, manual and automatic output, footer and compact heights OK");
}
