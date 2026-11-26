// The engine supplies projected XY and reciprocal camera depth. Interpolating
// those independently moves corners at different physical speeds in a zoom.
// Interpolate the camera-space point, then project it into the current view.
static void interpolate_battle_point(const float* prior,const float* current,float alpha,float* point){
    const float before=prior[3],after=current[3];
    float fraction=alpha;
    point[3]=before+alpha*(after-before);
    if(alpha>0&&alpha<1&&before!=after&&before>0&&before<.1f&&after>0&&after<.1f){
        const float inverse=1.f/((1-alpha)*after+alpha*before);
        fraction=alpha*before*inverse;
        point[3]=before*after*inverse;
    }
    for(int axis=0;axis<2;++axis)point[axis]=prior[axis]+fraction*(current[axis]-prior[axis]);
    point[2]=prior[2]+alpha*(current[2]-prior[2]);
}

// Battle vertices have already been projected into the game's 640-unit view.
// An edge can grow in screen space during a zoom or turn and still represent
// the same rigid edge. Use reciprocal depth to check that case before
// rejecting a history that would stretch a small face across two actors.
static float battle_screen_edge2(const float* p,const float* q){
    const float x=p[0]-q[0],y=p[1]-q[1];return x*x+y*y;
}
static bool battle_edge_stretches(const float* a,const float* b,const float* past_a,const float* past_b){
    const float screen=battle_screen_edge2(a,b),past_screen=battle_screen_edge2(past_a,past_b);
    if(std::max(screen,past_screen)<=4*std::min(screen,past_screen)+16*16)return false;
    for(auto p:{a,b,past_a,past_b})if(!(p[3]>0&&p[3]<.1f))return false;
    const auto view_length2=[](const float* p,const float* q){
        const double z=1./p[3],other_z=1./q[3];
        const double x=(p[0]-320.)*z-(q[0]-320.)*other_z;
        const double y=(p[1]-240.)*z-(q[1]-240.)*other_z;
        const double depth=1024.*(z-other_z);
        return x*x+y*y+depth*depth;
    };
    const double size=view_length2(a,b),past_size=view_length2(past_a,past_b);
    const double slack=8./std::min({a[3],b[3],past_a[3],past_b[3]});
    return std::max(size,past_size)>6.25*std::min(size,past_size)+slack*slack;
}

// Vertices can move at different rates in screen space as their depths
// change. Validate the generated edge itself when the rates are far apart.
static bool battle_edge_overshoots(const float* a,const float* b,const float* past_a,const float* past_b){
    const float left=past_a[3]*b[3],right=past_b[3]*a[3];
    if(std::max(left,right)<=1.3f*std::min(left,right))return false;
    for(auto p:{a,b,past_a,past_b})if(!(p[3]>0&&p[3]<.1f))return false;
    const float limit=2.25f*std::max(battle_screen_edge2(a,b),battle_screen_edge2(past_a,past_b))+16*16;
    if(std::max(battle_screen_edge2(a,past_a),battle_screen_edge2(b,past_b))<=limit)return false;
    for(float phase:{.25f,.5f,.75f}){
        float p[4],q[4];
        interpolate_battle_point(past_a,a,phase,p);
        interpolate_battle_point(past_b,b,phase,q);
        if(battle_screen_edge2(p,q)>limit)return true;
    }
    return false;
}

using HeldBattlePoints=std::unordered_set<uint64_t>;
static HeldBattlePoints protect_battle_edges(const std::vector<Primitive>& old,const std::vector<Primitive>& faces){
    // Textured additive passes can draw the visible surface of an actor
    // with depth testing but without depth writes. Check those meshes too.
    // Untextured flashes and overlays without depth testing stay on their
    // original effect path.
    const auto mesh=[](const Op& op){
        return opaque_projected(op)||(projected_geometry(op)&&op.state.depth&&
            (op.state.depth_mask||(op.state.texture[0].id&&op.state.src_rgb==GL_ONE&&op.state.dst_rgb==GL_ONE)));
    };
    std::vector<unsigned> rejected;
    unsigned last=UINT_MAX;bool checked=false;
    for(unsigned i=0;i<faces.size();++i){
        const auto& face=faces[i];const auto& op=current.ops[face.op];
        if(face.op!=last){last=face.op;checked=mesh(op);}
        if(!checked||!(op.motion_valid[face.start]||op.motion_valid[face.start+1]||op.motion_valid[face.start+2]))continue;
        const auto* vertex=op.vertices.data()+face.start;
        const float* past[3];
        for(unsigned v=0;v<3;++v)past[v]=op.motion_valid[face.start+v]?op.prior_positions[face.start+v].data():vertex[v].p;
        for(unsigned v=0;v<3;++v){
            const unsigned w=(v+1)%3;
            if(battle_edge_overshoots(vertex[v].p,vertex[w].p,past[v],past[w])){
                rejected.push_back(i);break;
            }
            if(!battle_edge_stretches(vertex[v].p,vertex[w].p,past[v],past[w]))continue;
            if(face.prior>=0){
                const auto& prior=old[face.prior];
                const auto* native=previous.ops[prior.op].vertices.data()+prior.start;
                const auto same=[&](unsigned k){
                    return past[k][0]==native[k].p[0]&&past[k][1]==native[k].p[1]&&past[k][3]==native[k].p[3];
                };
                if(same(v)&&same(w))continue;
            }
            rejected.push_back(i);break;
        }
    }
    if(rejected.empty())return {};
    // Only the exceptional pair needs a connectivity index. Material or draw
    // call boundaries often pass through a limb and must share its decision.
    std::vector<unsigned> parent(faces.size());
    for(unsigned i=0;i<parent.size();++i)parent[i]=i;
    const auto root=[&](unsigned i){while(i!=parent[i]){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    std::unordered_map<uint64_t,unsigned> owner;
    last=UINT_MAX;uint64_t state=0;
    for(unsigned i=0;i<faces.size();++i){
        const auto& face=faces[i];const auto& op=current.ops[face.op];
        if(face.op!=last){last=face.op;checked=mesh(op);if(checked)state=surface_state_key(op);}
        if(!checked)continue;
        for(unsigned v=0;v<3;++v){
            const auto* p=op.vertices[face.start+v].p;
            if(!(p[3]>0&&p[3]<.1f))continue;
            const auto slot=owner.emplace(surface_point_key(state,p),i);
            if(!slot.second)parent[root(i)]=root(slot.first->second);
        }
    }
    std::vector<bool> held(faces.size());
    for(auto i:rejected)held[root(i)]=true;
    for(unsigned i=0;i<faces.size();++i)if(held[root(i)]){
        const auto& face=faces[i];auto& op=current.ops[face.op];
        std::fill_n(op.motion_valid.begin()+face.start,3,0);
        std::fill_n(op.color_valid.begin()+face.start,3,0);
    }
    HeldBattlePoints result;
    for(const auto& point:owner)if(held[root(point.second)])result.insert(point.first);
    return result;
}
