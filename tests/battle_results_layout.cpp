#define GEO_TEST
#include "../src/geometry.cpp"

using namespace geo;

static Frame results(int w,int h,int source_w,int source_h){
    Frame f;f.w=w;f.h=h;f.mode=after_battle_mode;
    auto& copy=f.ops.emplace_back();
    copy.mode=GL_TRIANGLES;copy.vertices.resize(6);
    const int vp[]{(w-source_w)/2,(h-source_h)/2,source_w,source_h};
    std::copy_n(vp,4,copy.state.viewport);
    return f;
}

static void check_fit(int w,int h,int source_w,int source_h){
    Frame battle;battle.mode=3;
    Frame f=results(w,h,source_w,source_h);
    resolve_menu_layout(f,battle);
    WideView view(f,true);
    demand(f.layout()==6&&view.menu,"post-battle results must use menu layout immediately");
    const auto& vp=view.result_viewport;
    demand(vp[0]==(w-source_w)/2&&vp[1]==(h-source_h)/2&&vp[2]==source_w&&vp[3]==source_h,
           "results composite viewport");
    demand(std::abs(view.outer_width*3-view.outer_height*4)<=4,"preserve menu aspect");

    // Put green in the visible menu rectangle and red in its native margins.
    // A wrong source viewport leaves a red band above or below the results.
    Surface target;resize(target,w,h,false,true);
    pBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    const int bottom=vp[1]+int(std::lround((480-f.menu_bottom)*vp[3]/480.));
    const int top=vp[1]+int(std::lround((480-f.menu_top)*vp[3]/480.));
    glEnable(GL_SCISSOR_TEST);glScissor(vp[0],bottom,vp[2],top-bottom);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    demand(present_native_layout(f,view,target.fbo),"present results");
    pBindFramebuffer(GL_READ_FRAMEBUFFER,target.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    for(int y:{2,h/2,h-3}){
        BYTE rgb[4]{};
        glReadPixels(w/2,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
        demand(rgb[1]>240&&rgb[0]<15,"full-height result bounds");
    }
    if(w>view.outer_width+4){
        BYTE rgb[4]{};glReadPixels(0,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
        demand(!rgb[0]&&!rgb[1]&&!rgb[2],"centered side margins");
    }
    demand(!WideView(f,false).menu,"original 4:3 remains native");
    Frame world;world.mode=2;resolve_menu_layout(world,f);
    demand(world.layout()==2&&!world.menu_edge,"results must not resize returning world");
    release(target);
}

int main(){
    HWND window=CreateWindowA("STATIC","Results layout",WS_POPUP,0,0,1280,800,
                              nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR format{sizeof(format),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    demand(SetPixelFormat(dc,ChoosePixelFormat(dc,&format),&format)&&
           wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl(),"OpenGL context");
    check_fit(1280,800,960,720);
    check_fit(1920,1080,1440,1080);
    check_fit(3440,1440,1280,960);
    Frame f=results(1280,800,1280,800);
    resolve_menu_layout(f,Frame{});
    demand(!WideView(f,true).menu,"do not crop a fullscreen output");
    f=results(1280,800,960,720);f.ops.back().target=1;
    resolve_menu_layout(f,Frame{});
    demand(!WideView(f,true).menu,"an offscreen draw cannot identify final composition");
    f.ops.clear();resolve_menu_layout(f,Frame{});
    demand(!WideView(f,true).menu,"an empty frame supplies no source");
    puts("Post-battle results fit verified at 800p, 1080p and ultrawide, with inset and full-height source viewports");
}
