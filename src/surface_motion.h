// Estimate the pose of vertices revealed or hidden by a turn. Fit in
// homogeneous view space so a rotating surface can move across the screen
// and in depth together. Only exact, mutually consistent matches are anchors.
struct SurfaceNode {
    std::array<float,4> point{}, destination{};
    std::vector<unsigned> adjacent;
    int partner=-1;
    bool predicted=false,native=false;
};
struct SurfaceGraph {
    std::vector<SurfaceNode> nodes;
    std::vector<std::vector<unsigned>> refs;
    std::unordered_map<uint64_t,unsigned> index;
};
static uint64_t surface_point_key(uint64_t state,const float*p){
    float xyw[]={p[0]==0?0:p[0],p[1]==0?0:p[1],p[3]};
    return hash(state,xyw,sizeof(xyw));
}
static uint64_t surface_state_key(const Op&op){
    uint64_t key=hash(1469598103934665603ULL,&op.target,sizeof(op.target));
    key=hash(key,op.state.projection,sizeof(op.state.projection));
    return hash(key,op.state.viewport,sizeof(op.state.viewport));
}
static bool surface_draw(const Op&op){
    return projected_geometry(op)&&(!op.state.blend||(op.state.src_rgb==GL_ONE&&op.state.dst_rgb==GL_ZERO));
}
static SurfaceGraph surface_graph(const Frame&frame){
    SurfaceGraph graph;
    graph.refs.resize(frame.ops.size());
    for(unsigned oi=0;oi<frame.ops.size();oi++){
        const Op&op=frame.ops[oi];if(op.indexed_complete||!surface_draw(op))continue;
        if((oi&15)==0&&matching_checkpoint)matching_checkpoint();
        auto&refs=graph.refs[oi];refs.assign(op.vertices.size(),UINT_MAX);
        const uint64_t state=surface_state_key(op);
        for(unsigned at=0;at+2<op.vertices.size();at+=3){
            bool scene=true;
            for(unsigned v=0;v<3;v++){float rhw=op.vertices[at+v].p[3];scene&=rhw>0&&rhw<.1f;}
            if(!scene)continue;
            for(unsigned v=0;v<3;v++){
                const float*p=op.vertices[at+v].p;
                auto slot=graph.index.emplace(surface_point_key(state,p),(unsigned)graph.nodes.size());
                unsigned node=slot.first->second;
                if(slot.second){graph.nodes.emplace_back();memcpy(graph.nodes.back().point.data(),p,4*sizeof(float));}
                refs[at+v]=node;
            }
            for(unsigned v=0;v<3;v++){
                unsigned a=refs[at+v],b=refs[at+(v+1)%3];if(a==b)continue;
                graph.nodes[a].adjacent.push_back(b);graph.nodes[b].adjacent.push_back(a);
            }
        }
    }
    for(auto&node:graph.nodes){auto&adj=node.adjacent;std::sort(adj.begin(),adj.end());adj.erase(std::unique(adj.begin(),adj.end()),adj.end());}
    return graph;
}
static std::array<double,3> local_coordinates(const float*p,const float*query){
    double ratio=double(query[3])/p[3];
    return {(p[0]-320.)*ratio-(query[0]-320.),(p[1]-240.)*ratio-(query[1]-240.),(ratio-1.)*1024.};
}
static bool fit_surface_vertex(SurfaceNode&node,const std::vector<const SurfaceNode*>&anchors){
    if(anchors.size()<4)return false;
    struct Sample{std::array<double,3> source,delta;double weight;};
    std::array<Sample,24> samples;
    double radius2=0;
    for(size_t i=0;i<anchors.size();i++){
        auto&sample=samples[i];sample.source=local_coordinates(anchors[i]->point.data(),node.point.data());
        auto dest=local_coordinates(anchors[i]->destination.data(),node.point.data());
        for(int k=0;k<3;k++){sample.delta[k]=dest[k]-sample.source[k];radius2+=sample.source[k]*sample.source[k];}
    }
    radius2/=anchors.size();if(radius2<.01)return false;
    double scale=std::sqrt(radius2),normal[4][4]{},rhs[4][3]{},solution[4][3]{},motion=0;
    for(size_t i=0;i<anchors.size();i++){
        const auto&s=samples[i].source;double feature[]={s[0]/scale,s[1]/scale,s[2]/scale,1};
        double dist=(s[0]*s[0]+s[1]*s[1]+s[2]*s[2])/radius2;
        double weight=1./(.1+dist);samples[i].weight=weight;
        motion=std::max(motion,std::hypot(samples[i].delta[0],samples[i].delta[1]));
        for(int j=0;j<4;j++){
            for(int k=0;k<4;k++)normal[j][k]+=weight*feature[j]*feature[k];
            for(int k=0;k<3;k++)rhs[j][k]+=weight*feature[j]*samples[i].delta[k];
        }
    }
    // The normal of a flat patch is underconstrained. Regularize only the
    // slope, so a translation of any size is reproduced exactly.
    double ridge=.0003*normal[3][3];
    for(int i=0;i<3;i++)normal[i][i]+=ridge;
    if(!solve_camera(normal,rhs,solution))return false;
    double squared=0,weights=0;
    for(size_t i=0;i<anchors.size();i++){
        auto&s=samples[i];double error=0;
        for(int k=0;k<3;k++){
            double predicted=solution[3][k];
            for(int j=0;j<3;j++)predicted+=s.source[j]*solution[j][k]/scale;
            double delta=predicted-s.delta[k];error+=delta*delta;
        }
        squared+=s.weight*error;weights+=s.weight;
    }
    if(std::sqrt(squared/weights)>std::clamp(.65+.025*motion,.65,2.5))return false;
    double ratio=1.+solution[3][2]/1024.;
    if(!std::isfinite(ratio)||ratio<.55||ratio>1.8)return false;
    node.destination=node.point;
    node.destination[0]=(float)(320.+(node.point[0]-320.+solution[3][0])/ratio);
    node.destination[1]=(float)(240.+(node.point[1]-240.+solution[3][1])/ratio);
    node.destination[3]=(float)(node.point[3]/ratio);
    return std::isfinite(node.destination[0])&&std::isfinite(node.destination[1])&&
        std::hypot(node.destination[0]-node.point[0],node.destination[1]-node.point[1])<220.;
}
static void fill_surface(SurfaceGraph&graph){
    std::vector<unsigned> seen(graph.nodes.size(),UINT_MAX),queue;queue.reserve(144);
    std::vector<const SurfaceNode*> anchors;anchors.reserve(24);
    for(unsigned i=0;i<graph.nodes.size();i++){
        if((i&127)==0&&matching_checkpoint)matching_checkpoint();
        auto&node=graph.nodes[i];if(node.partner>=0||node.native)continue;
        queue.clear();anchors.clear();queue.push_back(i);seen[i]=i;
        unsigned first=0,last=1;
        for(unsigned ring=0;ring<5&&first<last&&anchors.size()<12;ring++){
            for(;first<last;first++)for(unsigned next:graph.nodes[queue[first]].adjacent){
                if(seen[next]==i||queue.size()>=144)continue;
                seen[next]=i;queue.push_back(next);
                if((graph.nodes[next].partner>=0||graph.nodes[next].native)&&anchors.size()<24)anchors.push_back(&graph.nodes[next]);
            }
            last=(unsigned)queue.size();
        }
        node.predicted=fit_surface_vertex(node,anchors);
    }
}
#include "hidden_pose.h"
static bool surface_motion_enabled=true;
static unsigned surface_reverse_count=0,surface_forward_count=0;
using ForwardSurface=std::unordered_map<uint64_t,std::array<float,4>>;
static unsigned registered_before=0,registered_after=0;
static BoneTracks active_bones;
static void classify_native_draws(int mode){
    registered_before=registered_after=0;
    active_bones=mode==3?link_bones(previous.bones,current.bones):BoneTracks{};
    auto classify=[](Frame&f,const BoneLinks&links){
        unsigned hits=0;std::unordered_map<uint64_t,bool> unique;
        for(auto&op:f.ops){op.indexed_complete=false;
            if(links.empty()||!surface_draw(op))continue;
            bool covered=!op.vertices.empty();
            for(const auto&v:op.vertices){
                const auto*bone=lookup_bone(links,v.p);
                if(bone){hits++;if(unique.size()<16){BonePoint q;from_gl(v.p,q);unique[q.key()]=true;}}
                else covered=false;
            }op.indexed_complete=covered;
        }return unique.size()>=16?hits:0u;
    };
    registered_before=classify(previous,active_bones.forward);
    registered_after=classify(current,active_bones.backward);
    if(!registered_before||!registered_after){active_bones=BoneTracks{};registered_before=registered_after=0;
        for(auto*f:{&previous,&current})for(auto&o:f->ops)o.indexed_complete=false;}
}
static ForwardSurface prepare_surface_motion(){
    auto before=surface_graph(previous),after=surface_graph(current);
    auto vote=[](int&slot,unsigned value){if(slot==-1)slot=(int)value;else if(slot!=(int)value)slot=-2;};
    for(unsigned oi=0;oi<current.ops.size();oi++){
        auto&op=current.ops[oi];const auto&refs=after.refs[oi];if(refs.empty())continue;
        const uint64_t state=surface_state_key(op);
        for(unsigned v=0;v<refs.size();v++){
            if(refs[v]==UINT_MAX||!op.motion_valid[v])continue;
            auto found=before.index.find(surface_point_key(state,op.prior_positions[v].data()));
            if(found==before.index.end())continue;
            vote(after.nodes[refs[v]].partner,found->second);vote(before.nodes[found->second].partner,refs[v]);
        }
    }
    for(unsigned i=0;i<after.nodes.size();i++){
        auto&node=after.nodes[i];if(node.partner<0)continue;
        auto&other=before.nodes[node.partner];
        if(other.partner!=(int)i){node.partner=-2;continue;}
        node.destination=other.point;other.destination=node.point;
    }
    for(unsigned i=0;i<before.nodes.size();i++){auto&node=before.nodes[i];if(node.partner>=0&&after.nodes[node.partner].partner!=(int)i)node.partner=-2;}
    auto register_pose=[](SurfaceGraph&g,const BoneLinks&links){
        if(links.empty())return;
        for(auto&node:g.nodes)if(const auto*bone=lookup_bone(links,node.point.data())){
            node.destination=node.point;bone->apply(node.destination);node.native=node.predicted=true;
        }
    };
    register_pose(before,active_bones.forward);register_pose(after,active_bones.backward);
    fill_surface(after);fill_surface(before);
    complete_hidden_pose(after);complete_hidden_pose(before);
#ifdef GEO_TRACE_SURFACES
    if(const char*path=getenv("FF8_TRACE_NODES")){
        FILE*f=fopen(path,"w");
        if(f){for(const auto&n:before.nodes)fprintf(f,"%.8g,%.8g,%.10g,%.8g,%.8g,%.10g,%d,%d\n",n.point[0],n.point[1],n.point[3],n.destination[0],n.destination[1],n.destination[3],n.partner,n.predicted);fclose(f);}
    }
#endif
    surface_reverse_count=surface_forward_count=0;
    for(unsigned oi=0;oi<current.ops.size();oi++){
        auto&op=current.ops[oi];const auto&refs=after.refs[oi];
        if(op.indexed_complete)for(unsigned v=0;v<op.vertices.size();v++){
            auto&prior=op.prior_positions[v];memcpy(prior.data(),op.vertices[v].p,sizeof(op.vertices[v].p));
            lookup_bone(active_bones.backward,op.vertices[v].p)->apply(prior);op.motion_valid[v]=1;
        }
        for(unsigned v=0;v<refs.size();v++)if(refs[v]!=UINT_MAX&&after.nodes[refs[v]].predicted){
            op.prior_positions[v]=after.nodes[refs[v]].destination;op.motion_valid[v]=1;surface_reverse_count++;}
    }
    ForwardSurface result;
    for(const auto&entry:before.index){const auto&node=before.nodes[entry.second];if(node.predicted){result.emplace(entry.first,node.destination);surface_forward_count++;}}
    return result;
}
