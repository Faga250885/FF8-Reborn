// Projected seam continuity with flat adjacency and one trajectory per point.
static unsigned coherent_conflicts=0,coherent_filled=0,coherent_nodes=0;
// Node ids are already dense. Scatter adjacency into node order and sort
// just the short runs within each node, preserving lexicographic order.
template<class Value>
static void group_neighbours(std::vector<std::pair<unsigned,Value>>& entries,
                             size_t nodes,std::vector<unsigned>& offsets){
    offsets.assign(nodes+1,0);
    for(const auto&entry:entries)++offsets[entry.first+1];
    for(size_t i=1;i<offsets.size();i++)offsets[i]+=offsets[i-1];
    auto cursor=offsets;
    std::vector<Value> values(entries.size());
    for(const auto&entry:entries)values[cursor[entry.first]++]=entry.second;
    unsigned write=0;
    for(unsigned node=0;node<nodes;node++){
        auto first=values.begin()+offsets[node],last=values.begin()+offsets[node+1];
        std::sort(first,last);
        offsets[node]=write;
        for(auto value=first;value!=last;++value)
            if(value==first||*value!=value[-1])entries[write++]={node,*value};
    }
    offsets[nodes]=write;
    entries.resize(write);
}
static bool projected_geometry(const Op&op){return !op.clear&&op.mode==GL_TRIANGLES&&op.state.program&&zero_model(op.state);}
static void coherent_projected_motion(const std::vector<Primitive>* field_faces=nullptr,
                                      const std::vector<Primitive>* old_faces=nullptr){
    struct Node{
        std::array<float,4> point{},reference{},first_prior{},result{};
        std::array<double,3> delta{};
        bool vote=false,anchor=false,conflict=false,reachable=false,camera=false,hold=false;
    };
    struct Ref{unsigned op,vertex,node;};
    size_t vertices=0;for(const auto&op:current.ops)if(!op.indexed_complete&&projected_geometry(op))vertices+=op.vertices.size();
    std::vector<Node> nodes;nodes.reserve(vertices/2);
    std::unordered_map<uint64_t,unsigned> lookup;lookup.reserve(vertices/2);
    std::vector<Ref> refs;refs.reserve(vertices);
    std::vector<std::pair<unsigned,unsigned>> edges;edges.reserve(vertices*2);
    auto distance=[](const std::array<double,3>&a,const std::array<double,3>&b){return std::hypot(a[0]-b[0],a[1]-b[1])+250*fabs(a[2]-b[2]);};
    for(unsigned i=0;i<current.ops.size();i++){
        auto&op=current.ops[i];if(op.indexed_complete||!projected_geometry(op))continue;
        if((i&7)==0&&matching_checkpoint)matching_checkpoint();
        unsigned tri[3];
        uint64_t state_key=hash(1469598103934665603ULL,&op.target,sizeof(op.target));
        state_key=hash(state_key,op.state.projection,sizeof(op.state.projection));state_key=hash(state_key,op.state.viewport,sizeof(op.state.viewport));
        for(unsigned j=0;j<op.vertices.size();j++){
            const auto&p=op.vertices[j].p;float xyw[]={p[0]==0?0:p[0],p[1]==0?0:p[1],p[3]==0?0:p[3]};
            uint64_t key=hash(state_key,xyw,sizeof(xyw));
            auto found=lookup.find(key);unsigned n;
            if(found==lookup.end()){
                n=(unsigned)nodes.size();lookup.emplace(key,n);nodes.emplace_back();auto&node=nodes.back();
                memcpy(node.point.data(),p,sizeof(p));node.reference=node.point;
                node.camera=camera_predict(p,node.reference.data());
            }else n=found->second;
            auto&node=nodes[n];refs.push_back({i,j,n});tri[j%3]=n;
            if(op.motion_valid[j]&&p[3]>0&&op.prior_positions[j][3]>0){
                const auto&old=op.prior_positions[j];
                if(!node.vote||old[0]!=node.first_prior[0]||old[1]!=node.first_prior[1]||old[3]!=node.first_prior[3]){
                    std::array<double,3> vote={old[0]-node.reference[0],old[1]-node.reference[1],
                        old[3]==node.reference[3]?0:std::log(double(old[3])/node.reference[3])};
                    if(!node.vote){node.vote=true;node.first_prior=old;node.delta=vote;}
                    else node.conflict|=distance(vote,node.delta)>.01;
                }
            }
            if(j%3==2)for(int a=0;a<3;a++)for(int b=a+1;b<3;b++)if(tri[a]!=tri[b]){
                edges.emplace_back(tri[a],tri[b]);edges.emplace_back(tri[b],tri[a]);}
        }
    }
    std::vector<unsigned> offsets;
    group_neighbours(edges,nodes.size(),offsets);
    coherent_conflicts=coherent_filled=0;coherent_nodes=(unsigned)nodes.size();
    for(auto&node:nodes){node.anchor=node.vote&&!node.conflict;coherent_conflicts+=node.conflict;}
    std::vector<unsigned> queue;queue.reserve(nodes.size());
    for(unsigned i=0;i<nodes.size();i++)if(nodes[i].anchor){nodes[i].reachable=true;queue.push_back(i);}
    size_t at=0;auto spread=[&](){for(;at<queue.size();at++){unsigned index=queue[at];
        for(unsigned e=offsets[index];e<offsets[index+1];e++){unsigned next=edges[e].second;
            if(!nodes[next].reachable){nodes[next].reachable=true;nodes[next].delta=nodes[index].delta;queue.push_back(next);}}}};
    spread();
    for(unsigned i=0;i<nodes.size();i++)if(!nodes[i].reachable&&(nodes[i].vote||nodes[i].camera)){
        auto&node=nodes[i];node.reachable=true;node.anchor=true;
        if(!node.vote)node.delta={};queue.push_back(i);spread();
    }
    std::vector<unsigned> relaxed;relaxed.reserve(nodes.size());
    for(unsigned i=0;i<nodes.size();i++)if(nodes[i].reachable&&!nodes[i].anchor&&offsets[i+1]>offsets[i])relaxed.push_back(i);
    for(int iteration=0;iteration<48;iteration++){
        double change=0;
        for(unsigned i:relaxed){auto&node=nodes[i];unsigned degree=offsets[i+1]-offsets[i];
            std::array<double,3> mean{};
            for(unsigned e=offsets[i];e<offsets[i+1];e++)for(int k=0;k<3;k++)mean[k]+=nodes[edges[e].second].delta[k];
            for(int k=0;k<3;k++){mean[k]/=degree;change=std::max(change,fabs(mean[k]-node.delta[k]));node.delta[k]=mean[k];}
        }
        if(change<1e-5)break;
    }
    for(auto&node:nodes)if(node.reachable){node.result[0]=(float)(node.reference[0]+node.delta[0]);node.result[1]=(float)(node.reference[1]+node.delta[1]);
        node.result[3]=(float)(node.reference[3]*std::exp(node.delta[2]));}
    if(field_faces && old_faces){
        // A repeated texture can give neighbouring vertices histories from
        // different props. Check the reconstructed edges against the actual
        // matched native triangle before accepting large changes in length.
        size_t ref=0;
        queue.clear();
        for(const auto& face:*field_faces){
            if(ref>=refs.size())break;
            if(refs[ref].op!=face.op||refs[ref].vertex!=face.start)continue;
            const auto* corners=refs.data()+ref;
            ref+=3;
            for(int i=0;i<3;i++){
                const int j=(i+1)%3;
                auto& a=nodes[corners[i].node];auto& b=nodes[corners[j].node];
                if(!a.reachable||!b.reachable||a.point[3]<=0||a.point[3]>=.1f||
                   b.point[3]<=0||b.point[3]>=.1f)continue;
                const float x=a.point[0]-b.point[0],y=a.point[1]-b.point[1];
                const float u=a.result[0]-b.result[0],v=a.result[1]-b.result[1];
                const float native=x*x+y*y,prior=u*u+v*v;
                // Six native pixels of slack also cover tiny animated details.
                if(std::max(native,prior)<=4*std::min(native,prior)+36)continue;
                if(face.prior>=0){
                    const auto& old=(*old_faces)[face.prior];
                    const auto& verts=previous.ops[old.op].vertices;
                    auto same_point=[](const Node& n,const Vertex& p){
                        return std::abs(n.result[0]-p.p[0])<.01f&&std::abs(n.result[1]-p.p[1])<.01f&&
                               std::abs(n.result[3]-p.p[3])<=.0001f*p.p[3];
                    };
                    if(same_point(a,verts[old.start+i])&&same_point(b,verts[old.start+j]))continue;
                }
                if(!a.hold){a.hold=true;queue.push_back(corners[i].node);}
            }
        }
        // Hold the whole connected piece for this pair so a rejected vertex
        // cannot leave a crack along its neighbouring material or UV seam.
        for(size_t i=0;i<queue.size();i++){
            const auto id=queue[i];
            for(unsigned e=offsets[id];e<offsets[id+1];e++){
                const auto next=edges[e].second;
                if(!nodes[next].hold){nodes[next].hold=true;queue.push_back(next);}
            }
        }
    }
    for(auto ref:refs){auto&node=nodes[ref.node];if(!node.reachable)continue;
        auto&op=current.ops[ref.op];unsigned j=ref.vertex;coherent_filled+=!op.motion_valid[j];
        if(node.hold){op.motion_valid[j]=0;op.color_valid[j]=0;continue;}
        auto&p=op.prior_positions[j];p=node.result;p[2]=op.vertices[j].p[2];op.motion_valid[j]=1;
    }
}
