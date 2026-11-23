#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
using namespace retro_panel;
namespace buttons=controller_bindings;

int main(){
    wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH,folder)&&GetTempFileNameW(folder,L"f8c",0,path));
    display_settings.load(path);
    auto& settings=display_settings.retro;
    auto key=[&](UINT pressed){handle_key(pressed,path,settings);};
    ULONGLONG clock=10000;
    auto press=[&](unsigned held,int slot=0,ULONGLONG elapsed=10){
        clock+=elapsed;
        pad_input(pads[slot],slot,held,clock,path,settings);
    };
    auto tap=[&](unsigned held,int slot=0){press(held,slot);press(0,slot);};
    auto record=[&](Row id){
        assert(open&&!recording_shortcut);
        focus_section(Section::MenuSettings);row=id;
        key(VK_RETURN);
        assert(recording_shortcut&&shortcut_target==id);
    };
    auto hold_second=[&](unsigned held,int slot=0){
        press(held,slot);press(held,slot,999);
        assert(recording_shortcut&&shortcut_progress==9);
        press(held,slot,1);
    };
    constexpr unsigned select=XINPUT_GAMEPAD_BACK,rb=XINPUT_GAMEPAD_RIGHT_SHOULDER,lb=XINPUT_GAMEPAD_LEFT_SHOULDER;
    constexpr unsigned a=XINPUT_GAMEPAD_A,b=XINPUT_GAMEPAD_B,x=XINPUT_GAMEPAD_X,y=XINPUT_GAMEPAD_Y;
    constexpr unsigned start=XINPUT_GAMEPAD_START;
    constexpr unsigned down=XINPUT_GAMEPAD_DPAD_DOWN;
    assert(display_settings.menu_shortcut==int(select|start)&&display_settings.previous_filter_shortcut==int(select|lb)&&
        display_settings.next_filter_shortcut==int(select|rb));

    // Simultaneous edges on different controllers cannot make a shortcut.
    press(select,0);tap(rb,1);tap(start,1);assert(!open&&active_profile==8);
    press(select|rb);assert(!open&&active_profile==9);
    press(select|rb,0,2000);assert(active_profile==9);
    press(select);press(select|lb);assert(active_profile==8);
    press(select);press(select|rb);assert(active_profile==9);
    press(select);press(select|start);assert(open&&owner==0&&capture_pad);
    press(select|start,0,1300);assert(open);
    press(select);press(select|start);assert(!open&&!capture_pad);
    press(select);press(select|lb);assert(active_profile==8);
    press(select);press(select|rb);assert(active_profile==9);
    press(select);press(select|start);assert(open&&owner==0&&capture_pad);
    tap(down,1);assert(row==Aspect);
    press(0);tap(down);assert(row==FPS);
    key(VK_F8);assert(!open&&!capture_pad);key(VK_F8);assert(open&&row==Aspect);

    // The act of starting a recording with A cannot record that same press.
    focus_section(Section::MenuSettings);row=MenuShortcut;
    press(a);assert(recording_shortcut);
    press(a,0,1400);assert(shortcut_candidate==0&&recording_shortcut);
    press(0);press(b);press(0,0,90);assert(!recording_shortcut&&display_settings.menu_shortcut==buttons::menu);
    record(MenuShortcut);
    hold_second(b);assert(!recording_shortcut&&display_settings.menu_shortcut==int(b));
    press(b,0,1500);assert(open);press(0);
    key(VK_F8);assert(!open);tap(b);assert(open&&owner==0);key(VK_F8);assert(!open);
    key(VK_F8);assert(open); // F8 still opens even after remapping the controller.

    record(PreviousFilterShortcut);hold_second(buttons::lt);
    assert(!recording_shortcut&&display_settings.previous_filter_shortcut==int(buttons::lt));press(0);
    record(NextFilterShortcut);press(rb);press(rb,0,700);press(rb|buttons::rt);
    assert(shortcut_progress==0&&shortcut_candidate==(rb|buttons::rt)); // adding the trigger starts its own full second
    press(rb|buttons::rt,0,999);assert(recording_shortcut&&shortcut_progress==9);
    press(rb|buttons::rt,0,1);
    assert(!recording_shortcut&&display_settings.next_filter_shortcut==int(rb|buttons::rt));press(0);
    display_settings.load(path);
    assert(display_settings.menu_shortcut==int(b)&&display_settings.previous_filter_shortcut==int(buttons::lt)&&
           display_settings.next_filter_shortcut==int(rb|buttons::rt));
    assert(shortcut_label(rb|buttons::rt)==L"RB + RT");

    record(NextFilterShortcut);press(x|y|a);press(x|y|a,0,1200);
    assert(recording_shortcut&&shortcut_too_many&&display_settings.next_filter_shortcut==int(rb|buttons::rt));
    press(x);press(x,0,1100);assert(recording_shortcut); // release every button after an invalid chord
    press(0);hold_second(b);assert(recording_shortcut&&shortcut_conflict);
    assert(display_settings.next_filter_shortcut==int(rb|buttons::rt));
    press(0);hold_second(buttons::rt);assert(!recording_shortcut&&display_settings.next_filter_shortcut==int(buttons::rt));press(0);
    record(NextFilterShortcut);press(x);key(VK_ESCAPE);press(0);
    assert(!recording_shortcut&&display_settings.next_filter_shortcut==int(buttons::rt));

    // A single mapped button shared with a chord fires only when released on its own.
    record(MenuShortcut);hold_second(x|buttons::rt);press(0);
    key(VK_F8);assert(!open);
    const int initial=active_profile;
    press(buttons::rt);press(buttons::rt,0,1350);assert(active_profile==initial&&!open);
    press(0);assert(active_profile==(initial+1)%profile_count&&!open);
    const int after_single=active_profile;
    press(buttons::rt);press(buttons::rt|x);assert(open&&active_profile==after_single);
    press(buttons::rt);press(0);key(VK_F8);assert(!open&&active_profile==after_single);

    // Filtering the game's XInput result consumes a complete binding and preserves other game controls.
    auto read_game=[](WORD held,BYTE left=0,BYTE right=0,int slot=0){
        XINPUT_STATE state{};auto& p=state.Gamepad;
        p.wButtons=held;p.bLeftTrigger=left;p.bRightTrigger=right;p.sThumbRX=-13200;p.sThumbRY=12400;
        assert(filter_game_pad(ERROR_SUCCESS,slot,&state)==ERROR_SUCCESS);
        return p;
    };
    auto raw=read_game(WORD(y|x),70,200);
    assert(raw.wButtons==y&&!raw.bRightTrigger&&raw.bLeftTrigger==70&&raw.sThumbRX==-13200&&raw.sThumbRY==12400);
    raw=read_game(WORD(y|x));assert(raw.wButtons==y); // the remaining half of a consumed chord stays hidden
    raw=read_game(y,127);assert(raw.wButtons==y&&raw.bLeftTrigger==127); // below digital threshold
    raw=read_game(y,128);assert(raw.wButtons==y&&raw.bLeftTrigger==0);
    raw=read_game(y,0,0,1);assert(raw.wButtons==y);
    key(VK_F8);
    raw=read_game(y,80,80);assert(!raw.wButtons&&!raw.bLeftTrigger&&!raw.sThumbRX);
    key(VK_F8);
    raw=read_game(y);assert(!raw.wButtons); // no held-through-menu action leaks to the game
    read_game(0);raw=read_game(y);assert(raw.wButtons==y&&raw.sThumbRX==-13200);

    key(VK_F8);focus_section(Section::MenuSettings);
    for(Row id:{MenuShortcut,PreviousFilterShortcut,NextFilterShortcut}){row=id;key(VK_LEFT);assert(!recording_shortcut);}
    display_settings.load(path);
    assert(display_settings.menu_shortcut==buttons::menu&&display_settings.previous_filter_shortcut==buttons::previous&&
           display_settings.next_filter_shortcut==buttons::next);
    key(VK_F8);assert(!open);
    raw=read_game(WORD(select|y));assert(raw.wButtons==y&&raw.sThumbRX==-13200);
    raw=read_game(y);assert(raw.wButtons==(select|y)); // short Select by itself performs its native action
    select_taps[0].until=0;
    raw=read_game(y);assert(raw.wButtons==y);
    for(unsigned shoulder:{rb,lb,rb}){
        raw=read_game(WORD(select|y));assert(raw.wButtons==y);
        raw=read_game(WORD(select|shoulder|y));assert(raw.wButtons==y&&raw.sThumbRY==12400);
    }
    raw=read_game(y);assert(raw.wButtons==y); // a used modifier must never replay a native tap
    press(0);key(VK_F8);
    record(NextFilterShortcut);press(x,1);press(y,0);press(y,0,1050);
    assert(recording_shortcut&&shortcut_candidate==x&&recording_pad==1);
    press(x,1,1);assert(!recording_shortcut&&display_settings.next_filter_shortcut==int(x));
    press(0,1);press(0,0);
    key(VK_LEFT);assert(display_settings.next_filter_shortcut==buttons::next);key(VK_F8);
    tap(buttons::menu,1);assert(open&&owner==1);
    const int original_row=row;
    tap(down,0);assert(row==original_row);tap(down,1);assert(row!=original_row);
    tap(buttons::menu,1);assert(!open&&!capture_pad);
    assert(DeleteFileW(path));
    puts("Controller shortcuts: held modifier, independent pads, recording and persistence, overlap, capture and gameplay input OK");
}
