// A vertex's incident textured faces identify its place on a mesh more
// strongly than an isolated triangle. Partial stars survive faces entering or
// leaving visibility, but require mutual, unambiguous multi-face support.
static unsigned topology_anchors=0,topology_corrected=0,topology_partial=0;
#ifdef GEO_AUTOTEST
static bool partial_topology_enabled=true;
#endif
struct TopologyPoint {
    std::array<float,4> position{};
    uint64_t sum=0,xors=0,signature=0;
    unsigned count=0;int prior=-1;
};
struct TopologyRef {unsigned op,vertex,node;};
struct Topology {std::vector<TopologyPoint> points;std::vector<TopologyRef> refs;std::vector<std::pair<unsigned,uint64_t>> faces;std::vector<unsigned> offsets;};
static Topology topology_points(const Frame&frame,const std::vector<Primitive>&tris){
    Topology result;auto&points=result.points;points.reserve(tris.size());result.refs.reserve(tris.size()*3);
    PointIndex lookup(tris.size()*3);
    std::vector<uint64_t> states;states.reserve(frame.ops.size());
    for(const auto&op:frame.ops){uint64_t key=hash(1469598103934665603ULL,&op.target,sizeof(op.target));
        key=hash(key,op.state.projection,sizeof(op.state.projection));states.push_back(hash(key,op.state.viewport,sizeof(op.state.viewport)));}
    unsigned visited=0;
    for(const auto&tri:tris){const auto&op=frame.ops[tri.op];if(op.indexed_complete||!projected_geometry(op))continue;
        if((visited++&127)==0&&matching_checkpoint)matching_checkpoint();
        uint64_t state=states[tri.op];
        for(unsigned v=0;v<3;v++){const auto&p=op.vertices[tri.start+v].p;if(p[3]<=0||p[3]>=.1f)continue;
            float xyw[]={p[0]==0?0:p[0],p[1]==0?0:p[1],p[3]};uint64_t key=hash(state,xyw,sizeof(xyw));
            const unsigned n=lookup.node(key,static_cast<unsigned>(points.size()));
            if(n==points.size()){points.emplace_back();memcpy(points.back().position.data(),p,sizeof(p));}
            uint64_t face=hash(tri.key,&v,sizeof(v));auto&point=points[n];point.sum+=face;point.xors^=face;point.count++;
            result.refs.push_back({tri.op,tri.start+v,n});
            result.faces.emplace_back(n,face);
        }
    }
    for(auto&point:points){uint64_t fields[]={point.sum,point.xors,point.count};point.signature=hash(1469598103934665603ULL,fields,sizeof(fields));}
    group_neighbours(result.faces,points.size(),result.offsets);
    return result;
}
static void anchor_vertex_topology(const std::vector<Primitive>&old,const std::vector<Primitive>&cur,bool reuse_field=false){
    topology_anchors=topology_corrected=topology_partial=0;
    Topology previous_topology;
    if(reuse_field){
        if(!previous.match_input->field_topology)
            previous.match_input->field_topology=std::make_shared<Topology>(topology_points(previous,old));
    }else previous_topology=topology_points(previous,old);
    const auto& before_topology=reuse_field?*previous.match_input->field_topology:previous_topology;
    auto after_topology=topology_points(current,cur);
    const auto& before=before_topology.points;auto&after=after_topology.points;
    PointIndex a(before.size()),b(after.size());
    for(unsigned i=0;i<before.size();i++){auto& id=a.entry(before[i].signature);id=id?UINT_MAX:i+1;}
    for(unsigned i=0;i<after.size();i++){auto& id=b.entry(after[i].signature);id=id?UINT_MAX:i+1;}
    for(auto& dest:after){
        if(b.entry(dest.signature)==UINT_MAX)continue;
        const unsigned id=a.entry(dest.signature);
        if(!id||id==UINT_MAX)continue;
        const auto&source=before[id-1];
        if(source.count<2||source.count!=dest.count||source.sum!=dest.sum||source.xors!=dest.xors)continue;
        float predicted[4];camera_predict(dest.position.data(),predicted);
        double radius=1,delta=0;for(int k=0;k<2;k++){radius=std::max(radius,std::max(fabs(double(source.position[k])),fabs(double(predicted[k]))));delta=std::max(delta,fabs(double(source.position[k])-predicted[k]));}
        if(delta>std::max(32.,radius*.35)||fabs(std::log(double(source.position[3])/predicted[3]))>.7)continue;
        topology_anchors++;dest.prior=int(id-1);
    }
#ifdef GEO_AUTOTEST
    if(partial_topology_enabled){
#endif
    // Exact, unique vertex stars already establish identity. Reserve those
    // source vertices before the partial search, so an incomplete neighbour
    // cannot claim the same old vertex and pull two distinct points together.
    std::vector<bool> claimed(before.size());
    for(const auto&point:after)if(point.prior>=0)claimed[point.prior]=true;
    std::pmr::monotonic_buffer_resource face_storage;
    std::pmr::unordered_map<uint64_t,std::pmr::vector<unsigned>> face_nodes{&face_storage};face_nodes.reserve(before_topology.faces.size());
    for(const auto&face:before_topology.faces)if(!claimed[face.first])face_nodes[face.second].push_back(face.first);
    struct Best{int node=-1;unsigned score=0,second=0;};std::vector<Best> best_old(before.size()),best_new(after.size());
    auto vote=[](Best&best,unsigned node,unsigned score){if(score>best.score){best.second=best.score;best.score=score;best.node=(int)node;}else best.second=std::max(best.second,score);};
    std::vector<unsigned> candidates;
    for(unsigned i=0;i<after.size();i++){
        if(after[i].prior>=0)continue;
        if((i&127)==0&&matching_checkpoint)matching_checkpoint();candidates.clear();
        for(unsigned f=after_topology.offsets[i];f<after_topology.offsets[i+1];f++){
            auto found=face_nodes.find(after_topology.faces[f].second);if(found==face_nodes.end()||found->second.size()>32)continue;
            candidates.insert(candidates.end(),found->second.begin(),found->second.end());}
        std::sort(candidates.begin(),candidates.end());
        for(size_t at=0;at<candidates.size();){unsigned j=candidates[at],score=0;do{at++;score++;}while(at<candidates.size()&&candidates[at]==j);
            if(score<2)continue;unsigned support=std::min(before_topology.offsets[j+1]-before_topology.offsets[j],after_topology.offsets[i+1]-after_topology.offsets[i]);
            if(score*5<support*3)continue;
            const auto&p=before[j].position;const auto&q=after[i].position;
            if(std::hypot(p[0]-q[0],p[1]-q[1])>160||fabs(std::log(double(p[3])/q[3]))>.7)continue;
            vote(best_new[i],j,score);vote(best_old[j],i,score);
        }
    }
    for(unsigned i=0;i<after.size();i++)if(after[i].prior<0){const auto&match=best_new[i];if(match.node<0||match.score<=match.second)continue;
        const auto&back=best_old[match.node];if(back.node!=(int)i||back.score<=back.second)continue;
        after[i].prior=match.node;topology_anchors++;topology_partial++;}
#ifdef GEO_AUTOTEST
    }
#endif
    for(auto ref:after_topology.refs){int index=after[ref.node].prior;if(index<0)continue;const auto&source=before[index];
        auto&op=current.ops[ref.op];auto&prior=op.prior_positions[ref.vertex];
        topology_corrected+=!op.motion_valid[ref.vertex]||fabs(prior[0]-source.position[0])>.001||fabs(prior[1]-source.position[1])>.001||fabs(prior[3]-source.position[3])>1e-9;
        prior=source.position;op.motion_valid[ref.vertex]=1;
    }
    if(reuse_field)current.match_input->field_topology=std::make_shared<Topology>(std::move(after_topology));
}
