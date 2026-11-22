#define GEO_TEST
#include "../src/geometry.cpp"

using namespace geo;

static Frame field(int w,int h,int source_x,int source_w){
    Frame f;f.w=w;f.h=h;f.mode=1;
    auto& scene=f.targets[7];scene.w=1280;scene.h=960;
    auto& tiles=f.ops.emplace_back();tiles.target=7;tiles.mode=GL_TRIANGLES;
    tiles.state.projection[0]=2.f/640;
    tiles.state.viewport[2]=1280;tiles.state.viewport[3]=960;
    tiles.state.scissor_on=true;tiles.state.scissor[2]=1280;tiles.state.scissor[3]=896;

    auto& copy=f.ops.emplace_back();copy.mode=GL_TRIANGLES;
    copy.state.texture[0].id=7;copy.state.texture[0].enabled=true;
    copy.state.viewport[0]=source_x;copy.state.viewport[2]=source_w;copy.state.viewport[3]=h;
    for(int i=0;i<16;i+=5)copy.state.projection[i]=copy.state.model[i]=1;
    for(int corner:{0,1,2,2,1,3}){
        Vertex v{};v.p[0]=(corner&1)?1:-1;v.p[1]=(corner&2)?1:-1;v.p[3]=1;
        v.uv[0][0]=(corner&1)?1:0;v.uv[0][1]=(corner&2)?1:0;copy.vertices.push_back(v);
    }
    return f;
}

static void check(int w,int h,int source_x,int source_w){
    const Frame f=field(w,h,source_x,source_w);
    const WideView view(f,true);
    demand(view.outer_width>0&&view.outer_width<w,"expected lateral field margins");
    const int left=(w-view.outer_width)/2,right=left+view.outer_width;
    Surface output;resize(output,w,h,false);
    for(bool blur:{false,true}){
        display_settings.field_sides=blur;
        pBindFramebuffer(GL_FRAMEBUFFER,output.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(source_x,0,source_w,h);
        glClearColor(.8f,.8f,.8f,1);glClear(GL_COLOR_BUFFER_BIT);
        // A dark vertical feature inside the artwork must stay dark.
        glScissor(source_x+source_w/2-3,0,6,h);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        demand(present_native_layout(f,view,output.fbo),"present native field");
        pBindFramebuffer(GL_READ_FRAMEBUFFER,output.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::vector<BYTE> row(size_t(w)*4);glReadPixels(0,h/2,w,1,GL_RGBA,GL_UNSIGNED_BYTE,row.data());
        auto light=[&](int x){return int(row[4*x]);};
        demand(light(w/2)==0,"keep dark features inside the actual field");
        if(!blur){
            demand(light(left-1)==0&&light(right)==0,"side fill disabled");
            continue;
        }
        for(int x:{left-1,left,left+1,right-2,right-1,right})
            demand(light(x)>=195,"black gutter leaked into one of the field joins");
        demand(std::abs(light(left)-light(right-1))<=1,"left and right joins must sample symmetrically");
    }
    release(output);
}

static void check_partial_copy(){
    Frame f=field(1280,800,107,1066);
    f.travel=Frame::Travel::scene;
    // A partial render-target copy during a field transition cannot describe
    // the source rectangle of the whole field.
    auto& vp=f.ops.back().state.viewport;
    vp[0]=320;vp[1]=160;vp[2]=640;vp[3]=480;
    const WideView view(f,true);
    Surface output;resize(output,f.w,f.h,false);
    display_settings.field_sides=true;
    pBindFramebuffer(GL_FRAMEBUFFER,output.fbo);glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(107,0,170,f.h);
    glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    demand(present_native_layout(f,view,output.fbo),"present outgoing field");
    pBindFramebuffer(GL_READ_FRAMEBUFFER,output.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    BYTE rgb[4]{};glReadPixels(150,400,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
    demand(rgb[0]==255,"a small centered operation must not crop the outgoing field");
    release(output);
}

int main(){
    HWND window=CreateWindowA("STATIC","Field edges",WS_POPUP,0,0,256,256,nullptr,nullptr,
                              GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR format{sizeof(format),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,
                                 PFD_TYPE_RGBA,32};
    demand(SetPixelFormat(dc,ChoosePixelFormat(dc,&format),&format)&&
           wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl(),"OpenGL");
    check(1280,800,107,1066);
    check(1280,800,107,1067);
    check(1280,720,160,960);
    check(2560,1440,320,1920);
    check_partial_copy();
    puts("Prerendered field joins match on both sides at 800p, 720p and 1440p.");
}
