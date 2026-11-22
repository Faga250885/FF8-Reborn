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
    assert(w.intervals==3 && w.percentile(50)==33 && w.percentile(95)==249);
    assert(w.busy.late==1 && w.clear.late==0 && w.mixed.late==1);
    assert(std::abs(w.busy.elapsed+w.clear.elapsed+w.mixed.elapsed-w.elapsed)<1e-8);
    std::string result;
    clock.report([&](const char* s){result+=s;});
    assert(result.find("world gaps under 250 ms | p50 <=33 ms | p95 <=249 ms | p99 <=249 ms")!=std::string::npos);
    assert(result.find("world gaps over 25 ms/intervals | busy 1/1 (0.25s) | clear 0/1 (0.02s) | mixed 1/1 (0.03s)")!=std::string::npos);
    assert(result.find("251.0 ms between swaps")!=std::string::npos);
    assert(clock.scenes[1].intervals==0);
    clock.present(1,t+=.0205,.50f,0,0,0,false);
    assert(clock.scenes[0].intervals==1 && clock.scenes[0].clear.intervals==1);
    puts("Deck histogram: split windows, quantiles, load transitions, gap reporting and resets OK");
    return 0;
}
