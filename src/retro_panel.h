// Included inside namespace geo. The window procedure and the renderer share
// the GLFW window thread. Only keys delivered to the game's window are used.
static void log(const char*);
namespace retro_panel {
static bool open=false,dirty=false,failed=false;
enum class CloseChoice { Save, Discard, KeepEditing };
static bool confirm_close=false;
static CloseChoice close_choice=CloseChoice::KeepEditing;
enum Row { Profile, Lines, Pixels, Scan, Mask, Glow, Dither, Edges, MotionBlur, Map, Aspect, FPS, Sides, Counter, TVSimulation, TVScreen, PhosphorStrength, PhosphorSharpness, SaveCustom, Restore, ModUpdate, InstalledVersion, MenuSize, MenuShortcut, PreviousFilterShortcut, NextFilterShortcut, RowCount };
enum class Section { Display, ScreenFilter, MenuSettings };
static constexpr Section sections[]={Section::Display,Section::ScreenFilter,Section::MenuSettings};
static Section page=Section::Display;
static bool choosing_section=false;
static Section pending_section=Section::Display;
static int row=Aspect;

static HWND attached=nullptr;
static RetroSettings* current=nullptr;
static std::wstring ini;
static bool claimed[256]={};
static std::atomic<bool> capture_pad{false};

static bool is_open(){return open;}
static bool save_failed(){return failed;}
static int selected(){return row;}
static Section section_view(){return page;}
enum UpdateState { update_idle, update_checking, update_current, update_available,
                   update_downloading, update_ready, update_error, update_verifying, update_saving };
static UpdateState (*update_status)()=nullptr;
static void (*update_action)()=nullptr;
static void (*update_detail)(wchar_t* text,size_t capacity)=nullptr;
static void (*update_bytes)(unsigned& received,unsigned& total)=nullptr;
static bool (*close_for_update)()=nullptr;
static bool update_choice_open=false,update_choice_seen=false,close_update_now=false,update_close_failed=false;
static bool update_actionable(UpdateState state){
    return state==update_idle||state==update_current||state==update_available||state==update_error||state==update_ready;
}
static constexpr int RetroSettings::* fields[]={
    &RetroSettings::lines,&RetroSettings::pixelation,&RetroSettings::scanlines,&RetroSettings::mask,
    &RetroSettings::glow,&RetroSettings::dither,&RetroSettings::edge_softness,&RetroSettings::motion_blur
};
static constexpr RetroSettings profiles[]={
    {},{1,240,100,0,0,0,100,0,true},
    RetroSettings::with_blur({2,320,85,35,20,10,20,0},30),
    RetroSettings::with_blur({2,240,100,52,24,4,100,8,true},10),
    RetroSettings::with_blur({1,320,100,0,0,0,20,65},20),
    RetroSettings::with_blur({2,240,100,23,14,8,100,42,true},30),
    RetroSettings::with_blur({1,320,80,0,0,0,30,32},10),
    RetroSettings::with_blur({1,320,32,0,0,0,6,70,false,true},10),
    RetroSettings::recommended(),
    RetroSettings::with_blur({2,240,90,68,65,18,12,26,true,false,RetroSettings::aperture_grille},10),
    RetroSettings::with_blur({2,240,85,54,62,25,8,46,true,false,RetroSettings::slot_mask},20),
    RetroSettings::with_blur({2,240,80,44,70,32,6,60,true,false,RetroSettings::dot_triad},40)
};
static constexpr int profile_count=sizeof(profiles)/sizeof(profiles[0]);
static bool editable(const RetroSettings& s,int id) {
    if(id>=TVScreen&&id<=PhosphorSharpness)return s.tv_active();
    if(id==TVSimulation||(id>=Lines&&id<=MotionBlur)){
        if(!s.filter_enabled())return false;
        return id<Scan||id>Glow||s.mode==2;
    }
    return true;
}
static int preset(const RetroSettings& s) {
    if(s.mode!=1 && s.mode!=2)return 0;
    for(int p=1;p<profile_count;p++){
        const auto& ref=profiles[p];
        if(s.mode!=ref.mode||s.ps1_raster!=ref.ps1_raster||s.phosphors!=ref.phosphors)continue;
        bool same=true;
        for(int i=0;i<7;i++){
            if(editable(s,i+1) && s.*fields[i]!=ref.*fields[i]){same=false;break;}
        }
        if(same)return p;
    }
    return s.mode==1?profile_count:profile_count+1;
}
static constexpr Row display_order[]={Aspect,FPS,Map,Sides,Counter,ModUpdate,InstalledVersion};
static constexpr Row filter_order[]={Profile,Lines,Pixels,Edges,Dither,MotionBlur,Scan,Mask,Glow,TVSimulation,TVScreen,PhosphorStrength,PhosphorSharpness,Restore,SaveCustom};
static constexpr Row menu_order[]={MenuSize,MenuShortcut,PreviousFilterShortcut,NextFilterShortcut};
static_assert(std::size(display_order)+std::size(filter_order)+std::size(menu_order)==RowCount,"every available setting must be reachable");
struct RowOrder {
    const Row* rows;
    int count;
    constexpr const Row* begin() const {return rows;}
    constexpr const Row* end() const {return rows+count;}
};
static constexpr RowOrder order_for(Section view){
    if(view==Section::Display)return {display_order,int(std::size(display_order))};
    if(view==Section::ScreenFilter)return {filter_order,int(std::size(filter_order))};
    return {menu_order,int(std::size(menu_order))};
}
static constexpr int custom=profile_count;
struct ShortcutSetting {
    const wchar_t* key;
    int factory;
    std::atomic<int> DisplaySettings::* member;
};
static constexpr ShortcutSetting shortcuts[]={
    {L"OpenShortcut",controller_bindings::menu,&DisplaySettings::menu_shortcut},
    {L"PreviousFilterShortcut",controller_bindings::previous,&DisplaySettings::previous_filter_shortcut},
    {L"NextFilterShortcut",controller_bindings::next,&DisplaySettings::next_filter_shortcut}
};
static void load_shortcuts(const wchar_t* path){
    std::array<int,std::size(shortcuts)> values{};
    for(size_t i=0;i<values.size();++i){
        const auto& def=shortcuts[i];
        const int saved=int(GetPrivateProfileIntW(L"Menu",def.key,def.factory,path));
        values[i]=controller_bindings::valid(saved)?saved:def.factory;
    }
    // An older or hand-edited INI must not dispatch two actions on the same press.
    if(values[0]==values[1]||values[0]==values[2]||values[1]==values[2]){
        for(size_t i=0;i<values.size();++i)values[i]=shortcuts[i].factory;
    }
    for(size_t i=0;i<values.size();++i)display_settings.*shortcuts[i].member=values[i];
}
static std::wstring shortcut_label(unsigned mask){
    std::wstring name;
    for(auto button:controller_bindings::buttons)if(mask&button.bit){
        if(!name.empty())name+=L" + ";
        name+=button.label;
    }
    return name;
}
static bool recording_shortcut=false,shortcut_too_many=false,shortcut_conflict=false;
static int shortcut_target=MenuShortcut,shortcut_progress=0,recording_pad=-1;
static unsigned shortcut_candidate=0,recording_epoch=0;
static ULONGLONG shortcut_since=0;
static std::wstring shortcut_candidate_label(){return shortcut_label(shortcut_candidate);}
static void start_recording(int target){
    recording_shortcut=true;shortcut_target=target;shortcut_candidate=0;shortcut_progress=0;
    shortcut_too_many=false;shortcut_conflict=false;failed=false;recording_pad=-1;
    ++recording_epoch;
}
static const wchar_t* const names[]={L"Off",L"PS1 Digital",L"Soft CRT",L"PS1 · RGB Monitor",L"PS1 Backgrounds",
    L"PS1 · CRT TV",L"PS1 Sharp",L"HD Blend",L"FF8 Reborn (recommended)",L"Sony Trinitron KV-27FS100",
    L"RCA Precision In-Line",L"RCA CT-100",L"Custom"};
static const wchar_t* profile_name(int id){return menu_locale::text(names[id]);}
static int active_profile=8,custom_base=8;
static bool custom_exists=false,migrate_custom=false;
static int migrated_slot=-1;
static RetroSettings slots[custom+1];
static int base_profile(){return active_profile==custom?custom_base:active_profile;}
static bool same_filter(const RetroSettings& a,const RetroSettings& b){
    if(a.mode!=b.mode||a.ps1_raster!=b.ps1_raster||a.hd_blend!=b.hd_blend||a.phosphors!=b.phosphors||a.auto_lines!=b.auto_lines)return false;
    if(a.tv_enabled!=b.tv_enabled||a.tv_screen!=b.tv_screen||a.phosphor_strength!=b.phosphor_strength||a.phosphor_sharpness!=b.phosphor_sharpness)return false;
    for(auto field:fields)if((field!=&RetroSettings::lines||!a.auto_lines)&&a.*field!=b.*field)return false;
    return true;
}
static bool can_restore(const RetroSettings& s){return !same_filter(s,profiles[base_profile()]);}
static bool visible_row(int id,const RetroSettings& s){return id!=Restore||can_restore(s);}
static void focus_section(Section next){
    choosing_section=false;
    page=next;
    row=order_for(page).rows[0];
}
static void change_section(int step){
    if(confirm_close||update_choice_open)return;
    constexpr int count=int(std::size(sections));
    const auto from=choosing_section?pending_section:page;
    pending_section=sections[(int(from)+step+count)%count];
    choosing_section=true;
}
static void move_row(int step,const RetroSettings& s){
    const auto order=order_for(page);
    int index=int(std::find(order.begin(),order.end(),row)-order.begin());
    do {
        index=(index+step+order.count)%order.count;
        row=order.rows[index];
    } while(!visible_row(row,s)||!editable(s,row));
}
// Custom has always been stored in slot 9. New factory profiles follow it on disk.
static int disk_id(int id){return id==custom?9:id>=9?id+1:id;}
static int menu_id(int id){return id==9?custom:id>9?id-1:id;}
static std::wstring section(int id){return L"RetroPerfil"+std::to_wstring(disk_id(id));}
static int selected_profile(const wchar_t* path,const RetroSettings& live){
    const int id=menu_id(int(GetPrivateProfileIntW(L"Retro",L"Selected",UINT_MAX,path)));
    return id>=0&&id<=custom?id:std::min(custom,preset(live));
}
static void read_selection(const wchar_t* path,const RetroSettings& live){
    active_profile=selected_profile(path,live);
    custom_base=menu_id(int(GetPrivateProfileIntW(L"Retro",L"CustomBase",8,path)));
    if(custom_base<0||custom_base>=custom)custom_base=8;
}
static bool has_setting(const wchar_t* path,const wchar_t* group,const wchar_t* key){
    wchar_t value[32];
    return GetPrivateProfileStringW(group,key,L"",value,std::size(value),path)>0;
}
static void update_factory_defaults(const wchar_t* path,RetroSettings& live){
    read_selection(path,live);
    const int id=active_profile;
    if(id!=custom)live.mode=profiles[id].mode;
    const auto slot=section(id);
    const bool stored_slot=has_setting(path,slot.c_str(),L"Mode");
    const bool explicit_policy=has_setting(path,L"Retro",L"LinesAuto");
    const bool saved_lines=stored_slot&&has_setting(path,slot.c_str(),L"Lines");
    const bool live_lines=has_setting(path,L"Retro",L"Lines");
    if(!has_setting(path,L"Retro",L"TVEnabled")&&has_setting(path,slot.c_str(),L"TVEnabled"))
        live.tv_enabled=GetPrivateProfileIntW(slot.c_str(),L"TVEnabled",0,path)!=0;
    if(!live_lines&&saved_lines){
        RetroSettings saved;saved.load(path,slot.c_str(),base_profile());live.lines=saved.lines;
    }
    // Browsing a factory filter in deck.6 cached its old default (zero) in
    // Retro. A separate slot identifies an explicitly saved blur override.
    if(id!=custom&&!live.motion_blur&&!has_setting(path,slot.c_str(),L"MotionBlur"))
        live.motion_blur=profiles[id].motion_blur;
    if(explicit_policy)return;
    if(!stored_slot&&(id==2||id==4||id==6||id==7)){
        const int former=id==6?360:id==7?480:240;
        if(live.lines==former)live.lines=profiles[id].lines;
    }
    // Old versions cached the selected factory profile under Retro. Only a
    // separate saved slot (or selecting Custom) records an explicit copy.
    live.auto_lines=saved_lines?GetPrivateProfileIntW(slot.c_str(),L"LinesAuto",0,path)!=0:
        !live_lines||(id!=custom&&live.lines==profiles[id].lines);
}
static void load_slots(const wchar_t* path,const RetroSettings& live){
    read_selection(path,live);
    for(int i=0;i<=custom;i++){
        slots[i]=profiles[i==custom?custom_base:i];
        const auto key=section(i);
        const bool exists=GetPrivateProfileIntW(key.c_str(),L"Mode",UINT_MAX,path)!=UINT_MAX;
        if(exists){
            slots[i].load(path,key.c_str(),i==custom?custom_base:i);
            if(i!=custom)slots[i].mode=profiles[i].mode;
            if(!has_setting(path,key.c_str(),L"LinesAuto")&&has_setting(path,key.c_str(),L"Lines"))
                slots[i].auto_lines=false;
            if(i!=custom&&GetPrivateProfileIntW(key.c_str(),L"MotionBlur",UINT_MAX,path)==UINT_MAX)
                slots[i].motion_blur=profiles[i].motion_blur;
        }
        if(i==custom)custom_exists=exists;
    }
    migrate_custom=active_profile==custom&&!custom_exists;
    if(migrate_custom){slots[custom]=live;custom_exists=true;}
    migrated_slot=-1;
    auto& slot=slots[active_profile];
    if(slot.tv_enabled!=live.tv_enabled||slot.tv_screen!=live.tv_screen||
       slot.phosphor_strength!=live.phosphor_strength||slot.phosphor_sharpness!=live.phosphor_sharpness){
        migrated_slot=active_profile;
        slot.tv_enabled=live.tv_enabled;slot.tv_screen=live.tv_screen;
        slot.phosphor_strength=live.phosphor_strength;slot.phosphor_sharpness=live.phosphor_sharpness;
    }
    if(slots[active_profile].motion_blur!=live.motion_blur){
        migrated_slot=active_profile;
        slots[active_profile].motion_blur=live.motion_blur;
    }
    if(slots[active_profile].auto_lines!=live.auto_lines||(!live.auto_lines&&slots[active_profile].lines!=live.lines)){
        migrated_slot=active_profile;
        slots[active_profile].auto_lines=live.auto_lines;slots[active_profile].lines=live.lines;
    }
    dirty=false;failed=false;
}
static bool number(const wchar_t* file,const wchar_t* group,const wchar_t* key,int value){
    return WritePrivateProfileStringW(group,key,std::to_wstring(value).c_str(),file)!=FALSE;
}
static bool write_filter(const wchar_t* file,const wchar_t* group,const RetroSettings& s){
    static const wchar_t* keys[]={L"Lines",L"Pixelation",L"Scanlines",L"Mask",L"Glow",L"Dither",L"EdgeSoftness",L"MotionBlur"};
    if(!number(file,group,L"Mode",s.mode)||!number(file,group,L"PS1Raster",s.ps1_raster)||
       !number(file,group,L"LinesAuto",s.auto_lines))return false;
    if(!number(file,group,L"Phosphors",s.phosphors))return false;
    if(!number(file,group,L"TVEnabled",s.tv_enabled)||!number(file,group,L"TVScreen",s.tv_screen)||
       !number(file,group,L"PhosphorStrength",s.phosphor_strength)||!number(file,group,L"PhosphorSharpness",s.phosphor_sharpness))return false;
    static_assert(std::size(keys)==std::size(fields));
    for(size_t i=0;i<std::size(keys);i++)if(!number(file,group,keys[i],s.*fields[i]))return false;
    return true;
}
// The installer uses the same mutex and writes through a temporary copy.
static bool save_file(const wchar_t* path,const RetroSettings& value,int selected,int write_slot,int display_row,int display_value){
    if(!path)return false;
    const wchar_t* name=wcsrchr(path,L'\\');if(!name)return false;
    wchar_t temp[MAX_PATH]={};const std::wstring folder(path,name+1);
    if(!GetTempFileNameW(folder.c_str(),L"f8r",0,temp))return false;
    DWORD attr=GetFileAttributesW(path);bool exists=attr!=INVALID_FILE_ATTRIBUTES;
    bool ok=exists?!(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_READONLY)):GetLastError()==ERROR_FILE_NOT_FOUND;
    if(ok&&exists)ok=CopyFileW(path,temp,FALSE)!=FALSE;
    if(display_row==MenuSize){
        if(ok)ok=number(temp,L"Menu",L"ScalePercent",display_value);
    }else if(display_row>=MenuShortcut&&display_row<=NextFilterShortcut){
        if(ok)ok=number(temp,L"Menu",shortcuts[display_row-MenuShortcut].key,display_value);
    }else if(display_row>=0){
        static const wchar_t* const keys[]={L"Atlas",L"Wide",L"FPS",L"FieldSides",L"FPSCounter"};
        if(ok)ok=number(temp,L"Display",keys[display_row-Map],display_value);
    }else{
        if(ok&&migrated_slot>=0&&migrated_slot!=write_slot)ok=write_filter(temp,section(migrated_slot).c_str(),slots[migrated_slot]);
        if(ok&&migrate_custom)ok=write_filter(temp,section(custom).c_str(),slots[custom]);
        if(ok&&write_slot>=0)ok=write_filter(temp,section(write_slot).c_str(),value);
        if(ok&&write_slot==custom)ok=number(temp,L"Retro",L"CustomBase",disk_id(active_profile==custom?custom_base:active_profile));
        if(ok)ok=write_filter(temp,L"Retro",value)&&number(temp,L"Retro",L"Selected",disk_id(selected));
    }
    if(ok)ok=exists?ReplaceFileW(path,temp,nullptr,0,nullptr,nullptr)!=FALSE:MoveFileExW(temp,path,MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temp);return ok;
}
static bool persist(const wchar_t* path,const RetroSettings& value,int selected,int write_slot=-1,int display_row=-1,int display_value=0){
    HANDLE writer=CreateMutexW(nullptr,FALSE,L"Local\\FF8RebornDisplaySettings");
    DWORD wait=writer?WaitForSingleObject(writer,0):WAIT_FAILED;
    bool lock=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    failed=!lock||!save_file(path,value,selected,write_slot,display_row,display_value);
    if(lock)ReleaseMutex(writer);if(writer)CloseHandle(writer);
    if(!failed&&display_row<0){migrate_custom=false;migrated_slot=-1;}
    return !failed;
}
static void save_custom(const wchar_t* path,const RetroSettings& s){
    if(!persist(path,s,custom,custom))return;
    if(active_profile!=custom)custom_base=active_profile;
    active_profile=custom;custom_exists=true;slots[custom]=s;dirty=false;
}
static bool select_profile(const wchar_t* path,RetroSettings& s,int id){
    if(!persist(path,slots[id],id))return false;
    active_profile=id;s=slots[id];dirty=false;return true;
}
struct QuickFilterNotice {
    int profile=-1;
    ULONGLONG began=0;
    void show(int id,ULONGLONG time){profile=id;began=time;}
    float opacity(ULONGLONG time){
        if(profile<0)return 0;
        const ULONGLONG elapsed=time-began;
        if(elapsed>=2500){profile=-1;return 0;}
        return elapsed<=2000?1.f:float(2500-elapsed)/500.f;
    }
};
static QuickFilterNotice quick_notice;
static void cycle_filter_shortcut(const wchar_t* path,RetroSettings& s,int step){
    if(open||!path)return;
    load_slots(path,s);
    const int next=(base_profile()+step+profile_count)%profile_count;
    if(select_profile(path,s,next))quick_notice.show(next,GetTickCount64());
}
// Factory presets and saved filters can start between two stops.
static int next_percent(int value,int direction){
    if(direction>0)return std::min(100,(value/10+1)*10);
    return std::max(0,(value-1)/10*10);
}
static void close_menu(){
    recording_shortcut=false;
    update_choice_open=false;update_close_failed=false;
    open=false;confirm_close=false;choosing_section=false;dirty=false;failed=false;capture_pad=false;
}
static bool offer_update_choice(){
    if(!open||dirty||confirm_close||choosing_section||recording_shortcut||update_choice_open||
       page!=Section::Display||row!=ModUpdate||!update_status||update_status()!=update_ready)return false;
    close_update_now=false;update_close_failed=false;update_choice_open=true;update_choice_seen=true;return true;
}
static void handle_update_choice(UINT key){
    if(key==VK_UP||key==VK_DOWN){close_update_now=!close_update_now;return;}
    if(key==VK_ESCAPE||key==VK_F8||(key==VK_RETURN&&!close_update_now)){
        update_choice_open=false;return;
    }
    if(key!=VK_RETURN)return;
    if(update_status&&update_status()==update_ready&&close_for_update&&close_for_update())close_menu();
    else{close_update_now=false;update_close_failed=true;}
}
static void handle_close_choice(UINT key,const wchar_t* path,RetroSettings& s){
    if(key==VK_ESCAPE||key==VK_F8){
        confirm_close=false;failed=false;return;
    }
    if(key==VK_UP||key==VK_DOWN){
        const int step=key==VK_UP?-1:1;
        close_choice=CloseChoice((int(close_choice)+step+3)%3);
        return;
    }
    if(key!=VK_RETURN)return;
    if(close_choice==CloseChoice::KeepEditing){confirm_close=false;failed=false;return;}
    if(close_choice==CloseChoice::Save){
        save_custom(path,s);
        if(failed)return;
    }else s=slots[active_profile];
    close_menu();
}
static void handle_key(UINT key,const wchar_t* path,RetroSettings& s){
    if(key==VK_F8&&!open){
        quick_notice.profile=-1;load_slots(path,s);focus_section(page);open=true;capture_pad=true;return;
    }
    if(!open)return;
    if(recording_shortcut){
        if(key==VK_ESCAPE||key==VK_F8){recording_shortcut=false;failed=false;shortcut_conflict=false;}
        return;
    }
    if(confirm_close){handle_close_choice(key,path,s);return;}
    if(update_choice_open){handle_update_choice(key);return;}
    if(choosing_section&&key!=VK_F8&&key!=VK_TAB){
        switch(key){
        case VK_ESCAPE: choosing_section=false;break;
        case VK_RETURN: focus_section(pending_section);break;
        case VK_LEFT: change_section(-1);break;
        case VK_RIGHT: change_section(1);break;
        }
        return;
    }
    if(key==VK_ESCAPE||key==VK_F8){
        if(dirty){confirm_close=true;close_choice=CloseChoice::KeepEditing;failed=false;}
        else close_menu();
        return;
    }
    if(key==VK_TAB){
        change_section(GetKeyState(VK_SHIFT)<0?-1:1);
        return;
    }
    if(key==VK_UP||key==VK_DOWN){
        if(key==VK_UP&&row==order_for(page).rows[0]){
            pending_section=page;choosing_section=true;return;
        }
        move_row(key==VK_UP?-1:1,s);
        return;
    }
    const bool activate=key==VK_RETURN;
    if(!activate&&key!=VK_LEFT&&key!=VK_RIGHT)return;
    if(row==InstalledVersion||!editable(s,row))return;
    const int step=key==VK_LEFT?-1:1;
    if(row==MenuSize){
        const int size=activate?100:std::clamp(display_settings.menu_scale+step*DisplaySettings::menu_scale_step,
                                                DisplaySettings::min_menu_scale,DisplaySettings::max_menu_scale);
        if(size!=display_settings.menu_scale&&persist(path,s,active_profile,-1,MenuSize,size))display_settings.menu_scale=size;
        return;
    }
    if(row>=MenuShortcut&&row<=NextFilterShortcut){
        if(activate)start_recording(row);
        else if(key==VK_LEFT){
            const auto& def=shortcuts[row-MenuShortcut];
            bool in_use=false;
            for(const auto& other:shortcuts)
                if(&other!=&def && (display_settings.*other.member).load()==def.factory)in_use=true;
            if(in_use){start_recording(row);shortcut_conflict=true;}
            else if(persist(path,s,active_profile,-1,row,def.factory))display_settings.*def.member=def.factory;
        }
        return;
    }
    if(row==ModUpdate){
        if(activate&&update_status){
            const auto state=update_status();
            if(state==update_ready)offer_update_choice();
            else if(update_action&&update_actionable(state))update_action();
        }
        return;
    }
    if(row==SaveCustom||row==Restore){
        if(activate&&row==SaveCustom)save_custom(path,s);
        if(activate&&row==Restore&&can_restore(s)){
            s=profiles[base_profile()];dirty=!same_filter(s,slots[active_profile]);
            failed=false;row=Profile;
        }
        return;
    }
    if(row==Counter){
        const int size=(display_settings.fps_counter+4+step)%4;
        if(persist(path,s,active_profile,-1,row,size))display_settings.fps_counter=size;
        return;
    }
    if(row>=Map&&row<=Sides){
        int value;
        if(row==FPS){
            const auto* rates=DisplaySettings::frame_rates;
            constexpr int count=DisplaySettings::frame_rate_count;
            const auto* current=std::find(rates,rates+count,display_settings.fps_setting);
            value=rates[(current-rates+count+step)%count];
        }else value=row==Map?!display_settings.atlas:row==Aspect?!display_settings.wide:!display_settings.field_sides;
        if(!persist(path,s,active_profile,-1,row,value))return;
        if(row==Map)display_settings.atlas=value!=0;
        if(row==Aspect)display_settings.wide=value!=0;
        if(row==FPS)display_settings.select_fps(value);
        if(row==Sides)display_settings.field_sides=value!=0;
        can_present=false;
        return;
    }
    if(row==Profile){
        const int count=custom+(custom_exists?1:0),id=(active_profile+step+count)%count;
        select_profile(path,s,id);return;
    }
    if(row==TVSimulation)s.tv_enabled=!s.tv_enabled;
    else if(row==TVScreen){
        const int next=1+(int(s.selected_tube())-1+step+3)%3;
        const auto native=s.phosphors==RetroSettings::standard?RetroSettings::aperture_grille:s.phosphors;
        s.tv_screen=next==native?0:next;
    }
    else if(row==Lines){
        if(activate){
            if(s.auto_lines)return;
            s.auto_lines=true;s.lines=profiles[base_profile()].lines;
        }else{
            RECT area{};
            if(attached)GetClientRect(attached,&area);
            const int shown=s.effective_lines(area.right,area.bottom,base_profile());
            const int next=std::clamp(shown+step*RetroSettings::line_step,RetroSettings::min_lines,RetroSettings::max_lines);
            if(next==shown)return;
            s.lines=next;s.auto_lines=false;
        }
    }
    else{
        if(activate)return;
        const auto field=row==PhosphorStrength?&RetroSettings::phosphor_strength:
                         row==PhosphorSharpness?&RetroSettings::phosphor_sharpness:fields[row-Lines];
        int& value=s.*field;
        const int next=next_percent(value,step);
        if(next==value)return;
        value=next;
    }
    dirty=!same_filter(s,slots[active_profile]);failed=false;
}
static bool panel_key(UINT key){
    return recording_shortcut||confirm_close||update_choice_open||choosing_section||key==VK_F8||(open&&(key==VK_UP||key==VK_DOWN||key==VK_LEFT||key==VK_RIGHT||
        key==VK_RETURN||key==VK_ESCAPE||key==VK_SHIFT||key==VK_TAB));
}
static bool focused(HWND window){
    return !IsIconic(window) && GetAncestor(window,GA_ROOT)==GetForegroundWindow() && GetFocus()==window;
}
static LRESULT CALLBACK window_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(msg==WM_NCDESTROY){
        if(current && dirty)*current=slots[active_profile];close_menu();
        RemoveWindowSubclass(window,window_proc,id);
        attached=nullptr;current=nullptr;
        std::fill_n(claimed,256,false);
    }else if(msg==WM_KILLFOCUS){
        std::fill_n(claimed,256,false);
        
    }else if(wp<256 && (msg==WM_KEYUP || msg==WM_SYSKEYUP) && claimed[wp]){
        claimed[wp]=false;
        return 0;
    }else if(msg==WM_KEYDOWN && wp<256 && current && focused(window) && panel_key((UINT)wp)){
        const bool repeat=(lp & (LPARAM(1)<<30))!=0;
        // A press already delivered to GLFW keeps its corresponding release.
        if(repeat && !claimed[wp])return 0;
        claimed[wp]=true;
        if(wp!=VK_SHIFT && (!repeat || wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN))
            handle_key((UINT)wp,ini.c_str(),*current);
        return 0;
    }else if(msg==WM_KEYDOWN && wp<256 && claimed[wp]){
        // The panel can close before the key is physically released.
        return 0;
    }
    return DefSubclassProc(window,msg,wp,lp);
}
#include "retro_gamepad.h"
static void poll(HWND window,const wchar_t* path,RetroSettings& s) {
    if(!window || !path)return;
    if(window!=attached){
        static HWND last_attempt=nullptr;
        static ULONGLONG retry_after=0;
        static bool logged_thread=false,logged_error=false;
        ULONGLONG now=GetTickCount64();
        if(window==last_attempt && now<retry_after)return;
        last_attempt=window;retry_after=now+1000;
        if(GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()){
            if(!logged_thread){log("retro panel: GLFW window belongs to another thread\n");logged_thread=true;}
            return;
        }
        if(!SetWindowSubclass(window,window_proc,1,0)){
            if(!logged_error){log("retro panel: window input registration failed\n");logged_error=true;}
            return;
        }
        if(attached)RemoveWindowSubclass(attached,window_proc,1);
        attached=window;ini=path;
        std::fill_n(claimed,256,false);
        log("retro panel: F8 ready on the GLFW window thread\n");
    }
    current=&s;
    poll_pad(window,path,s);
}
} // namespace retro_panel
