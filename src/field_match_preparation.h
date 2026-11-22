// Prepare a crowded field while the render thread submits its captured
// endpoint for validation. Neither worker calls OpenGL or presents frames.
class FieldMatchPreparation {
    const Frame& frame;
    const Topology* prior;
    PTP_WORK work=nullptr;
    std::shared_ptr<MatchInput> input;
    std::unique_ptr<FieldTopologyJob> topology;
    static void CALLBACK prepare(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK){
        auto& job=*static_cast<FieldMatchPreparation*>(context);
        try{
            auto data=std::make_shared<MatchInput>(job.frame,nullptr);
            auto points=std::make_unique<FieldTopologyJob>(job.frame,data->triangles,true,job.prior);
            data->index_materials(nullptr);
            job.input=std::move(data);
            job.topology=std::move(points);
        }catch(...){ /* The render thread can retry with the regular matcher. */ }
    }
    void join(){
        if(!work)return;
        WaitForThreadpoolWorkCallbacks(work,TRUE);CloseThreadpoolWork(work);work=nullptr;
    }
public:
    FieldMatchPreparation(const Frame& f,bool start,const Topology* before):frame(f),prior(before){
        if(start&&before&&f.vertex_count>=4500){
            work=CreateThreadpoolWork(prepare,this,nullptr);
            if(work)SubmitThreadpoolWork(work);
        }
    }
    FieldMatchPreparation(const FieldMatchPreparation&)=delete;
    ~FieldMatchPreparation(){join();}
    std::shared_ptr<MatchInput> take(){join();return input;}
    FieldTopologyJob* points() const {return topology.get();}
};
