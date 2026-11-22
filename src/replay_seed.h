// An offscreen colour or depth attachment can be sampled by any earlier
// draw (including an interpolated outgoing face). Only inspect clears
// preceding the first possible reader. The default framebuffer has no
// texture name, so only earlier writes to it or reads of its own contents
// by a draw into it matter.
struct ReplaySeedPlan {
    unsigned first_outgoing=UINT_MAX, first_default_outgoing=UINT_MAX;
    ReplaySeedPlan(float alpha){
        if(alpha<1)for(const auto& draw:outgoing_draws){
            first_outgoing=std::min(first_outgoing,draw.after);
            if(!draw.op.target)first_default_outgoing=std::min(first_default_outgoing,draw.after);
        }
    }
    static GLbitfield overwritten(const Op& op,const Target& target){
        const auto& state=op.state;
        const auto& rect=state.scissor;
        if(state.scissor_on&&(rect[0]>0||rect[1]>0||
            int64_t(rect[0])+rect[2]<target.w||int64_t(rect[1])+rect[3]<target.h))return 0;
        GLbitfield planes=op.clear;
        if(!(state.mask[0]&&state.mask[1]&&state.mask[2]&&state.mask[3]))planes&=~GL_COLOR_BUFFER_BIT;
        if(!state.depth_mask)planes&=~GL_DEPTH_BUFFER_BIT;
        // The copies use packed 24/8. Require both write masks to enable
        // every bit for stencil and default drawables of any supported size.
        const int stencil_width=target.texture?8:stencil_bits;
        const GLuint stencil_mask=stencil_width>0&&stencil_width<32?(1u<<stencil_width)-1:UINT_MAX;
        if(!op.raster.mask_known||(GLuint(op.raster.face[0][6])&stencil_mask)!=stencil_mask||
           (GLuint(op.raster.face[1][6])&stencil_mask)!=stencil_mask)planes&=~GL_STENCIL_BUFFER_BIT;
        return planes;
    }
    static bool accesses_attachment(const Op& op,GLuint key,GLuint color,GLuint depth){
        if(op.target==key)return true;
        if(op.clear)return false;
        for(const auto& unit:op.state.texture){
            if(!unit.id||unit.copy)continue;
            if(GLuint(unit.id)==color||GLuint(unit.id)==depth)return true;
        }
        return false;
    }
    GLbitfield required(const Frame& frame,GLuint key,const Target& target)const{
        GLbitfield planes=GL_COLOR_BUFFER_BIT|(target.depth?GL_DEPTH_BUFFER_BIT:0)|
                          (target.stencil?GL_STENCIL_BUFFER_BIT:0);
        unsigned last_safe=key?first_outgoing:first_default_outgoing;
        // Deck postprocessing often clears several large scratch textures in
        // turn. Only a draw into this attachment or a sampler of its live
        // contents can observe its seed before that clear. Outgoing faces
        // execute *after* their anchor, so a clear at the anchor is safe.
        if(deck_renderer&&key&&display_settings.fps==60){
            constexpr GLbitfield bits[]={GL_COLOR_BUFFER_BIT,GL_DEPTH_BUFFER_BIT,GL_STENCIL_BUFFER_BIT};
            const auto it=frame.depth_texture_for_target.find(key);
            const GLuint depth=it==frame.depth_texture_for_target.end()?0:it->second;
            auto cached=frame.seed_clears.try_emplace(key,std::array<unsigned,3>{UINT_MAX,UINT_MAX,UINT_MAX});
            auto& clear=cached.first->second;
            if(cached.second){
                GLbitfield remaining=planes;
                for(unsigned i=0;i<frame.ops.size()&&remaining;++i){
                    const auto& op=frame.ops[i];
                    if(!op.clear&&accesses_attachment(op,key,target.texture,depth))break;
                    if(!op.clear||op.target!=key)continue;
                    const auto lost=remaining&overwritten(op,target);
                    for(unsigned k=0;k<3;++k)if(lost&bits[k])clear[k]=i;
                    remaining&=~lost;
                }
            }
            if(first_outgoing!=UINT_MAX){
                last_safe=UINT_MAX;
                for(const auto& draw:outgoing_draws)
                    if(accesses_attachment(draw.op,key,target.texture,depth))
                        last_safe=std::min(last_safe,draw.after);
            }
            for(unsigned k=0;k<3;++k)if(clear[k]!=UINT_MAX&&clear[k]<=last_safe)planes&=~bits[k];
            return planes;
        }
        for(unsigned i=0;i<frame.ops.size()&&planes&&i<=last_safe;++i){
            const auto& op=frame.ops[i];
            if(!op.clear&&(key||op.target==key))break;
            if(op.target==key)planes&=~overwritten(op,target);
        }
        return planes;
    }
};
