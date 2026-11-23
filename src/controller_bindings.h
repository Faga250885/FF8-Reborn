namespace controller_bindings {
constexpr unsigned lt=1u<<16,rt=1u<<17;
constexpr int menu=XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_START;
constexpr int previous=XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_LEFT_SHOULDER;
constexpr int next=XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_RIGHT_SHOULDER;
struct Button {unsigned bit;const wchar_t* label;};
constexpr Button buttons[]={
    {XINPUT_GAMEPAD_BACK,L"Select"},{XINPUT_GAMEPAD_START,L"Start"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER,L"LB"},{XINPUT_GAMEPAD_RIGHT_SHOULDER,L"RB"},
    {lt,L"LT"},{rt,L"RT"},{XINPUT_GAMEPAD_LEFT_THUMB,L"LS"},{XINPUT_GAMEPAD_RIGHT_THUMB,L"RS"},
    {XINPUT_GAMEPAD_A,L"A"},{XINPUT_GAMEPAD_B,L"B"},{XINPUT_GAMEPAD_X,L"X"},{XINPUT_GAMEPAD_Y,L"Y"},
    {XINPUT_GAMEPAD_DPAD_UP,L"Up"},{XINPUT_GAMEPAD_DPAD_DOWN,L"Down"},
    {XINPUT_GAMEPAD_DPAD_LEFT,L"Left"},{XINPUT_GAMEPAD_DPAD_RIGHT,L"Right"}
};
constexpr unsigned mask(){unsigned result=0;for(auto b:buttons)result|=b.bit;return result;}
constexpr bool valid(unsigned bits){
    const unsigned rest=bits&(bits-1);
    return bits && !(bits&~mask()) && !(rest&(rest-1));
}
static unsigned physical(const XINPUT_GAMEPAD& pad){
    return (pad.wButtons&mask())|(pad.bLeftTrigger>=128?lt:0)|(pad.bRightTrigger>=128?rt:0);
}
}
