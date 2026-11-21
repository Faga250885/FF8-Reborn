// Recover the visible world actor from the actual draw stream when no indexed
// pose matches this swap. Identify each material by multiple unique UV faces,
// within the depth and screen bounds of the player's ground shadow.
namespace world_actor {
namespace mesh {
struct Face {unsigned op,at;uint64_t uv;};
using Parts=std::unordered_map<GLuint,std::vector<Face>>;
struct Anchor {float x=0,y=0,w=0;GLuint target=0;GLint program=0;};
static bool anchor(const Frame& f,Anchor& a){
    const Op* shadow=nullptr;
    for(const auto& o:f.ops)if(shadow_strip(o)){if(shadow)return false;shadow=&o;}
    if(!shadow)return false;
    for(const auto& v:shadow->vertices){a.x+=v.p[0];a.y+=v.p[1];a.w+=v.p[3];}
    float n=float(shadow->vertices.size());a.x/=n;a.y/=n;a.w/=n;
    a.target=shadow->target;a.program=shadow->state.program;return a.w>0;
}
static Parts parts(const Frame& f,const Anchor& a){
    Parts parts;const float radius=65536*a.w;
    for(unsigned oi=0;oi<f.ops.size();oi++){
        const auto& o=f.ops[oi];
        if(o.target!=a.target||o.state.program!=a.program||!opaque_projected(o)||!WideView::uses_texture(o.state))continue;
        for(unsigned i=0;i+2<o.vertices.size();i+=3){
            const auto* v=&o.vertices[i];float w=v->p[3];
            if(w<a.w*.8f||w>a.w*1.25f||v[1].p[3]!=w||v[2].p[3]!=w)continue;
            uint64_t id=1469598103934665603ULL;bool near_player=true;
            for(unsigned j=0;j<3;j++){
                near_player&=std::abs(v[j].p[0]-a.x)<radius&&v[j].p[1]>a.y-1.7f*radius&&v[j].p[1]<a.y+.35f*radius;
                id=hash(id,v[j].uv[0],2*sizeof(float));
            }
            if(near_player)parts[o.state.texture[0].id].push_back({oi,i,id});
        }
    }
    return parts;
}
struct Point {float x,y;};
struct Link {Point now,before;bool unique=true;};
static uint64_t key(float x,float y){float p[]={x==0?0:x,y==0?0:y};return hash(1469598103934665603ULL,p,sizeof(p));}
static uint64_t point_key(const Vertex& v){return key(v.p[0],v.p[1]);}
static void recover(){
    if(current.mode!=2||previous.mode!=2)return;
    Anchor a,b;if(!anchor(previous,a)||!anchor(current,b)||a.target!=b.target||a.program!=b.program||
        std::hypot(a.x-b.x,a.y-b.y)>42||std::abs(a.w-b.w)>a.w*.15f)return;
    auto old=parts(previous,a),now=parts(current,b);
    using Index=std::unordered_map<uint64_t,const Face*>;
    std::unordered_map<GLuint,Index> candidates;
    for(const auto& part:old)for(const auto& f:part.second){
        auto r=candidates[part.first].emplace(f.uv,&f);if(!r.second)r.first->second=nullptr;
    }
    std::unordered_map<uint64_t,Link> links;
    std::vector<Face> actor;
    for(const auto& part:now){
        Index faces;
        for(const auto& f:part.second){auto r=faces.emplace(f.uv,&f);if(!r.second)r.first->second=nullptr;}
        const Index* winner=nullptr;unsigned best=0,runner=0;
        for(const auto& c:candidates){
            unsigned n=0;for(const auto& f:faces){auto it=c.second.find(f.first);n+=f.second&&it!=c.second.end()&&it->second;}
            if(n>best){runner=best;best=n;winner=&c.second;}else runner=std::max(runner,n);
        }
        if(best<6||best<=runner*2||best*4<part.second.size())continue;
        for(const auto& f:faces){
            auto it=winner->find(f.first);if(!f.second||it==winner->end()||!it->second)continue;
            auto v=current.ops[f.second->op].vertices.data()+f.second->at;
            auto q=previous.ops[it->second->op].vertices.data()+it->second->at;
            for(int j=0;j<3;j++){
                if(std::hypot(v[j].p[0]-q[j].p[0],v[j].p[1]-q[j].p[1])>40)continue;
                Link link{{v[j].p[0],v[j].p[1]},{q[j].p[0],q[j].p[1]}};
                auto r=links.emplace(point_key(v[j]),link);
                if(!r.second&&(r.first->second.before.x!=link.before.x||r.first->second.before.y!=link.before.y))r.first->second.unique=false;
            }
        }
        actor.insert(actor.end(),part.second.begin(),part.second.end());
    }
    std::vector<Link> known;for(const auto& l:links)if(l.second.unique)known.push_back(l.second);
    if(known.size()<12||actor.size()<16)return;
    for(const auto& f:actor){
        auto& op=current.ops[f.op];if(op.actor_mid.empty())op.actor_mid.resize(op.vertices.size());
        for(unsigned j=f.at;j<f.at+3;j++){
            const auto& v=op.vertices[j];auto& p=op.prior_positions[j];
            memcpy(p.data(),v.p,sizeof(v.p));auto it=links.find(point_key(v));
            if(it!=links.end()&&it->second.unique){p[0]=it->second.before.x;p[1]=it->second.before.y;}
            else{
                const Link* closest=nullptr;float distance=144;
                for(const auto& l:known){float x=v.p[0]-l.now.x,y=v.p[1]-l.now.y,d=x*x+y*y;
                    if(d<distance){distance=d;closest=&l;}}
                p[0]+=closest?closest->before.x-closest->now.x:a.x-b.x;
                p[1]+=closest?closest->before.y-closest->now.y:a.y-b.y;
            }
            op.motion_valid[j]=1;op.actor_mid[j].player=true;
        }
        ++link_count;
    }
    float ground_dx=0;track_shadow(ground_dx);
    // Retain continuity with an immediately preceding indexed pair.
    current.stride_follow.update(previous.stride_follow.end,0,0,current.stride_dt,false);
}
}
}
