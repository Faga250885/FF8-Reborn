// Included inside namespace geo, after matrix helpers.
static void (*matching_checkpoint)()=nullptr;
struct Primitive {unsigned op,start;uint64_t key;int prior=-1;std::array<std::array<double,4>,3> points{};};
static bool zero_model(const State&s){for(float value:s.model)if(value!=0)return false;return true;}
static void matching_position(const State&s,const float*p,double*out){
    // Actual battle shaders 1/7 render with a zero legacy modelview matrix.
    // That unused matrix must not collapse every scene vertex into one point.
    if(s.program&&zero_model(s)){for(int k=0;k<4;k++)out[k]=p[k];}
    else transform(s.model,p,out);
}
static std::vector<Primitive> primitives(const Frame&frame){
    std::vector<Primitive> out;size_t count=0;for(const auto&op:frame.ops)count+=op.vertices.size()/3;out.reserve(count);
    for(unsigned i=0;i<frame.ops.size();i++){const auto&op=frame.ops[i];if(op.clear||op.mode!=GL_TRIANGLES||op.vertices.size()%3)continue;
        if((i&7)==0&&matching_checkpoint)matching_checkpoint();
        bool used[4]={};for(int j=0;j<4;j++)used[j]=!op.state.program&&op.state.texture[j].enabled;
        for(const auto&u:op.state.uniforms)if(u.type==GL_SAMPLER_2D)used[u.i[0]]=true;
        uint64_t base=hash(1469598103934665603ULL,&op.target,sizeof(op.target));base=hash(base,&op.state.program,sizeof(op.state.program));
        base=hash(base,op.state.projection,sizeof(op.state.projection));base=hash(base,op.state.viewport,sizeof(op.state.viewport));
        base=hash(base,&op.state.blend,sizeof(op.state.blend));base=hash(base,&op.state.src_rgb,sizeof(op.state.src_rgb));base=hash(base,&op.state.dst_rgb,sizeof(op.state.dst_rgb));
        for(int j=0;j<4;j++)if(used[j])base=hash(base,&op.state.texture[j].id,sizeof(GLint));
        const bool screen_coordinates=op.state.program&&zero_model(op.state);
        for(unsigned start=0;start<op.vertices.size();start+=3){uint64_t key=base;
            for(unsigned v=0;v<3;v++)for(int j=0;j<4;j++)if(used[j])key=hash(key,op.vertices[start+v].uv[j],sizeof(op.vertices[start+v].uv[j]));
            out.push_back({i,start,key,-1});
            for(unsigned v=0;v<3;v++){
                const float* point=op.vertices[start+v].p;
                auto& projected=out.back().points[v];
                if(screen_coordinates)std::copy_n(point,4,projected.begin());
                else transform(op.state.model,point,projected.data());
            }}
    }return out;
}
struct Topology;
namespace field_scroll {struct TileIndex;}
struct MatchInput {
    std::vector<Primitive> triangles;
    // The group table and all its index lists have exactly one frame lifetime.
    // Release their storage together instead of returning thousands of small
    // blocks to the process heap during a swap.
    std::pmr::monotonic_buffer_resource group_storage;
    std::pmr::unordered_map<uint64_t,std::pmr::vector<unsigned>> groups{&group_storage};
    std::shared_ptr<Topology> field_topology;
    std::shared_ptr<field_scroll::TileIndex> field_tiles;

    explicit MatchInput(const Frame& frame):triangles(primitives(frame)) {
        groups.reserve(triangles.size());
        for(unsigned i=0;i<triangles.size();i++){
            groups[triangles[i].key].push_back(i);
            if((i&255)==0&&matching_checkpoint)matching_checkpoint();
        }
    }
};
#include "camera_motion.h"
#include "coherent_motion.h"
#include "vertex_topology.h"
#include "outgoing_motion.h"
#include "surface_motion.h"
#include "battle_visibility.h"
#include "triangle_depth.h"
#include "world_actor.h"
#include "world_visible_actor.h"
#include "field_scroll.h"
// Preserve the fluent baseline cost for battle. Only world mode may compensate
// global camera movement before comparing candidate triangle positions.
static double primitive_cost(const Primitive&a,const Primitive&b){
    double cost=0,radius=0,delta=0;
    for(unsigned i=0;i<3;i++){const auto&p=a.points[i];const auto&q=b.points[i];
        for(int k=0;k<3;k++){double d=q[k]-p[k];cost+=d*d;delta=std::max(delta,fabs(d));radius=std::max(radius,std::max(fabs(p[k]),fabs(q[k])));}}
    if(delta>std::max(.25,radius*.35))return 1e100;return cost;
}
// Repeated materials compare each triangle with many candidates. Its size
// bound depends only on its own three corners and can be reused for each pair.
static double primitive_radius(const Primitive& t){
    double radius=0;
    for(const auto& p:t.points)for(int k=0;k<3;k++)radius=std::max(radius,fabs(p[k]));
    return radius;
}
static double bounded_primitive_cost(const Primitive& a,const Primitive& b,double limit){
    double cost=0;
    for(int v=0;v<3;v++)for(int k=0;k<3;k++){
        double d=b.points[v][k]-a.points[v][k];
        if(fabs(d)>limit)return 1e100;
        cost+=d*d;
    }
    return cost;
}
#include "billboard_motion.h"
// The sea and its translucent shading contain hundreds of triangles with
// identical material keys. Search only near the camera-predicted first corner,
// then require all three corners (including depth) to agree in both directions.
static void match_dense_world_group(const std::vector<Primitive>&old,std::vector<Primitive>&cur,
                                    const std::pmr::vector<unsigned>& a,const std::pmr::vector<unsigned>& b){
    struct Choice {
        int index=-1;double best=1e20,second=1e20;
        void offer(int i,double cost){if(cost<best){second=best;best=cost;index=i;}else second=std::min(second,cost);}
        bool clear()const{return index>=0&&best+1e-8<second*.8;}
    };
    auto cell=[](double p){return int(std::floor(p/4.));};
    auto key=[](int x,int y){return (uint64_t(uint32_t(x))<<32)|uint32_t(y);};
    auto usable=[](const auto& p){return p[3]>0&&p[3]<.1&&fabs(p[0])<16384&&fabs(p[1])<16384;};
    std::unordered_map<uint64_t,std::vector<unsigned>> bins;
    std::vector<Choice> forward(a.size()),backward(b.size());
    for(unsigned i=0;i<a.size();i++){const auto&p=old[a[i]].points[0];if(usable(p))bins[key(cell(p[0]),cell(p[1]))].push_back(i);}
    for(unsigned j=0;j<b.size();j++){
        if((j&255)==0&&matching_checkpoint)matching_checkpoint();
        const auto& q=cur[b[j]].points;if(!usable(q[0]))continue;
        const int x=cell(q[0][0]),y=cell(q[0][1]);
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
            auto candidates=bins.find(key(x+dx,y+dy));
            if(candidates==bins.end()||candidates->second.size()>32)continue;
            for(unsigned i:candidates->second){
                const auto& p=old[a[i]].points;double cost=0;bool valid=true;
                for(int k=0;k<3;k++){
                    if(!usable(p[k])||!usable(q[k])){valid=false;break;}
                    double sx=p[k][0]-q[k][0],sy=p[k][1]-q[k][1];
                    double depth=512*(p[k][3]-q[k][3])/q[k][3];
                    if(std::abs(sx)>3||std::abs(sy)>3||std::abs(depth)>3){valid=false;break;}
                    cost+=sx*sx+sy*sy+depth*depth;
                }
                if(valid){forward[i].offer(j,cost);backward[j].offer(i,cost);}
            }
        }
    }
    for(unsigned j=0;j<b.size();j++)if(backward[j].clear()){
        unsigned i=backward[j].index;
        if(forward[i].clear()&&forward[i].index==int(j))cur[b[j]].prior=a[i];
    }
}
static void match_frames(int mode=-1){
    current.camera_cut=mode==3&&battle_shot_changed(previous.bones,current.bones);
#ifdef GEO_PROFILE
    double mark=now();auto mark_stage=[&](const char*name){double t=now();printf("%s %.3f ms\n",name,(t-mark)*1000);mark=t;};
#endif
    // A published field/world endpoint becomes the previous endpoint on the
    // next native tick. Bone/depth processing can change battle vertices.
    const bool reusable=(mode==1||mode==2)&&previous.started;
    if(!reusable||!previous.match_input)previous.match_input=std::make_shared<MatchInput>(previous);
    current.match_input=std::make_shared<MatchInput>(current);
    const auto& old=previous.match_input->triangles;
    auto cur=current.match_input->triangles;
    const auto& old_groups=previous.match_input->groups;
    const auto& new_groups=current.match_input->groups;
    if(matching_checkpoint)matching_checkpoint();
#ifdef GEO_PROFILE
    mark_stage("primitive keys and groups");
#endif
    camera_motion=CameraMotion{};if(mode==2)fit_camera_motion(old,cur,old_groups,new_groups);
    if(camera_motion.valid)for(auto&tri:cur){const auto&op=current.ops[tri.op];if(op.state.program&&zero_model(op.state))
        for(unsigned v=0;v<3;v++){float predicted[4];if(camera_predict(op.vertices[tri.start+v].p,predicted))for(int k=0;k<4;k++)tri.points[v][k]=predicted[k];}}
    for(auto&op:current.ops){op.match=-1;op.prior_positions.resize(op.vertices.size());op.motion_valid.assign(op.vertices.size(),0);
        op.prior_colors.resize(op.vertices.size());op.color_valid.assign(op.vertices.size(),0);op.actor_mid.clear();op.front_face.clear();op.depth_bias.clear();}
    if(matching_checkpoint)matching_checkpoint();
#ifdef GEO_PROFILE
    mark_stage("camera");
#endif
    // Mutual nearest matches with an ambiguity margin; never rely on draw-call
    // ordering, since FFVIII changes batch boundaries while sorting polygons.
    std::vector<int> best_a,best_b;
    std::vector<double> ca,cb,sa,sb,radius_a,radius_b;
    for(const auto&group:new_groups){auto found=old_groups.find(group.first);if(found==old_groups.end())continue;
        const auto&a=found->second;const auto&b=group.second;
        if(uint64_t(a.size())*b.size()>65536){
            if(camera_motion.valid)match_dense_world_group(old,cur,a,b);
            continue;
        }
        if(a.size()==1&&b.size()==1){if(primitive_cost(old[a[0]],cur[b[0]])<8e99)cur[b[0]].prior=a[0];continue;}
        best_a.assign(a.size(),-1);best_b.assign(b.size(),-1);
        ca.assign(a.size(),1e100);cb.assign(b.size(),1e100);
        sa.assign(a.size(),1e100);sb.assign(b.size(),1e100);
        const bool reuse_bounds=a.size()*b.size()>=64;
        if(reuse_bounds){
            radius_a.resize(a.size());radius_b.resize(b.size());
            for(unsigned i=0;i<a.size();i++)radius_a[i]=primitive_radius(old[a[i]]);
            for(unsigned j=0;j<b.size();j++)radius_b[j]=primitive_radius(cur[b[j]]);
        }
        for(unsigned i=0;i<a.size();i++)for(unsigned j=0;j<b.size();j++){
            const double cost=reuse_bounds?bounded_primitive_cost(old[a[i]],cur[b[j]],std::max(.25,std::max(radius_a[i],radius_b[j])*.35)):primitive_cost(old[a[i]],cur[b[j]]);
            if(cost<ca[i]){sa[i]=ca[i];ca[i]=cost;best_a[i]=j;}else sa[i]=std::min(sa[i],cost);
            if(cost<cb[j]){sb[j]=cb[j];cb[j]=cost;best_b[j]=i;}else sb[j]=std::min(sb[j],cost);}
        for(unsigned j=0;j<b.size();j++){int i=best_b[j];if(i<0||best_a[i]!=(int)j)continue;
            if(cb[j]+1e-12>=sb[j]*.8||ca[i]+1e-12>=sa[i]*.8)continue;cur[b[j]].prior=a[i];}
    }
#ifdef GEO_PROFILE
    mark_stage("mutual matching");
#endif
    matched_candidates=0;singular_rejects=0;
    for(auto&tri:cur)if(tri.prior>=0){auto&op=current.ops[tri.op];const auto&prior=old[tri.prior];const auto&before=previous.ops[prior.op];
        bool same=!memcmp(op.state.model,before.state.model,sizeof(op.state.model));float inverse[16];
        if(!same&&!invert_matrix(op.state.model,inverse)){tri.prior=-1;singular_rejects++;continue;}
        matched_candidates++;
        for(unsigned v=0;v<3;v++){auto&point=op.prior_positions[tri.start+v];const float*p=before.vertices[prior.start+v].p;
            if(same)memcpy(point.data(),p,4*sizeof(float));else{double view[4];transform(before.state.model,p,view);
                for(int r=0;r<4;r++){double value=0;for(int c=0;c<4;c++)value+=inverse[c*4+r]*view[c];point[r]=(float)value;}}
            op.motion_valid[tri.start+v]=1;
            if(!projected_geometry(op)||(p[3]>0&&p[3]<.1f)){
                memcpy(op.prior_colors[tri.start+v].data(),before.vertices[prior.start+v].c,4*sizeof(float));op.color_valid[tri.start+v]=1;}}
    }
#ifdef GEO_PROFILE
    mark_stage("assign positions");
#endif
    if(matching_checkpoint)matching_checkpoint();
    classify_native_draws(mode);
    if(mode!=2)anchor_vertex_topology(old,cur,mode==1&&previous.started);else topology_anchors=topology_corrected=topology_partial=0;
    if(matching_checkpoint)matching_checkpoint();
#ifdef GEO_PROFILE
    mark_stage("vertex topology");
#endif
    ForwardSurface forward_surface;
    if(mode==3&&surface_motion_enabled)forward_surface=prepare_surface_motion();
    coherent_projected_motion(mode==1?&cur:nullptr,mode==1?&old:nullptr);
    if(matching_checkpoint)matching_checkpoint();
    retain_outgoing(old,cur);
    if(mode==3&&battle_visibility_enabled){mark_battle_front_faces();retain_battle_outgoing(old,cur,forward_surface);}
    if(mode==3&&battle_depth_enabled)prepare_triangle_depth(old,cur);
#ifdef GEO_PROFILE
    mark_stage("coherence");
#endif
    // Connectivity is per triangle, never per batch. Material and UV seams
    // can share a geometric point. An unmatched/inconsistent connected piece
    // stays at the current pose as a whole, so no cracks are introduced.
    std::vector<unsigned> parent(cur.size());std::vector<bool> invalid(cur.size());
    for(unsigned i=0;i<cur.size();i++){parent[i]=i;invalid[i]=cur[i].prior<0;}
    auto root=[&](unsigned i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    struct Shared {unsigned tri;std::array<double,4> prior;bool valid;};
    std::pmr::monotonic_buffer_resource point_storage;
    std::pmr::unordered_map<uint64_t,Shared> points{&point_storage};
    unsigned last_op=UINT_MAX;uint64_t state_key=0;
    for(unsigned i=0;i<cur.size();i++){const auto&tri=cur[i];const auto&op=current.ops[tri.op];
        if(projected_geometry(op))continue;
        if(last_op!=tri.op){
            state_key=hash(1469598103934665603ULL,&op.target,sizeof(op.target));
            state_key=hash(state_key,&op.state.program,sizeof(op.state.program));
            state_key=hash(state_key,op.state.projection,sizeof(op.state.projection));last_op=tri.op;
        }
        for(unsigned v=0;v<3;v++){unsigned index=tri.start+v;double p[4];matching_position(op.state,op.vertices[index].p,p);
            float canonical[4];for(int k=0;k<4;k++)canonical[k]=p[k]==0?0:(float)p[k];
            uint64_t key=hash(state_key,canonical,sizeof(canonical));
            Shared point{i,{},op.motion_valid[index]!=0};if(point.valid)matching_position(op.state,op.prior_positions[index].data(),point.prior.data());
            auto found=points.find(key);if(found==points.end())points.emplace(key,point);
            else{unsigned a=root(i),b=root(found->second.tri);if(a!=b){parent[b]=a;invalid[a]=invalid[a]||invalid[b];}
                if(point.valid&&found->second.valid)for(int k=0;k<4;k++)if(fabs(point.prior[k]-found->second.prior[k])>(k==3?1e-7:1e-3))invalid[root(i)]=true;}
        }
    }
    incoherent_draws=0;for(unsigned i=0;i<cur.size();i++)if(invalid[root(i)]){auto&op=current.ops[cur[i].op];
        if(projected_geometry(op))continue;
        if(cur[i].prior>=0)incoherent_draws++;for(unsigned v=0;v<3;v++)op.motion_valid[cur[i].start+v]=0;}
#ifdef GEO_TRACE_SURFACES
    if(const char*path=getenv("FF8_TRACE_TRIANGLES")){
        FILE*f=fopen(path,"w");
        if(f){
            for(const auto&t:cur){
                const auto&op=current.ops[t.op];int prior_op=-1,prior_start=-1;
                if(t.prior>=0){prior_op=old[t.prior].op;prior_start=old[t.prior].start;}
                fprintf(f,"%u,%u,%d,%d,%d",t.op,t.start,op.state.texture[0].id,prior_op,prior_start);
                for(unsigned k=0;k<3;k++){const auto&p=op.prior_positions[t.start+k];fprintf(f,",%.8g,%.8g,%.10g",p[0],p[1],p[3]);}
                fputc('\n',f);
            }
            fclose(f);
        }
    }
#endif
    if(mode==2){
        world_actor::prepare();
        if(!world_actor::link_count)world_actor::mesh::recover();
    }
    if(mode==3)resolve_cards();
    if(mode==1)field_scroll::align(previous,current);
    moving_draws=0;
    for(auto&op:current.ops){bool moving=false,identical=true;
        // Screen-space HUD panels and glyph clipping are not mesh surfaces.
        // Neighbour deformation must never bend their rectangular borders.
        if(projected_geometry(op))for(size_t i=0;i<op.vertices.size();i++)if(op.vertices[i].p[3]>=.1f){op.motion_valid[i]=0;op.color_valid[i]=0;}
        for(size_t i=0;i<op.vertices.size();i++)if(op.motion_valid[i]){
            op.match=0;
            if(!moving&&memcmp(op.prior_positions[i].data(),op.vertices[i].p,sizeof(op.vertices[i].p))){
                identical=false;
                for(int k=0;k<4;k++)moving|=fabs(op.prior_positions[i][k]-op.vertices[i].p[k])>1e-7;
            }
        }
        for(size_t i=0;i<op.color_valid.size();i++)if(op.color_valid[i]){op.match=0;
            if(!moving&&memcmp(op.prior_colors[i].data(),op.vertices[i].c,sizeof(op.vertices[i].c))){
                identical=false;
                for(int k=0;k<4;k++)moving|=fabs(op.prior_colors[i][k]-op.vertices[i].c[k])>1e-7;
            }}
        // Exact endpoints can use the recorded vertices for every phase.
        // Actor arcs, depth corrections and face visibility also vary with
        // phase even when the endpoint attributes themselves happen to match.
        if(identical&&op.actor_mid.empty()&&op.depth_bias.empty()&&op.front_face.empty())op.match=-1;
        moving_draws+=moving;
    }
}
