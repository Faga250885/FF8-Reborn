// Count successful swaps separately from game updates. Starting a new
// scene or returning from pause begins a fresh interval.
struct DeckCadence {
    struct Scene {
        unsigned intervals=0,long_intervals=0,intermediate=0,source_intervals=0;
        double source_elapsed=0;
        double elapsed=0;
    };
    Scene scenes[3]{};
    int last_mode=0;
    double last_time=0;
    int last_source_mode=0;
    double last_source_time=0;
    enum class EventKind { gap, swap_interval, display_query, rate_change };
    struct Event {
        EventKind kind;
        double time, duration, driver;
        unsigned source;
        int mode, value;
        float alpha;
        bool applied=true;
        double replay_budget=0;
        bool source_busy=false;
    };
    Event events[128]{};
    unsigned event_count=0, omitted=0;
    double origin=0;
    double rate_start=0;
    unsigned rate_intervals=0;
    int rate_band=0;
    void record(Event event){
        if(!origin)origin=event.time;
        event.time-=origin;
        if(event_count<sizeof(events)/sizeof(events[0]))events[event_count++]=event;
        else ++omitted;
    }
    void source(int mode,double time){
        const double interval=time-last_source_time;
        if(mode>=1&&mode<=3&&mode==last_source_mode&&interval>0&&interval<.25){
            auto& scene=scenes[mode-1];
            ++scene.source_intervals;
            scene.source_elapsed+=interval;
        }
        last_source_mode=mode;
        last_source_time=time;
    }
    void present(int mode,double time,float alpha,unsigned source=0,double driver=0,
                 double replay_budget=0,bool source_busy=false){
        if(!origin)origin=time;
        const double interval=time-last_time;
        const bool same_scene=mode>=1&&mode<=3&&mode==last_mode;
        if(same_scene&&interval>=.080)record({EventKind::gap,time,interval,driver,source,mode,0,alpha,
            true,replay_budget,source_busy});
        if(same_scene&&interval>0&&interval<.25){
            auto& scene=scenes[mode-1];
            ++scene.intervals;
            scene.elapsed+=interval;
            scene.long_intervals+=interval>.025;
            scene.intermediate+=alpha>0&&alpha<1;
        }
        // Use the counter's half-second time scale. Hysteresis keeps a
        // fluctuating rate near a threshold from flooding the event buffer.
        if(!same_scene||interval<=0||interval>5.){
            rate_start=time;rate_intervals=0;rate_band=0;
        }else{
            ++rate_intervals;
            const double elapsed=time-rate_start;
            if(elapsed>=.5){
                const double fps=rate_intervals/elapsed;
                int band=rate_band;
                if(fps<=15)band=-1;
                else if(fps>=55)band=1;
                else if((band<0&&fps>=20)||(band>0&&fps<=45))band=0;
                if(band!=rate_band){
                    record({EventKind::rate_change,time,elapsed,0,source,mode,int(fps+.5),alpha});
                    rate_band=band;
                }
                rate_start=time;rate_intervals=0;
            }
        }
        last_mode=mode;
        last_time=time;
    }
    template<class Write=void (*)(const char*)> void report(Write write=log){
        const char* names[]={"field","world","battle"};
        char line[256];
        for(unsigned i=0;i<event_count;++i){
            const auto& e=events[i];
            const char* scene=e.mode>=1&&e.mode<=3?names[e.mode-1]:"transition";
            if(e.kind==EventKind::gap){
                const char* image=e.alpha<0?"native":(e.alpha>0&&e.alpha<1?"intermediate":"endpoint");
                snprintf(line,sizeof(line),"Deck gap +%.3f s | %s | tick %u | %.1f ms between swaps | %.1f ms inside swap | %s | replay budget %.1f ms | source busy %d\n",
                    e.time,scene,e.source,e.duration*1000,e.driver*1000,image,e.replay_budget*1000,e.source_busy);
            }else if(e.kind==EventKind::swap_interval){
                snprintf(line,sizeof(line),"Deck sync +%.3f s | %s | tick %u | requested interval %d | %s | %.1f ms\n",
                    e.time,scene,e.source,e.value,e.applied?"applied":"failed",e.duration*1000);
            }else if(e.kind==EventKind::display_query){
                snprintf(line,sizeof(line),"Deck display +%.3f s | %s | tick %u | reported %d Hz | query %.1f ms\n",
                    e.time,scene,e.source,e.value,e.duration*1000);
            }else{
                snprintf(line,sizeof(line),"Deck rate +%.3f s | %s | tick %u | %d swaps/s over %.3f s\n",
                    e.time,scene,e.source,e.value,e.duration);
            }
            write(line);
        }
        if(omitted){snprintf(line,sizeof(line),"Deck event buffer | %u additional events omitted\n",omitted);write(line);}
        event_count=omitted=0;
        for(int i=0;i<3;i++){
            auto& scene=scenes[i];
            if(!scene.intervals){scene={};continue;}
            char line[180];
            snprintf(line,sizeof(line),"Deck 60 %s | %.2f swaps/s | %u of %u intervals over 25 ms\n",
                     names[i],scene.intervals/scene.elapsed,scene.long_intervals,scene.intervals);
            write(line);
            snprintf(line,sizeof(line),"Deck 60 %s timing | %.2f game updates/s | %u intermediate swaps of %u\n",
                     names[i],scene.source_elapsed?scene.source_intervals/scene.source_elapsed:0.,
                     scene.intermediate,scene.intervals);
            write(line);scene={};
        }
    }
};
static DeckCadence deck_cadence;
