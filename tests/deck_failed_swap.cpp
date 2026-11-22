#define GEO_TEST
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
static BOOL swap_result=FALSE;
static unsigned attempts=0;
static BOOL WINAPI simulated_swap(HDC){++attempts;return swap_result;}

int main(){
    static_assert(deck_renderer,"compile with GEO_STEAM_DECK");
    QueryPerformanceFrequency(&frequency);
    real_swap=simulated_swap;
    display_settings.fps=30;
    current.mode=3;
    can_present=prepared_previous=true;
    next_output=now()+1./30.;
    presentation_clock.submitted(now());
    deck_wait_recovery.missed_window=.01;

    assert(!timed_swap(nullptr,.5f)&&attempts==1);
    assert(deck_needs_native_swap&&!can_present&&!prepared_previous);
    assert(next_output==0&&presentation_clock.last_submission==0&&deck_wait_recovery.missed_window==0);
    assert(!present_during_wait(true)&&attempts==1&&outputs==0);
    assert(!timed_swap(nullptr)&&deck_needs_native_swap&&attempts==2);
    swap_result=TRUE;
    assert(timed_swap(nullptr)&&!deck_needs_native_swap&&attempts==3);
    assert(presentation_clock.last_submission==0&&outputs==0);
    assert(timed_swap(nullptr,.5f)&&attempts==4);
    assert(presentation_clock.last_submission>0);
    assert(presentation_clock.refresh_period==0&&output_period()==1./30.);
    std::puts("Deck failed swap: native recovery and clean 30 Hz restart OK");
}
