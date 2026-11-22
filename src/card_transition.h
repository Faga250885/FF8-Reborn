// During Triple Triad setup the engine may present a smaller copy of its
// render target before switching to the normal board viewport.
namespace card_transition {
constexpr int mode=8;
struct Rect { int x=0,y=0,w=0,h=0; };
static bool entering(const Frame& f,const Frame& prior){
    return f.mode==mode||(f.mode==0&&(prior.travel!=Frame::Travel::none||
                                       prior.layout()==6||prior.mode==mode||prior.compact_card_view[2]>0));
}
static const Op* final_copy(const Frame& f){
    if(!f.valid)return nullptr;
    for(auto it=f.ops.rbegin();it!=f.ops.rend();++it){
        if(it->target)continue;
        if(it->clear||it->mode!=GL_TRIANGLES||it->vertices.size()!=6)return nullptr;
        const auto tex=it->state.texture[0].id;
        if(!tex||!f.targets.count(tex)||!WideView::uses_texture(it->state))return nullptr;
        WideView::Bounds bounds;
        if(!WideView::flat_rect(it->vertices.data(),bounds))return nullptr;
        for(int i=0;i<16;++i){
            const float identity=(i%5==0)?1.f:0.f;
            if(std::abs(it->state.projection[i]-identity)>.0001f||
               std::abs(it->state.model[i]-identity)>.0001f)return nullptr;
        }
        unsigned corners=0;
        for(const auto& v:it->vertices){
            const float u=v.uv[0][0],t=v.uv[0][1];
            if(v.p[3]!=1||std::min(std::abs(u),std::abs(u-1.f))>.005f||
               std::min(std::abs(t),std::abs(t-1.f))>.005f)return nullptr;
            corners|=1u<<((u>.5f?1:0)+(t>.5f?2:0));
        }
        if(corners!=15)return nullptr;
        return &*it;
    }
    return nullptr;
}
static Rect compact(const Frame& f,const Op* copy){
    if(!copy||f.w<=0||f.h<=0)return {};
    const auto* vp=copy->state.viewport;
    if(vp[2]<=0||vp[3]<=0||vp[0]<0||vp[1]<0||vp[0]+vp[2]>f.w||vp[1]+vp[3]>f.h||
       std::abs(2*vp[0]+vp[2]-f.w)>2||std::abs(2*vp[1]+vp[3]-f.h)>2)return {};
    // A full-height 4:3 board and a full-width widescreen board are both
    // already fitted. Scale only a centered composite boxed on both axes.
    // Some loading frames shrink the viewport, others shrink the quad
    // inside it. Measure both transforms on the same final render-target copy.
    WideView::Bounds bounds;
    if(!WideView::flat_rect(copy->vertices.data(),bounds)||bounds.l>=0||bounds.t>=0||
       bounds.r<=0||bounds.b<=0||bounds.l< -1.0001f||bounds.t< -1.0001f||bounds.r>1.0001f||bounds.b>1.0001f||
       std::abs(bounds.l+bounds.r)*vp[2]>2||std::abs(bounds.t+bounds.b)*vp[3]>2)return {};
    const int left=int(std::lround((1.+bounds.l)*.5*vp[2]));
    const int bottom=int(std::lround((1.+bounds.t)*.5*vp[3]));
    const Rect r={vp[0]+left,vp[1]+bottom,int(std::lround((1.+bounds.r)*.5*vp[2]))-left,
                                          int(std::lround((1.+bounds.b)*.5*vp[3]))-bottom};
    return r.x>1&&r.y>1&&r.w>1&&r.h>1?r:Rect{};
}
static Rect source_rect(Frame& f,const Frame& prior,const Op* copy){
    auto r=compact(f,copy);
    // Native BACK is restored after presentation. A subsequent swap with
    // no new window draw must present that same region at the same size.
    if(!r.w&&f.valid&&!copy&&f.w==prior.w&&f.h==prior.h&&
       std::none_of(f.ops.begin(),f.ops.end(),[](const Op& op){return !op.target;})){
        const auto& p=prior.compact_card_view;r={p[0],p[1],p[2],p[3]};
    }
    f.compact_card_view={r.x,r.y,r.w,r.h};return r;
}
static void resolve(Frame& f,const Frame& prior,const Op* copy){
    if(!f.valid||f.mode!=mode||(prior.layout()!=1&&prior.layout()!=6))return;
    if(!f.ops.empty()&&!copy)return;
    if(copy&&!WideView(f,false).menu_composite(*copy))return;
    if(prior.layout()==1){
        // Sometimes the mode word changes while the outgoing field is still
        // being drawn. Match an actual scene layer from that field.
        const auto repeated=std::find_if(f.ops.begin(),f.ops.end(),[&](const Op& op){
            if(!scenery_draw(op)||!op.state.texture[0].id)return false;
            return std::any_of(prior.ops.begin(),prior.ops.end(),[&](const Op& old){
                return old.target==op.target&&old.state.texture[0].id==op.state.texture[0].id&&
                       old.state.program==op.state.program&&scenery_draw(old);
            });
        });
        if(repeated!=f.ops.end()){f.layout_override=1;f.menu_edge=1;return;}
    }
    // A copy-only swap can still contain the preceding field or menu after
    // the game changes its mode word. Stop inheriting at the first board draw.
    for(const auto& op:f.ops){
        if(op.clear||!op.target)continue;
        const auto source=op.state.texture[0].id;
        if(!source||!f.targets.count(source)||op.vertices.size()!=6)return;
    }
    f.layout_override=prior.layout();
    f.menu_edge=1;
    f.menu_top=prior.menu_top;f.menu_bottom=prior.menu_bottom;f.menu_half_width=prior.menu_half_width;
}
static bool present(const Frame& f,Rect r,Surface& backup,GLuint target=0){
    if(!r.w||!r.h)return false;
    const double scale=std::min(double(f.w)/r.w,double(f.h)/r.h);
    const int dw=int(std::lround(r.w*scale)),dh=int(std::lround(r.h*scale));
    const int x=(f.w-dw)/2,y=(f.h-dh)/2;
    Guard guard(Frame{});
    resize(backup,f.w,f.h,false);
    blit(target,backup.fbo,f.w,f.h,f.w,f.h,GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);glDrawBuffer(target?GL_COLOR_ATTACHMENT0:GL_BACK);
    glClearColor(0,0,0,1);real_clear(GL_COLOR_BUFFER_BIT);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,backup.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    pBlitFramebuffer(r.x,r.y,r.x+r.w,r.y+r.h,x,y,x+dw,y+dh,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    return true;
}
}
