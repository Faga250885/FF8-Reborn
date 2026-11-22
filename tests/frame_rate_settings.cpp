#define GEO_TEST
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;

int main(){
    wchar_t folder[MAX_PATH],path[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,folder)>0);
    assert(GetTempFileNameW(folder,L"fps",0,path));
    assert(WritePrivateProfileStringW(L"Display",L"Wide",L"0",path));
    assert(WritePrivateProfileStringW(L"Retro",L"Selected",L"7",path));
    auto stored_fps=[&](){return int(GetPrivateProfileIntW(L"Display",L"FPS",-1,path));};

    for(const int saved:{-1,0,30,60,90,120,47}){
        if(saved!=-1)assert(retro_panel::number(path,L"Display",L"FPS",saved));
        display_settings.load(path);
        const bool legacy=saved==60||saved==90||saved==120;
        if(deck_renderer){
            assert(display_settings.fps==(saved==30||legacy?30:0));
            assert(!display_settings.automatic_fps);
            assert(stored_fps()==(legacy?30:saved));
        }else{
            assert(display_settings.fps==(saved==0||saved==120?saved:60));
            assert(display_settings.automatic_fps==(saved!=0&&saved!=60&&saved!=120));
            assert(stored_fps()==saved);
        }
    }

    assert(retro_panel::number(path,L"Display",L"FPS",0));
    display_settings.load(path);
    retro_panel::handle_key(VK_F8,path,display_settings.retro);
    retro_panel::row=retro_panel::FPS;
    auto step=[&](UINT key,int expected){
        can_present=true;
        retro_panel::handle_key(key,path,display_settings.retro);
        assert(!retro_panel::save_failed()&&!can_present);
        assert(display_settings.fps==expected&&!display_settings.automatic_fps);
        assert(stored_fps()==expected);
    };
    if(deck_renderer){
        step(VK_LEFT,30);step(VK_RIGHT,0);step(VK_RIGHT,30);
    }else{
        step(VK_LEFT,120);step(VK_RIGHT,0);step(VK_RIGHT,60);step(VK_RIGHT,120);
    }
    display_settings.load(path);
    assert(display_settings.fps==(deck_renderer?30:120));
    assert(!display_settings.wide&&GetPrivateProfileIntW(L"Retro",L"Selected",-1,path)==7);
    assert(DeleteFileW(path));
    std::puts(deck_renderer?"Deck Original/30 settings and migration OK":"Windows Original/60/120 settings OK");
}
