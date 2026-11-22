// The test chooses when a real Windows thread-pool callback can run. This
// exercises both outcomes of the renderer's cancel-or-join without sleeps.
#include <windows.h>
static thread_local PTP_CALLBACK_ENVIRON test_pool=nullptr;
static thread_local PTP_WORK last_created=nullptr;
static PTP_WORK WINAPI observe_work(PTP_WORK_CALLBACK callback,void* context,PTP_CALLBACK_ENVIRON env){
    return last_created=CreateThreadpoolWork(callback,context,test_pool?test_pool:env);
}
#define CreateThreadpoolWork observe_work
#define GEO_TEST
#include "../src/geometry.cpp"
#undef CreateThreadpoolWork
using namespace geo;

static void expect(bool condition,const char* message){
    if(!condition){std::fprintf(stderr,"failed: %s\n",message);std::exit(1);}
}

static Frame scene(int mode,unsigned count,bool reversed){
    Frame frame;
    frame.mode=mode;frame.started=true;frame.vertex_count=3*count;
    Op op{};
    op.mode=GL_TRIANGLES;op.state.program=1;
    Uniform sampler{};sampler.type=GL_SAMPLER_2D;op.state.uniforms.push_back(sampler);
    for(unsigned i=0;i<count;i++){
        unsigned id=reversed?count-1-i:i;
        for(unsigned corner=0;corner<3;corner++){
            Vertex v{};
            v.p[0]=16.f*(1+id%50)+4*(corner==1)+(reversed?.5f:0);
            v.p[1]=16.f*(1+id/50)+4*(corner==2);
            v.p[3]=1.f/float(300+(id%13)*17);
            v.uv[0][0]=float(id);v.uv[0][1]=float(corner);
            v.c[0]=float(id%4)/3;v.c[3]=1;
            op.vertices.push_back(v);
        }
    }
    frame.ops.push_back(std::move(op));
    return frame;
}

struct Blocker {
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    static void CALLBACK wait(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK){
        auto& gate=*static_cast<Blocker*>(context);
        SetEvent(gate.entered);WaitForSingleObject(gate.release,INFINITE);
    }
};

static void check_mode(int mode){
    const unsigned faces=mode==3?1501:1500;
    previous=scene(mode,faces,false);
    current=scene(mode,faces,true);
    match_frames(mode);
    const Op reference=current.ops.front();
    const CameraMotion reference_camera=camera_motion;
    expect(matched_candidates==faces,"all reordered triangles matched");
    expect(reference_camera.valid==(mode==2),"only world estimates a camera");
    for(unsigned i=0;i<faces;i++){
        expect(reference.motion_valid[3*i],"matched corner is usable");
        expect(reference.prior_positions[3*i][0]==previous.ops[0].vertices[3*(faces-1-i)].p[0],
               "reordered triangle uses its own previous corner");
    }

    auto compare=[&]{
        const Op& result=current.ops.front();
        expect(result.motion_valid==reference.motion_valid&&result.color_valid==reference.color_valid&&
               result.prior_positions==reference.prior_positions&&result.prior_colors==reference.prior_colors,
               "all interpolated positions, colors and validity flags agree with serial matching");
        expect(reference_camera.valid==camera_motion.valid&&reference_camera.confidence==camera_motion.confidence&&
               !std::memcmp(reference_camera.matrix,camera_motion.matrix,sizeof(camera_motion.matrix)),
               "camera agrees with serial matching");
    };
    current=scene(mode,faces,true);
    last_created=nullptr;
    {
        MatchPreparation ready(previous,current,true);
        expect(last_created!=nullptr,"4500 or more vertices submit work");
        WaitForThreadpoolWorkCallbacks(last_created,FALSE);
        PairCorrespondence pair;
        expect(ready.take(pair)!=nullptr,"completed worker supplies its match input");
    }
    {
        MatchPreparation ready(previous,current,true);
        WaitForThreadpoolWorkCallbacks(last_created,FALSE);
        match_frames(mode,&ready);compare();
    }
    current=scene(mode,1499,true);
    last_created=nullptr;
    {MatchPreparation small(previous,current,true);expect(!last_created,"4497 vertices use serial preparation");}

    // Keep a one-thread pool occupied. The submitted preparation stays
    // genuinely pending until take() cancels it and the renderer falls back.
    Blocker blocker;
    TP_CALLBACK_ENVIRON env;InitializeThreadpoolEnvironment(&env);
    auto pool=CreateThreadpool(nullptr);expect(pool!=nullptr,"create isolated pool");
    SetThreadpoolThreadMaximum(pool,1);SetThreadpoolCallbackPool(&env,pool);
    auto parked=CreateThreadpoolWork(Blocker::wait,&blocker,&env);expect(parked!=nullptr,"create blocker");
    SubmitThreadpoolWork(parked);
    expect(WaitForSingleObject(blocker.entered,5000)==WAIT_OBJECT_0,"worker entered blocker");
    test_pool=&env;
    current=scene(mode,faces,true);
    {
        MatchPreparation pending(previous,current,true);
        PairCorrespondence pair;
        expect(pending.take(pair)==nullptr,"cancel queued preparation");
    }
    {
        MatchPreparation pending(previous,current,true);
        match_frames(mode,&pending);compare();
    }
    test_pool=nullptr;
    SetEvent(blocker.release);WaitForThreadpoolWorkCallbacks(parked,FALSE);CloseThreadpoolWork(parked);
    CloseThreadpool(pool);DestroyThreadpoolEnvironment(&env);CloseHandle(blocker.release);CloseHandle(blocker.entered);
}

int main(){
    static_assert(deck_renderer,"compile the Steam Deck target");
    for(int mode:{1,2,3})check_mode(mode);
    std::puts("Field, world and battle match serial output with ready and cancelled work.");
}
