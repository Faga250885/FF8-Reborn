#define GEO_TEST
#include "../src/geometry.cpp"
using namespace geo;
namespace fm=field_motion_blur;

int main(){
    fm::Layer first,second;
    first.texture=second.texture=12;
    for(int i=0;i<8;i++){
        first.points[first.count++]={100u+unsigned(i),i*128,50};
        second.points[second.count++]={100u+unsigned(i),i*128+24,50};
    }
    demand(fm::estimate(first,second).x==3,"known scroll in source pixels");
    demand(fm::estimate(first,first).x==0,"stationary field");
    auto cut=second;
    for(auto& point:cut.points)point.x+=128;
    demand(fm::estimate(first,cut).x==0,"camera cut");
    for(unsigned i=0;i<4;i++)second.points[i].x-=24;
    demand(fm::estimate(first,second).x==0,"contradictory moving layers");
    fm::history.begin(1,100,1.);
    fm::history.now=first;fm::history.begin(2,101,1.03);
    demand(!fm::history.old.count,"different field");
    fm::history.now=first;fm::history.begin(3,101,1.60);
    demand(!fm::history.old.count,"resuming after a pause");

    HWND window=CreateWindowA("STATIC","Field exposure",WS_POPUP,0,0,640,480,0,0,GetModuleHandleA(nullptr),0);
    HDC display=GetDC(window);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    demand(SetPixelFormat(display,ChoosePixelFormat(display,&fmt),&fmt)&&
           wglMakeCurrent(display,wglCreateContext(display))&&initialize_gl(),"OpenGL context");
    Surface color;
    retro_display::allocate(color,320,240);
    std::vector<BYTE> source(320*240*4);
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
        BYTE* p=source.data()+4*(y*320+x);
        p[0]=(x%4<2)?240:20;p[1]=x>=160?125:45;p[2]=x>=160?220:10;p[3]=x>=160?0:255;
    }
    pActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color.color);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,source.data());
    auto make_sampler=retro_display::procedure<PFNGLGENSAMPLERSPROC>("glGenSamplers");
    retro_display::inspect_context();
    GLuint sampler=0;make_sampler(1,&sampler);
    retro_display::pipeline.bind_sampler(0,sampler);
    pActiveTexture(GL_TEXTURE1);
    glEnable(GL_SCISSOR_TEST);glScissor(16,17,28,29);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
    auto filtered=[&](int strength){
        {
            fm::UnitZero saved;
            demand(fm::filter_texture({6,0},strength),"private color texture");
        }
        GLint active=0,texture=0,read=0,draw=0,program=0,bound_sampler=0;GLboolean mask[4];
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        demand(active==GL_TEXTURE1&&!read&&!draw&&!program&&glIsEnabled(GL_SCISSOR_TEST)&&
               !mask[0]&&mask[1]&&!mask[2]&&!mask[3],"restored game draw state");
        pActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_SAMPLER_BINDING,&bound_sampler);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        demand(GLuint(texture)==color.color&&GLuint(bound_sampler)==sampler,"restored original color sampler");
        pActiveTexture(GL_TEXTURE1);
        pBindFramebuffer(GL_READ_FRAMEBUFFER,fm::filtered.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::vector<BYTE> pixels(source.size());
        glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        pBindFramebuffer(GL_READ_FRAMEBUFFER,0);
        return pixels;
    };
    auto subdued=filtered(30),strong=filtered(100);
    demand(subdued!=strong&&subdued!=source&&strong!=source,"strength varies exposure");
    for(size_t i=0;i<source.size();i+=4){
        demand(strong[i+3]==source[i+3],"unchanged alpha mask");
        demand(strong[i+2]==source[i+2],"no color leaking across coverage edge");
    }
    pBindFramebuffer(GL_READ_FRAMEBUFFER,color.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<BYTE> original(source.size());
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,original.data());
    demand(original==source&&glGetError()==GL_NO_ERROR,"game-owned color layer preserved");
    puts("Field landmarks, camera cuts, adjustable exposure, coverage and OpenGL state OK.");
}
