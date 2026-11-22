#define GEO_TEST
#include "../src/geometry.cpp"

using namespace geo;
namespace ui=retro_panel_draw;

struct OutputExample { int width,height;const wchar_t* label; };

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
        {1280,720,L"720p / 800p · 1280×720"}, {1280,800,L"720p / 800p · 1280×800"},
        {1920,1080,L"1080p · 1920×1080"}, {2560,1440,L"1440p (2K) · 2560×1440"},
        // Same panel dimensions and tier: only the window width changes.
        {3440,1440,L"1440p (2K) · 3440×1440"}, {3840,2160,L"2160p (4K) · 3840×2160"},
        {1280,800,L"720p / 800p · 1280×800"}
    };
    for(const auto& example:examples){
        demand(SetWindowPos(window,nullptr,0,0,example.width,example.height,
                            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize window");
        int w=0,h=0;
        demand(begin_retro_frame(dc,w,h)&&w==example.width&&h==example.height,"drawable dimensions");
        const auto& panel=ui::panel;
        ui::Canvas expected(panel.w,panel.h);
        demand(expected.pixels!=nullptr,"panel reference");
        const int top=ui::row_top(retro_panel::Profile,true)-ui::filter_header_height+28;
        expected.box(126,top,385,top+22,RGB(23,31,35));
        expected.label(example.label,126,top,385,top+22,0,RGB(181,201,199),true);
        GdiFlush();
        const int left=expected.px(126),width=expected.px(385)-left,height=expected.px(top+22)-expected.px(top);
        const int bottom=panel.h-expected.px(top+22);
        std::vector<BYTE> actual(size_t(width)*height*4);
        pBindFramebuffer(GL_READ_FRAMEBUFFER,panel.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(left,bottom,width,height,GL_BGRA,GL_UNSIGNED_BYTE,actual.data());
        for(int y=0;y<height;y++)for(int x=0;x<width;x++){
            const BYTE* ref=&expected.pixels[4*(size_t(y+bottom)*panel.w+left+x)];
            demand(memcmp(ref,&actual[4*(size_t(y)*width+x)],3)==0,"incorrect or stale output tuning label");
        }
        SIZE extent{};
        demand(GetTextExtentPoint32W(expected.dc,example.label,int(wcslen(example.label)),&extent)&&
               extent.cx<=width,"output label clipped");
        const int counter=fps_counter::tile_h*fps_counter::scale(3,w,h);
        const int margin=expected.px(16);
        demand(panel.h+margin+counter<=h&&panel.w+margin<=w,"panel outside drawable");
        demand(expected.px(ui::row_top(retro_panel::SaveCustom,false)+32+75)<=panel.h,"footer clipped");
        demand(SwapBuffers(dc),"present");
        retro_display::end_window(true,false,w,h);
    }
    puts("Filter panel: live resize, width-only change, four output classes and layout OK");
}
