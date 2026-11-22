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

static Op result_sprite(int target,int texture,float left,float top,float right,float bottom,int w,int h){
    Op op{};op.target=target;op.mode=GL_TRIANGLES;
    auto& s=op.state;s.texture[0].id=texture;s.texture[0].enabled=true;
    s.viewport[2]=w;s.viewport[3]=h;
    s.model[0]=s.model[5]=s.model[10]=s.model[15]=1;
    s.projection[0]=2.f/1440;s.projection[5]=-2.f/1080;
    s.projection[10]=s.projection[15]=1;s.projection[12]=-1;s.projection[13]=1;
    for(auto corner:{0,1,3,0,3,2}){
        auto& v=op.vertices.emplace_back();
        v.p[0]=(corner&1)?right:left;v.p[1]=(corner&2)?bottom:top;v.p[3]=1;
    }
    return op;
}

static void check_icons(int w,int h,int sw,int sh){
    auto f=results(w,h,sw,sh);
    const int final_surface=7,icon_texture=108;
    f.targets[final_surface].w=sw;f.targets[final_surface].h=sh;
    f.ops.back().state.texture[0].id=final_surface;
    resolve_menu_layout(f,Frame{});
    const WideView baseline(f,true);
    // A full page and a scissored screen effect must not count as help icons.
    f.targets[6].w=sw;f.targets[6].h=sh;
    auto page=result_sprite(7,6,-1,-1,1,1,sw,sh);
    for(auto& v:page.vertices)v.p[2]=1;
    auto effect=result_sprite(7,109,0,0,1440,1080,sw,sh);
    effect.state.scissor_on=true;effect.state.scissor[0]=0;effect.state.scissor[1]=sh-12;
    effect.state.scissor[2]=effect.state.scissor[3]=12;
    auto hidden=result_sprite(7,icon_texture,0,0,64,64,sw,sh);
    hidden.state.scissor_on=true;hidden.state.scissor[2]=sw;hidden.state.scissor[3]=sh/2;
    auto clip_effect=result_sprite(7,109,-1,-1,1,1,sw,sh);
    f.ops.insert(f.ops.begin(),{result_sprite(7,icon_texture,0,0,64,64,sw,sh),page,effect,hidden,
                                clip_effect,result_sprite(5,icon_texture,0,0,64,64,sw,sh)});
    const WideView absent(f,true);
    demand(absent.outer_height==baseline.outer_height&&absent.menu_y==baseline.menu_y,
           "offscreen, covered, clipped and fullscreen passes do not reduce the panel");

    // Actual EXP trace: two 64x64 sprites at (0,59) and (0,199) in the
    // compositor's 1440x1080 units. Capture both topmost visible rows.
    auto fist=result_sprite(7,icon_texture,0,59,64,123,sw,sh);
    auto encounters=result_sprite(7,icon_texture,0,199,64,263,sw,sh);
    // FFVIII's transformed-vertex shader projects positions directly.
    // A stale modelview translation must not move a programmable sprite.
    fist.state.program=1;fist.state.uniforms.emplace_back().type=GL_SAMPLER_2D;
    fist.state.model[13]=500;
    f.ops.insert(f.ops.end()-1,{fist,encounters});
    resolve_menu_layout(f,Frame{});
    const WideView live(f,true);
    const double scale=std::min(w/640.,h/(436.-26.));
    demand(live.outer_width==std::lround(640*scale)&&live.outer_height==std::lround(480*scale),
           "real projected bounds include the booster's top and left edges");
    Surface output;resize(output,w,h,false,true);
    pBindFramebuffer(GL_FRAMEBUFFER,output.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glClearColor(1,0,1,1);
    const int sx=(w-sw)/2,sy=(h-sh)/2;
    for(int y:{59,199}){
        const int bottom=sy+int(std::lround((1080-y-9)*sh/1080.));
        const int top=sy+int(std::lround((1080-y)*sh/1080.));
        glScissor(sx,bottom,int(std::lround(64.*sw/1440)),top-bottom);glClear(GL_COLOR_BUFFER_BIT);
    }
    demand(present_native_layout(f,live,output.fbo),"present real help icon rectangles");
    pBindFramebuffer(GL_READ_FRAMEBUFFER,output.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    for(int row:{59,199}){
        const int x=(w-live.outer_width)/2+int(std::lround(5.*live.outer_width/1440));
        const int y=live.menu_y+int(std::lround((1076-row)*live.outer_height/1080.));
        demand(x>=0&&x<w&&y>=0&&y<h,"top edge of each help icon is visible");
        BYTE rgb[4]{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
        demand(rgb[0]>240&&rgb[1]<15&&rgb[2]>240,"icon top edge survives final blit");
    }
    release(output);
    auto fade=results(w,h,sw,sh);
    fade.targets=f.targets;fade.ops.back().state.texture[0].id=final_surface;
    auto dim=page;dim.state.blend=true;dim.state.src_rgb=GL_SRC_ALPHA;dim.state.dst_rgb=GL_ONE_MINUS_SRC_ALPHA;
    fade.ops.insert(fade.ops.begin(),{page,fist,encounters,dim});
    resolve_menu_layout(fade,f);
    const WideView during_fade(fade,true);
    demand(during_fade.outer_height==live.outer_height&&during_fade.menu_y==live.menu_y,
           "fading the image with the assists visible keeps the same fit");
    // When the player toggles the aids off, do not inherit their extra margin.
    Frame next=results(w,h,sw,sh);next.targets=f.targets;next.ops.back().state.texture[0].id=final_surface;
    next.ops.insert(next.ops.begin(),page);resolve_menu_layout(next,fade);
    const WideView restored(next,true);
    demand(restored.outer_height==baseline.outer_height&&restored.menu_y==baseline.menu_y,
           "switching assists off immediately restores normal full-height EXP");
    // The same six vertices can be drawn under a different projection,
    // e.g. a smaller HUD viewport on an external screen.
    auto& inset=f.ops[f.ops.size()-3];
    inset.state.projection[5]=-2.f/540;
    inset.state.viewport[0]=sw/4;inset.state.viewport[2]=sw/2;
    f.ops.erase(f.ops.end()-2);
    const WideView inset_view(f,true);
    demand(inset_view.outer_height==baseline.outer_height&&inset_view.menu_y==baseline.menu_y,
           "the source matrix and viewport independently locate the icon inside the panel area");
    f.mode=6;resolve_menu_layout(f,Frame{});
    Frame menu=results(w,h,sw,sh);menu.mode=6;resolve_menu_layout(menu,Frame{});
    demand(WideView(f,true).outer_height==WideView(menu,true).outer_height,"only post-battle sizing changes");
}

static void check_fit(int w,int h,int source_w,int source_h){
    Frame battle;battle.mode=3;
    Frame f=results(w,h,source_w,source_h);
    resolve_menu_layout(f,battle);
    WideView view(f,true);
    demand(f.layout()==6&&view.menu,"post-battle results must use menu layout immediately");
    demand(f.menu_top==32&&f.menu_bottom==436&&f.menu_half_width==294,
           "EXP and loot must fit their own measured panel borders");
    const auto& vp=view.result_viewport;
    demand(vp[0]==(w-source_w)/2&&vp[1]==(h-source_h)/2&&vp[2]==source_w&&vp[3]==source_h,
           "results composite viewport");
    demand(std::abs(view.outer_width*3-view.outer_height*4)<=4,"preserve menu aspect");

    // Put green in the visible menu rectangle and red in its native margins.
    // A wrong source viewport leaves a red band above or below the results.
    Surface target;resize(target,w,h,false,true);
    pBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    const int bottom=vp[1]+int(std::lround(44.*vp[3]/480.));
    const int top=vp[1]+int(std::lround(448.*vp[3]/480.));
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
    check_icons(1280,800,960,720);
    check_icons(1920,1080,1440,1080);
    check_icons(3440,1440,1280,960);
    check_icons(1024,768,1024,768);
    Frame f=results(1280,800,1280,800);
    resolve_menu_layout(f,Frame{});
    demand(!WideView(f,true).menu,"do not crop a fullscreen output");
    f=results(1280,800,960,720);f.ops.back().target=1;
    resolve_menu_layout(f,Frame{});
    demand(!WideView(f,true).menu,"an offscreen draw cannot identify final composition");
    f.ops.clear();resolve_menu_layout(f,Frame{});
    demand(!WideView(f,true).menu,"an empty frame supplies no source");
    puts("Post-battle panels and optional assist icons verified at 4:3, 16:10, 16:9 and ultrawide");
}
