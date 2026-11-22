#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
using namespace retro_panel;

int main(){
    wchar_t folder[MAX_PATH],path[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,folder)&&GetTempFileNameW(folder,L"f8m",0,path));
    assert(number(path,L"Display",L"FPS",deck_renderer?30:60));
    assert(number(path,L"Retro",L"Selected",8));
    auto& s=display_settings.retro;
    display_settings.load(path);
    assert(s.motion_blur==0);
    assert(number(path,L"Display",L"MotionBlur3D",15));
    assert(number(path,L"Display",L"MotionBlurBackground",5));
    assert(number(path,L"Display",L"CRTPersistence",25));
    display_settings.load(path);
    assert(s.motion_blur==30);

    Pad pad;
    ULONGLONG tick=0;
    auto tap=[&](WORD buttons){
        pad_input(pad,0,buttons,++tick,path,s);
        pad_input(pad,0,0,++tick,path,s);
    };
    auto key=[&](UINT k){handle_key(k,false,path,s);};
    auto go=[&](Row id){
        for(int i=0;i<visible_rows&&selected()!=id;++i)tap(XINPUT_GAMEPAD_DPAD_DOWN);
        assert(selected()==id);
    };
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(open&&selected()==Profile&&active_profile==8);
    assert(visual_index(Profile)<visual_index(MotionBlur));
    assert(retro_panel_draw::row_top(MotionBlur,true)-retro_panel_draw::row_top(Edges,true)==retro_panel_draw::row_height);

    // Migrated settings belong only to the profile active at upgrade.
    key(VK_RIGHT);
    assert(s.motion_blur==0&&active_profile==9);
    key(VK_LEFT);
    assert(s.motion_blur==30&&active_profile==8);
    go(MotionBlur);
    tap(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_RIGHT_SHOULDER);
    handle_key(VK_RIGHT,true,path,s);
    key(VK_RIGHT);
    assert(dirty&&s.motion_blur==60);
    tap(XINPUT_GAMEPAD_B);
    assert(!open&&s.motion_blur==30);

    key(VK_F8);
    go(MotionBlur);
    for(int i=0;i<15;i++)key(VK_RIGHT);
    assert(s.motion_blur==100);
    for(int i=0;i<14;i++)key(VK_LEFT);
    assert(s.motion_blur==0);
    key(VK_RIGHT);
    key(VK_F8);
    assert(open&&selected()==SaveCustom);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    tap(XINPUT_GAMEPAD_A);
    assert(save_failed()&&dirty&&active_profile==8);
    assert(GetPrivateProfileIntW(L"Retro",L"MotionBlur",99,path)==30);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));
    tap(XINPUT_GAMEPAD_A);
    assert(!save_failed()&&!dirty&&active_profile==custom&&custom_base==8);
    key(VK_F8);
    display_settings.load(path);
    assert(s.motion_blur==10&&display_settings.fps==(deck_renderer?30:60));
    key(VK_F8);
    assert(active_profile==custom);
    key(VK_RIGHT);
    assert(active_profile==0&&s.motion_blur==0);
    // Blur by itself remains accessible with the spatial filter set to Off.
    key(VK_DOWN);
    assert(selected()==MotionBlur);
    key(VK_RIGHT);
    assert(s.motion_blur==10&&!s.active());
    key(VK_ESCAPE);
    assert(s.motion_blur==0);
    key(VK_F8);
    key(VK_LEFT);
    assert(active_profile==custom&&s.motion_blur==10);
    go(Restore);
    tap(XINPUT_GAMEPAD_A);
    assert(!dirty&&s.motion_blur==0&&selected()==Profile);
    while(active_profile!=8)key(VK_LEFT);
    assert(s.motion_blur==30);
    go(Restore);
    tap(XINPUT_GAMEPAD_A);
    key(VK_F8);
    display_settings.load(path);
    assert(s.motion_blur==0);
    // Explicit profile values, including zero, win over legacy global values.
    assert(number(path,L"Retro",L"MotionBlur",103));
    display_settings.load(path);
    assert(s.motion_blur==100);
    assert(number(path,L"Retro",L"MotionBlur",24));
    display_settings.load(path);
    assert(s.motion_blur==20);
    assert(number(path,L"Retro",L"MotionBlur",-25));
    display_settings.load(path);
    assert(s.motion_blur==0);
    assert(DeleteFileW(path));
    puts("Per-filter motion blur: migration, steps of ten, controller, custom save, cancel, restore and reload OK");
}
