// Smooth the presentation timeline, not the game's simulation clock.
// One prepared source pair remains available while its successor is matched.
struct PresentationClock {
    double origin=0,period=1./30.,nominal=1./30.;
    double draw_started=0, slot=0, scheduled=0;
    double replay_cost=.0025;
    double last_submission=0, quick_draw=0, refresh_period=0;
    double recovery=0;

    void submitted(double time){
        const double elapsed=std::max(0.,time-draw_started);
        // An inexpensive following draw can reach SwapBuffers earlier than
        // the last one. A lower estimate is safer for spacing submissions.
        quick_draw=last_submission ? std::min(elapsed,quick_draw*.95+elapsed*.05) : elapsed;
        last_submission=time;
    }

    double next_slot(double due,double finished,double tick) {
        // Keep the ideal grid independent of wake-up jitter. A delayed draw
        // represents the latest elapsed slot; older slots are no longer useful.
        if (due!=scheduled || slot==0){slot=due;recovery=0;}
        const double started=draw_started>0 ? draw_started : finished;
        slot+=tick;
        if (slot<=started){
            slot+=(1+std::floor((started-slot)/tick))*tick;
            recovery=1.;
        }else{
            // After a missed slot allow a slightly shorter recovery gap.
            // Close it gradually across roughly half a second of on-time draws.
            recovery=std::max(0.,recovery-tick/.5);
        }
        // Space the first two submissions after a stall. The next punctual
        // draw automatically rejoins the ideal grid.
        double earliest=started+tick*.5;
        if(last_submission>=started){
            // Cap the recovery spacing below the target period so that late
            // slots can recover their phase even on a slower or VRR display.
            const double steady_gap=refresh_period>0 ? std::min(refresh_period,tick*.85) : tick*.75;
            const double gap=steady_gap-recovery*std::max(0.,steady_gap-tick*2./3.);
            const double lead=std::clamp(quick_draw-.00025,0.,tick*.5);
            earliest=std::max(earliest,last_submission+gap-lead);
        }
        scheduled=std::max({slot,earliest,finished});
        return scheduled;
    }

    void measured_replay(double elapsed) {
        // Reserve time for backup, swap and restore as well as the draw.
        // Adapt immediately to a heavier scene and relax after cheaper draws.
        replay_cost=std::clamp(std::max(elapsed, replay_cost*.9+elapsed*.1),.0001,.100);
    }

    bool fits_wait(double start,double end) const {
        return end-start>=replay_cost+.00025;
    }

    void update(double arrival,double interval,bool continuous){
        if(!continuous){
            nominal=period=interval;origin=arrival+std::min(interval*.5,.025);return;
        }
        origin+=period;
        // Arrival jitter is not a scene change. Restarting here discards the
        // tail of the preceding pair; a very slow estimator also lets both
        // retained pairs drift into the future and clamp repeatedly to zero.
        nominal+=.1*(std::clamp(interval,nominal*.5,nominal*1.5)-nominal);
        double error=arrival+std::min(nominal*.5,.025)-origin;
        period=std::clamp(nominal+error*.5,nominal*.5,nominal*1.5);
    }
};
static PresentationClock presentation_clock;

// FFVIII sets an idle delay for the current source frame. Only reclaim the
// portion beyond its deadline that the compositor has already spent drawing.
struct NativeFrameWait {
    double arrival=0, period=0, draw_credit=0;

    void start(int mode,double time,double interval,double rendered,bool continuous){
        period=mode==3?1./15.:mode==6?1./60.:1./30.;
        arrival=time;
        draw_credit=continuous&&interval>period&&interval<period*2.5 ?
            std::min(rendered,interval-period):0.;
    }
    double remaining(double now,double request){
        if(!draw_credit||request<=0||request>period)return request;
        const double over=std::max(0.,now+request-arrival-period);
        const double used=std::min({request,over,draw_credit});
        draw_credit-=used;
        return request-used;
    }
};
static NativeFrameWait native_frame_wait;
