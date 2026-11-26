#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
static void log(const char*){}
#include "../src/deck_cadence.h"
int main(){
    DeckCadence clock;
    double t=1;
    clock.present(2,t,.50f,0,0,0,false);
    clock.present(2,t+=.0165,.50f,0,0,0,false);
    clock.present(2,t+=.0325,.50f,0,0,0,true);
    clock.present(2,t+=.2485,.50f,0,0,0,true);
    clock.present(2,t+=.2510,.50f,0,0,0,true);
    clock.present(1,t+=.60,.50f,0,0,0,true);
    clock.present(1,t+=.0405,.50f,0,0,0,true);
    clock.present(1,t+=.0065,.50f,0,0,0,false);
    const auto& w=clock.scenes[1];
    assert(w.intervals==4 && w.histogram_intervals==3 && w.percentile(50)==33 && w.percentile(95)==249);
    assert(w.busy.late==2 && w.clear.late==0 && w.mixed.late==0);
    assert(std::abs(w.busy.elapsed+w.clear.elapsed+w.mixed.elapsed-w.elapsed)<1e-8);
    std::string result;
    clock.report([&](const char* s){result+=s;});
    assert(result.find("world gaps under 250 ms | p50 <=33 ms | p95 <=249 ms | p99 <=249 ms")!=std::string::npos);
    assert(result.find("world gaps over 50 ms/intervals | busy 2/2 (0.50s) | clear 0/1 (0.02s) | mixed 0/1 (0.03s)")!=std::string::npos);
    assert(result.find("gaps of 250 ms or longer | 1 included in rate")!=std::string::npos);
    assert(result.find("251.0 ms between swaps")!=std::string::npos);
    assert(clock.scenes[1].intervals==0);
    clock.present(1,t+=.0205,.50f,0,0,0,false);
    assert(clock.scenes[0].intervals==1 && clock.scenes[0].clear.intervals==1);
    DeckCadence battle;
    t=1;
    battle.source(3,t);
    battle.present(3,t,1.f);
    for(unsigned i=1;i<=60;i++){
        t+=1./30.;
        if((i%2)==0)battle.source(3,t);
        battle.present(3,t,(i%2)?0.5f:1.f);
    }
    result.clear();
    battle.report([&](const char* line){result+=line;});
    assert(result.find("Deck 30 battle | 30.00 swaps/s | 0 of 60 intervals over 50 ms")!=std::string::npos);
    assert(result.find("15.00 game updates/s | 30 intermediate swaps of 60")!=std::string::npos);
    DeckCadence sixty;
    sixty.target=60;
    sixty.present(3,1.,.25f);
    sixty.present(3,1.017,.50f);
    sixty.present(3,1.047,.75f);
    result.clear();
    sixty.report([&](const char* text){result+=text;});
    assert(result.find("Deck 60 battle |")!=std::string::npos);
    assert(result.find("1 of 2 intervals over 25 ms")!=std::string::npos);
    DeckCadence stalled;
    stalled.source(3,1.);
    stalled.source(3,1.30);
    assert(stalled.scenes[2].source_intervals==1&&std::abs(stalled.scenes[2].source_elapsed-.3)<1e-8);
    stalled.source(3,8.); // Do not count suspension as an in-game stall.
    assert(stalled.scenes[2].source_intervals==1);
    puts("Deck histogram: split windows, quantiles, load transitions, gap reporting and resets OK");
    return 0;
}
