// The engine submits only currently front-facing opaque battle triangles.
// Keep a departing face only when all three of its current vertices are known
// from adjacent visible faces, and it is demonstrably turning away.
static bool battle_visibility_enabled=true;
static double screen_area(const Vertex*a){return double(a[1].p[0]-a[0].p[0])*(a[2].p[1]-a[0].p[1])-double(a[2].p[0]-a[0].p[0])*(a[1].p[1]-a[0].p[1]);}
static bool opaque_projected(const Op&op){return projected_geometry(op)&&(!op.state.blend||(op.state.src_rgb==GL_ONE&&op.state.dst_rgb==GL_ZERO));}
#include "battle_edges.h"
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
static void retain_battle_outgoing(const std::vector<Primitive>&old,const std::vector<Primitive>&cur,const ForwardSurface&extra,const HeldBattlePoints&held){
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
        for(unsigned v=0;v<3;++v){
            const auto w=(v+1)%3;
            if((!held.empty()&&held.count(surface_point_key(before_state,predicted[v].p)))||
               battle_edge_stretches(before.vertices[tri.start+v].p,before.vertices[tri.start+w].p,predicted[v].p,predicted[w].p)){
                valid=false;break;
            }
        }
        if(!valid)continue;
        auto& draw=departing_batch(before,tri.op,tail->second);draw.front_face.push_back(1);
        for(unsigned v=0;v<3;v++){draw.vertices.push_back(predicted[v]);std::array<float,4> prior;memcpy(prior.data(),before.vertices[tri.start+v].p,sizeof(predicted[v].p));draw.prior_positions.push_back(prior);draw.motion_valid.push_back(1);}
        outgoing_triangles++;
    }
}

// A fast tilt can expose small wedges along the bottom of the reconstructed
// floor. Compare it with the current game frame and fall back to that frame
// for the pair if the game fills an area the interpolation would leave open.
static bool battle_loses_lower_edge(const Frame& frame,const std::vector<OutgoingDraw>& departing){
    if(frame.ops.empty())return false;
    const auto& end=frame.ops.back();
    const float width=std::round(end.state.viewport[2]*480.f/432);
    if(width<=0||width>=frame.w||!centered_menu_composite(end,frame.w,frame.h))return false;
    constexpr unsigned columns=64;
    constexpr uint64_t all=~uint64_t{0};
    constexpr std::array<float,8> phases={1.f,.0625f,.125f,.3125f,.5f,.6875f,.875f,.9375f};
    constexpr std::array<float,3> rows={449.f,454.f,455.5f};
    std::array<std::array<uint64_t,rows.size()>,phases.size()> coverage{};
    const float step=640.f*frame.w/(width*columns);
    const float left=320.f-step*(columns-1)/2;

    auto collect=[&](const Op& op,bool leaving){
        if(!op.target||!opaque_projected(op)||!op.state.depth_mask||
           std::abs(op.state.projection[0]-2.f/640)>.00001f||
           std::abs(op.state.projection[5]+2.f/480)>.00001f)return;
        for(size_t i=0;i+2<op.vertices.size();i+=3){
            float low=INFINITY,high=-INFINITY;
            for(size_t corner=0;corner<3;++corner){
                const size_t v=i+corner;
                const auto* p=op.vertices[v].p;
                if(!(p[3]>0&&p[3]<.1f)){low=rows.back()+1;break;}
                low=std::min(low,p[1]);
                high=std::max(high,p[1]);
                if(op.match>=0&&op.motion_valid[v]){
                    low=std::min(low,op.prior_positions[v][1]);
                    high=std::max(high,op.prior_positions[v][1]);
                }
            }
            if(low>rows.back()||high<rows.front())continue;
            for(size_t phase=leaving?1:0;phase<phases.size();++phase){
                auto& mask=coverage[phase];
                if(std::all_of(mask.begin(),mask.end(),[](uint64_t row){return row==all;}))continue;
                float x[3],y[3];
                for(size_t corner=0;corner<3;++corner){
                    const size_t v=i+corner;
                    const auto* p=op.vertices[v].p;
                    float point[4];
                    if(phase&&op.match>=0&&op.motion_valid[v]){
                        interpolate_battle_point(op.prior_positions[v].data(),p,phases[phase],point);
                        p=point;
                    }
                    x[corner]=p[0];y[corner]=p[1];
                }
                const float area=(x[1]-x[0])*(y[2]-y[0])-(y[1]-y[0])*(x[2]-x[0]);
                if(!std::isfinite(area)||std::abs(area)<.001f||
                   (phase&&i/3<op.front_face.size()&&op.front_face[i/3]&&area>0))continue;
                for(size_t row=0;row<rows.size();++row){
                    if(mask[row]==all||rows[row]<low||rows[row]>high)continue;
                    float first=INFINITY,last=-INFINITY;
                    for(size_t a=0;a<3;++a){
                        const size_t b=(a+1)%3;
                        if(rows[row]<std::min(y[a],y[b])||rows[row]>std::max(y[a],y[b]))continue;
                        if(y[a]==y[b]){
                            first=std::min({first,x[a],x[b]});last=std::max({last,x[a],x[b]});
                        }else{
                            const float crossing=x[a]+(x[b]-x[a])*((rows[row]-y[a])/(y[b]-y[a]));
                            first=std::min(first,crossing);last=std::max(last,crossing);
                        }
                    }
                    const float begin=std::max(0.f,std::ceil((first-left)/step));
                    const float finish=std::min(float(columns-1),std::floor((last-left)/step));
                    if(begin<=finish)mask[row]|=(all<<unsigned(begin))&(all>>(columns-1-unsigned(finish)));
                }
            }
        }
    };
    for(const auto& op:frame.ops)collect(op,false);
    for(const auto& draw:departing)collect(draw.op,true);
    for(size_t row=0;row<rows.size();++row){
        const uint64_t endpoint=coverage[0][row];
        if(std::bitset<columns>(endpoint).count()<columns-3)continue;
        for(size_t i=1;i<phases.size();++i){
            const uint64_t lost=endpoint&~coverage[i][row];
            if(lost&(lost>>1))return true;
        }
    }
    return false;
}
