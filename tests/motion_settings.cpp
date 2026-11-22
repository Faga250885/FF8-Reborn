#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
using namespace retro_panel;

int main(){
    wchar_t folder[MAX_PATH],path[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,folder)&&GetTempFileNameW(folder,L"f8m",0,path));
    const int fps=deck_renderer?30:60;
    assert(number(path,L"Display",L"FPS",fps));
    auto& s=display_settings.retro;
    auto read=[&](){display_settings.load(path);};
    auto key=[&](UINT k){handle_key(k,path,s);};
    Pad pad;
    ULONGLONG tick=0;
    auto tap=[&](WORD held){
        pad_input(pad,0,held,++tick,path,s);
        pad_input(pad,0,0,++tick,path,s);
    };
    auto go=[&](Row id){
        for(int i=0;i<visible_rows&&selected()!=id;++i)tap(XINPUT_GAMEPAD_DPAD_DOWN);
        assert(selected()==id);
    };
    auto read_int=[&](const wchar_t* group,const wchar_t* name){
        return int(GetPrivateProfileIntW(group,name,UINT_MAX,path));
    };
    constexpr int strengths[]={0,0,30,10,20,30,10,10,20,10,20,40};
    static_assert(std::size(strengths)==profile_count);
    read();
    assert(s.motion_blur==20);
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(open&&selected()==Profile&&active_profile==8);
    for(int i=0;i<profile_count;++i){
        key(VK_RIGHT);
        assert(s.motion_blur==strengths[active_profile]&&selected()==Profile);
        RetroSettings before=s;
        read();
        assert(same_filter(s,before));
    }
    assert(s.glow==8&&s.lines==320);
    const std::pair<Row,int RetroSettings::*> sliders[]={
        {Pixels,&RetroSettings::pixelation},{Scan,&RetroSettings::scanlines},
        {Mask,&RetroSettings::mask},{Glow,&RetroSettings::glow},{Dither,&RetroSettings::dither},
        {Edges,&RetroSettings::edge_softness},{MotionBlur,&RetroSettings::motion_blur},
        {PhosphorStrength,&RetroSettings::phosphor_strength},{PhosphorSharpness,&RetroSettings::phosphor_sharpness}
    };
    for(auto [slider,field]:sliders){
        go(slider);
        s.*field=8;key(VK_RIGHT);assert(s.*field==10);
        s.*field=8;tap(XINPUT_GAMEPAD_DPAD_LEFT);assert(s.*field==0);
        key(VK_LEFT);assert(s.*field==0);
        s.*field=32;key(VK_LEFT);assert(s.*field==30);
        s.*field=32;tap(XINPUT_GAMEPAD_DPAD_RIGHT);assert(s.*field==40);
        tap(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_RIGHT_SHOULDER);assert(s.*field==50);
        s.*field=98;key(VK_RIGHT);key(VK_RIGHT);assert(s.*field==100);
    }
    go(Lines);key(VK_RIGHT);assert(s.lines==340);
    tap(XINPUT_GAMEPAD_DPAD_LEFT);assert(s.lines==320);
    key(VK_F8);
    assert(same_filter(s,profiles[8]));
    // A factory selection saved by deck.6 cached zero in the live section.
    assert(number(path,L"Retro",L"MotionBlur",0));
    read();
    assert(s.motion_blur==20);
    // deck.5 stored a global blur. Carry it only into the active filter.
    assert(WritePrivateProfileStringW(L"Retro",L"MotionBlur",nullptr,path));
    assert(number(path,L"Display",L"MotionBlur3D",15));
    assert(number(path,L"Display",L"MotionBlurBackground",5));
    assert(number(path,L"Display",L"CRTPersistence",25));
    read();
    assert(s.motion_blur==30);
    key(VK_F8);
    key(VK_RIGHT);
    assert(s.motion_blur==10);
    key(VK_LEFT);
    assert(s.motion_blur==30&&active_profile==8);
    assert(read_int(section(8).c_str(),L"MotionBlur")==30);

    // Both keyboard and D-pad change the profile immediately from a preview.
    go(MotionBlur);
    tap(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_RIGHT_SHOULDER);
    key(VK_RIGHT);
    assert(dirty&&s.motion_blur==50);
    go(Profile);
    tap(XINPUT_GAMEPAD_DPAD_RIGHT);
    assert(selected()==Profile&&active_profile==9&&!dirty&&s.motion_blur==10);
    go(Scan);
    key(VK_LEFT);
    assert(dirty);
    go(Profile);
    key(VK_LEFT);
    assert(selected()==Profile&&active_profile==8&&!dirty&&s.motion_blur==30);
    assert(read_int(section(custom).c_str(),L"Mode")==-1);
    go(MotionBlur);
    key(VK_LEFT);
    key(VK_RIGHT);
    assert(!dirty);
    key(VK_RIGHT);
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(!open&&!dirty&&s.motion_blur==30);
    read();
    assert(s.motion_blur==30&&read_int(L"Retro",L"Selected")==8);

    // Only an explicit save can create or replace the custom profile.
    key(VK_F8);
    go(MotionBlur);
    for(int i=0;i<12;++i)key(VK_RIGHT);
    assert(s.motion_blur==100);
    for(int i=0;i<12;++i)key(VK_LEFT);
    assert(dirty&&s.motion_blur==0);
    go(SaveCustom);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    tap(XINPUT_GAMEPAD_A);
    assert(save_failed()&&dirty&&active_profile==8);
    assert(read_int(L"Retro",L"MotionBlur")==30);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));
    tap(XINPUT_GAMEPAD_A);
    assert(!dirty&&!save_failed()&&active_profile==custom&&custom_base==8);
    key(VK_F8);
    read();
    assert(s.motion_blur==0&&display_settings.fps==fps);

    key(VK_F8);
    go(MotionBlur);
    key(VK_RIGHT);
    go(Profile);
    key(VK_RIGHT);
    assert(active_profile==0&&s.motion_blur==0&&selected()==Profile);
    tap(XINPUT_GAMEPAD_DPAD_LEFT);
    assert(active_profile==custom&&s.motion_blur==0);
    go(Restore);
    key(VK_RETURN);
    assert(dirty&&selected()==Profile&&s.motion_blur==20);
    assert(read_int(section(custom).c_str(),L"MotionBlur")==0);
    tap(XINPUT_GAMEPAD_B);
    assert(!open&&s.motion_blur==0);

    key(VK_F8);
    go(Restore);
    key(VK_RETURN);
    go(SaveCustom);
    key(VK_RETURN);
    key(VK_F8);
    read();
    assert(s.motion_blur==20&&read_int(section(custom).c_str(),L"MotionBlur")==20);
    // Explicit old factory slot overrides survive even when the saved value is zero.
    key(VK_F8);
    while(active_profile!=8)key(VK_LEFT);
    key(VK_F8);
    assert(number(path,section(8).c_str(),L"MotionBlur",0));
    assert(number(path,L"Retro",L"MotionBlur",0));
    read();
    assert(s.motion_blur==0);
    assert(DeleteFileW(path));
    puts("Factory motion blur values, upgrade, preview navigation, manual save, discard, restore and restart OK");
}
