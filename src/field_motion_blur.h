// FFVIII composes prerendered field tiles onto a 320 x 240 color texture.
// Its full-screen composite reads that texture separately from actor meshes,
// depth masks and the HUD. Only replace that sampler for the one game draw.
namespace field_motion_blur {
struct Anchor {uint64_t art;int x,y;};
struct Layer {
    std::array<Anchor,40> points{};
    unsigned count=0;
    GLuint texture=0;
    void observe(const Vertex* v,GLuint atlas){
        if(count>=points.size())return;
        WideView::Bounds b;
        if(!(v->p[3]>0&&v->p[3]<.1f)||!WideView::flat_rect(v,b)||
           b.r-b.l!=32||b.b-b.t!=32||std::abs(b.l)>4096||std::abs(b.t)>4096)return;
        uint64_t art=hash(1469598103934665603ULL,&atlas,sizeof(atlas));
        for(unsigned i:{0u,1u,2u})art=hash(art,v[i].uv[0],2*sizeof(float));
        art=hash(art,v->p+2,2*sizeof(float));
        points[count++]={art,int(std::lround(4*b.l)),int(std::lround(4*b.t))};
    }
};
struct Displacement {float x=0,y=0;};
static Displacement estimate(const Layer& old,const Layer& next){
    if(!old.texture||old.texture!=next.texture)return {};
    field_scroll::Scroll consensus;
    for(unsigned i=0;i<next.count;i++){
        const auto& point=next.points[i];
        bool duplicate=false;
        for(unsigned j=0;j<next.count&&!duplicate;j++)duplicate=j!=i&&next.points[j].art==point.art;
        if(duplicate)continue;
        const Anchor* match=nullptr;
        for(unsigned j=0;j<old.count;j++)if(old.points[j].art==point.art){
            if(match){match=nullptr;break;}
            match=&old.points[j];
        }
        if(match)consensus.observe({point.x-match->x,point.y-match->y},{point.x,point.y});
    }
    const auto* best=consensus.consensus();
    // Four distinct atlas landmarks must move together. Large jumps are cuts.
    if(!best||best->count<4||std::max(std::abs(best->delta[0]),std::abs(best->delta[1]))>96)return {};
    return {best->delta[0]/8.f,-best->delta[1]/8.f};
}
static struct History {
    unsigned tick=~0u;
    int room=-1;
    bool active=false,complete=false;
    double last_time=0;
    Layer old,now;
    Displacement delta;
    void begin(unsigned source,int id,double time){
        old=(tick+1==source&&id>=0&&room==id&&time>last_time&&time-last_time<=.12)?now:Layer{};
        now={};delta={};complete=false;tick=source;room=id;last_time=time;active=id>=0;
    }
} history;
static Surface filtered;
static GLuint linear_sampler=0;
static Op sample;
static CaptureRegions regions;
static void forget(){history={};filtered={};linear_sampler=0;sample={};regions={};}

struct UnitZero {
    GLint active,texture;
    UnitZero(){
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        if(active!=GL_TEXTURE0)pActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    }
    ~UnitZero(){
        glBindTexture(GL_TEXTURE_2D,texture);
        if(active!=GL_TEXTURE0)pActiveTexture(active);
    }
};
// Called on texture unit zero with the original color layer bound. Keep its
// sampling state intact and mirror that state on our private substitute.
static bool filter_texture(Displacement move,int strength){
    using namespace retro_display;
    auto& effect=field_background_pipeline;
    if(!prepare(false,Stage::field_background)||!pipeline.bind_sampler)return false;
    GLint w=0,h=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
    if(w!=320||h!=240)return false;
    allocate(filtered,w,h);
    if(!linear_sampler){
        auto create=procedure<PFNGLGENSAMPLERSPROC>("glGenSamplers");
        auto parameter=procedure<PFNGLSAMPLERPARAMETERIPROC>("glSamplerParameteri");
        if(!create||!parameter)return false;
        create(1,&linear_sampler);
        if(!linear_sampler)return false;
        parameter(linear_sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        parameter(linear_sampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        parameter(linear_sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        parameter(linear_sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    GLint program=0,min_filter,mag_filter,wrap_s,wrap_t,sampler;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&min_filter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&mag_filter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrap_s);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&wrap_t);
    {
        TargetState target(filtered.fbo,true);
        glViewport(0,0,w,h);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        for(auto cap:{GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_ALPHA_TEST,GL_CULL_FACE,
                      GL_COLOR_LOGIC_OP,GL_DITHER,GL_POLYGON_STIPPLE})glDisable(cap);
        for(unsigned i=0;i<6;i++)glDisable(GL_CLIP_PLANE0+i);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        pUseProgram(effect.program);pipeline.bind_sampler(0,linear_sampler);
        const float amplitude=std::min(1.f,8.f/std::max(.001f,std::hypot(move.x,move.y)))*strength*.0025f;
        const GLfloat dimensions[]={float(w),float(h)},step[]={move.x*amplitude/w,move.y*amplitude/h};
        pUniform2fv(effect.size,1,dimensions);pUniform2fv(effect.grid,1,step);
        fullscreen_triangle();pUseProgram(program);pipeline.bind_sampler(0,sampler);
    }
    glBindTexture(GL_TEXTURE_2D,filtered.color);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,mag_filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,min_filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap_s);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap_t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    return true;
}

static bool draw(GLenum mode,GLsizei count,GLenum type,const void* indices){
    if(!loaded||inside||!enabled||current.mode!=1||current.layout()!=1||
       current.menu_edge||mode!=GL_TRIANGLES||count<6||count%6||
       GetCurrentThreadId()!=thread||wglGetCurrentContext()!=context)return false;
    if(history.tick!=sources){
        const int room=game_mode()==1?infirmary::field_id():-1;
        history.begin(sources,room,geo::now());
        regions={};
    }
    if(!history.active)return false;
    const bool moving=history.delta.x||history.delta.y;
    if(history.complete&&(!moving||count!=6))return false;
    GLint rect[4];glGetIntegerv(GL_VIEWPORT,rect);
    if(rect[2]==320&&rect[3]==240){
        if(history.complete||history.now.count>=history.now.points.size())return false;
        GLint fbo=0,layer=0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
        if(!fbo)return false;
        pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&layer);
        if(!layer||(history.now.texture&&GLuint(layer)!=history.now.texture))return false;
        UnitZero unit;
        if(!unit.texture||unit.texture==layer)return false;
        read_draw_vertices(sample,9,std::min(count,24),type,indices,&regions);
        const unsigned before=history.now.count;
        for(size_t i=0;i<sample.vertices.size();i+=6)history.now.observe(&sample.vertices[i],unit.texture);
        if(history.now.count!=before)history.now.texture=layer;
        return false;
    }
    if(count!=6||!history.now.texture||rect[2]<320||rect[3]<240)return false;
    UnitZero unit;
    if(GLuint(unit.texture)!=history.now.texture)return false;
    if(!history.complete){
        history.complete=true;
        history.delta=estimate(history.old,history.now);
        if(!history.delta.x&&!history.delta.y)return false;
    }
    // A later shadow or water pass can modify this same color texture.
    // Always sample its current contents, even on the same simulation tick.
    if(!filter_texture(history.delta,display_settings.motion_background))return false;
    if(unit.active!=GL_TEXTURE0)pActiveTexture(unit.active);
    real_draw(mode,count,type,indices);
    if(unit.active!=GL_TEXTURE0)pActiveTexture(GL_TEXTURE0);
    return true;
}
}
