#define GEO_TEST
#include "../src/geometry.cpp"

using namespace geo;

static Frame copy_frame(int w,int h,int sw,int sh){
    Frame f;f.mode=card_transition::mode;f.w=w;f.h=h;
    f.targets[7].active=true;
    auto& op=f.ops.emplace_back();op.mode=GL_TRIANGLES;
    op.state.texture[0].id=7;
    op.state.texture[0].enabled=true;
    int vp[]={(w-sw)/2,(h-sh)/2,sw,sh};std::copy_n(vp,4,op.state.viewport);
    for(int i:{0,1,2,2,1,3}){
        Vertex v{};v.p[0]=(i&1)?1:-1;v.p[1]=(i&2)?1:-1;v.p[3]=1;
        v.uv[0][0]=(i&1)?1:0;v.uv[0][1]=(i&2)?1:0;
        op.vertices.push_back(v);
    }
    for(int i=0;i<16;i+=5)op.state.model[i]=op.state.projection[i]=1;
    return f;
}

static void check_size(int w,int h){
    const int sw=640,sh=480;
    Frame field;field.w=w;field.h=h;field.mode=1;field.travel=Frame::Travel::scene;
    Frame compact=copy_frame(w,h,sw,sh);
    demand(card_transition::entering(compact,field),"start of card game");
    const auto src=card_transition::source_rect(compact,field,card_transition::final_copy(compact));
    demand(src.w==sw&&src.h==sh,"compact render target");
    Frame held;held.mode=card_transition::mode;held.w=w;held.h=h;
    demand(card_transition::source_rect(held,compact,nullptr).w==sw,"hold compact framing across empty swap");
    held.valid=false;
    demand(!card_transition::source_rect(held,compact,nullptr).w,"invalid capture must not reuse an old source");
    held.valid=true;held.ops.emplace_back().clear=GL_COLOR_BUFFER_BIT;
    demand(!card_transition::source_rect(held,compact,nullptr).w,"window clear ends compact handoff");
    Surface target,backup;resize(target,w,h,false);
    pBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(src.x,src.y,sw,sh);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    demand(card_transition::present(compact,src,backup,target.fbo),"expand card copy");
    pBindFramebuffer(GL_READ_FRAMEBUFFER,target.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    for(int y:{1,h/2,h-2}){
        BYTE p[4]{};glReadPixels(w/2,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        demand(p[0]==255&&p[1]==0,"visible at full height on first swap");
    }
    BYTE side[4]{};glReadPixels(0,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,side);
    demand(!side[0],"card aspect preserved");
    Frame fade=copy_frame(w,h,int(std::lround(h*4./3)),h);
    card_transition::resolve(fade,field,card_transition::final_copy(fade));
    demand(fade.layout()==1,"preserve outgoing field through a copy-only swap");
    Frame empty;empty.mode=card_transition::mode;empty.w=w;empty.h=h;
    card_transition::resolve(empty,fade,nullptr);
    demand(empty.layout()==1,"no one-frame shrink between two load swaps");
    Frame board=copy_frame(w,h,w,h);
    demand(!card_transition::compact(board,card_transition::final_copy(board)).w,"native board already fits");
    Frame boxed=board;
    for(auto& v:boxed.ops.back().vertices){v.p[0]*=float(sw)/w;v.p[1]*=float(sh)/h;}
    const auto inner=card_transition::compact(boxed,card_transition::final_copy(boxed));
    demand(inner.w==sw&&inner.h==sh&&inner.x==src.x&&inner.y==src.y,"inset final quad inside full viewport");
    auto atlas=board.ops.back();atlas.target=7;atlas.state.texture[0].id=32;
    board.ops.insert(board.ops.begin(),atlas);
    card_transition::resolve(board,empty,card_transition::final_copy(board));
    demand(board.layout()==board.mode,"native board takes over");
    compact.ops.back().state.texture[0].id=32;
    demand(!card_transition::final_copy(compact),"card sprite is not the output copy");
    compact.ops.back().state.texture[0].id=7;
    for(auto& v:compact.ops.back().vertices)v.uv[0][0]*=.5f;
    demand(!card_transition::final_copy(compact),"partial atlas should not scale the screen");
    Frame title;title.mode=12;
    demand(!card_transition::entering(title,field),"unrelated modes");
    release(backup);release(target);
}

int main(){
    HWND win=CreateWindowA("STATIC","Card handoff",WS_POPUP,0,0,256,256,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(win);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    demand(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt)&&wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl(),"OpenGL");
    check_size(1280,800);check_size(1920,1080);check_size(3440,1440);
    puts("Triple Triad: compact, copy-only and empty load swaps, then native board, at three aspect ratios");
}
