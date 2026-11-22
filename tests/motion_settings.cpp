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
    for(const auto& control:motion_controls)assert((display_settings.*control.setting)==0);

    Pad pad;
    ULONGLONG tick=0;
    auto tap=[&](WORD buttons){
        pad_input(pad,0,buttons,++tick,path,s);
        pad_input(pad,0,0,++tick,path,s);
    };
    auto key=[&](UINT k){handle_key(k,false,path,s);};
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(open&&selected()==Profile);
    assert((visual_index(MotionBackground)<visible_rows)==deck_renderer);
    const int motion_span=retro_panel_draw::row_top(CRTPersistence,true)-retro_panel_draw::row_top(Motion3D,true);
    assert(motion_span==retro_panel_draw::row_height*(deck_renderer?2:1));
    for(int id=CRTPersistence;id>=Motion3D;--id){
        if(!deck_renderer&&id==MotionBackground)continue;
        tap(XINPUT_GAMEPAD_DPAD_UP);
        assert(selected()==id);
        tap(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_RIGHT_SHOULDER);
        key(VK_RIGHT);
        const auto& control=motion_controls[id-Motion3D];
        assert(display_settings.*control.setting==25&&!dirty);
        assert(GetPrivateProfileIntW(L"Display",control.key,0,path)==25);
    }
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(!open&&GetPrivateProfileIntW(L"Retro",L"Selected",0,path)==8);

    key(VK_F8);
    key(VK_RIGHT); // Switching a screen-filter preset leaves global motion values intact.
    const DisplaySettings expected=display_settings;
    for(const auto& control:motion_controls){
        display_settings.*control.setting=0;
    }
    key(VK_ESCAPE);
    display_settings.load(path);
    for(const auto& control:motion_controls)assert(display_settings.*control.setting==expected.*control.setting);
    assert(display_settings.fps==(deck_renderer?30:60));

    // An unsuccessful save must leave the live renderer and stored value in sync.
    key(VK_F8);
    key(VK_UP);
    assert(selected()==CRTPersistence);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    key(VK_RIGHT);
    assert(save_failed()&&display_settings.crt_persistence==25);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));
    for(int i=0;i<7;++i)key(VK_LEFT);
    assert(!save_failed()&&display_settings.crt_persistence==0);
    assert(display_settings.motion_3d==25&&display_settings.motion_background==(deck_renderer?25:0));
    key(VK_ESCAPE);

    assert(number(path,L"Display",L"MotionBlur3D",-25));
    assert(number(path,L"Display",L"MotionBlurBackground",135));
    display_settings.load(path);
    assert(display_settings.motion_3d==0&&display_settings.motion_background==100);
    assert(DeleteFileW(path));
    puts("Live motion controls: keyboard, gamepad, independent saves, limits, and preset changes OK");
}
