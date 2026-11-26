#define GEO_TEST
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
int main(){
    HWND window=CreateWindowA("STATIC","Encounter blend",WS_POPUP,0,0,128,80,
                               nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR fmt{};
    fmt.nSize=sizeof(fmt);fmt.nVersion=1;
    fmt.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    fmt.iPixelType=PFD_TYPE_RGBA;fmt.cColorBits=32;fmt.cDepthBits=24;fmt.cStencilBits=8;
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt));
    assert(wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl());
    width=128;height=80;
    resize(encounter_previous_screen,width,height,false,true);
    resize(composed_screen,width,height,false,true);
    const auto paint=[](const Surface& s,float red,float blue){
        pBindFramebuffer(GL_FRAMEBUFFER,s.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearColor(red,0,blue,1);glClear(GL_COLOR_BUFFER_BIT);
    };
    paint(encounter_previous_screen,1,0);paint(composed_screen,0,1);
    Surface output;resize(output,width,height,false);
    for(float alpha:{0.f,.25f,.5f,.75f,1.f}){
        blit(composed_screen.fbo,output.fbo,width,height,width,height,GL_COLOR_BUFFER_BIT);
        draw_encounter_blend(output.fbo,alpha);
        pBindFramebuffer(GL_READ_FRAMEBUFFER,output.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
        for(int x:{1,width/2,width-2}){
            BYTE rgb[4]{};
            glReadPixels(x,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
            assert(std::abs(rgb[0]-255*(1-alpha))<=1&&rgb[1]==0&&std::abs(rgb[2]-255*alpha)<=1);
        }
    }
    assert(glGetError()==GL_NO_ERROR);
    puts("Five encounter phases verified at the center and both edges");
}
