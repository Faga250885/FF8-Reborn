#define GEO_TEST
#include "../src/geometry.cpp"
using namespace geo;

static Vertex vertex(float x,float y,float z=1000){
    Vertex v{};v.p[0]=x;v.p[1]=y;v.p[3]=1.f/z;return v;
}
static Op mesh(std::initializer_list<Vertex> corners){
    Op op{};op.mode=GL_TRIANGLES;op.state.program=1;
    op.vertices=corners;op.motion_valid.assign(corners.size(),1);op.color_valid=op.motion_valid;
    for(const auto& v:corners)op.prior_positions.push_back({v.p[0]+10,v.p[1],v.p[2],v.p[3]});
    return op;
}
int main(){
    // The head shares a mesh seam with the body across a material boundary.
    // A pooled vertex address incorrectly points from its head to an enemy.
    auto a=vertex(100,100),b=vertex(105,100),c=vertex(104,105),d=vertex(104,110);
    current.ops={mesh({a,b,c}),mesh({b,c,d}),mesh({vertex(270,100),vertex(275,100),vertex(274,105)})};
    current.ops[0].indexed_complete=true;
    current.ops[0].prior_positions[0][0]=270;
    std::vector<Primitive> faces{{0,0,0},{1,0,0},{2,0,0}};
    auto held=protect_battle_edges({},faces);
    demand(held.size()==4,"hold only the affected connected mesh");
    for(auto op:{0,1})for(auto valid:current.ops[op].motion_valid)demand(!valid,"material seam split");
    for(auto valid:current.ops[2].motion_valid)demand(valid,"enemy lost its motion");

    // Foreshortening alone can make a rigid edge shrink almost to a point.
    auto horizontal=vertex(360,240),center=vertex(320,240),toward=vertex(320,240,1125);
    demand(!battle_edge_stretches(center.p,horizontal.p,center.p,toward.p),"legitimate rotation");
    auto near_a=vertex(320,240,200),near_b=vertex(520,240,200);
    demand(!battle_edge_stretches(center.p,horizontal.p,near_a.p,near_b.p),"legitimate zoom");

    current.ops={mesh({a,b,c})};previous.ops={mesh({vertex(270,100),b,c})};
    for(unsigned v=0;v<3;++v)memcpy(current.ops[0].prior_positions[v].data(),previous.ops[0].vertices[v].p,sizeof(Vertex::p));
    faces={{0,0,0,0}};
    demand(protect_battle_edges(faces,faces).empty(),"authored prior face changed shape");

    // One projected point shared by two source addresses is ambiguous if the
    // next frame separates them. Both directions must reject that trajectory.
    std::vector<BoneSample> before{{1,1,400,400,1000,0},{2,2,400,400,1000,0},{3,3,600,400,1000,0}};
    auto after=before;after[0].x=420;after[1].x=1200;after[2].x=620;
    const auto tracks=link_bones(before,after);
    auto projected=[](const BoneSample& sample){return vertex(.25f*sample.x,24+.25f*sample.y,sample.z);};
    demand(!lookup_bone(tracks.forward,projected(before[0]).p)&&
           !lookup_bone(tracks.backward,projected(after[0]).p)&&!lookup_bone(tracks.backward,projected(after[1]).p),"split track accepted");
    demand(lookup_bone(tracks.backward,projected(after[2]).p),"unique track lost");
    const auto reverse=link_bones(after,before);
    demand(!lookup_bone(reverse.forward,projected(after[0]).p)&&!lookup_bone(reverse.backward,projected(before[0]).p),"merged track accepted");

    previous.ops={mesh({vertex(100,100),vertex(100,105),vertex(105,100)})};
    current.ops={mesh({vertex(270,100),vertex(275,100),vertex(271,105)})};
    const auto state=surface_state_key(previous.ops.front());
    ForwardSurface destinations;
    for(const auto& v:previous.ops.front().vertices)
        destinations[surface_point_key(state,v.p)]={v.p[0],v.p[1],v.p[2],v.p[3]};
    auto& tip=destinations.at(surface_point_key(state,previous.ops.front().vertices.front().p));
    tip[0]=270;
    retain_battle_outgoing({{0,0,0}},{},destinations,{});
    demand(outgoing_draws.empty(),"back-facing enemy spike retained");
    tip[0]=110;
    retain_battle_outgoing({{0,0,0}},{},destinations,{});
    demand(outgoing_triangles==1,"legitimate turning face discarded");
    puts("battle edge continuity passed");
}
