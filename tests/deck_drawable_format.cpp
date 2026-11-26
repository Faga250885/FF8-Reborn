#define GEO_TEST
#include "../src/geometry.cpp"
namespace geo {
#include "../src/test_snapshot.h"
}
using namespace geo;

int main(int argc,char** argv){
    if(argc!=2)return 2; // Path for a disposable snapshot.
    HWND window=CreateWindowA("STATIC","Drawable format",WS_POPUP,0,0,256,256,
                              nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC drawable=GetDC(window);
    PIXELFORMATDESCRIPTOR format{sizeof(format),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|
                                 PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    format.cDepthBits=24;format.cStencilBits=8;
    demand(SetPixelFormat(drawable,ChoosePixelFormat(drawable,&format),&format)&&
           wglMakeCurrent(drawable,wglCreateContext(drawable))&&initialize_gl(),"WGL");
    const int window_stencil=stencil_bits;
    demand(window_stencil>0,"test needs a window with stencil");
    Surface offscreen;
    resize(offscreen,256,256,true,false,GL_RGBA8,0);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,offscreen.fbo);
    GLint offscreen_stencil=-1,draw=-1,read=-1;
    glGetIntegerv(GL_STENCIL_BITS,&offscreen_stencil);
    demand(offscreen_stencil==0&&initialize_gl(),"initialize with depth-only FBO bound");
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    demand(stencil_bits==window_stencil&&GLuint(draw)==offscreen.fbo&&read==0,
           "window queried without changing game bindings");
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    stencil_bits=0;
    snapshot::pair(argv[1],false);
    stencil_bits=window_stencil;
    snapshot::pair(argv[1],true);
    demand(stencil_bits==window_stencil,"snapshot overwrote window stencil format");
    release(offscreen);
    std::puts("Window format preserved across initialization and snapshot replay");
}
