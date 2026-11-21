// Keep world polygons that leave the frustum until the interpolated camera
// has actually passed them. The native current endpoint remains untouched.
struct OutgoingDraw {Op op;unsigned after=0;};
static std::vector<OutgoingDraw> outgoing_draws;
static unsigned outgoing_triangles=0;
static uint64_t surface_point_key(uint64_t state,const float*p){
    float xyw[]={p[0]==0?0:p[0],p[1]==0?0:p[1],p[3]==0?0:p[3]};
    return hash(state,xyw,sizeof(xyw));
}
static uint64_t surface_state_key(const Op&op){
    uint64_t key=hash(1469598103934665603ULL,&op.target,sizeof(op.target));
    key=hash(key,op.state.projection,sizeof(op.state.projection));
    return hash(key,op.state.viewport,sizeof(op.state.viewport));
}
static Op& departing_batch(const Op& before,unsigned index,unsigned after){
    if(outgoing_draws.empty()||outgoing_draws.back().op.key!=index){
        auto& draw=outgoing_draws.emplace_back();
        draw.after=after;
        auto& op=draw.op;
        op.key=index;op.match=0;op.mode=before.mode;op.target=before.target;
        op.state=before.state;op.raster=before.raster;op.indexed_complete=before.indexed_complete;
        op.card_before=before.card_before;
    }
    return outgoing_draws.back().op;
}
static bool camera_forward_matrix(float*out){
    float m[16]{};m[15]=1;
    for(int k=0;k<4;k++)for(int j=0;j<3;j++)m[k*4+j]=(float)camera_motion.matrix[k][j];
    return camera_motion.valid&&invert_matrix(m,out);
}
static bool camera_forward(const float*matrix,const float*p,float*out){
    if(p[3]<=0||p[3]>=.1f)return false;
    float q[4]={p[0]/p[3],p[1]/p[3],1/p[3],1};double r[4];transform(matrix,q,r);
    if(!std::isfinite(r[2])||r[2]<=1)return false;
    memcpy(out,p,4*sizeof(float));out[0]=(float)(r[0]/r[2]);out[1]=(float)(r[1]/r[2]);out[3]=(float)(1/r[2]);
    return std::isfinite(out[0])&&std::isfinite(out[1]);
}
static void retain_outgoing(const std::vector<Primitive>&old,const std::vector<Primitive>&cur){
    outgoing_draws.clear();outgoing_triangles=0;float forward[16];if(!camera_forward_matrix(forward))return;
    const WideView view(current,display_settings.wide);
    std::vector<bool> matched(old.size());for(const auto&t:cur)if(t.prior>=0)matched[t.prior]=true;
    std::unordered_map<uint64_t,std::array<float,4>> shared;
    for(const auto&op:current.ops)if(projected_geometry(op)){
        const auto state=surface_state_key(op);
        for(unsigned v=0;v<op.vertices.size();v++)if(op.motion_valid[v]){
        std::array<float,4> point;memcpy(point.data(),op.vertices[v].p,sizeof(point));shared.emplace(surface_point_key(state,op.prior_positions[v].data()),point);}
    }
    std::unordered_map<GLuint,unsigned> last;
    for(unsigned i=0;i<current.ops.size();i++)if(!current.ops[i].clear)last[current.ops[i].target]=i;
    unsigned last_source=UINT_MAX;uint64_t before_state=0;
    for(unsigned i=0;i<old.size();i++){
        if(matched[i])continue;const auto&tri=old[i];const auto&before=previous.ops[tri.op];
        if(!projected_geometry(before)||before.state.src_rgb!=GL_ONE||before.state.dst_rgb!=GL_ZERO)continue;
        auto tail=last.find(before.target);if(tail==last.end())continue;
        // The known projected shaders use an orthographic screen transform.
        const auto&m=before.state.projection;if(m[0]==0||m[5]==0||m[3]!=0||m[7]!=0||m[15]!=1)continue;
        const bool wide_target=view.outer_width&&before.target&&before.state.viewport[2]==view.inner_width&&
            before.state.viewport[3]==view.drawable_height&&std::abs(m[0]-2.f/640)<.00001f;
        const float edge=wide_target?1.f/view.horizontal:1.f;
        float xmin=std::min((-edge-m[12])/m[0],(edge-m[12])/m[0]);
        float xmax=std::max((-edge-m[12])/m[0],(edge-m[12])/m[0]);
        if(wide_target&&view.horizontal<1){
            const float guard=turn_guard(current.w,current.h,448);
            xmin-=guard;xmax+=guard;
        }
        float ymin=std::min((-1-m[13])/m[5],(1-m[13])/m[5]);
        float ymax=std::max((-1-m[13])/m[5],(1-m[13])/m[5]);
        Vertex predicted[3];bool valid=true,left=true,right=true,top=true,bottom=true;
        bool was_left=true,was_right=true,was_top=true,was_bottom=true;
        if(last_source!=tri.op){last_source=tri.op;before_state=surface_state_key(before);}
        for(unsigned v=0;v<3;v++){
            const auto* p=before.vertices[tri.start+v].p;
            was_left&=p[0]<xmin+1;was_right&=p[0]>xmax-1;
            was_top&=p[1]<ymin+1;was_bottom&=p[1]>ymax-1;
            predicted[v]=before.vertices[tri.start+v];valid&=camera_forward(forward,predicted[v].p,predicted[v].p);
            auto welded=shared.find(surface_point_key(before_state,before.vertices[tri.start+v].p));if(welded!=shared.end()){
                predicted[v].p[0]=welded->second[0];predicted[v].p[1]=welded->second[1];predicted[v].p[3]=welded->second[3];}
            left&=predicted[v].p[0]<xmin+1;right&=predicted[v].p[0]>xmax-1;
            top&=predicted[v].p[1]<ymin+1;bottom&=predicted[v].p[1]>ymax-1;
        }
        if(!valid||!((left&&!was_left)||(right&&!was_right)||(top&&!was_top)||(bottom&&!was_bottom)))continue;
        auto& draw=departing_batch(before,tri.op,tail->second);
        for(unsigned v=0;v<3;v++){draw.vertices.push_back(predicted[v]);std::array<float,4> p;memcpy(p.data(),before.vertices[tri.start+v].p,sizeof(p));draw.prior_positions.push_back(p);draw.motion_valid.push_back(1);}
        outgoing_triangles++;
    }
}
