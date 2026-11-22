// The battle projector tags body meshes with their actor slot before culling
// or sorting triangles. Use those tags to keep the exposure off the stage,
// particles and HUD, even when they share a shader or texture with a model.
namespace actor_shutter {
static void prepare(Frame& frame){
    BoneLinks owners;
    if(!frame.camera_cut&&!frame.menu_edge&&frame.layout()==3&&!frame.bones.empty()){
        owners.reserve(frame.bones.size());
        for(const auto& p:frame.bones){
            if(!p.z||(p.flags&2))continue;
            const auto actor=p.group>>16;
            const BonePoint projected{p.x,p.y,p.z};
            owners.vote(projected.key(),{short(actor>=1&&actor<=7?actor:0),0,1});
        }
    }
    for(auto& op:frame.ops){
        auto& indices=op.shutter_indices;
        indices.clear();
        const auto& s=op.state;
        if(owners.empty()||op.match<0||!op.target||!surface_draw(op)||!s.depth||s.logic_on||
           op.raster.stencil||op.motion_valid.size()!=op.vertices.size()||
           op.prior_positions.size()!=op.vertices.size())continue;
        for(GLuint start=0;start+2<op.vertices.size();start+=3){
            short owner=0;
            float fastest=0;
            for(unsigned i=start;i<start+3;++i){
                const auto* p=op.vertices[i].p;
                BonePoint point;
                if(!op.motion_valid[i]||!from_gl(p,point)){owner=0;break;}
                const auto* id=owners.find(point.key());
                if(!id||!id->z||!id->x||(owner&&owner!=id->x)){owner=0;break;}
                owner=id->x;
                const auto& prior=op.prior_positions[i];
                if(!op.indexed_complete){
                    const auto* tracked=active_bones.backward.find(point.key());
                    BonePoint matched;
                    if(!tracked||!tracked->z||!from_gl(prior.data(),matched)||!(*tracked==matched)){
                        owner=0;break;
                    }
                }
                const float dx=p[0]-prior[0],dy=p[1]-prior[1],length2=dx*dx+dy*dy;
                if(!(length2<64.f*64.f)){owner=0;break;}
                fastest=std::max(fastest,length2);
            }
            if(owner&&fastest>=.25f)indices.insert(indices.end(),{start,start+1,start+2});
        }
    }
}

// One depth-tested near sample softens the moving silhouette. It uses this
// tick's lighting, texture coordinates and fragment shader. A short shutter
// limits separation on a sharp turn and leaves all framebuffer alpha intact.
static void draw(const Op& op,const Vertex* pose,GLuint array_buffer,uintptr_t position,
                 int strength,int target_rate){
    const float amount=strength*.01f;
    const float exposure=amount*7.5f/std::max(target_rate,15);
    const float radius=3.f*amount,radius2=radius*radius;
    static std::vector<std::array<float,4>> trail;
    trail.resize(op.vertices.size());
    for(auto index:op.shutter_indices){
        const auto& point=pose[index].p;
        const auto& previous=op.prior_positions[index];
        float dx=(op.vertices[index].p[0]-previous[0])*exposure;
        float dy=(op.vertices[index].p[1]-previous[1])*exposure;
        const float distance2=dx*dx+dy*dy;
        if(distance2>radius2){
            const float scale=radius/std::sqrt(distance2);
            dx*=scale;dy*=scale;
        }
        trail[index]={point[0]-dx,point[1]-dy,point[2],point[3]};
    }
    for(size_t i=0;i<op.shutter_indices.size();i+=3){
        const auto first=op.shutter_indices[i];
        const auto& a=trail[first];const auto& b=trail[first+1];const auto& c=trail[first+2];
        const float area=(b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1]);
        // Interpolation may deliberately collapse a face that turns away.
        // Offsetting its three vertices must not reveal that hidden surface.
        if(screen_area(pose+first)*area<=0)trail[first+1]=trail[first+2]=a;
    }
    const auto& s=op.state;
    if(array_buffer)pBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexPointer(4,GL_FLOAT,sizeof(trail[0]),trail.data());
    if(!s.blend)glEnable(GL_BLEND);
    pBlendColor(0,0,0,.35f*amount);
    pBlendFuncSeparate(GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_ZERO,GL_ONE);
    pBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    if(s.depth_mask)glDepthMask(GL_FALSE);
    if(s.depth_func==GL_LESS)glDepthFunc(GL_LEQUAL);
    else if(s.depth_func==GL_GREATER)glDepthFunc(GL_GEQUAL);
    if(op.shutter_indices.size()==op.vertices.size())glDrawArrays(GL_TRIANGLES,0,GLsizei(op.vertices.size()));
    else glDrawElements(GL_TRIANGLES,GLsizei(op.shutter_indices.size()),GL_UNSIGNED_INT,op.shutter_indices.data());
    if(s.depth_func==GL_LESS||s.depth_func==GL_GREATER)glDepthFunc(s.depth_func);
    if(s.depth_mask)glDepthMask(GL_TRUE);
    pBlendFuncSeparate(s.src_rgb,s.dst_rgb,s.src_alpha,s.dst_alpha);
    pBlendEquationSeparate(s.eq_rgb,s.eq_alpha);
    pBlendColor(s.blend_color[0],s.blend_color[1],s.blend_color[2],s.blend_color[3]);
    if(!s.blend)glDisable(GL_BLEND);
    if(array_buffer)pBindBuffer(GL_ARRAY_BUFFER,array_buffer);
    glVertexPointer(4,GL_FLOAT,sizeof(Vertex),reinterpret_cast<const void*>(position));
}
}
