#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
namespace rd=retro_display;

int main(){
    HWND window=CreateWindowA("STATIC","phosphor response",WS_POPUP,0,0,96,64,
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
        rd::phosphor_history.begin(amount,scene,dc,96,64,stamp);
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
        rd::phosphor_history.previous_time=0;
        const auto original=frame(19,0,1);
        assert(original[2]==0&&original[3]==255);
        frame(16,100,1);
        const auto high=frame(19,100,1);
        assert(high[0]==0&&high[2]>0&&high[2]<=32&&high[3]==255);
        assert(frame(19,100,1)==original); // No feedback from the displayed tail.
        const std::array<GLubyte,8> black{};
        assert(frame(96,100,1)==black);
        frame(16,25,1);
        const auto low=frame(19,25,1);
        assert(low[2]>0&&low[2]<high[2]);
        frame(16,100,1);
        assert(frame(19,100,2)==original);
        frame(16,100,2,false);
        assert(frame(19,100,2)==original);
        frame(16,100,2);
        stamp+=.4;
        assert(frame(19,100,2)==original);
        frame(16,100,2);
        assert(frame(19,0,2)==original);
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
    std::puts("CRT persistence: moving edge, static frame, both swap methods, strength, cut, pause, failed swap and Off OK");
}
