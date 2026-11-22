// Included inside namespace geo. The window procedure and the renderer share
// the GLFW window thread. Only keys delivered to the game's window are used.
static void log(const char*);
namespace retro_panel {
static bool open=false,dirty=false,failed=false;
static int row=0;

static HWND attached=nullptr;
static RetroSettings* current=nullptr;
static std::wstring ini;
static bool claimed[256]={};
static std::atomic<bool> capture_pad{false};

static bool is_open(){return open;}
static bool save_failed(){return failed;}
static int selected(){return row;}
static constexpr int RetroSettings::* fields[]={
    &RetroSettings::lines,&RetroSettings::pixelation,&RetroSettings::scanlines,&RetroSettings::mask,
    &RetroSettings::glow,&RetroSettings::dither,&RetroSettings::edge_softness
};
static constexpr RetroSettings profiles[]={
    {},{1,240,100,0,0,0,100,0,true},{2,320,85,35,20,10,20,0},
    {2,240,100,52,24,4,100,8,true},{1,320,100,0,0,0,20,65},
    {2,240,100,23,14,8,100,42,true},{1,320,80,0,0,0,30,32},{1,320,32,0,0,0,6,70,false,true},
    RetroSettings::recommended(),
    {2,240,90,68,65,18,12,26,true,false,RetroSettings::aperture_grille},
    {2,240,85,54,62,25,8,46,true,false,RetroSettings::slot_mask},
    {2,240,80,44,70,32,6,60,true,false,RetroSettings::dot_triad}
};
static constexpr int profile_count=sizeof(profiles)/sizeof(profiles[0]);
static bool editable(int mode,int index) {
    return index==0 || mode==2 || (mode==1 && (index<=2 || index>=6));
}
static int preset(const RetroSettings& s) {
    if(s.mode!=1 && s.mode!=2)return 0;
    for(int p=1;p<profile_count;p++){
        const auto& ref=profiles[p];
        if(s.mode!=ref.mode||s.ps1_raster!=ref.ps1_raster||s.phosphors!=ref.phosphors)continue;
        bool same=true;
        for(int i=0;i<7;i++){
            if(editable(s.mode,i+1) && s.*fields[i]!=ref.*fields[i]){same=false;break;}
        }
        if(same)return p;
    }
    return s.mode==1?profile_count:profile_count+1;
}
enum Row { Profile, Lines, Pixels, Scan, Mask, Glow, Dither, Edges, FilterType, Map, Aspect, FPS, Sides, Counter, TVScreen, PhosphorStrength, PhosphorSharpness, SaveCustom, Restore, RowCount };
static constexpr Row visual_order[]={Map,Aspect,FPS,Counter,Sides,Profile,Lines,Pixels,Scan,Mask,Glow,Dither,Edges,FilterType,TVScreen,PhosphorStrength,PhosphorSharpness,Restore,SaveCustom};
static_assert(sizeof(visual_order)/sizeof(visual_order[0])==RowCount,"every setting must be reachable");
static int visual_index(int id){return int(std::find(visual_order,visual_order+RowCount,id)-visual_order);}
static constexpr int custom=profile_count;
static const wchar_t* const names[]={L"Off",L"PS1 Digital",L"Soft CRT",L"PS1 · RGB Monitor",L"PS1 Backgrounds",
    L"PS1 · CRT TV",L"PS1 Sharp",L"HD Blend",L"FF8 Reborn (recommended)",L"Sony Trinitron KV-27FS100",
    L"RCA Precision In-Line",L"RCA CT-100",L"Custom"};
static int active_profile=8,custom_base=8;
static bool custom_exists=false,migrate_custom=false,needs_choice=false;
static int legacy_screen_slot=-1;
static RetroSettings slots[custom+1];
static int base_profile(){return active_profile==custom?custom_base:active_profile;}
static bool can_restore(const RetroSettings& s){
    const auto& original=profiles[base_profile()];
    if(s.mode!=original.mode||s.ps1_raster!=original.ps1_raster||s.phosphors!=original.phosphors)return true;
    if(s.tv_screen!=original.tv_screen||s.phosphor_strength!=original.phosphor_strength||s.phosphor_sharpness!=original.phosphor_sharpness)return true;
    for(auto field:fields)if(s.*field!=original.*field)return true;
    return false;
}
// Custom has always been stored in slot 9. New factory profiles follow it on disk.
static int disk_id(int id){return id==custom?9:id>=9?id+1:id;}
static int menu_id(int id){return id==9?custom:id>9?id-1:id;}
static std::wstring section(int id){return L"RetroPerfil"+std::to_wstring(disk_id(id));}
static void update_factory_resolution(const wchar_t* path,RetroSettings& live){
    const int id=menu_id(int(GetPrivateProfileIntW(L"Retro",L"Selected",UINT_MAX,path)));
    if(id!=2&&id!=4&&id!=6&&id!=7)return;
    const int former=id==6?360:id==7?480:240;
    if(live.lines==former&&GetPrivateProfileIntW(section(id).c_str(),L"Mode",UINT_MAX,path)==UINT_MAX)
        live.lines=profiles[id].lines;
}
static void load_slots(const wchar_t* path,const RetroSettings& live){
    custom_base=menu_id(int(GetPrivateProfileIntW(L"Retro",L"CustomBase",8,path)));
    if(custom_base<0||custom_base>=custom)custom_base=8;
    for(int i=0;i<=custom;i++){
        slots[i]=profiles[i==custom?custom_base:i];
        const auto key=section(i);
        const bool exists=GetPrivateProfileIntW(key.c_str(),L"Mode",UINT_MAX,path)!=UINT_MAX;
        if(exists)slots[i].load(path,key.c_str(),i==custom?custom_base:i);
        if(i==custom)custom_exists=exists;
    }
    active_profile=menu_id(int(GetPrivateProfileIntW(L"Retro",L"Selected",UINT_MAX,path)));
    if(active_profile<0||active_profile>custom)active_profile=std::min(custom,preset(live));
    migrate_custom=active_profile==custom&&!custom_exists;
    if(migrate_custom){slots[custom]=live;custom_exists=true;}
    wchar_t marker[32];
    legacy_screen_slot=-1;
    if(!GetPrivateProfileStringW(L"Retro",L"TVScreen",L"",marker,32,path) &&
       (live.tv_screen||live.phosphor_strength!=100||live.phosphor_sharpness!=70)){
        legacy_screen_slot=active_profile;
        auto& slot=slots[active_profile];
        slot.tv_screen=live.tv_screen;slot.phosphor_strength=live.phosphor_strength;slot.phosphor_sharpness=live.phosphor_sharpness;
    }
    dirty=false;needs_choice=false;failed=false;
}
static bool number(const wchar_t* file,const wchar_t* group,const wchar_t* key,int value){
    return WritePrivateProfileStringW(group,key,std::to_wstring(value).c_str(),file)!=FALSE;
}
static bool write_filter(const wchar_t* file,const wchar_t* group,const RetroSettings& s){
    static const wchar_t* keys[]={L"Lines",L"Pixelation",L"Scanlines",L"Mask",L"Glow",L"Dither",L"EdgeSoftness"};
    if(!number(file,group,L"Mode",s.mode)||!number(file,group,L"PS1Raster",s.ps1_raster))return false;
    if(!number(file,group,L"Phosphors",s.phosphors))return false;
    if(!number(file,group,L"TVScreen",s.tv_screen)||!number(file,group,L"PhosphorStrength",s.phosphor_strength)||!number(file,group,L"PhosphorSharpness",s.phosphor_sharpness))return false;
    for(int i=0;i<7;i++)if(!number(file,group,keys[i],s.*fields[i]))return false;
    return true;
}
// The installer uses the same mutex and writes through a temporary copy.
static bool save_file(const wchar_t* path,const RetroSettings& value,int selected,int write_slot,bool reset,int display_row,int display_value){
    if(!path)return false;
    const wchar_t* name=wcsrchr(path,L'\\');if(!name)return false;
    wchar_t temp[MAX_PATH]={};const std::wstring folder(path,name+1);
    if(!GetTempFileNameW(folder.c_str(),L"f8r",0,temp))return false;
    DWORD attr=GetFileAttributesW(path);bool exists=attr!=INVALID_FILE_ATTRIBUTES;
    bool ok=exists?!(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_READONLY)):GetLastError()==ERROR_FILE_NOT_FOUND;
    if(ok&&exists)ok=CopyFileW(path,temp,FALSE)!=FALSE;
    if(display_row>=0){
        static const wchar_t* const keys[]={L"Atlas",L"Wide",L"FPS",L"FieldSides",L"FPSCounter"};
        if(ok)ok=number(temp,L"Display",keys[display_row-Map],display_value);
    }else{
        if(ok&&legacy_screen_slot>=0&&legacy_screen_slot!=write_slot)ok=write_filter(temp,section(legacy_screen_slot).c_str(),slots[legacy_screen_slot]);
        if(ok&&migrate_custom)ok=write_filter(temp,section(custom).c_str(),slots[custom]);
        if(ok&&write_slot>=0){
            const auto key=section(write_slot);
            ok=reset&&write_slot!=custom?WritePrivateProfileStringW(key.c_str(),nullptr,nullptr,temp)!=FALSE:
                                          write_filter(temp,key.c_str(),value);
        }
        if(ok&&write_slot==custom)ok=number(temp,L"Retro",L"CustomBase",disk_id(active_profile==custom?custom_base:active_profile));
        if(ok)ok=write_filter(temp,L"Retro",value)&&number(temp,L"Retro",L"Selected",disk_id(selected));
    }
    if(ok)ok=exists?ReplaceFileW(path,temp,nullptr,0,nullptr,nullptr)!=FALSE:MoveFileExW(temp,path,MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temp);return ok;
}
static bool persist(const wchar_t* path,const RetroSettings& value,int selected,int write_slot=-1,bool reset=false,int display_row=-1,int display_value=0){
    HANDLE writer=CreateMutexW(nullptr,FALSE,L"Local\\FF8RebornDisplaySettings");
    DWORD wait=writer?WaitForSingleObject(writer,0):WAIT_FAILED;
    bool lock=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    failed=!lock||!save_file(path,value,selected,write_slot,reset,display_row,display_value);
    if(lock)ReleaseMutex(writer);if(writer)CloseHandle(writer);
    if(!failed&&display_row<0){migrate_custom=false;legacy_screen_slot=-1;}
    return !failed;
}
static void commit(const wchar_t* path,RetroSettings& s,int destination,bool reset){
    RetroSettings value=reset?profiles[destination==custom?custom_base:destination]:s;
    if(!persist(path,value,destination,destination,reset))return;
    if(destination==custom){if(active_profile!=custom)custom_base=active_profile;custom_exists=true;}
    active_profile=destination;s=slots[destination]=value;dirty=needs_choice=false;
}
static void handle_key(UINT key,bool coarse,const wchar_t* path,RetroSettings& s){
    if(key==VK_F8&&!open){
        load_slots(path,s);row=Profile;open=true;capture_pad=true;return;
    }
    if(!open)return;
    if(key==VK_ESCAPE){if(dirty)s=slots[active_profile];dirty=needs_choice=false;failed=false;open=false;capture_pad=false;return;}
    if(key==VK_F8){if(dirty){row=SaveCustom;needs_choice=true;}else {open=false;capture_pad=false;}return;}
    if(key==VK_UP||key==VK_DOWN){
        int step=key==VK_UP?-1:1,index=visual_index(row);
        do{index=(index+step+RowCount)%RowCount;row=visual_order[index];}
        while((row>=Lines&&row<=Edges&&!editable(s.mode,row))||(row==Restore&&!can_restore(s)));
        return;
    }
    const bool activate=key==VK_RETURN;
    if(!activate&&key!=VK_LEFT&&key!=VK_RIGHT)return;
    const int step=key==VK_LEFT?-1:1;
    if(row>=SaveCustom){
        if(activate&&row==SaveCustom)commit(path,s,custom,false);
        if(activate&&row==Restore&&can_restore(s)){
            commit(path,s,active_profile,true);
            if(!failed)row=Profile;
        }
        return;
    }
    if(row==Counter){
        const int size=(display_settings.fps_counter+4+step)%4;
        if(persist(path,s,active_profile,-1,false,row,size))display_settings.fps_counter=size;
        return;
    }
    if(row>=Map&&row<=Sides){
        const int top=120;
        int value=row==Map?!display_settings.atlas:row==Aspect?!display_settings.wide:row==Sides?!display_settings.field_sides:
            deck_renderer?(display_settings.fps?0:60):
            (display_settings.fps==0?(step>0?60:top):display_settings.fps==60?(step>0?top:0):(step>0?0:60));
        if(!persist(path,s,active_profile,-1,false,row,value))return;
        if(row==Map)display_settings.atlas=value!=0;
        if(row==Aspect)display_settings.wide=value!=0;
        if(row==FPS){display_settings.fps=value;display_settings.automatic_fps=false;}
        if(row==Sides)display_settings.field_sides=value!=0;
        can_present=false;
        return;
    }
    if(row==Profile){
        if(dirty){row=SaveCustom;needs_choice=true;return;}
        const int count=custom+(custom_exists?1:0),id=(active_profile+step+count)%count;
        if(!persist(path,slots[id],id))return;
        active_profile=id;s=slots[id];return;
    }
    if(row==TVScreen)s.tv_screen=(s.tv_screen+step+4)%4;
    else if(row==PhosphorStrength||row==PhosphorSharpness){
        if(activate)return;
        int& value=row==PhosphorStrength?s.phosphor_strength:s.phosphor_sharpness;
        const int next=std::clamp(value+step*(coarse?10:1),0,100);
        if(next==value)return;
        value=next;
    }else if(row==FilterType)s.mode=(s.mode+step+3)%3;
    else{
        if(activate||!editable(s.mode,row))return;
        int& v=s.*fields[row-1];int n=std::clamp(v+step*(row==Lines?20:coarse?10:1),row==Lines?120:0,row==Lines?720:100);
        if(v==n)return;v=n;
    }
    dirty=true;failed=false;
}
static bool panel_key(UINT key){
    return key==VK_F8||(open&&(key==VK_UP||key==VK_DOWN||key==VK_LEFT||key==VK_RIGHT||
        key==VK_RETURN||key==VK_ESCAPE||key==VK_SHIFT));
}
static bool focused(HWND window){
    return !IsIconic(window) && GetAncestor(window,GA_ROOT)==GetForegroundWindow() && GetFocus()==window;
}
static LRESULT CALLBACK window_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(msg==WM_NCDESTROY){
        if(current && dirty)*current=slots[active_profile];open=dirty=needs_choice=false;capture_pad=false;
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
            handle_key((UINT)wp,(GetKeyState(VK_SHIFT)&0x8000)!=0,ini.c_str(),*current);
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
