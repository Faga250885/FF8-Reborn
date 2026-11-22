#include <algorithm>
#include <cassert>
#include <cmath>
#include "../src/presentation_clock.h"

int main(){
    // A single blocked intermediate swap can still be shorter than a 60 Hz
    // display interval but longer than every subsequent idle window.
    for(double cost:{.0075,.0143,.0166,.075}){
        PresentationClock clock;
        DeckWaitRecovery recovery;
        clock.measured_replay(cost);
        const double start=1.,duration=.006;
        assert(!clock.fits_wait(start,start+duration));
        recovery.missed(start,start,start+duration);
        for(unsigned i=0;i<3;++i){
            assert(recovery.needs_sample(clock,start+i/30.));
            recovery.source(clock,start+i/30.,.002,.0004);
        }
        assert(clock.fits_wait(start,start+duration));
        clock.measured_replay(.0028);
        recovery.extra(start+.1,.0028);
        assert(!recovery.needs_sample(clock,start+.2));
    }

    PresentationClock clock;
    DeckWaitRecovery recovery;
    clock.measured_replay(.0143);
    recovery.missed(2.01,2.,2.006); // An empty slot cannot trigger a probe.
    assert(!recovery.needs_sample(clock,2.));
    recovery.missed(2.,2.,2.006);
    for(unsigned i=0;i<6;++i)recovery.source(clock,2.+i/30.,.011,.002);
    assert(!clock.fits_wait(2.,2.006)); // Truly slow draws stay throttled.
    for(unsigned i=0;i<3;++i)recovery.source(clock,3.+i/30.,.002,.0004);
    assert(clock.fits_wait(3.,3.006));
    clock.measured_replay(.0143);
    recovery.extra(3.1,.0143);
    recovery.missed(3.2,3.2,3.206);
    assert(!recovery.needs_sample(clock,3.4)); // Failed probe backs off.
    assert(recovery.needs_sample(clock,3.61));

    // A newly estimated draw may not fit once the remaining windows shrink.
    // Source draws must be able to re-estimate it before the first probe runs.
    clock=PresentationClock{};recovery=DeckWaitRecovery{};
    clock.measured_replay(.0143);
    recovery.missed(4.,4.,4.008);
    for(unsigned i=0;i<3;++i)recovery.source(clock,4.+i/30.,.004,.0004);
    assert(clock.fits_wait(4.,4.008)&&!clock.fits_wait(4.,4.004));
    recovery.missed(4.1,4.1,4.104);
    for(unsigned i=0;i<3;++i)recovery.source(clock,4.1+i/30.,.002,.0003);
    assert(clock.fits_wait(4.2,4.204));

    clock.replay_cost=.003;
    clock.last_submission=4.;
    const double tick=1./60.,due=4.+tick;
    const double slot=clock.wait_slot(due,4.013,4.019,tick);
    assert(slot<due&&slot>=clock.last_submission+.85*tick);
    assert(clock.fits_wait(slot,4.019)&&!clock.fits_wait(due,4.019));
    assert(clock.wait_slot(due,4.012,4.016,tick)==due);
    assert(clock.wait_slot(due,4.013,4.024,tick)==due);
    const double battle_tick=1./30.,battle_due=4.+battle_tick;
    const double early=clock.wait_slot(battle_due,4.030,4.0355,battle_tick);
    assert(early<battle_due&&early>=clock.last_submission+.85*battle_tick);
    assert(clock.fits_wait(early,4.0355)&&!clock.fits_wait(battle_due,4.0355));

    DeckSourceBudget source;
    source.relative_period=1.13;
    source.outputs_at_tick=100;
    assert(source.can_draw_battle(100,.015,.006)); // Source swap arrived early.
    assert(source.can_draw_battle(101,.040,.006)); // Second slot in this tick.
    assert(!source.can_draw_battle(102,.040,.006));
    assert(!source.can_draw_battle(101,.062,.006)); // Preserve the next game tick.
    assert(!source.can_draw_battle(101,.020,.015)); // Expensive replay.
    source.relative_period=1.27;
    assert(!source.can_draw_battle(101,.020,.006)); // Insufficient source budget.
}
