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
    QuickFilterNotice preview;
    preview.show(8,10000);
    assert(preview.opacity(12000)==1.f&&preview.opacity(12250)==.5f);
    preview.show(7,12250);
    assert(preview.profile==7&&preview.opacity(12250)==1.f&&preview.opacity(14250)==1.f);
    assert(preview.opacity(14750)==0.f&&preview.profile==-1);
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
    // The Off label is an overlay in its own right, with the update and FPS
    // counter both disabled. Its clock and presentation never play a sound.
    mod_update::say(update_current,L"");
    quick_notice.show(0,GetTickCount64());
    fill(0);
    assert(begin_retro_frame(dc,w,h,&drawn)&&!drawn&&filter_notice::cached_profile==0);
    const auto off=filter_notice::place(w,h,0,0);
    auto off_pixel=pixel(0,off.x+6*off.w/filter_notice::width(0),off.y+off.h/2);
    assert(off_pixel[2]>off_pixel[0]+40);
    retro_display::end_window(false,false,w,h);
    // When both notifications are visible, verify rectangles, rendered pixels
    // and fresh profile text even when repeatedly cycling during the fade.
    display_settings.fps_counter=3;
    mod_update::say(update_available,L"");update_notice::lifetime={};
    const auto update=update_notice::place(w,h,fps_counter::tile_h*fps_counter::scale(3,w,h));
    for(int id:{9,0,7}){
        quick_notice.show(id,GetTickCount64());
        fill(0);assert(begin_retro_frame(dc,w,h,&drawn)&&drawn==first&&filter_notice::cached_profile==id);
        const auto next=filter_notice::place(w,h,h-update.y,id);
        assert(next.w&&next.h&&next.y>=0&&next.x+next.w<=w&&next.y+next.h<update.y);
        assert(filter_notice::tile.w==filter_notice::width(id));
        auto filter_pixel=pixel(0,next.x+6*next.w/filter_notice::width(id),next.y+next.h/2);
        assert(filter_pixel[2]>filter_pixel[0]+40);
        auto update_pixel=pixel(0,update.x+6*update.w/update_notice::width(),update.y+update.h/2);
        assert(update_pixel[2]>update_pixel[0]+40);
        retro_display::end_window(false,false,w,h);
    }
    assert(glGetError()==GL_NO_ERROR);
    puts("Update and quick filter notices passed: timing, per-profile text, Off overlay, stacking, alpha and counter clearance.");
}
