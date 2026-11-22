#define GEO_TEST
#ifndef GEO_NATIVE_CAPTURE
#define GEO_NATIVE_CAPTURE
#endif
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>
using namespace geo;

using Pixel=std::array<BYTE,4>;
static Pixel pixel(GLuint target,int x,int y){
    pBindFramebuffer(GL_READ_FRAMEBUFFER,target);glReadBuffer(target?GL_COLOR_ATTACHMENT0:GL_BACK);
    Pixel p{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;
}
static void fill(GLuint target){
    pBindFramebuffer(GL_FRAMEBUFFER,target);glDrawBuffer(target?GL_COLOR_ATTACHMENT0:GL_BACK);
    glDisable(GL_SCISSOR_TEST);glClearColor(1,.2f,.4f,.38f);glClear(GL_COLOR_BUFFER_BIT);
}
int main(){
    using namespace retro_panel;
    update_notice::Lifetime life;
    assert(life.opacity(false,40)==0&&life.began<0);
    assert(life.opacity(true,61)==1&&life.opacity(true,71)==1);
    assert(life.opacity(true,71.5)==.75f&&life.opacity(true,72)==.5f&&life.opacity(true,72.5)==.25f);
    assert(life.opacity(true,73)==0&&life.opacity(true,100)==0);
    HWND win=CreateWindowA("STATIC","Update notice",WS_POPUP,0,0,800,550,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(win);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt));
    assert(wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl());
    // The UI test controls the feed result. The production network worker
    // is exercised separately, including a real first-present HTTP request.
    assert(!mod_update::renderer);
    display_settings.fps_counter=0;display_settings.retro={};
    int w=0,h=0;
    for(auto status:{update_idle,update_checking,update_current,update_error,update_downloading,update_verifying,update_saving,update_ready}){
        mod_update::say(status,L"");
        assert(!begin_retro_frame(dc,w,h)&&update_notice::lifetime.began<0);
    }
    mod_update::say(update_available,L"");fill(0);
    retro_panel::open=true;
    assert(begin_retro_frame(dc,w,h)&&update_notice::lifetime.began<0);
    retro_display::end_window(false,false,w,h);retro_panel::open=false;
    assert(begin_retro_frame(dc,w,h)&&update_notice::lifetime.began>=0);
    retro_display::end_window(false,false,w,h);
    Surface color;resize(color,w,h,false);
    for(int counter:{0,1,3}){
        int occupied=fps_counter::tile_h*fps_counter::scale(counter,w,h);
        auto l=update_notice::place(w,h,occupied);
        assert(l.x>=0&&l.y>=0&&l.x+l.w<=w&&l.y+l.h<=h-occupied);
        fill(color.fbo);
        auto sample=[&](){return pixel(color.fbo,l.x+l.w-6,l.y+3);};
        Pixel background=sample();
        assert(update_notice::draw(color.fbo,l,1));
        Pixel solid=sample();
        assert(solid[0]>=22&&solid[0]<=24&&solid[1]==31&&solid[2]==35);
        assert(pixel(color.fbo,l.x-1,l.y+3)==background);
        for(float a:{.75f,.5f,.25f,0.f}){
            fill(color.fbo);assert(update_notice::draw(color.fbo,l,a)==(a>0));
            Pixel mixed=sample();
            for(unsigned c=0;c<3;c++)
                assert(std::abs(mixed[c]-int(std::lround(solid[c]*a+background[c]*(1-a))))<=1);
            assert(mixed[3]==background[3]);
            assert(pixel(color.fbo,l.x-1,l.y+3)==background);
        }
        assert(solid[3]==background[3]);
    }
    assert(glGetError()==GL_NO_ERROR);
    puts("Update availability, 10-second hold, 2-second fade, RGB blending, alpha and FPS clearance passed.");
}
