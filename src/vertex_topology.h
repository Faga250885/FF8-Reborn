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
struct Topology {std::vector<TopologyPoint> points;std::vector<TopologyRef> refs;std::vector<std::pair<unsigned,uint64_t>> faces;std::vector<unsigned> offsets;};
static Topology topology_points(const Op* ops,size_t op_count,const std::vector<Primitive>&tris,bool field_worker){
    Topology result;auto&points=result.points;points.reserve(tris.size());result.refs.reserve(tris.size()*3);
    PointIndex lookup(tris.size()*3);
    std::vector<uint64_t> states;states.reserve(op_count);
    for(size_t i=0;i<op_count;++i){const auto& op=ops[i];uint64_t key=hash(1469598103934665603ULL,&op.target,sizeof(op.target));
        key=hash(key,op.state.projection,sizeof(op.state.projection));states.push_back(hash(key,op.state.viewport,sizeof(op.state.viewport)));}
    unsigned visited=0;
    for(const auto&tri:tris){const auto&op=ops[tri.op];if((!field_worker&&op.indexed_complete)||!projected_geometry(op))continue;
        if(!field_worker&&(visited++&127)==0&&matching_checkpoint)matching_checkpoint();
        uint64_t state=states[tri.op];
        for(unsigned v=0;v<3;v++){const auto&p=op.vertices[tri.start+v].p;if(p[3]<=0||p[3]>=.1f)continue;
            uint64_t key=projected_point_key(state,p);
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
static Topology topology_points(const Frame& frame,const std::vector<Primitive>& tris){
    return topology_points(frame.ops.data(),frame.ops.size(),tris,false);
}
struct TopologyVotes {unsigned anchors=0,partial=0;};
static TopologyVotes resolve_topology(const Topology& before_topology,Topology& after_topology,bool field_worker){
    TopologyVotes votes;
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
        auto predicted=dest.position;if(!field_worker)camera_predict(dest.position.data(),predicted.data());
        double radius=1,delta=0;for(int k=0;k<2;k++){radius=std::max(radius,std::max(fabs(double(source.position[k])),fabs(double(predicted[k]))));delta=std::max(delta,fabs(double(source.position[k])-predicted[k]));}
        const double depth_ratio=double(source.position[3])/predicted[3];
        if(delta>std::max(32.,radius*.35)||depth_ratio<std::exp(-.7)||depth_ratio>std::exp(.7))continue;
        votes.anchors++;dest.prior=int(id-1);
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
        if(!field_worker&&(i&127)==0&&matching_checkpoint)matching_checkpoint();candidates.clear();
        for(unsigned f=after_topology.offsets[i];f<after_topology.offsets[i+1];f++){
            auto found=face_nodes.find(after_topology.faces[f].second);if(found==face_nodes.end()||found->second.size()>32)continue;
            candidates.insert(candidates.end(),found->second.begin(),found->second.end());}
        std::sort(candidates.begin(),candidates.end());
        for(size_t at=0;at<candidates.size();){unsigned j=candidates[at],score=0;do{at++;score++;}while(at<candidates.size()&&candidates[at]==j);
            if(score<2)continue;unsigned support=std::min(before_topology.offsets[j+1]-before_topology.offsets[j],after_topology.offsets[i+1]-after_topology.offsets[i]);
            if(score*5<support*3)continue;
            const auto&p=before[j].position;const auto&q=after[i].position;
            const double dx=p[0]-q[0],dy=p[1]-q[1],depth_ratio=double(p[3])/q[3];
            if(dx*dx+dy*dy>160.*160.||depth_ratio<std::exp(-.7)||depth_ratio>std::exp(.7))continue;
            vote(best_new[i],j,score);vote(best_old[j],i,score);
        }
    }
    for(unsigned i=0;i<after.size();i++)if(after[i].prior<0){const auto&match=best_new[i];if(match.node<0||match.score<=match.second)continue;
        const auto&back=best_old[match.node];if(back.node!=(int)i||back.score<=back.second)continue;
        after[i].prior=match.node;votes.anchors++;votes.partial++;}
#ifdef GEO_AUTOTEST
    }
#endif
    return votes;
}
// A field has no native bone classification. Its corner index depends only
// on captured state, positions and triangle keys. The Op allocation stays
// put even when a presentation checkpoint swaps the two Frame objects.
class FieldTopologyJob {
    const Op* ops;
    size_t count;
    const std::vector<Primitive>& triangles;
    const Topology* previous_index;
    Topology result;
    TopologyVotes counts;
    PTP_WORK work=nullptr;
    bool complete=false;
    static void CALLBACK run(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK){
        auto& job=*static_cast<FieldTopologyJob*>(context);
        try{
            job.result=topology_points(job.ops,job.count,job.triangles,true);
            if(job.previous_index)job.counts=resolve_topology(*job.previous_index,job.result,true);
            job.complete=true;
        }catch(...){ /* The render thread can retry serially. */ }
    }
    void join(){
        if(!work)return;
        // If the pool is busy and the callback is still queued, the renderer
        // can now do the work itself. A callback already running must finish
        // before its captured arrays can move to the next native frame.
        WaitForThreadpoolWorkCallbacks(work,TRUE);CloseThreadpoolWork(work);work=nullptr;
    }
public:
    FieldTopologyJob(const Frame& frame,const std::vector<Primitive>& tris,bool start,const Topology* before):
        ops(frame.ops.data()),count(frame.ops.size()),triangles(tris),previous_index(before){
        if(start&&triangles.size()>=1500){
            work=CreateThreadpoolWork(run,this,nullptr);
            if(work)SubmitThreadpoolWork(work);
        }
    }
    FieldTopologyJob(const FieldTopologyJob&)=delete;
    FieldTopologyJob& operator=(const FieldTopologyJob&)=delete;
    ~FieldTopologyJob(){join();}
    bool matched()const{return previous_index&&complete;}
    TopologyVotes votes()const{return counts;}
    Topology* finish(){join();return complete?&result:nullptr;}
};
static void anchor_vertex_topology(const std::vector<Primitive>&old,const std::vector<Primitive>&cur,bool reuse_field=false,FieldTopologyJob* job=nullptr){
    topology_anchors=topology_corrected=topology_partial=0;
    Topology previous_topology;
    if(reuse_field){
        if(!previous.match_input->field_topology)
            previous.match_input->field_topology=std::make_shared<Topology>(topology_points(previous,old));
    }else previous_topology=topology_points(previous,old);
    const auto& before_topology=reuse_field?*previous.match_input->field_topology:previous_topology;
    auto* prepared=job?job->finish():nullptr;
    auto after_topology=prepared?std::move(*prepared):topology_points(current,cur);
    TopologyVotes votes;
    if(prepared&&job->matched())votes=job->votes();
    else votes=resolve_topology(before_topology,after_topology,false);
    topology_anchors=votes.anchors;topology_partial=votes.partial;
    const auto& before=before_topology.points;const auto& after=after_topology.points;
    for(auto ref:after_topology.refs){int index=after[ref.node].prior;if(index<0)continue;const auto&source=before[index];
        auto&op=current.ops[ref.op];auto&prior=op.prior_positions[ref.vertex];
        topology_corrected+=!op.motion_valid[ref.vertex]||fabs(prior[0]-source.position[0])>.001||fabs(prior[1]-source.position[1])>.001||fabs(prior[3]-source.position[3])>1e-9;
        prior=source.position;op.motion_valid[ref.vertex]=1;
    }
    if(reuse_field)current.match_input->field_topology=std::make_shared<Topology>(std::move(after_topology));
}
