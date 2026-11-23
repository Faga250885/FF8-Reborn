#define GEO_TEST
#include "../src/geometry.cpp"
using namespace geo;
static Op rect(float l,float r,float t,float b,float z=.0015f){
 Op op{};op.target=4;op.mode=GL_TRIANGLES;op.state.projection[0]=2.f/640;
 for(int i:{0,1,3,0,3,2}){auto& v=op.vertices.emplace_back();v.p[0]=(i&1)?r:l;v.p[1]=(i&2)?b:t;v.p[2]=z;v.p[3]=1;}
 return op;
}
static Frame frame(int w,int h,int mode,std::initializer_list<Op> ops){
 Frame f;f.w=w;f.h=h;f.mode=mode;f.ops=ops;auto& copy=f.ops.emplace_back();copy.mode=GL_TRIANGLES;copy.vertices.resize(6);
 int nw=std::lround(h*4./3),vp[]={int((w-nw)/2),0,nw,h};std::copy_n(vp,4,copy.state.viewport);return f;
}
static void check(int w,int h){
 Frame white=frame(w,h,0,{rect(-1,1,-1,1,1),rect(-.39f,.39f,-.73f,-.65f,1),rect(-.47f,.47f,-.732f,-.728f,1)});
 resolve_menu_layout(white,Frame{});WideView white_view(white,true);
 demand(white.opening==Frame::Opening::white_title&&white_view.menu&&white_view.outer_height>h,"white title is fitted to visible content");
 demand(std::abs(white_view.outer_width*3-white_view.outer_height*4)<=4,"logo scales uniformly");
 demand(!WideView(white,false).menu,"original aspect stays untouched");
 Frame black=frame(w,h,1,{rect(0,426.667f,16,306,.00106452f),rect(426.667f,640,16,306,.00109677f)});
 resolve_menu_layout(black,white);WideView bv(black,true);
 demand(black.opening==Frame::Opening::black_title&&bv.menu&&bv.outer_height>h,"black title fills the height without stretching");
 Frame save=frame(w,h,1,{rect(450,600,28,72,.00103226f),rect(50,590,74,126,.00135484f)});
 resolve_menu_layout(save,black);WideView sv(save,true);
 demand(save.opening==Frame::Opening::save_menu&&sv.menu&&sv.outer_height>h,"save screens use proportional full-height layout");
 Frame movie=frame(w,h,0,{rect(0,640,60,420,1)});resolve_menu_layout(movie,Frame{});
 demand(movie.opening==Frame::Opening::none&&!WideView(movie,true).menu,"movie frames are not classified as titles");
 Frame credits=frame(w,h,0,{rect(0,256,0,256),rect(256,512,0,256),rect(512,640,256,480)});
 resolve_menu_layout(credits,Frame{});
 demand(credits.opening==Frame::Opening::none,"tiled startup screens are not the white title");
 Frame field=save;auto scenery=rect(0,128,0,64,.00155f);for(auto& v:scenery.vertices)v.p[3]=1.f/3;
 field.ops.insert(field.ops.begin(),scenery);resolve_menu_layout(field,Frame{});
 demand(field.opening==Frame::Opening::none&&field.layout()==1,"field scenery cannot be classified as save menu");
 Surface target;resize(target,w,h,false,true);pBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
 glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
 glEnable(GL_SCISSOR_TEST);const int sx=(w-int(std::lround(h*4./3)))/2;
 glScissor(sx,0,w-2*sx,h);glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
 demand(present_native_layout(white,white_view,target.fbo),"draw white title");
 pBindFramebuffer(GL_READ_FRAMEBUFFER,target.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
 for(int x:{0,w/2,w-1}){BYTE pixel[4]{};glReadPixels(x,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);demand(pixel[0]>245&&pixel[1]>245&&pixel[2]>245,"white title background reaches both edges");}
 const int nw=black.ops.back().state.viewport[2];
 std::vector<BYTE> full_left(h*4),full_right(h*4);
 for(int level:{255,128,0}){
     pBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);glDisable(GL_SCISSOR_TEST);
     glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
     glEnable(GL_SCISSOR_TEST);glScissor(sx+nw/3,h/2,nw/3,h*2/5);
     glClearColor(level/255.f,level/255.f,level/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
     demand(present_native_layout(black,bv,target.fbo),"draw black title");
     pBindFramebuffer(GL_READ_FRAMEBUFFER,target.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
     BYTE center[4]{};glReadPixels(w/2,h*2/3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,center);
     demand(center[0]==level&&center[1]==level,"central title is the native frame");
     for(bool right:{false,true}){
         std::vector<BYTE> pixels(h*4);
         glReadPixels(right?w-5:4,0,1,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
         auto& full=right?full_right:full_left;
         if(level==255)full=pixels;
         int peak=0;
         for(int row=0;row<h;row++){
             peak=std::max(peak,int(full[row*4]));
             for(int c=0;c<3;c++){
                 int i=row*4+c;
                 demand(std::abs(pixels[i]-full[i]*level/255.)<=2,"painted fire follows native fade");
                 if(row<h/3)demand(pixels[i]==0,"fire stays above the menu");
             }
         }
         demand(peak>30,"distinct visible fire reaches each side");
     }
     demand(full_left!=full_right,"the two wings are independently painted");
 }
 release(target);
}
int main(){
 HWND window=CreateWindowA("STATIC","Opening layout",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
 HDC dc=GetDC(window);PIXELFORMATDESCRIPTOR format{sizeof(format),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
 demand(SetPixelFormat(dc,ChoosePixelFormat(dc,&format),&format)&&wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl(),"OpenGL context");
 check(1280,800);check(1920,1080);check(2560,1440);check(2560,1080);
 puts("Opening and save layout, painted title margins, native fades and movie/field guards verified.");
}
