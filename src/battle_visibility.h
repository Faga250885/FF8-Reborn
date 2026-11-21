// The engine submits only currently front-facing opaque battle triangles.
// Keep a departing face only when all three of its current vertices are known
// from adjacent visible faces, and it is demonstrably turning away.
static bool battle_visibility_enabled=true;
static double screen_area(const Vertex*a){return double(a[1].p[0]-a[0].p[0])*(a[2].p[1]-a[0].p[1])-double(a[2].p[0]-a[0].p[0])*(a[1].p[1]-a[0].p[1]);}
static bool opaque_projected(const Op&op){return projected_geometry(op)&&(!op.state.blend||(op.state.src_rgb==GL_ONE&&op.state.dst_rgb==GL_ZERO));}
static void mark_battle_front_faces(){
    for(auto&op:current.ops)if(opaque_projected(op)){
        op.front_face.assign(op.vertices.size()/3,0);
        for(unsigned i=0;i+2<op.vertices.size();i+=3){bool scene=true;for(unsigned v=0;v<3;v++)scene&=op.vertices[i+v].p[3]>0&&op.vertices[i+v].p[3]<.1f;
            op.front_face[i/3]=scene&&screen_area(&op.vertices[i])<-1e-5;}
    }
}
static void suppress_backfaces(const Op&op,std::vector<Vertex>&vertices){
    for(unsigned i=0;i<op.front_face.size();i++)if(op.front_face[i]&&screen_area(&vertices[i*3])>0){
        memcpy(vertices[i*3+1].p,vertices[i*3].p,sizeof(vertices[i*3].p));memcpy(vertices[i*3+2].p,vertices[i*3].p,sizeof(vertices[i*3].p));}
}
static void retain_battle_outgoing(const std::vector<Primitive>&old,const std::vector<Primitive>&cur,const ForwardSurface&extra={}){
    struct Shared{std::array<float,4> point;bool valid=true;};std::unordered_map<uint64_t,Shared> shared;
    for(const auto&op:current.ops)if(!op.indexed_complete&&opaque_projected(op)){
        const auto state=surface_state_key(op);
        for(unsigned i=0;i<op.vertices.size();i++)if(op.motion_valid[i]&&op.vertices[i].p[3]>0&&op.vertices[i].p[3]<.1f){
        Shared value;memcpy(value.point.data(),op.vertices[i].p,sizeof(op.vertices[i].p));auto inserted=shared.emplace(surface_point_key(state,op.prior_positions[i].data()),value);
        if(!inserted.second){auto&entry=inserted.first->second;for(int k:{0,1,3})if(entry.point[k]!=value.point[k])entry.valid=false;}}
    }
    for(const auto&entry:extra)shared.emplace(entry.first,Shared{entry.second,true});
    std::vector<bool> matched(old.size());
    for(const auto&t:cur)if(t.prior>=0){
        bool same=true;
        if(registered_after&&opaque_projected(current.ops[t.op])){
            const auto&o=current.ops[t.op];const auto&b=old[t.prior];
            for(unsigned j=0;j<3;j++)if(o.motion_valid[t.start+j]){
                const auto&p=o.prior_positions[t.start+j];const auto&q=previous.ops[b.op].vertices[b.start+j].p;
                if(p[0]!=q[0]||p[1]!=q[1]||fabs(p[3]-q[3])>1e-9){same=false;break;}}
        }
        if(same)matched[t.prior]=true;
    }
    std::unordered_map<GLuint,unsigned> last;for(unsigned i=0;i<current.ops.size();i++)if(!current.ops[i].clear)last[current.ops[i].target]=i;
    unsigned last_source=UINT_MAX;uint64_t before_state=0;
    for(unsigned i=0;i<old.size();i++){
        if((i&255)==0&&matching_checkpoint)matching_checkpoint();if(matched[i])continue;
        const auto&tri=old[i];const auto&before=previous.ops[tri.op];if(!opaque_projected(before)||screen_area(&before.vertices[tri.start])>=-1e-5)continue;
        auto tail=last.find(before.target);if(tail==last.end())continue;Vertex predicted[3];bool valid=true;
        if(last_source!=tri.op){last_source=tri.op;before_state=surface_state_key(before);}
        for(unsigned v=0;v<3;v++){predicted[v]=before.vertices[tri.start+v];const auto&p=predicted[v].p;
            if(p[3]<=0||p[3]>=.1f){valid=false;break;}
            if(registered_before)if(const auto*bone=lookup_bone(active_bones.forward,p)){
                std::array<float,4> dest;memcpy(dest.data(),p,sizeof(dest));bone->apply(dest);memcpy(predicted[v].p,dest.data(),sizeof(dest));continue;}
            auto found=shared.find(surface_point_key(before_state,p));if(found==shared.end()||!found->second.valid){valid=false;break;}
            for(int k:{0,1,3})predicted[v].p[k]=found->second.point[k];}
        if(!valid||screen_area(predicted)<1e-5)continue;
        auto& draw=departing_batch(before,tri.op,tail->second);draw.front_face.push_back(1);
        for(unsigned v=0;v<3;v++){draw.vertices.push_back(predicted[v]);std::array<float,4> prior;memcpy(prior.data(),before.vertices[tri.start+v].p,sizeof(predicted[v].p));draw.prior_positions.push_back(prior);draw.motion_valid.push_back(1);}
        outgoing_triangles++;
    }
}
