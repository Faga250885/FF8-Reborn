// One immutable source pair. Select the largest offscreen field prefix which
// can be restored before the first draw (including reads of depth attachments).
struct FieldPrefixPlan {
    GLuint target=0;
    unsigned last=0,draws=0;
    static bool pose_invariant(const Op& op){
        if(!op.card_before.empty()||!op.actor_mid.empty()||!op.depth_bias.empty())return false;
        if(op.match<0)return true;
        for(size_t i=0;i<op.motion_valid.size();++i)if(op.motion_valid[i]&&
            memcmp(op.prior_positions[i].data(),op.vertices[i].p,sizeof(Vertex::p)))return false;
        for(size_t i=0;i<op.color_valid.size();++i)if(op.color_valid[i]&&
            memcmp(op.prior_colors[i].data(),op.vertices[i].c,sizeof(Vertex::c)))return false;
        return true;
    }
    static FieldPrefixPlan select(const Frame& frame,const std::vector<OutgoingDraw>& outgoing){
        FieldPrefixPlan plan;
        if(frame.mode!=1||frame.camera_cut||!outgoing.empty())return plan;
        struct Candidate {bool stopped=false;unsigned draws=0,last=0;};
        std::unordered_map<GLuint,Candidate> candidates;
        std::unordered_map<GLuint,GLuint> attachments;
        for(auto&[key,t]:frame.targets)if(key&&t.active){
            attachments[t.texture]=key;
            auto dep=frame.depth_texture_for_target.find(key);
            if(dep!=frame.depth_texture_for_target.end())attachments[dep->second]=key;
            if(t.format!=GL_RGBA8&&t.format!=GL_RGB8)continue;
            if(t.w<=0||t.h<=0||uint64_t(t.w)*t.h*(t.depth?8:4)>64*1024*1024)continue;
            candidates.emplace(key,Candidate{});
        }
        for(unsigned i=0;i<frame.ops.size();++i){
            const auto& op=frame.ops[i];
            bool immutable=true;
            if(!op.clear)for(auto& tex:op.state.texture)if(tex.id){
                auto a=attachments.find(tex.id);
                if(a!=attachments.end()){
                    auto producer=candidates.find(a->second);
                    if(producer!=candidates.end())producer->second.stopped=true;
                    immutable=false;
                }else if(!tex.copy)immutable=false;
            }
            auto own=candidates.find(op.target);
            if(own==candidates.end()||own->second.stopped)continue;
            auto& c=own->second;
            if(op.clear)continue;
            if(!immutable||!pose_invariant(op)){c.stopped=true;continue;}
            ++c.draws;c.last=i;
        }
        for(auto&[key,c]:candidates){
            const auto& target=frame.targets.at(key);
            // Two full-surface copies only paid off for dense draws.
            const uint64_t pixels=uint64_t(target.w)*target.h;
            if(c.draws<32||uint64_t(c.draws)*1000000<80*pixels)continue;
            if(c.draws>plan.draws||(c.draws==plan.draws&&key<plan.target))plan={key,c.last,c.draws};
        }
        return plan;
    }
};

// Cross-replay reuse is confined to a single renderer-owned wait. The game
// cannot relink programs on this thread or submit new native drawing there.
struct FieldPrefixCache {
    std::weak_ptr<MatchInput> pair;
    Surface saved;
    FieldPrefixPlan plan;
    bool active=false, wide=false,ready=false;
    double window_end=0,capture_until=0;
    unsigned hits=0;
    struct Window {
        FieldPrefixCache& cache;
        Window(FieldPrefixCache& c,double deadline,bool allowed=true):cache(c){
            c.reset();c.active=allowed;c.window_end=deadline;c.capture_until=0;
        }
        ~Window(){cache.reset();cache.active=false;cache.window_end=cache.capture_until=0;}
    };
    void reset(){pair.reset();plan={};ready=false;}
    void next(double pair_end,double tick,double replay_cost){
        capture_until=std::min(window_end,pair_end)-tick-replay_cost-.0005;
    }
    void prepare(const Frame&frame,float alpha,bool desired_wide){
        if(!active)return;
        if(alpha>=1||frame.mode!=1||frame.camera_cut||!frame.match_input){reset();return;}
        if(pair.lock()==frame.match_input&&wide==desired_wide)return;
        reset();
        if(now()>capture_until)return;
        pair=frame.match_input;wide=desired_wide;
        plan=FieldPrefixPlan::select(frame,outgoing_draws);
    }
    static GLbitfield planes(const Target&t){
        return GL_COLOR_BUFFER_BIT|(t.depth?GL_DEPTH_BUFFER_BIT:0)|(t.depth&&t.stencil?GL_STENCIL_BUFFER_BIT:0);
    }
    void save(GLuint fbo,const Target& t){
        if(now()>capture_until)return;
        try{
            resize(saved,t.w,t.h,t.depth,false,t.format,t.stencil);
            if(now()>capture_until)return;
            blit(fbo,saved.fbo,t.w,t.h,t.w,t.h,planes(t));
            if(glGetError()==GL_NO_ERROR){ready=true;return;}
        }catch(const std::exception&){}
        reset();active=false;
    }
    bool restore(GLuint fbo,const Target& t){
        blit(saved.fbo,fbo,t.w,t.h,t.w,t.h,planes(t));
        if(glGetError()!=GL_NO_ERROR){reset();active=false;return false;}
        ++hits;return true;
    }
};
