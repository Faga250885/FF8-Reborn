// XInput includes pads mapped by Steam Input. Each slot has its own edge
// detector, so two controllers cannot accidentally complete the shortcut.
struct Pad {
    unsigned previous=0,paired=0,delayed=0,ignore=0,seen_recording=0;
    WORD direction=0;
    bool connected=false,capture_ready=false;
    ULONGLONG repeat_at=0,probe_at=0;
};
static Pad pads[4];
static int owner=-1;
// Select is also a game control. Pass a standalone tap to the game on release,
// but consume it when the player uses it as a shortcut modifier.
struct SelectTap {
    std::atomic<bool> pending{false},visible{false};
    std::atomic<ULONGLONG> until{0};
    std::atomic<DWORD> extra_packets{0};
};
static SelectTap select_taps[4];
static std::atomic<unsigned> shortcut_consumed[4]{};
static void record_pad(Pad& pad,int index,unsigned held,unsigned before,ULONGLONG tick,const wchar_t* path,RetroSettings& s){
    if(recording_pad>=0&&recording_pad!=index)return;
    if(pad.seen_recording!=recording_epoch){
        pad.seen_recording=recording_epoch;
        pad.capture_ready=!before;
    }
    if(!held){
        if(recording_pad==index && shortcut_candidate==XINPUT_GAMEPAD_B && tick-shortcut_since<400 && pad.capture_ready)
            recording_shortcut=false;
        pad.capture_ready=true;
        if(recording_pad==index){shortcut_candidate=0;shortcut_progress=0;recording_pad=-1;}
        return;
    }
    if(!pad.capture_ready)return;
    recording_pad=index;
    if(held!=shortcut_candidate){
        shortcut_candidate=held;shortcut_progress=0;shortcut_since=tick;failed=false;
        shortcut_too_many=!controller_bindings::valid(held);shortcut_conflict=false;
        if(shortcut_too_many){pad.capture_ready=false;return;}
    }
    shortcut_progress=int(std::min<ULONGLONG>(10,(tick-shortcut_since)/100));
    if(shortcut_progress<10)return;
    const auto& def=shortcuts[shortcut_target-MenuShortcut];
    for(const auto& other:shortcuts)
        if(&other!=&def&&unsigned((display_settings.*other.member).load())==held)shortcut_conflict=true;
    pad.capture_ready=false;
    if(shortcut_conflict||!persist(path,s,active_profile,-1,shortcut_target,int(held)))return;
    display_settings.*def.member=int(held);
    recording_shortcut=false;pad.ignore=held;
}
// Match the edge that completes a chord. Its modifier may be held between taps.
// A single button shared with a longer shortcut fires on release only when the
// chord was never completed. This makes overlapping custom bindings unambiguous.
static int gameplay_shortcut(Pad& pad,unsigned held,unsigned before){
    const unsigned bindings[]={unsigned(display_settings.menu_shortcut.load()),
        unsigned(display_settings.previous_filter_shortcut.load()),unsigned(display_settings.next_filter_shortcut.load())};
    int action=-1;
    for(int i=0;i<3;++i){
        const unsigned chord=bindings[i];
        if((chord&(chord-1)) && (held&chord)==chord && (before&chord)!=chord){
            pad.paired|=chord;pad.delayed&=~chord;
            if(action<0)action=i;
        }
    }
    for(int i=0;i<3;++i){
        const unsigned bit=bindings[i];
        if(!bit||(bit&(bit-1)))continue;
        if((held&bit)&&!(before&bit)&&!(pad.paired&bit)){
            bool shared=false;
            for(unsigned chord:bindings)if(chord!=bit&&(chord&bit))shared=true;
            if(shared)pad.delayed|=bit;else if(action<0)action=i;
        }
        if((pad.delayed&bit)&&!(held&bit)&&action<0)action=i;
    }
    pad.paired&=held;pad.delayed&=held;
    return action;
}
static void pad_input(Pad& pad,int index,unsigned held,ULONGLONG tick,const wchar_t* path,RetroSettings& s,WORD stick_arrows=0){
    const unsigned before=pad.previous,pressed=held&~before;
    pad.previous=held;
    if(!open)owner=-1;
    if(owner>=0&&index!=owner)return;
    if(recording_shortcut){record_pad(pad,index,held,before,tick,path,s);return;}
    pad.ignore&=held;
    if(pad.ignore)return;
    if(!open){
        const int action=gameplay_shortcut(pad,held,before);
        if(action>=0){
            const unsigned used=unsigned((display_settings.*shortcuts[action].member).load());
            shortcut_consumed[index].fetch_or(held&used);
            if(used&XINPUT_GAMEPAD_BACK)select_taps[index].pending=false;
        }
        if(action==0){
            handle_key(VK_F8,path,s);owner=index;pad.direction=0;
            const unsigned binding=unsigned(display_settings.menu_shortcut.load());
            pad.ignore=held&(binding==controller_bindings::menu?XINPUT_GAMEPAD_START:binding);
            pad.delayed=0;
        }else if(action>0)cycle_filter_shortcut(path,s,action==1?-1:1);
        return;
    }
    pad.paired=pad.delayed=0;
    constexpr unsigned chord=controller_bindings::menu;
    if(display_settings.menu_shortcut==int(chord)&&(held&chord)==chord&&(before&chord)!=chord){
        handle_key(VK_F8,path,s);owner=open?index:-1;pad.ignore=XINPUT_GAMEPAD_START;pad.direction=0;return;
    }
    const WORD arrows=stick_arrows|(held&(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT));
    const WORD shoulders=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if(pressed&XINPUT_GAMEPAD_B)handle_key(VK_ESCAPE,path,s);
    else if(pressed&XINPUT_GAMEPAD_A)handle_key(VK_RETURN,path,s);
    else if(!arrows&&(pressed&shoulders)&&(held&shoulders)!=shoulders)
        change_section((pressed&XINPUT_GAMEPAD_RIGHT_SHOULDER)?1:-1);
    else if(arrows&&(!(arrows&(arrows-1)))&&(pad.direction!=arrows||tick>=pad.repeat_at)){
        handle_key(arrows==1?VK_UP:arrows==2?VK_DOWN:arrows==4?VK_LEFT:VK_RIGHT,path,s);
        pad.repeat_at=tick+(arrows==pad.direction?110:420);
    }
    pad.direction=arrows;
}
using ReadPad=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
static ReadPad system_pad(){

    static ReadPad read=[]()->ReadPad {
        for(const wchar_t* lib:{L"xinput1_4.dll",L"xinput9_1_0.dll"}){
            HMODULE m=LoadLibraryExW(lib,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!m)continue;
            auto fn=reinterpret_cast<ReadPad>(GetProcAddress(m,"XInputGetState"));
            if(fn)return fn;FreeLibrary(m);
        }
        return nullptr;
    }();
    return read;
}
static void poll_pad(HWND window,const wchar_t* path,RetroSettings& s){
    auto read=system_pad();
    if(!read)return;
    const bool focus=focused(window);
    const auto now=GetTickCount64();
    for(int i=0;i<4;i++){
        auto& pad=pads[i];if(now<pad.probe_at)continue;
        XINPUT_STATE state{};
        if(read(i,&state)!=ERROR_SUCCESS){
            if(owner==i)owner=-1;
            if(recording_shortcut&&recording_pad==i)start_recording(shortcut_target);
            pad={};pad.probe_at=now+900;continue;
        }
        const unsigned held=controller_bindings::physical(state.Gamepad);
        WORD arrows=0;
        if(state.Gamepad.sThumbLY>18000)arrows|=XINPUT_GAMEPAD_DPAD_UP;
        if(state.Gamepad.sThumbLY<-18000)arrows|=XINPUT_GAMEPAD_DPAD_DOWN;
        if(state.Gamepad.sThumbLX>18000)arrows|=XINPUT_GAMEPAD_DPAD_RIGHT;
        if(state.Gamepad.sThumbLX<-18000)arrows|=XINPUT_GAMEPAD_DPAD_LEFT;
        if(focus&&pad.connected)pad_input(pad,i,held,now,path,s,arrows);
        else{
            pad.previous=held;pad.delayed=pad.paired=0;
            if(recording_shortcut&&recording_pad==i)start_recording(shortcut_target);
        }
        pad.connected=true;
    }
}

static std::atomic<bool> release_pad[4]{};
static bool native_select(SelectTap& tap,bool held,bool used){
    if(used){tap.pending=false;tap.until=0;return false;}
    if(held){tap.pending=true;tap.until=0;return false;}
    if(tap.pending.exchange(false))tap.until=GetTickCount64()+75;
    const auto until=tap.until.load();
    if(!until)return false;
    if(GetTickCount64()<until)return true;
    tap.until=0;return false;
}
static DWORD filter_game_pad(DWORD result,DWORD id,XINPUT_STATE* state){
    if(result!=ERROR_SUCCESS||!state||id>=4)return result;
    auto& p=state->Gamepad;
    const unsigned held=controller_bindings::physical(p);
    const bool neutral=!held&&std::abs(int(p.sThumbLX))<14000&&std::abs(int(p.sThumbLY))<14000;
    if(capture_pad.load())release_pad[id]=true;
    else if(neutral)release_pad[id]=false;
    unsigned consumed=shortcut_consumed[id].load()&held;
    bool select_modifier=false;
    for(const auto& shortcut:shortcuts){
        const unsigned binding=unsigned((display_settings.*shortcut.member).load());
        if((held&binding)==binding)consumed|=binding;
        if((binding&XINPUT_GAMEPAD_BACK)&&binding!=XINPUT_GAMEPAD_BACK)select_modifier=true;
    }
    shortcut_consumed[id]=consumed;
    auto& tap=select_taps[id];
    const bool tap_visible=native_select(tap,(held&XINPUT_GAMEPAD_BACK)!=0,
        !select_modifier||release_pad[id]||(consumed&XINPUT_GAMEPAD_BACK));
    if(tap.visible.exchange(tap_visible)!=tap_visible)++tap.extra_packets;
    state->dwPacketNumber+=tap.extra_packets.load();
    if(release_pad[id])p={};
    else{
        if(select_modifier)p.wButtons&=WORD(~XINPUT_GAMEPAD_BACK);
        p.wButtons&=WORD(~consumed);
        if(tap_visible)p.wButtons|=XINPUT_GAMEPAD_BACK;
        if(consumed&controller_bindings::lt)p.bLeftTrigger=0;
        if(consumed&controller_bindings::rt)p.bRightTrigger=0;
    }
    return result;
}
static ReadPad game_read=nullptr;
static DWORD WINAPI game_pad(DWORD id,XINPUT_STATE* state){
    return filter_game_pad(game_read(id,state),id,state);
}
static DWORD WINAPI glfw_pad(DWORD id,XINPUT_STATE* state){
    auto read=system_pad();
    return read?filter_game_pad(read(id,state),id,state):ERROR_DEVICE_NOT_CONNECTED;
}
