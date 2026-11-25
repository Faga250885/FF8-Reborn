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

    // A camera dolly shifts one rigid edge with two different camera depths.
    // Screen-space interpolation alone incorrectly changes its 3D length.
    const auto project=[](float x,float y,float z){return vertex(320+1024*x/z,240+1024*y/z,z);};
    const auto prior_u=project(-60,20,240),prior_v=project(80,40,540);
    const auto current_u=project(50,55,120),current_v=project(190,75,420);
    const auto sample=[](const Vertex& from,const Vertex& to,float phase){
        std::array<float,4> point;
        interpolate_battle_point(from.p,to.p,phase,point.data());
        return view_point(point);
    };
    for(float phase:{0.f,.125f,.25f,.5f,.75f,.875f,1.f}){
        const auto u=sample(prior_u,current_u,phase),v=sample(prior_v,current_v,phase);
        const Point3 expected={-60+110*phase,20+35*phase,240-120*phase};
        for(unsigned axis=0;axis<3;++axis){
            demand(std::abs(u[axis]-expected[axis])<.0002,"camera-space trajectory");
            constexpr Point3 rigid_edge={140,20,300};
            demand(std::abs(v[axis]-u[axis]-rigid_edge[axis])<.0002,"dolly stretched a rigid edge");
        }
    }

    current.ops={mesh({a,b,c})};previous.ops={mesh({vertex(270,100),b,c})};
    for(unsigned v=0;v<3;++v)memcpy(current.ops[0].prior_positions[v].data(),previous.ops[0].vertices[v].p,sizeof(Vertex::p));
    faces={{0,0,0,0}};
    demand(protect_battle_edges(faces,faces).empty(),"authored prior face changed shape");

    // A cut from a low shot to an overhead view changes both visibility
    // and depth. A smooth orbit and independent actor motion still blend.
    std::vector<BoneSample> shot,orbit,cut;
    for(unsigned i=0;i<12;++i){
        const auto address=0x0a082000+6*i;
        const auto x=int16_t(400+100*i);
        shot.push_back({5,address,x,880,1000,battle_stage_sample});
        orbit.push_back({5,address,int16_t(x+130),920,1050,battle_stage_sample});
        cut.push_back({5,address,int16_t(x-1700),1160,2400,battle_stage_sample});
    }
    const auto actor=0x0a088000u;
    shot.push_back({0,actor,600,1000,900,0});
    orbit.push_back({0,actor,2200,240,2500,0});
    demand(!battle_shot_changed(shot,orbit),"continuous camera orbit treated as a cut");
    demand(battle_shot_changed(shot,cut),"camera cut missed when the old stage leaves view");
    for(auto& p:cut)p.x+=1700;
    demand(battle_shot_changed(shot,cut),"camera cut missed when screen position stayed centered");
    demand(battle_shot_changed({},shot),"stage projection appeared at the camera cut");
    demand(!battle_shot_changed({},{}),"empty capture marked as a cut");

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
    Frame battle;
    battle.w=1280;battle.h=720;
    auto floor=mesh({vertex(-90,420),vertex(730,456),vertex(730,420),
                     vertex(-90,420),vertex(-90,456),vertex(730,456)});
    floor.target=1;floor.match=0;floor.state.depth_mask=true;
    floor.state.projection[0]=2.f/640;floor.state.projection[5]=-2.f/480;
    auto composite=mesh({a,b,c,a,b,c});
    composite.state.viewport[0]=160;
    composite.state.viewport[2]=960;composite.state.viewport[3]=720;
    battle.ops={floor,composite};
    demand(!battle_loses_lower_edge(battle,{}),"continuous floor rejected");
    for(auto& p:battle.ops.front().prior_positions)p[1]-=28;
    demand(battle_loses_lower_edge(battle,{}),"interpolation exposed a strip beneath the floor");
    demand(!battle_loses_lower_edge(battle,{{floor,0}}),"departing floor already covers the strip");
    battle.ops.front().vertices[1].p[1]=432;
    battle.ops.front().vertices[4].p[1]=432;
    battle.ops.front().vertices[5].p[1]=432;
    demand(!battle_loses_lower_edge(battle,{}),"native floor framing was treated as an interpolation error");

    const auto patch=[&](float left,float right){
        auto strip=floor;
        strip.vertices={vertex(left,420),vertex(right,456),vertex(right,420),
                        vertex(left,420),vertex(left,456),vertex(right,456)};
        for(unsigned v=0;v<strip.vertices.size();++v)
            memcpy(strip.prior_positions[v].data(),strip.vertices[v].p,sizeof(Vertex::p));
        return strip;
    };
    battle.ops={patch(-120,210),patch(210,240),patch(240,760),composite};
    for(auto& p:battle.ops[1].prior_positions)
        if(p[1]==456)p[1]=455;
    demand(battle_loses_lower_edge(battle,{}),"narrow wedge missed at the edge of the battle viewport");
    for(auto& v:battle.ops[1].vertices)
        if(v.p[1]==456)v.p[1]=455;
    demand(!battle_loses_lower_edge(battle,{}),"pre-existing narrow gap changed battle cadence");

    puts("battle edge continuity passed");
}
