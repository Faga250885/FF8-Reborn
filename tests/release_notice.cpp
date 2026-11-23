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
    constexpr unsigned first=3400, earlier=3399, second=3401;
    assert(life.opacity(0,40)==0&&life.last_shown==0);
    assert(life.opacity(first,40)==1&&life.opacity(first,60)==1&&life.last_shown==0);
    assert(life.presented(first,61)&&!life.presented(first,65));
    assert(life.opacity(first,71)==1);
    assert(life.opacity(first,71.5)==.75f&&life.opacity(first,72)==.5f&&life.opacity(first,72.5)==.25f);
    assert(life.opacity(first,73)==0&&life.opacity(first,100)==0);
    assert(!life.presented(earlier,100)&&life.opacity(earlier,100)==0);
    assert(life.opacity(0,100)==0&&!life.presented(0,100));
    assert(life.opacity(second,101)==1&&life.presented(second,101)&&!life.presented(second,102));
    assert(life.opacity(second,112)==.5f&&life.opacity(second,113)==0);
    HWND win=CreateWindowA("STATIC","Update notice",WS_POPUP,0,0,800,550,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(win);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt));
    assert(wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl());
    // The UI test controls the feed result. The production network worker
    // is exercised separately, including a real first-present HTTP request.
    assert(!mod_update::renderer);
    display_settings.fps_counter=0;display_settings.retro={};
    int w=0,h=0;unsigned drawn=999;
    mod_update::offered.build=first;
    for(auto status:{update_idle,update_checking,update_current,update_error,update_downloading,update_verifying,update_saving,update_ready}){
        mod_update::say(status,L"");
        assert(!begin_retro_frame(dc,w,h,&drawn)&&!drawn&&update_notice::lifetime.last_shown==0);
    }
    mod_update::say(update_available,L"");fill(0);
    retro_panel::open=true;
    assert(begin_retro_frame(dc,w,h,&drawn)&&!drawn&&update_notice::lifetime.last_shown==0);
    retro_display::end_window(false,false,w,h);retro_panel::open=false;
    assert(begin_retro_frame(dc,w,h,&drawn)&&drawn==first);
    update_notice::after_swap(drawn,false);
    assert(update_notice::lifetime.last_shown==0);
    retro_display::end_window(false,false,w,h);
    assert(begin_retro_frame(dc,w,h,&drawn)&&drawn==first);
    assert(SwapBuffers(dc));
    retro_display::end_window(true,false,w,h);
    update_notice::after_swap(drawn,true);
    const double visible_at=update_notice::lifetime.began;
    update_notice::after_swap(drawn,true);
    assert(update_notice::lifetime.last_shown==first&&update_notice::lifetime.began==visible_at);
    Surface color;resize(color,w,h,false);
    for(int counter:{0,1,3}){
        int occupied=fps_counter::tile_h*fps_counter::scale(counter,w,h);
        auto l=update_notice::place(w,h,occupied);
        assert(l.x>=0&&l.y>=0&&l.x+l.w<=w&&l.y+l.h<=h-occupied);
        fill(color.fbo);
        auto sample=[&](){return pixel(color.fbo,l.x+7*l.w/update_notice::width(),l.y+l.h/2);};
        auto corner=[&](){return pixel(color.fbo,l.x,l.y+l.h-1);};
        Pixel background=sample();
        assert(update_notice::draw(color.fbo,l,1));
        Pixel solid=sample();
        assert(solid[0]==solid[1]&&solid[2]>solid[0]+40);
        assert(corner()==background);
        assert(pixel(color.fbo,l.x-1,l.y+3)==background);
        for(float a:{.75f,.5f,.25f,0.f}){
            fill(color.fbo);assert(update_notice::draw(color.fbo,l,a)==(a>0));
            Pixel mixed=sample();
            for(unsigned c=0;c<3;c++)
                assert(std::abs(mixed[c]-int(std::lround(solid[c]*a+background[c]*(1-a))))<=1);
            assert(mixed[3]==background[3]);
            assert(corner()==background);
            assert(pixel(color.fbo,l.x-1,l.y+3)==background);
        }
        assert(solid[3]==background[3]);
    }
    assert(glGetError()==GL_NO_ERROR);
    puts("Update notice passed: first successful swap per build, 10s hold, 2s fade, pixel border, alpha and FPS counter clearance.");
}
