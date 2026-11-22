#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
namespace rd=retro_display;

int main(){
    HWND window=CreateWindowA("STATIC","motion exposure",WS_POPUP,0,0,96,64,
        nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt));
    assert(wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl());
    RetroSettings filter{};
    double stamp=1.;
    auto frame=[&](int x,int amount,int scene,bool swapped=true){
        stamp+=1./30.;
        pBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glReadBuffer(GL_BACK);
        glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(x,0,3,64);glClearColor(1,1,1,1);
        glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        rd::motion_history.begin(amount,scene,dc,96,64,stamp);
        bool pass=rd::begin_window(filter,dc,96,64);
        std::array<GLubyte,8> pixels{};
        glReadPixels(16,32,8,1,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
        if(swapped)assert(SwapBuffers(dc));
        if(pass)rd::end_window(swapped,false,96,64);
        assert(glGetError()==GL_NO_ERROR);
        return pixels;
    };
    for(bool exchange:{false,true}){
        filter={};
        rd::swap_dc=dc;rd::exchanges_front=exchange;rd::valid_front=false;
        rd::motion_history.reset();
        const auto original=frame(19,0,1);
        assert(original[2]==0&&original[3]==255);
        frame(16,100,1);
        const auto high=frame(19,100,1);
        // The previous release capped the maximum brightening at 31/255.
        // The new exposure softens both sides of a translating silhouette.
        assert(high[2]>64&&high[2]<=110&&high[3]<225&&high[3]>128);
        assert(frame(19,100,1)==original); // No feedback from the displayed tail.
        const std::array<GLubyte,8> black{};
        assert(frame(96,100,1)==black);
        int last=0;
        for(int strength=10;strength<=100;strength+=10){
            frame(16,strength,1);
            const auto result=frame(19,strength,1);
            assert(result[2]>last&&result[6]==0);
            last=result[2];
        }
        frame(16,100,1);
        assert(frame(19,100,2)==original);
        frame(16,100,2,false);
        assert(frame(19,100,2)==original);
        frame(16,100,2);
        stamp+=.4;
        assert(frame(19,100,2)==original);
        frame(16,100,2);
        assert(frame(19,0,2)==original);
        frame(16,100,2);
        assert(frame(19,100,-1)==original); // menu or full-screen map
        assert(frame(19,100,2)==original);  // fresh gameplay history
        for(bool tv:{false,true}){
            filter=RetroSettings::recommended();
            filter.lines=24; // Exercise the Deck's two-pass signal at fixture size.
            if(tv)filter.phosphors=RetroSettings::aperture_grille;
            auto reference=frame(19,0,2);
            assert(frame(19,100,2)==reference);
            assert(frame(19,100,2)==reference);
            frame(16,100,2);
            frame(19,100,2);
            assert(frame(19,100,2)==reference);
        }
    }
    // Exercise the game's real presentation entry, including its layout and
    // atlas checks. The selected profile is the sole source of the strength.
    display_settings.retro={};
    display_settings.retro.motion_blur=80;
    auto routed=[&](bool expected){
        int w=0,h=0;
        bool applied=begin_retro_frame(dc,w,h);
        assert(rd::motion_history.strength==(expected?80:0));
        assert(rd::motion_history.pending==expected);
        assert(SwapBuffers(dc));
        if(applied)rd::end_window(true,false,w,h);
    };
    for(int mode:{1,2,3}){
        current={};current.mode=mode;native_atlas_view=0;
        routed(true);
        current.menu_edge=1;routed(false);
        current.menu_edge=0;current.camera_cut=true;routed(false);
        current.camera_cut=false;current.travel=Frame::Travel::fade;routed(false);
    }
    current={};current.mode=2;native_atlas_view=3;routed(false);
    current.mode=6;routed(false);
    current.mode=3;display_settings.retro.motion_blur=0;routed(false);
    std::puts("Motion blur: both edges, 10 strengths, still frames, both swap methods, scene routing, cuts, pause, failed swap and Off OK");
}
