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
        assert(!choosing_section);
        for(int i=0;i<3;++i){
            const auto order=order_for(section_view());
            if(std::find(order.begin(),order.end(),id)!=order.end())break;
            key(VK_TAB);key(VK_RETURN);
        }
        for(int i=0;i<=RowCount&&selected()!=id;++i)tap(XINPUT_GAMEPAD_DPAD_DOWN);
        assert(selected()==id);
    };
    auto read_int=[&](const wchar_t* group,const wchar_t* name){
        return int(GetPrivateProfileIntW(group,name,UINT_MAX,path));
    };
    auto discard=[&](){
        assert(open&&capture_pad&&dirty&&confirm_close&&close_choice==CloseChoice::KeepEditing);
        tap(XINPUT_GAMEPAD_DPAD_UP);assert(close_choice==CloseChoice::Discard);
        tap(XINPUT_GAMEPAD_A);assert(!open&&!capture_pad&&!dirty&&!confirm_close);
    };
    constexpr int strengths[]={0,0,30,10,20,30,10,10,20,10,20,40};
    static_assert(std::size(strengths)==profile_count);
    read();
    assert(s.motion_blur==20);
    assert(!panel_key(VK_TAB));
    const auto initial=s;
    const bool initial_wide=display_settings.wide;
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(open&&capture_pad&&selected()==Aspect&&section_view()==Section::Display&&active_profile==8);
    assert(panel_key(VK_TAB));
    for(const Row setting:display_order){
        assert(section_view()==Section::Display&&selected()==setting);
        tap(XINPUT_GAMEPAD_DPAD_DOWN);
    }
    assert(selected()==Aspect);
    key(VK_UP);assert(choosing_section&&pending_section==Section::Display);
    key(VK_DOWN);assert(!choosing_section&&selected()==Aspect);
    key(VK_DOWN);key(VK_UP);assert(selected()==Aspect);
    row=InstalledVersion;
    static int updates=0;
    update_status=[](){return update_available;};update_action=[](){++updates;};
    key(VK_LEFT);key(VK_RIGHT);key(VK_RETURN);tap(XINPUT_GAMEPAD_A);
    assert(selected()==InstalledVersion&&section_view()==Section::Display&&updates==0&&!dirty);
    assert(same_filter(initial,s)&&display_settings.fps==fps&&display_settings.wide==initial_wide);
    key(VK_UP);assert(selected()==ModUpdate);
    key(VK_LEFT);assert(updates==0);tap(XINPUT_GAMEPAD_A);assert(updates==1);
    update_status=[](){return update_downloading;};key(VK_RETURN);assert(updates==1);
    assert(!offer_update_choice());
    static int exits=0;
    update_status=[](){return update_ready;};
    close_for_update=[](){++exits;return false;};
    key(VK_RETURN);assert(update_choice_open&&!close_update_now&&capture_pad);
    assert(!offer_update_choice()&&panel_key('W'));
    key(VK_TAB);tap(XINPUT_GAMEPAD_RIGHT_SHOULDER);
    assert(!choosing_section&&selected()==ModUpdate);
    tap(XINPUT_GAMEPAD_A);assert(!update_choice_open&&open&&exits==0);
    key(VK_RETURN);tap(XINPUT_GAMEPAD_DPAD_UP);tap(XINPUT_GAMEPAD_A);
    assert(update_choice_open&&update_close_failed&&!close_update_now&&exits==1&&open);
    tap(XINPUT_GAMEPAD_B);assert(!update_choice_open&&open);
    assert(offer_update_choice());tap(XINPUT_GAMEPAD_DPAD_DOWN);
    update_status=[](){return update_error;};key(VK_RETURN);
    assert(update_choice_open&&update_close_failed&&!close_update_now&&exits==1);
    key(VK_F8);assert(!update_choice_open&&open);
    update_status=[](){return update_ready;};close_for_update=[](){++exits;return true;};
    key(VK_RETURN);tap(XINPUT_GAMEPAD_DPAD_UP);tap(XINPUT_GAMEPAD_A);
    assert(!open&&!update_choice_open&&!capture_pad&&exits==2);
    assert(!offer_update_choice());
    key(VK_F8);assert(!offer_update_choice());
    go(ModUpdate);
    close_for_update=nullptr;
    update_status=nullptr;update_action=nullptr;
    key(VK_DOWN);assert(selected()==InstalledVersion);
    key(VK_DOWN);assert(selected()==Aspect);
    pad_input(pad,0,XINPUT_GAMEPAD_LEFT_SHOULDER,++tick,path,s);
    assert(choosing_section&&pending_section==Section::MenuSettings&&section_view()==Section::Display);
    pad_input(pad,0,XINPUT_GAMEPAD_LEFT_SHOULDER,tick+500,path,s);
    assert(choosing_section&&pending_section==Section::MenuSettings&&selected()==Aspect);
    tick+=500;tap(0);
    tap(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER);
    assert(choosing_section&&pending_section==Section::MenuSettings);
    tap(XINPUT_GAMEPAD_RIGHT_SHOULDER);assert(pending_section==Section::Display);
    key(VK_LEFT);assert(pending_section==Section::MenuSettings);
    tap(XINPUT_GAMEPAD_DPAD_RIGHT);assert(pending_section==Section::Display);
    key(VK_TAB);assert(pending_section==Section::ScreenFilter);
    assert(display_settings.wide==initial_wide&&selected()==Aspect&&!dirty);
    tap(XINPUT_GAMEPAD_A);assert(!choosing_section&&section_view()==Section::ScreenFilter&&selected()==Profile);
    tap(XINPUT_GAMEPAD_DPAD_UP);assert(choosing_section&&pending_section==Section::ScreenFilter);
    tap(XINPUT_GAMEPAD_DPAD_DOWN);
    assert(selected()==Profile&&active_profile==8&&same_filter(initial,s)&&!dirty);
    go(Glow);key(VK_RIGHT);
    assert(dirty&&s.glow==10);
    key(VK_TAB);assert(choosing_section&&pending_section==Section::MenuSettings&&selected()==Glow);
    key(VK_ESCAPE);assert(!choosing_section&&selected()==Glow&&s.glow==10&&dirty);
    key(VK_TAB);tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);
    assert(confirm_close&&choosing_section&&dirty&&pending_section==Section::MenuSettings);
    key(VK_RETURN);assert(!confirm_close&&choosing_section&&selected()==Glow);
    tap(XINPUT_GAMEPAD_B);assert(!choosing_section&&selected()==Glow&&dirty&&s.glow==10);
    key(VK_TAB);key(VK_RETURN);assert(selected()==MenuSize&&section_view()==Section::MenuSettings);
    tap(XINPUT_GAMEPAD_DPAD_RIGHT);
    const int larger_scale=read_int(L"Menu",L"ScalePercent");
    assert(larger_scale==110&&display_settings.menu_scale==110&&dirty&&s.glow==10);
    tap(XINPUT_GAMEPAD_DPAD_LEFT);
    assert(read_int(L"Menu",L"ScalePercent")==100&&dirty);
    key(VK_LEFT);assert(read_int(L"Menu",L"ScalePercent")<100);
    tap(XINPUT_GAMEPAD_A);assert(read_int(L"Menu",L"ScalePercent")==100&&dirty);
    go(Aspect);key(VK_RIGHT);
    assert(read_int(L"Display",L"Wide")==!initial_wide&&dirty&&s.glow==10);
    key(VK_LEFT);
    tap(XINPUT_GAMEPAD_RIGHT_SHOULDER);assert(choosing_section&&pending_section==Section::ScreenFilter);
    tap(XINPUT_GAMEPAD_A);assert(selected()==Profile&&section_view()==Section::ScreenFilter);
    tap(XINPUT_GAMEPAD_B);assert(confirm_close&&open&&dirty&&capture_pad);
    assert(close_choice==CloseChoice::KeepEditing&&panel_key('W'));
    key(VK_TAB);tap(XINPUT_GAMEPAD_LEFT_SHOULDER);tap(XINPUT_GAMEPAD_RIGHT_SHOULDER);
    key(VK_LEFT);assert(section_view()==Section::ScreenFilter&&selected()==Profile&&close_choice==CloseChoice::KeepEditing);
    tap(XINPUT_GAMEPAD_A);assert(open&&dirty&&!confirm_close&&s.glow==10);
    key(VK_ESCAPE);tap(XINPUT_GAMEPAD_B);assert(open&&dirty&&!confirm_close&&s.glow==10);
    key(VK_F8);discard();assert(same_filter(initial,s)&&!panel_key('W'));
    key(VK_F8);assert(selected()==Profile&&section_view()==Section::ScreenFilter);
    for(int i=0;i<profile_count;++i){
        key(VK_RIGHT);
        assert(s.motion_blur==strengths[active_profile]&&selected()==Profile);
        assert(s.tv_enabled==(active_profile>=9));
        RetroSettings before=s;
        read();
        assert(same_filter(s,before));
    }
    assert(s.glow==8&&s.lines==320);
    go(TVSimulation);key(VK_DOWN);assert(selected()==SaveCustom);
    tap(XINPUT_GAMEPAD_DPAD_UP);assert(selected()==TVSimulation);
    key(VK_RETURN);assert(s.tv_active()&&s.selected_tube()==RetroSettings::aperture_grille);
    tap(XINPUT_GAMEPAD_A);assert(!s.tv_active()&&!dirty);
    tap(XINPUT_GAMEPAD_DPAD_RIGHT);go(TVScreen);key(VK_RIGHT);
    assert(s.selected_tube()==RetroSettings::slot_mask);
    go(TVSimulation);key(VK_LEFT);key(VK_RIGHT);go(TVScreen);
    assert(s.selected_tube()==RetroSettings::slot_mask);
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
    go(Lines);key(VK_RIGHT);assert(s.lines==325);
    tap(XINPUT_GAMEPAD_DPAD_LEFT);assert(s.lines==320);
    key(VK_F8);discard();
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
    key(VK_F8);go(Profile);
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
    assert(confirm_close&&capture_pad&&s.motion_blur==40);
    discard();assert(s.motion_blur==30);
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
    key(VK_F8);assert(close_choice==CloseChoice::KeepEditing&&!save_failed());
    tap(XINPUT_GAMEPAD_DPAD_DOWN);assert(close_choice==CloseChoice::Save);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    tap(XINPUT_GAMEPAD_A);assert(open&&capture_pad&&confirm_close&&save_failed()&&dirty);
    assert(read_int(section(custom).c_str(),L"Mode")==-1);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));
    tap(XINPUT_GAMEPAD_A);assert(!open&&!capture_pad&&!confirm_close);
    assert(!dirty&&!save_failed()&&active_profile==custom&&custom_base==8);
    read();
    assert(s.motion_blur==0&&display_settings.fps==fps);

    key(VK_F8);
    go(MotionBlur);
    key(VK_RIGHT);
    go(Profile);
    key(VK_RIGHT);
    assert(active_profile==0&&s.motion_blur==0&&selected()==Profile);
    // A disabled filter skips its inactive sliders in both directions.
    key(VK_DOWN);assert(selected()==SaveCustom);
    key(VK_UP);assert(selected()==Profile);
    key(VK_UP);assert(choosing_section&&pending_section==Section::ScreenFilter);
    key(VK_DOWN);assert(!choosing_section&&selected()==Profile&&section_view()==Section::ScreenFilter);
    tap(XINPUT_GAMEPAD_DPAD_LEFT);
    assert(active_profile==custom&&s.motion_blur==0);
    go(Restore);
    key(VK_RETURN);
    assert(dirty&&selected()==Profile&&s.motion_blur==20);
    assert(read_int(section(custom).c_str(),L"MotionBlur")==0);
    tap(XINPUT_GAMEPAD_B);discard();
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
    key(VK_F8);go(Profile);
    while(active_profile!=8)key(VK_LEFT);
    key(VK_F8);
    assert(number(path,section(8).c_str(),L"MotionBlur",0));
    assert(number(path,L"Retro",L"MotionBlur",0));
    read();
    assert(s.motion_blur==0);
    for(int target:{315,325}){
        key(VK_F8);go(Lines);
        if(target==315)tap(XINPUT_GAMEPAD_DPAD_LEFT);
        else{key(VK_RIGHT);tap(XINPUT_GAMEPAD_DPAD_RIGHT);}
        assert(s.lines==target);
        go(SaveCustom);tap(XINPUT_GAMEPAD_A);key(VK_F8);read();
        assert(s.lines==target&&read_int(section(custom).c_str(),L"Lines")==target);
    }
    // Older manually edited files may contain numbers between slider stops.
    for(auto [saved,expected]:{std::pair{118,120},{317,315},{318,320},{360,360},{724,720}}){
        assert(number(path,L"OldLines",L"Lines",saved));
        RetroSettings old;old.load(path,L"OldLines");
        assert(old.lines==expected);
    }
    assert(DeleteFileW(path));
    // Old installs cache the factory selection and can also have a deliberately
    // saved Custom with the exact same number of lines.
    assert(number(path,L"Retro",L"Selected",8)&&number(path,L"Retro",L"Mode",2));
    assert(number(path,L"Retro",L"Lines",320)&&number(path,L"Retro",L"CustomBase",8));
    assert(number(path,L"RetroPerfil9",L"Mode",2)&&number(path,L"RetroPerfil9",L"Lines",320));
    read();
    assert(!open&&active_profile==8&&s.auto_lines&&s.effective_lines(1280,800,base_profile())==265);
    key(VK_F8);go(Profile);
    while(active_profile!=custom)key(VK_RIGHT);
    assert(!s.auto_lines&&s.lines==320);
    key(VK_F8);read();
    assert(active_profile==custom&&base_profile()==8&&!s.auto_lines);
    assert(s.effective_lines(1280,800,base_profile())==320&&read_int(L"Retro",L"LinesAuto")==0);

    // Exercise the slider against a real window so its first step starts at
    // the value visible on the handheld, not the desktop factory value.
    HWND window=CreateWindowW(L"STATIC",L"Filter settings",WS_POPUP,0,0,1280,800,
                              nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(window);attached=window;
    tap(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK);go(Profile);
    while(active_profile!=8)key(VK_LEFT);
    go(Lines);key(VK_RIGHT);
    assert(dirty&&!s.auto_lines&&s.lines==270);
    tap(XINPUT_GAMEPAD_DPAD_LEFT);
    assert(dirty&&!s.auto_lines&&s.lines==265);
    go(SaveCustom);tap(XINPUT_GAMEPAD_A);
    key(VK_F8);read();
    assert(!s.auto_lines&&s.lines==265&&read_int(L"RetroPerfil9",L"LinesAuto")==0);
    assert(SetWindowPos(window,nullptr,0,0,2560,1440,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    assert(s.effective_lines(2560,1440,base_profile())==265);
    key(VK_F8);go(Lines);tap(XINPUT_GAMEPAD_A);
    assert(dirty&&s.auto_lines&&s.effective_lines(2560,1440,base_profile())==320);
    key(VK_LEFT);
    assert(!s.auto_lines&&s.lines==315);
    key(VK_RETURN);
    assert(s.auto_lines&&s.effective_lines(2560,1440,base_profile())==320);
    go(Glow);key(VK_RIGHT);
    assert(s.auto_lines);
    go(SaveCustom);key(VK_RETURN);key(VK_F8);read();
    assert(active_profile==custom&&base_profile()==8&&read_int(L"RetroPerfil9",L"LinesAuto")==1);
    assert(s.auto_lines&&s.effective_lines(1280,800,base_profile())==265&&s.glow==10);
    key(VK_F8);go(Lines);key(VK_RIGHT);tap(XINPUT_GAMEPAD_B);discard();
    assert(!open&&s.auto_lines&&s.effective_lines(1280,800,base_profile())==265);
    attached=nullptr;assert(DestroyWindow(window));

    // An old saved factory override is explicit even at the default number.
    assert(DeleteFileW(path));
    assert(number(path,L"Retro",L"Selected",8)&&number(path,L"Retro",L"Mode",2));
    assert(number(path,L"Retro",L"Lines",320));
    assert(number(path,L"RetroPerfil8",L"Mode",2)&&number(path,L"RetroPerfil8",L"Lines",320));
    read();assert(active_profile==8&&!s.auto_lines&&s.lines==320);
    key(VK_F8);go(Restore);key(VK_RETURN);
    assert(dirty&&s.auto_lines&&s.effective_lines(1280,800,base_profile())==265);
    key(VK_ESCAPE);discard();assert(!s.auto_lines&&s.lines==320);
    assert(DeleteFileW(path));
    // Resolve the base even before F8, e.g. a saved Auto HD Blend on a dock.
    assert(number(path,L"Retro",L"Selected",9)&&number(path,L"Retro",L"CustomBase",7));
    assert(number(path,L"Retro",L"Mode",1)&&number(path,L"Retro",L"Lines",320));
    assert(number(path,L"Retro",L"LinesAuto",1));
    read();assert(active_profile==custom&&base_profile()==7&&s.auto_lines);
    assert(s.effective_lines(3840,2160,base_profile())==720);
    assert(DeleteFileW(path));
    // Legacy TV controls used to live in Display. Preserve an explicit model
    // as part of the active filter when switching to another profile.
    assert(number(path,L"Display",L"TVScreen",3)&&number(path,L"Display",L"PhosphorStrength",60));
    read();assert(s.tv_active()&&s.selected_tube()==RetroSettings::dot_triad);
    key(VK_F8);go(Profile);key(VK_RIGHT);key(VK_LEFT);
    assert(active_profile==8&&s.tv_active()&&s.phosphor_strength==60);
    assert(read_int(section(8).c_str(),L"TVEnabled")==1);
    go(TVSimulation);tap(XINPUT_GAMEPAD_A);
    go(SaveCustom);tap(XINPUT_GAMEPAD_A);key(VK_F8);read();
    assert(active_profile==custom&&!s.tv_active()&&s.tv_screen==3&&s.phosphor_strength==60);
    assert(read_int(L"Retro",L"TVEnabled")==0&&read_int(section(custom).c_str(),L"TVEnabled")==0);
    key(VK_F8);go(TVSimulation);key(VK_RIGHT);go(TVScreen);key(VK_RIGHT);
    assert(s.tv_active()&&s.selected_tube()==RetroSettings::aperture_grille);
    key(VK_ESCAPE);discard();assert(!s.tv_enabled&&s.tv_screen==3);
    // Older Custom profiles can retain a disabled filter method and nonzero
    // effect values. Every dependent row must still be skipped.
    assert(number(path,L"Retro",L"Mode",0)&&number(path,section(custom).c_str(),L"Mode",0));
    assert(number(path,L"Retro",L"TVEnabled",1)&&number(path,L"Retro",L"MotionBlur",30));
    read();assert(s.tv_enabled&&s.motion_blur==30&&!s.tv_active());
    key(VK_F8);go(Profile);tap(XINPUT_GAMEPAD_DPAD_DOWN);assert(selected()==Restore);
    key(VK_RETURN);assert(s.filter_enabled()&&selected()==Profile&&!s.tv_enabled);
    key(VK_ESCAPE);discard();assert(!s.filter_enabled());
    assert(DeleteFileW(path));
    // Older versions allowed replacing a named profile's rendering method.
    // The Off name always bypasses filters, even with stale live and slot data.
    assert(number(path,L"Retro",L"Selected",0)&&number(path,L"Retro",L"Mode",2));
    assert(number(path,L"Retro",L"MotionBlur",80)&&number(path,L"Retro",L"TVEnabled",1));
    assert(number(path,L"RetroPerfil0",L"Mode",2)&&number(path,L"RetroPerfil1",L"Mode",2));
    assert(number(path,L"RetroPerfil1",L"Dither",40));
    assert(number(path,L"Menu",L"ScalePercent",120));
    read();assert(active_profile==0&&!s.filter_enabled()&&!s.tv_active());
    assert(display_settings.menu_scale==120);
    key(VK_F8);go(Profile);key(VK_RIGHT);assert(active_profile==1&&s.mode==1&&s.dither==40);
    key(VK_LEFT);assert(active_profile==0&&!s.filter_enabled()&&!s.tv_active()&&s.motion_blur==80);
    key(VK_F8);assert(!open);
    assert(DeleteFileW(path));
    // Quick cycling starts at a custom filter's base, leaves its edits on
    // disk, and wraps directly between Off and the last named preset.
    auto saved_custom=profiles[8];saved_custom.motion_blur=90;
    auto saved_blend=profiles[7];saved_blend.dither=80;
    assert(write_filter(path,section(custom).c_str(),saved_custom));
    assert(write_filter(path,section(7).c_str(),saved_blend));
    for(int direction:{1,-1}){
        assert(write_filter(path,L"Retro",saved_custom)&&number(path,L"Retro",L"Selected",disk_id(custom)));
        assert(number(path,L"Retro",L"CustomBase",disk_id(8)));
        read();assert(active_profile==custom&&same_filter(s,saved_custom));
        for(int i=1;i<=profile_count+1;++i){
            const int expected=(8+i*direction+2*profile_count)%profile_count;
            cycle_filter_shortcut(path,s,direction);
            assert(active_profile==expected&&quick_notice.profile==expected);
            assert(read_int(L"Retro",L"Selected")==disk_id(expected));
            if(expected==0)assert(!s.filter_enabled());
            if(expected==7)assert(same_filter(s,saved_blend));
            assert(read_int(section(custom).c_str(),L"MotionBlur")==90);
        }
        const auto last=s;
        read();assert(same_filter(s,last));
    }
    const int last_profile=active_profile;
    const auto last=s;
    key(VK_F8);assert(quick_notice.profile==-1);
    cycle_filter_shortcut(path,s,+1);assert(active_profile==last_profile&&same_filter(s,last));
    key(VK_F8);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    cycle_filter_shortcut(path,s,-1);
    assert(save_failed()&&quick_notice.profile==-1&&active_profile==last_profile&&same_filter(s,last));
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));
    quick_notice.show(last_profile,GetTickCount64()-2250);
    cycle_filter_shortcut(path,s,+1);assert(!save_failed()&&quick_notice.opacity(GetTickCount64())==1.f);
    assert(DeleteFileW(path));
    puts("Profile shortcuts, TV and Off controls, menu scale, section focus, save/discard and update close choices passed.");
}
