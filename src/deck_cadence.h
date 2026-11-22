// Count successful swaps separately from game updates. Starting a new
// scene or returning from pause begins a fresh interval.
struct DeckCadence {
    struct Scene {
        unsigned intervals=0,long_intervals=0;
        double elapsed=0;
    };
    Scene scenes[3]{};
    int last_mode=0;
    double last_time=0;
    void present(int mode,double time){
        const double interval=time-last_time;
        if(mode>=1&&mode<=3&&mode==last_mode&&interval>0&&interval<.25){
            auto& scene=scenes[mode-1];
            ++scene.intervals;
            scene.elapsed+=interval;
            scene.long_intervals+=interval>.025;
        }
        last_mode=mode;
        last_time=time;
    }
    void report(){
        const char* names[]={"field","world","battle"};
        for(int i=0;i<3;i++){
            auto& scene=scenes[i];
            if(!scene.intervals)continue;
            char line[180];
            snprintf(line,sizeof(line),"Deck 60 %s | %.2f swaps/s | %u of %u intervals over 25 ms\n",
                     names[i],scene.intervals/scene.elapsed,scene.long_intervals,scene.intervals);
            log(line);scene={};
        }
    }
};
static DeckCadence deck_cadence;
