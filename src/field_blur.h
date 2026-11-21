// Recognise gameplay once per native frame. Read colour later, after each
// presented pose (including intermediate poses) has actually been rendered.
namespace field_blur {
static bool preferred=true;
static Surface levels[13];
static void prepare(Frame& frame){
    if(frame.layout()!=1||frame.vertex_count<144||frame.w<=0||frame.h<=0)return;
    WideView view(frame,true);
    if(!view.outer_width||view.outer_width>=frame.w)return;
    // Confirm a textured scene pass with substantial geometry. Title cards
    // and movies also run as mode 1, but normally submit one textured quad.
    bool tiles=false;
    for(const auto&op:frame.ops){
        const State&s=op.state;
        if(op.target&&s.scissor_on&&s.scissor[2]>=view.scene_width-2&&
           s.viewport[2]==view.scene_width&&s.viewport[3]==view.scene_height&&
           std::abs(s.scissor[3]-view.scene_height*448/480)<=2&&
           std::abs(s.projection[0]-2.f/640)<.00001f&&op.vertices.size()>=36&&s.texture[0].id){
            tiles=true;break;
        }
    }
    frame.reactive_sides=tiles;
}

// Each halving averages four neighbouring pixels. Successive reductions
// integrate thin silhouettes and highlights; the final linear upscale
// produces a soft mirrored continuation of the composited edge.
static void draw_margins(const Frame& frame,GLuint target,int content_width){
    if(content_width<=0||content_width>=frame.w)return;
    const int left=(frame.w-content_width)/2,right=left+content_width;
    const int extent=std::min(content_width/2,std::max(left,content_width/4));
    if(extent<1)return;
    // At most two colours across and six down each side. This deliberately
    // removes all recognizable faces, dialogue glyphs and clothing outlines.
    glDisable(GL_SCISSOR_TEST);
    for(int side=0;side<2;side++){
        int x=side?right-extent:left,w=extent,h=frame.h;
        GLuint src=target;
        for(auto& next:levels){
            const int nw=std::max(1,(w+1)/2),nh=std::max(1,(h+1)/2);
            resize(next,nw,nh,false);
            pBindFramebuffer(GL_READ_FRAMEBUFFER,src);glReadBuffer(src?GL_COLOR_ATTACHMENT0:GL_BACK);
            pBindFramebuffer(GL_DRAW_FRAMEBUFFER,next.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
            pBlitFramebuffer(x,0,x+w,h,0,0,nw,nh,GL_COLOR_BUFFER_BIT,GL_LINEAR);
            src=next.fbo;x=0;w=nw;h=nh;
            if(h<=6&&w<=2)break;
        }
        pBindFramebuffer(GL_READ_FRAMEBUFFER,src);glReadBuffer(GL_COLOR_ATTACHMENT0);
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);glDrawBuffer(target?GL_COLOR_ATTACHMENT0:GL_BACK);
        const int begin=side?right:0,end=side?frame.w:left;
        pBlitFramebuffer(w,0,0,h,begin,0,end,frame.h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    }
}
static void draw(const Frame& frame,GLuint target,const WideView& view){
    if(frame.reactive_sides)draw_margins(frame,target,view.outer_width);
}
}
static void draw_field_sides(const Frame&f,GLuint dest,const WideView&v){
    if(field_blur::preferred)field_blur::draw(f,dest,v);
    else infirmary::draw(f.infirmary_room,f.infirmary_top,dest,v);
}
