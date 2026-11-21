// XInput includes pads mapped by Steam Input. Each slot has its own edge
// detector, so two controllers cannot accidentally complete the shortcut.
struct Pad {
    WORD previous=0,direction=0;
    bool connected=false;
    ULONGLONG repeat_at=0,probe_at=0;
};
static Pad pads[4];
static int owner=-1;
static void pad_input(Pad& pad,int index,WORD held,ULONGLONG tick,const wchar_t* path,RetroSettings& s){
    const WORD chord=XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK;
    const WORD pressed=held&~pad.previous;
    const bool toggle=(held&chord)==chord&&(pad.previous&chord)!=chord;
    pad.previous=held;
    if(!open)owner=-1;
    if(owner>=0&&index!=owner)return;
    if(toggle){handle_key(VK_F8,false,path,s);owner=open?index:-1;pad.direction=0;return;}
    if(!open||(held&chord))return;
    const WORD arrows=held&(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT);
    if(pressed&XINPUT_GAMEPAD_B)handle_key(VK_ESCAPE,false,path,s);
    else if(pressed&XINPUT_GAMEPAD_A)handle_key(VK_RETURN,false,path,s);
    else if(arrows&&(!(arrows&(arrows-1)))&&(pad.direction!=arrows||tick>=pad.repeat_at)){
        handle_key(arrows==1?VK_UP:arrows==2?VK_DOWN:arrows==4?VK_LEFT:VK_RIGHT,
                   (held&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0,path,s);
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
            if(owner==i)owner=-1;pad={};pad.probe_at=now+900;continue;
        }
        WORD held=state.Gamepad.wButtons;
        if(state.Gamepad.sThumbLY>18000)held|=XINPUT_GAMEPAD_DPAD_UP;
        if(state.Gamepad.sThumbLY<-18000)held|=XINPUT_GAMEPAD_DPAD_DOWN;
        if(state.Gamepad.sThumbLX>18000)held|=XINPUT_GAMEPAD_DPAD_RIGHT;
        if(state.Gamepad.sThumbLX<-18000)held|=XINPUT_GAMEPAD_DPAD_LEFT;
        if(focus&&pad.connected)pad_input(pad,i,held,now,path,s);
        else pad.previous=held;
        pad.connected=true;
    }
}

static std::atomic<bool> release_pad[4]{};
static DWORD filter_game_pad(DWORD result,DWORD id,XINPUT_STATE* state){
    if(result!=ERROR_SUCCESS||!state||id>=4)return result;
    const auto& p=state->Gamepad;
    const WORD chord=XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK;
    const bool chord_down=(p.wButtons&chord)==chord;
    const bool neutral=!p.wButtons&&std::abs(int(p.sThumbLX))<14000&&std::abs(int(p.sThumbLY))<14000;
    if(capture_pad.load()||chord_down)release_pad[id]=true;
    else if(neutral)release_pad[id]=false;
    if(release_pad[id])state->Gamepad={};
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