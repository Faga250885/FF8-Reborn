#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>
using namespace geo;

static Op packet(GLenum mode){
    Op op{};
    op.mode=mode;op.target=1;
    auto& s=op.state;
    s.viewport[2]=1280;s.viewport[3]=960;s.scissor_on=true;
    s.scissor[1]=48;s.scissor[2]=1279;s.scissor[3]=864;
    s.projection[0]=2.f/640;s.projection[5]=-2.f/480;s.projection[10]=1;
    s.projection[12]=-1;s.projection[13]=1;s.projection[15]=1;
    return op;
}
static void rect(Op& op,float l,float t,float r,float b,float rhw=1){
    constexpr unsigned triangles[]={0,1,2,2,3,0},strip[]={0,1,3,2},fan[]={0,1,2,3};
    const unsigned* ids=op.mode==GL_TRIANGLES?triangles:op.mode==GL_TRIANGLE_STRIP?strip:fan;
    int count=op.mode==GL_TRIANGLES?6:4;
    for(int i=0;i<count;i++){
        unsigned c=ids[i];Vertex v{};
        v.p[0]=(c==1||c==2)?r:l;v.p[1]=(c>=2)?b:t;v.p[2]=.001614f;v.p[3]=rhw;
        v.uv[0][0]=float(c==1||c==2);v.uv[0][1]=float(c>=2);
        v.c[1]=float(i+1)/count;v.c[3]=1;
        op.vertices.push_back(v);
    }
}
static bool changed(const WideView& view,const Op& op,std::vector<Vertex>& adjusted){
    GLsizei count=GLsizei(op.vertices.size());
    const auto* data=view.screen_layers(op,op.vertices.data(),adjusted,count);
    assert(size_t(count)==op.vertices.size());
    if(data==op.vertices.data())return false;
    for(int i=0;i<count;i++){
        Vertex original=adjusted[i];original.p[0]=op.vertices[i].p[0];
        assert(memcmp(&original,&op.vertices[i],sizeof(original))==0);
    }
    return true;
}
int main(){
    HWND win=CreateWindowA("STATIC","Battle flash",WS_POPUP,0,0,256,256,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(win);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt));
    assert(wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl());
    Surface scene;
    resize(scene,1280,960,false);pBindFramebuffer(GL_FRAMEBUFFER,scene.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<Vertex> out;
    Frame frame;frame.mode=3;
    WideView view(frame,false);view.outer_width=view.scene_width=1280;view.scene_height=960;
    // These are three target aspect ratios after removing the native 24-line bars.
    for(const auto dims:{std::pair{1280,800},std::pair{1920,1080},std::pair{3440,1440}}){
        view.horizontal=(640.f/432)/(float(dims.first)/dims.second);
        for(auto mode:{GL_TRIANGLES,GL_QUADS,GL_TRIANGLE_FAN,GL_TRIANGLE_STRIP}){
            auto op=packet(mode);op.state.texture[0].enabled=true;
            rect(op,1,24,639,456);
            assert(changed(view,op,out));
            glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);glViewport(0,0,1280,960);view.apply(op);
            glColor3f(1,1,1);glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(4,GL_FLOAT,sizeof(Vertex),out[0].p);glDrawArrays(mode,0,GLsizei(out.size()));
            std::vector<BYTE> pixels(1280*4);glReadPixels(0,480,1280,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for(int x=0;x<1280;x++)assert(pixels[x*4]==255);
        }
    }
    auto tiles=packet(GL_TRIANGLES);
    rect(tiles,25,330,118,397);rect(tiles,0,24,160,456);rect(tiles,160,24,320,456);
    rect(tiles,320,24,490,456);rect(tiles,490,24,640,456);rect(tiles,543,345,573,375);
    assert(changed(view,tiles,out));
    assert(!memcmp(tiles.vertices.data(),out.data(),6*sizeof(Vertex)));
    assert(!memcmp(tiles.vertices.data()+30,out.data()+30,6*sizeof(Vertex)));
    auto stripes=packet(GL_QUADS);
    rect(stripes,0,24,640,240);rect(stripes,0,240,640,456);
    assert(changed(view,stripes,out));
    auto padded_flash=packet(GL_TRIANGLES);
    rect(padded_flash,0,24,640,536);
    assert(changed(view,padded_flash,out));
    // Native impact of the Rinoa and Angelo attack in Galbadia. It starts at the lower right
    // and the active battle viewport clips the bottom row of both tiles.
    auto angelo=packet(GL_TRIANGLES);
    rect(angelo,320,264,640,504);rect(angelo,0,264,320,504);
    rect(angelo,320,24,640,264);rect(angelo,0,24,320,264);
    assert(changed(view,angelo,out));
    for(size_t i=0;i<out.size();++i){
        float x=angelo.vertices[i].p[0];assert(out[i].p[0]==320+(x-320)/view.horizontal);
    }
    for(int defect=0;defect<4;defect++){
        auto incomplete=angelo;
        if(defect==0)incomplete.vertices.resize(18);
        if(defect==1)for(size_t j=18;j<24;j++)incomplete.vertices[j].p[0]+=1;
        if(defect==2)for(size_t j=18;j<24;j++)incomplete.vertices[j].p[2]+=.001f;
        if(defect==3){
            auto icon=packet(GL_TRIANGLES);rect(icon,35,44,80,89);
            incomplete.vertices.insert(incomplete.vertices.begin()+6,icon.vertices.begin(),icon.vertices.end());
        }
        assert(!changed(view,incomplete,out));
    }
    auto cinema=packet(GL_TRIANGLES);cinema.state.blend=true;rect(cinema,0,24,640,60);
    for(auto& v:cinema.vertices)v.c[1]=0;
    assert(changed(view,cinema,out));
    for(auto bad:{0,1,2,3,4,5}){
        auto local=packet(GL_TRIANGLES);
        if(bad==0)rect(local,0,209,640,230);             // projectile
        if(bad==1)rect(local,12,365,628,442);            // Squall's trigger gauge
        if(bad==2)rect(local,0,24,640,456,1.f/87);       // projected scenery
        if(bad==3){rect(local,0,24,318,456);rect(local,321,24,640,456);}
        if(bad==4){rect(local,0,24,640,456);local.vertices[1].p[2]+=.001f;}
        if(bad==5){rect(local,0,24,640,456);local.vertices[3]=local.vertices[1];}
        assert(!changed(view,local,out));
    }
    auto storm=packet(GL_TRIANGLES);
    storm.state.depth=storm.state.blend=true;
    storm.state.src_rgb=storm.state.dst_rgb=GL_ONE;
    storm.state.eq_rgb=GL_FUNC_ADD;
    storm.state.texture[0].id=2;
    for(int y=24;y<456;y+=12)rect(storm,0,float(y),640,float(y+12),1.f/3000);
    for(auto& v:storm.vertices)v.p[2]=.026f;
    auto clear=packet(GL_TRIANGLES);clear.clear=GL_COLOR_BUFFER_BIT;
    auto effect=storm;effect.state.depth_mask=true;
    const std::vector<Op> backdrop={clear,storm,effect};
    view.find_battle_backdrop(backdrop);
    assert(changed(view,backdrop[1],out));
    assert(!changed(view,backdrop[2],out));

    auto warped_page=[](float l,float r){
        auto op=packet(GL_TRIANGLES);
        op.state.depth=op.state.depth_mask=true;
        for(int y=24;y<456;y+=24)rect(op,l,float(y),r,float(y+24));
        for(auto& v:op.vertices){
            v.p[1]+=(v.p[0]==r?3:0);
            v.p[2]=.01072f;
        }
        return op;
    };
    std::vector<Op> separate_sprite={clear,warped_page(0,320),warped_page(320,640),warped_page(100,200)};
    for(auto& v:separate_sprite.back().vertices)v.p[2]=.0108f;
    view.find_battle_backdrop(separate_sprite);
    assert(changed(view,separate_sprite[1],out));
    assert(changed(view,separate_sprite[2],out));
    assert(!changed(view,separate_sprite[3],out));
    const std::vector<Op> incomplete_mesh={clear,warped_page(0,180),warped_page(460,640)};
    view.find_battle_backdrop(incomplete_mesh);
    assert(!changed(view,incomplete_mesh[1],out));
    assert(!changed(view,incomplete_mesh[2],out));

    view.battle=view.corner_hud=true;
    auto timer=packet(GL_TRIANGLES);
    rect(timer,574,36,606,68);
    for(auto& v:timer.vertices)v.p[2]=.0010483871f;
    timer.vertices.front().uv[0][1]=.625f;
    view.inspect_battle_hud(timer);
    auto hand=packet(GL_TRIANGLES);
    rect(hand,12,338,60,370);
    for(auto& v:hand.vertices)v.p[2]=.0010645161f;
    assert(view.sprite_side(hand.vertices.data(),0)==-1);
    for(auto& v:hand.vertices)v.p[2]=.00179032f;
    assert(view.sprite_side(hand.vertices.data(),0)==0);

    auto gf=packet(GL_TRIANGLES);
    gf.state.scissor[1]=64;gf.state.scissor[2]=1248;
    rect(gf,244,388,388,418);rect(gf,388,388,508,418);
    gf.vertices.front().uv[0][1]=.375f;
    assert(view.clipped_hud_side(gf.state)==0);
    view.inspect_battle_hud(gf);
    assert(view.clipped_hud_side(gf.state)==1);
    for(size_t i=0;i<gf.vertices.size();i+=6)assert(view.sprite_side(gf.vertices.data()+i,1)==1);

    view.horizontal=1;assert(!changed(view,tiles,out)); // Native presentation.
    assert(glGetError()==GL_NO_ERROR);
    puts("Battle flashes cover both edges. Native view, local effects, HUD and projected geometry preserved.");
}
