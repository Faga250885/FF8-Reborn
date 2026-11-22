// At Original FPS the engine already owns the correct shaders, textures,
// depth and draw order. Change its world projection for each native draw.
// A validated replay establishes the canvas and HUD layout on scene entry.
#include "deck_world_state.h"
static int game_mode();
struct DeckWorldPass {
    std::unique_ptr<WideView> view;
    int selector=-1;
    unsigned draws=0;
    double setup_seconds=0,wait_credit=0;
    bool checked=false,active=false,scene=false,output=false,failed=false;
    void begin(){checked=active=scene=output=failed=false;draws=0;setup_seconds=wait_credit=0;world_state::state={};world_state::tracking=false;}
};
static DeckWorldPass deck_world;
// Inspect only positions for potential minimap, cloud and fade layers.
// The caller validates the format and memory range before reading.

struct WorldPositions {
    const BYTE* base=nullptr;
    size_t stride=0,index_size=0;
    const BYTE* indices=nullptr;
    ReadableRegion region;
    bool open(GLsizei count,GLenum type,const void* elements){
        Array pos;
        if(world_state::observing&&world_state::state.position_known){
            GLint vbo=0;glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&vbo);if(vbo)return false;
            pos=world_state::state.position;
        }else pos=array(GL_VERTEX_ARRAY,GL_VERTEX_ARRAY_POINTER,GL_VERTEX_ARRAY_SIZE,GL_VERTEX_ARRAY_TYPE,GL_VERTEX_ARRAY_STRIDE,GL_VERTEX_ARRAY_BUFFER_BINDING);
        if(!pos.enabled||pos.size!=4||pos.type!=GL_FLOAT)return false;
        GLint buffer=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&buffer);
        index_size=type==GL_UNSIGNED_SHORT?2:type==GL_UNSIGNED_INT?4:type==GL_UNSIGNED_BYTE?1:0;
        if(buffer||!index_size||!readable(elements,size_t(count)*index_size))return false;
        base=pos.p;stride=size_t(pos.stride);indices=static_cast<const BYTE*>(elements);return true;
    }
    void get(int i,float* destination){
        unsigned index=0;memcpy(&index,indices+i*index_size,index_size);
        const uint64_t addr=uint64_t(uintptr_t(base))+uint64_t(index)*stride;
        demand(addr<=UINTPTR_MAX-16&&readable(reinterpret_cast<const void*>(uintptr_t(addr)),16,&region),"invalid world position");
        memcpy(destination,reinterpret_cast<const void*>(uintptr_t(addr)),16);
    }
};
static bool direct_world_frame(){
    auto& pass=deck_world;
    if(!pass.view||inside||!enabled||!loaded||display_settings.fps||!display_settings.wide||
       GetCurrentThreadId()!=thread||wglGetCurrentContext()!=context)return false;
    if(!pass.checked){
        pass.checked=true;
        RECT r{};
        pass.active=game_mode()==2&&!native_atlas_possible()&&native_atlas_view==pass.selector&&
            GetClientRect(WindowFromDC(dc),&r)&&r.right==pass.view->drawable_width&&r.bottom==pass.view->drawable_height;
        world_state::tracking=pass.active;
    }
    return pass.active;
}
static void submit_world_layer(const Vertex* vertices,GLsizei count,GLenum mode){
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    pBindBuffer(GL_ARRAY_BUFFER,0);
    glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(4,GL_FLOAT,sizeof(Vertex),vertices->p);
    glEnableClientState(GL_COLOR_ARRAY);glColorPointer(4,GL_FLOAT,sizeof(Vertex),vertices->c);
    glEnableClientState(GL_NORMAL_ARRAY);glNormalPointer(GL_FLOAT,sizeof(Vertex),vertices->n);
    for(int j=0;j<4;j++){
        pClientActiveTexture(GL_TEXTURE0+j);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(4,GL_FLOAT,sizeof(Vertex),vertices->uv[j]);
    }
    glDrawArrays(mode,0,count);glPopClientAttrib();
}
static void draw_world_geometry(const WideView& view,GLenum mode,GLsizei count,GLenum type,const void* indices,
                                GLuint framebuffer,GLuint output,double began=0){
    Op op;op.mode=mode;
    auto& s=op.state;
    world_state::rect(GL_VIEWPORT,s.viewport);
    if(framebuffer==output){
        if(!deck_world.scene||s.viewport[2]!=view.inner_width||s.viewport[3]!=view.drawable_height||
           mode!=GL_TRIANGLES||count!=6){real_draw(mode,count,type,indices);return;}
        s.scissor_on=world_state::cap_enabled(GL_SCISSOR_TEST);
        if(s.scissor_on)world_state::rect(GL_SCISSOR_BOX,s.scissor);
        view.apply_output(s,(view.drawable_height-view.outer_height)/2);
        real_draw(mode,count,type,indices);
        glViewport(s.viewport[0],s.viewport[1],s.viewport[2],s.viewport[3]);
        if(s.scissor_on)glScissor(s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]);
        deck_world.output=true;return;
    }
    if(s.viewport[2]!=view.scene_width||s.viewport[3]!=view.scene_height){
        real_draw(mode,count,type,indices);return;
    }
    world_state::projection(s.projection);
    if(std::abs(s.projection[0]-2.f/640)>.00001f){real_draw(mode,count,type,indices);return;}
    op.target=framebuffer; // Nonzero scene target; resolve its texture only for actual HUD candidates.
    s.scissor_on=world_state::cap_enabled(GL_SCISSOR_TEST);
    if(s.scissor_on)world_state::rect(GL_SCISSOR_BOX,s.scissor);
    if(s.scissor_on&&s.scissor[2]>=view.scene_width-2&&
       std::abs(s.scissor[3]-view.scene_height*448/480)<=2)deck_world.scene=true;
    const Vertex* edited=nullptr;
    static std::vector<Vertex> layer;
    GLsizei edited_count=count;
    const bool quads=mode==GL_TRIANGLES&&count>=6&&count%6==0;
    if(mode==GL_TRIANGLES&&(quads||view.map_target)){
        demand(count>0&&unsigned(count)<=MAX_VERTICES,"world batch too large");
        WorldPositions pos;
        demand(pos.open(count,type,indices),"world position format unavailable");
        Vertex first{};pos.get(0,first.p);
        if(view.map_target&&WideView::map_layer(first)){
            GLint texture=0;
            pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&texture);
            op.target=GLuint(texture);op.vertices.resize(count);
            for(int i=0;i<count;++i)pos.get(i,op.vertices[i].p);
        }
        const bool clouds=quads&&std::abs(first.p[3]-1.f/4095)<1e-7f;
        s.blend=quads&&world_state::cap_enabled(GL_BLEND);
        // Only clouds and full-width colour rectangles need new vertices.
        // Test rectangle bounds before inspecting a shader or fetching UVs.
        bool possible_overlay=false;
        if(s.blend)for(int i=0;i<count;i+=6){
            Vertex corners[6];pos.get(i,corners[0].p);
            for(int j=1;j<6;++j)pos.get(i+j,corners[j].p);
            WideView::Bounds b;
            if(WideView::flat_rect(corners,b)&&
                ((b.l>=-2&&b.l<=1&&b.r>=639&&b.r<=642)||
                 (count==12&&b.t<=24&&b.b>=456))){possible_overlay=true;break;}
        }
        if(clouds||possible_overlay){
            unsigned inputs=127;s=get_state(nullptr,false,&inputs,true);
            read_draw_vertices(op,inputs,count,type,indices);
            const Vertex* base=op.vertices.data();
            auto result=view.screen_layers(op,base,layer,edited_count);
            if(result!=base)edited=result;
        }
    }
    const GLint matrix_mode=world_state::matrix_mode();
    view.apply(op);
    if(began){
        const double setup=now()-began;
        deck_world.setup_seconds+=setup;deck_world.wait_credit+=setup;
    }
    if(edited)submit_world_layer(edited,edited_count,mode);
    else real_draw(mode,count,type,indices);
    glMatrixMode(GL_PROJECTION);glLoadMatrixf(s.projection);glMatrixMode(matrix_mode);
    if(s.scissor_on&&(s.scissor[0]!=0||s.scissor[2]!=view.scene_width))
        glScissor(s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]);
}
static void direct_world_draw(GLenum mode,GLsizei count,GLenum type,const void* indices){
    ++deck_world.draws;
    const double began=now();
    GLint framebuffer=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
    try{draw_world_geometry(*deck_world.view,mode,count,type,indices,GLuint(framebuffer),0,began);}
    catch(const std::exception&){
        deck_world.failed=true;real_draw(mode,count,type,indices);
    }
}
