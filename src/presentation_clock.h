// Smooth the presentation timeline, not the game's simulation clock.
// One prepared source pair remains available while its successor is matched.
struct PresentationClock {
    double origin=0,period=1./30.,nominal=1./30.;
    double draw_started=0, slot=0, scheduled=0;
    double replay_cost=.0025;
    double last_submission=0, quick_draw=0, refresh_period=0;
    double recovery=0;

    static bool live_pair(double time,double origin,double period){
        return period>0&&time<=origin+1.5*period;
    }

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

    double wait_slot(double due,double time,double end,double tick) const {
        if(!last_submission||fits_wait(std::max(time,due),end))return due;
        // A real idle window can start just before the target slot and end
        // too soon for a draw started exactly on it. Protect both the game's
        // wake deadline and the distance from the preceding displayed frame.
        const double latest=end-replay_cost-.00075;
        const double first=std::max({time,due-std::min(replay_cost,.004),last_submission+tick*.85});
        return latest>=first?std::min(latest,due):due;
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

// A long tick followed by a short one can still deliver the full native rate.
// Reserve the rendering thread when the average slips, or immediately on a
// major stall. The scheduler can still present during actual idle waits.
struct DeckSourceBudget {
    double relative_period=1;
    bool busy=false;
    unsigned outputs_at_tick=0;

    bool can_draw_battle(unsigned rendered,double elapsed,double cost,int target=30) const {
        constexpr double tick=1./15.;
        // A source swap can arrive before its screen deadline and emit
        // nothing. Count actual output for the entire tick so that both
        // slots can be serviced later without creating unbounded work.
        const unsigned slots=target==60?4:2;
        return rendered-outputs_at_tick<slots&&elapsed>=0&&elapsed+2*cost<tick&&
            cost<=tick*.36/slots&&relative_period+cost/tick<=1.30;
    }

    void observe(double interval,double nominal){
        const double ratio=interval/nominal;
        relative_period+=.25*(std::clamp(ratio,0.,2.)-relative_period);
        busy=ratio>=1.5||relative_period>(busy?1.06:1.12);
    }
};
static DeckSourceBudget deck_source_budget;

// A blocked extra swap can cost more than any subsequent game wait. In that
// case the wait gate cannot get a fresh measurement from another extra swap.
struct DeckWaitRecovery {
    int cheap_sources=0;
    double worst_source=0, missed_window=0, probe_budget=0, retry_at=0, retry_delay=.5;
    unsigned missed_windows=0,probes=0,failed_probes=0;

    void clear_samples(){cheap_sources=0;worst_source=0;}

    void missed(double due,double now,double end){
        // The scheduler could have submitted an already due frame during
        // this real idle window if the last slow swap had cost less.
        const double available=end-std::max(due,now)-.00025;
        if(available>=.001){
            ++missed_windows;
            missed_window=std::max(missed_window,available);
        }
    }

    bool needs_sample(const PresentationClock& clock,double time) const {
        return (missed_window>0||(!probe_budget&&clock.replay_cost>=1./30.))&&time>=retry_at;
    }

    void source(PresentationClock& clock,double time,double elapsed,double backup){
        if(!needs_sample(clock,time))return;
        // The validated source replay includes backup, draw and swap. Allow
        // another measured transfer for the restore and a 25% safety margin.
        const double budget=1.25*(elapsed+backup);
        const bool cheaper=missed_window>0 ? budget<=missed_window&&budget<clock.replay_cost*.8 :
            budget<clock.replay_cost*.5;
        if(!(elapsed>0&&backup>=0&&cheaper)){clear_samples();return;}
        worst_source=std::max(worst_source,budget);
        if(++cheap_sources==3){
            clock.replay_cost=probe_budget=std::max(.0001,worst_source);
            ++probes;missed_window=0;clear_samples();
        }
    }

    void extra(double time,double elapsed){
        clear_samples();missed_window=0;
        if(!probe_budget)return;
        if(elapsed>std::max(2*probe_budget,probe_budget+.002)){
            ++failed_probes;
            retry_at=time+retry_delay;
            retry_delay=std::min(8.,2*retry_delay);
        }else{retry_at=0;retry_delay=.5;}
        probe_budget=0;
    }
};
static DeckWaitRecovery deck_wait_recovery;

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
