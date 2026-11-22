// Match the new geometry while the render thread validates the same endpoint.
// The worker only reads captured arrays and never calls into the GL driver.
class MatchPreparation {
    const Frame& previous_frame;
    const Frame& frame;
    PTP_WORK work=nullptr;
    std::shared_ptr<MatchInput> input;
    std::unique_ptr<FieldTopologyJob> topology;
    PairCorrespondence correspondence;
    static void CALLBACK prepare(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK){
        auto& job=*static_cast<MatchPreparation*>(context);
        try{
            const auto& prior=*job.previous_frame.match_input;
            auto data=std::make_shared<MatchInput>(job.frame,nullptr);
            std::unique_ptr<FieldTopologyJob> points;
            if(job.frame.mode==1)
                points=std::make_unique<FieldTopologyJob>(job.frame,data->triangles,true,prior.field_topology.get());
            data->index_materials(nullptr);
            job.correspondence.resolve(job.frame.mode,job.previous_frame,job.frame,prior,*data,nullptr);
            job.input=std::move(data);
            job.topology=std::move(points);
        }catch(...){ /* The render thread can retry with the regular matcher. */ }
    }
    void join(){
        if(!work)return;
        WaitForThreadpoolWorkCallbacks(work,TRUE);CloseThreadpoolWork(work);work=nullptr;
    }
public:
    MatchPreparation(const Frame& before,const Frame& f,bool start):previous_frame(before),frame(f){
        if(start&&before.match_input&&before.started&&before.mode==f.mode&&f.vertex_count>=4500){
            work=CreateThreadpoolWork(prepare,this,nullptr);
            if(work)SubmitThreadpoolWork(work);
        }
    }
    MatchPreparation(const MatchPreparation&)=delete;
    ~MatchPreparation(){join();}
    std::shared_ptr<MatchInput> take(PairCorrespondence& result){
        join();
        if(input)result=std::move(correspondence);
        return input;
    }
    FieldTopologyJob* points() const {return topology.get();}
};
