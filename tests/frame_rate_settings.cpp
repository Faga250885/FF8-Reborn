#define GEO_TEST
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;

int main(){
    assert(QueryPerformanceFrequency(&frequency));
    wchar_t folder[MAX_PATH],path[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,folder)>0);
    assert(GetTempFileNameW(folder,L"fps",0,path));
    assert(WritePrivateProfileStringW(L"Display",L"Wide",L"0",path));
    assert(WritePrivateProfileStringW(L"Retro",L"Selected",L"7",path));
    auto stored_fps=[&](){return int(GetPrivateProfileIntW(L"Display",L"FPS",-1,path));};

    for(const int saved:{-1,0,30,60,90,120,-2,47}){
        if(saved!=-1)assert(retro_panel::number(path,L"Display",L"FPS",saved));
        display_settings.load(path);
        const bool legacy=saved==60||saved==90||saved==120;
        if(deck_renderer){
            assert(display_settings.fps==(saved==30||legacy?30:0));
            assert(display_settings.fps_setting==display_settings.fps);
            assert(stored_fps()==(legacy?30:saved));
        }else{
            const bool valid=saved==0||saved==30||saved==60||saved==90||saved==120;
            assert(display_settings.fps==(valid?saved:60));
            assert(display_settings.fps_setting==(valid?saved:DisplaySettings::auto_fps));
            assert(stored_fps()==(saved==-2?DisplaySettings::auto_fps:saved));
        }
    }

    assert(retro_panel::number(path,L"Display",L"FPS",0));
    display_settings.load(path);
    retro_panel::handle_key(VK_F8,path,display_settings.retro);
    retro_panel::row=retro_panel::FPS;
    auto step=[&](UINT key,int setting,int effective){
        can_present=true;
        retro_panel::handle_key(key,path,display_settings.retro);
        assert(!retro_panel::save_failed()&&!can_present);
        assert(display_settings.fps_setting==setting&&display_settings.fps==effective);
        assert(stored_fps()==setting);
        if(effective)assert(output_period()==1./effective);
    };
    if(deck_renderer){
        step(VK_LEFT,30,30);step(VK_RIGHT,0,0);step(VK_RIGHT,30,30);
    }else{
        constexpr int automatic=DisplaySettings::auto_fps;
        display_settings.monitor_refresh(75);
        step(VK_LEFT,automatic,60);
        for(const auto [hz,expected]:std::array<std::pair<unsigned,int>,12>{{
            {0,60},{1,60},{50,30},{59,60},{60,60},{75,60},{89,90},
            {90,90},{100,90},{119,120},{120,120},{144,120}}}){
            display_settings.monitor_refresh(hz);
            assert(display_settings.fps==expected&&output_period()==1./expected);
            assert(display_settings.fps_setting==automatic&&stored_fps()==automatic);
        }
        display_settings.load(path);
        assert(display_settings.fps_setting==automatic&&display_settings.fps==120);
        step(VK_RIGHT,0,0);step(VK_RIGHT,30,30);step(VK_RIGHT,60,60);
        step(VK_RIGHT,90,90);step(VK_LEFT,60,60);step(VK_RIGHT,90,90);
        step(VK_RIGHT,120,120);step(VK_RIGHT,automatic,120);step(VK_LEFT,120,120);
        display_settings.monitor_refresh(60);
        assert(display_settings.fps==120&&stored_fps()==120);
    }
    display_settings.load(path);
    assert(display_settings.fps==(deck_renderer?30:120));
    assert(!display_settings.wide&&GetPrivateProfileIntW(L"Retro",L"Selected",-1,path)==7);
    assert(DeleteFileW(path));
    std::puts(deck_renderer?"Deck Original/30 settings and migration OK":"Windows Auto/Original/30/60/90/120 settings and migration OK");
}
