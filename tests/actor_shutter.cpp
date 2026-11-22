#define GEO_TEST
#include "../src/geometry.cpp"
#undef NDEBUG
#include <cassert>

using namespace geo;

static void check_selection(){
    Frame frame;
    frame.mode=3;
    Op& op=frame.ops.emplace_back();
    op.state.program=1;op.state.depth=true;op.target=4;op.match=0;op.mode=GL_TRIANGLES;op.indexed_complete=true;
    for(unsigned triangle=0;triangle<7;++triangle)for(unsigned corner=0;corner<3;++corner){
        auto& vertex=op.vertices.emplace_back();
        const float x=float(32*triangle+4*(corner==1));
        const float y=32.f+4*(corner==2);
        vertex.p[0]=x;vertex.p[1]=y;vertex.p[3]=1.f/1024;
        const unsigned group=triangle==1?0:(triangle==4&&corner==1?2:1)<<16;
        BoneSample sample{group,0,short(x*4),short((y-24)*4),1024,0};
        frame.bones.push_back(sample);
        if(triangle==5&&corner==2){sample.group=0;frame.bones.push_back(sample);}
        const float dx=triangle==2?0.f:triangle==6?128.f:4.f;
        op.prior_positions.push_back({x-dx,y,0,vertex.p[3]});
        op.motion_valid.push_back(triangle!=3||corner!=1);
    }
    auto verify=[&](bool eligible){
        actor_shutter::prepare(frame);
        if(eligible)assert((op.shutter_indices==std::vector<GLuint>{0,1,2}));
        else assert(op.shutter_indices.empty());
    };
    verify(true);
    op.indexed_complete=false;verify(false);op.indexed_complete=true;
    frame.camera_cut=true;verify(false);frame.camera_cut=false;
    frame.menu_edge=1;verify(false);frame.menu_edge=0;
    frame.mode=2;verify(false);frame.mode=3;
    op.raster.stencil=true;verify(false);op.raster.stencil=false;
    op.state.depth=false;verify(false);
}

static void check_draw(bool streamed,bool partial){
    constexpr int size=64;
    Surface target;resize(target,size,size);
    pBindFramebuffer(GL_FRAMEBUFFER,target.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);glViewport(0,0,size,size);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,size,0,size,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    glClearColor(0,0,0,.25f);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glColor4f(0,1,0,.25f);
    glBegin(GL_QUADS);
    for(const auto& xy:std::array<std::array<float,2>,4>{{{8,2},{55,2},{55,19},{8,19}}})
        glVertex3f(xy[0],xy[1],.5f);
    glEnd();
    Op op{};
    op.state.depth=true;op.state.depth_func=GL_LESS;op.state.depth_mask=GL_TRUE;
    op.state.src_rgb=op.state.src_alpha=GL_ONE;
    op.state.dst_rgb=op.state.dst_alpha=GL_ZERO;
    op.state.eq_rgb=op.state.eq_alpha=GL_FUNC_ADD;
    for(const auto& xy:std::array<std::array<float,2>,3>{{{20,6},{50,6},{20,53}}}){
        auto& v=op.vertices.emplace_back();v.p[0]=xy[0];v.p[1]=xy[1];v.p[3]=1;
        op.prior_positions.push_back({xy[0]-16,xy[1],0,1});
    }
    op.shutter_indices={0,1,2};
    if(partial)for(unsigned i=0;i<3;++i){
        auto v=op.vertices[i];v.p[0]+=32;
        op.vertices.push_back(v);
    }
    GLuint vbo=0;
    if(streamed){
        pGenBuffers(1,&vbo);pBindBuffer(GL_ARRAY_BUFFER,vbo);
        pBufferData(GL_ARRAY_BUFFER,op.vertices.size()*sizeof(Vertex),op.vertices.data(),GL_STATIC_DRAW);
    }
    auto position=streamed?uintptr_t(0):reinterpret_cast<uintptr_t>(op.vertices.data());
    glVertexPointer(4,GL_FLOAT,sizeof(Vertex),reinterpret_cast<const void*>(position));
    glEnableClientState(GL_VERTEX_ARRAY);glColor4f(1,0,0,.25f);
    glDrawArrays(GL_TRIANGLES,0,GLsizei(op.vertices.size()));
    using Pixels=std::array<BYTE,size*size*4>;
    using Depth=std::array<float,size*size>;
    Pixels before,after;Depth old_z,new_z;
    auto read=[](Pixels& rgb,Depth& z){
        glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,rgb.data());
        glReadPixels(0,0,size,size,GL_DEPTH_COMPONENT,GL_FLOAT,z.data());
    };
    read(before,old_z);
    actor_shutter::draw(op,op.vertices.data(),vbo,position,100,30);
    read(after,new_z);
    assert(old_z==new_z&&before!=after);
    for(unsigned i=0;i<old_z.size();++i){
        assert(before[4*i+3]==after[4*i+3]);
        if(i/size<19||(partial&&i%size>=50))assert(!memcmp(&before[i*4],&after[i*4],4));
    }
    const int edge=(size*22+18)*4;
    assert(!before[edge]&&after[edge]>50&&after[edge]<120);
    GLint bound,func;GLboolean mask;void* ptr=nullptr;
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&bound);glGetPointerv(GL_VERTEX_ARRAY_POINTER,&ptr);
    glGetIntegerv(GL_DEPTH_FUNC,&func);glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
    assert(GLuint(bound)==vbo&&uintptr_t(ptr)==position&&func==GL_LESS&&mask&&!glIsEnabled(GL_BLEND));
    assert(glGetError()==GL_NO_ERROR);
    pBindBuffer(GL_ARRAY_BUFFER,0);
    if(vbo)reinterpret_cast<PFNGLDELETEBUFFERSPROC>(wglGetProcAddress("glDeleteBuffers"))(1,&vbo);
    pBindFramebuffer(GL_FRAMEBUFFER,0);release(target);
}

int main(){
    check_selection();
    HWND window=CreateWindowA("STATIC","actor exposure",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    fmt.cDepthBits=24;
    if(!SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt)||
       !wglMakeCurrent(dc,wglCreateContext(dc))||!initialize_gl())return 1;
    check_draw(false,false);check_draw(true,false);
    check_draw(false,true);check_draw(true,true);
    puts("Actor shutter selection, depth occlusion, alpha and GL state passed.");
}
