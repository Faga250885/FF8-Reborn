#define GEO_TEST
#include "../src/geometry.cpp"
#include <cstdio>
using namespace geo;
static unsigned accepted_cases=0;
static void expect(const char* name,bool result){
    if(!result){std::fprintf(stderr,"failed %s\n",name);std::exit(1);}
    ++accepted_cases;
}
int main(){
    display_settings.fps=60;
    constexpr GLbitfield C=GL_COLOR_BUFFER_BIT,D=GL_DEPTH_BUFFER_BIT,S=GL_STENCIL_BUFFER_BIT;
    Frame f;Target t;t.texture=37;t.depth=t.stencil=true;t.w=640;t.h=480;
    f.depth_texture_for_target[37]=91;
    Op clear;clear.target=37;clear.clear=C|D|S;
    std::fill_n(clear.state.mask,4,GL_TRUE);clear.state.depth_mask=GL_TRUE;
    clear.raster.mask_known=true;clear.raster.face[0][6]=clear.raster.face[1][6]=255;
    Op other;other.target=5;
    auto run=[&](const std::vector<Op>& ops,GLbitfield result,const char* label){
        f.ops=ops;f.seed_clears.clear();
        expect(label,ReplaySeedPlan(1).required(f,37,t)==result);
        expect(label,ReplaySeedPlan(.5f).required(f,37,t)==result);
    };
    run({other,clear},0,"unrelated draw before full clear");
    Op read=other;read.state.texture[3].id=37;
    run({read,clear},C|D|S,"read live color before clear");
    read.state.texture[3].id=91;run({read,clear},C|D|S,"read shared depth texture");
    read.state.texture[3].copy=std::make_shared<TextureCopy>();run({read,clear},0,"frozen copy");
    Op written=other;written.target=37;run({written,clear},C|D|S,"write before full clear");
    Op small=clear;small.state.scissor_on=true;small.state.scissor[2]=639;small.state.scissor[3]=480;
    run({other,small},C|D|S,"partial scissor");
    small=clear;small.state.mask[2]=0;small.raster.face[1][6]=15;run({other,small},C|S,"partial color and back stencil masks");
    Op color=clear,depth=clear;color.clear=C;depth.clear=D|S;
    run({other,color,other,depth},0,"successive clears");
    auto& outgoing=outgoing_draws.emplace_back();outgoing.after=1;outgoing.op=other;outgoing.op.state.texture[0].id=91;
    expect("departure between clears",ReplaySeedPlan(.4f).required(f,37,t)==(D|S));
    expect("endpoint excludes departures",ReplaySeedPlan(1).required(f,37,t)==0);
    outgoing.after=0;expect("early departing reader",ReplaySeedPlan(.4f).required(f,37,t)==(C|D|S));
    outgoing.op.state.texture[0].copy=std::make_shared<TextureCopy>();expect("frozen departing reader",ReplaySeedPlan(.4f).required(f,37,t)==0);
    Frame preceding;
    auto& actor=preceding.ops.emplace_back();
    actor.target=5;actor.mode=GL_TRIANGLES;actor.state.program=7;actor.state.texture[0].id=91;
    f.ops.clear();
    expect("capture starts with clear",discarded_seed_planes(f,preceding,clear,t)==(C|D|S));
    f.ops.push_back(other);
    expect("possible departing reader",discarded_seed_planes(f,preceding,clear,t)==0);
    actor.target=6;
    expect("departing reader has no earlier anchor",discarded_seed_planes(f,preceding,clear,t)==(C|D|S));
    expect("capture preserves masked planes",discarded_seed_planes(f,preceding,small,t)==D);
    f.ops.back().state.texture[0].id=37;
    expect("capture preserves live sampler",discarded_seed_planes(f,preceding,clear,t)==0);
    f.ops.back().state.texture[0].copy=std::make_shared<TextureCopy>();
    expect("capture isolates frozen sampler",discarded_seed_planes(f,preceding,clear,t)==(C|D|S));
    reset_building();building.seed_clears[5]={1,2,3};building.captured_seed_planes[5]=C;
    reset_building();expect("clear on next capture",building.seed_clears.empty()&&building.captured_seed_planes.empty());
    std::printf("%u plane and dependency checks passed\n",accepted_cases);
}
