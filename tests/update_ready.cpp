#define GEO_TEST
#define GEO_NATIVE_CAPTURE
#include "../src/geometry.cpp"

using namespace geo;
using namespace geo::retro_panel;

// Put the receipt and staged DLL next to an isolated module from this test.
// Loading it as an inert image supplies a real HMODULE without running any code.
struct UpdateFixture {
    std::wstring folder,module_file;
    HMODULE module=nullptr;
    std::vector<unsigned char> payload;
    unsigned build=update_version::build+1;

    UpdateFixture(){
        wchar_t temp[MAX_PATH],name[MAX_PATH],system[MAX_PATH];
        demand(GetTempPathW(MAX_PATH,temp)&&GetTempFileNameW(temp,L"f8r",0,name),"test directory name");
        demand(DeleteFileW(name)&&CreateDirectoryW(name,nullptr),"isolated update directory");
        folder=name;module_file=folder+L"\\renderer-fixture.dll";
        demand(GetSystemDirectoryW(system,MAX_PATH)>0,"system directory");
        const auto source=std::wstring(system)+L"\\version.dll";
        demand(CopyFileW(source.c_str(),module_file.c_str(),TRUE),"copy inert test module");
        module=LoadLibraryExW(module_file.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
        demand(module!=nullptr,"map inert isolated module");
        update_slots::Handle file(update_slots::open_read(module_file));
        demand(file.valid(),"test image");
        payload.resize(update_slots::file_size(file.value));DWORD read=0;
        demand(ReadFile(file.value,payload.data(),DWORD(payload.size()),&read,nullptr)&&read==payload.size(),"read fixture");
        const update_slots::Identity id={"FF8-Reborn-Renderer-v1",build,update_slots::platform_id(update_version::channel)};
        const auto* bytes=reinterpret_cast<const unsigned char*>(&id);
        payload.insert(payload.end(),bytes,bytes+sizeof(id));
    }
    ~UpdateFixture(){
        update_slots::Paths paths(module);
        FreeLibrary(module);
        DeleteFileW(paths.slot(0).c_str());DeleteFileW(paths.slot(1).c_str());
        DeleteFileW(paths.receipt().c_str());DeleteFileW(module_file.c_str());RemoveDirectoryW(folder.c_str());
    }
};

int main() try{
    UpdateFixture test;
    const auto channel=update_version::channel;
    const auto current=update_version::build;
    const auto sum=update_slots::sha256(test.payload.data(),test.payload.size());
    demand(update_slots::stage(test.module,channel,current,test.build,sum,test.payload),"stage a verified update");
    mod_update::setup(test.module);
    // Run just the existing-receipt branch. No network request is necessary.
    mod_update::say(update_checking,L"");mod_update::work(nullptr);
    demand(update_status()==update_ready,"a prior verified receipt is recognized");
    mod_update::on_first_present();demand(!open&&!update_choice_open,"no auto dialog during gameplay");

    const HWND window=CreateWindowW(L"STATIC",L"FF8 Reborn test window",WS_POPUP,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
    demand(window!=nullptr,"test window");
    attached=window;
    auto queued=[&](){MSG message{};return PeekMessageW(&message,window,WM_CLOSE,WM_CLOSE,PM_REMOVE)!=0;};
    demand(!mod_update::close_for_update()&&!queued(),"unattached window rejected");
    demand(SetWindowSubclass(window,window_proc,1,0),"attach test panel");
    retro_panel::current=&display_settings.retro;
    RetroSettings settings=RetroSettings::recommended();
    open=true;page=Section::Display;row=ModUpdate;dirty=true;
    mod_update::on_first_present();demand(!update_choice_open,"leave an active edit alone");
    dirty=false;mod_update::on_first_present();
    demand(update_choice_open&&!close_update_now&&!queued(),"verified completion prompts with Later selected");
    handle_key(VK_RETURN,nullptr,settings);
    mod_update::on_first_present();
    demand(!update_choice_open&&open&&!queued()&&update_slots::pending(test.module,channel,current),"Later keeps the verified update");
    handle_key(VK_RETURN,nullptr,settings);
    demand(update_choice_open,"ready row can reopen the choice");
    handle_key(VK_UP,nullptr,settings);handle_key(VK_RETURN,nullptr,settings);
    // Remove the message without dispatching it. This is always a private test window.
    demand(!open&&queued()&&!queued()&&IsWindow(window),"one standard close request for the game window");
    demand(update_slots::pending(test.module,channel,current),"next launch can still see the receipt");

    update_slots::Paths paths(test.module);
    test.payload.back()^=1;
    update_slots::write_atomic(paths.slot(0),test.payload.data(),DWORD(test.payload.size()),true);
    demand(!mod_update::close_for_update()&&!queued()&&update_status()==update_error,"altered payload prevents closing");
    DestroyWindow(window);
    puts("Verified staging: Later, deferred prompt, manual reopen, single window close, next-launch receipt and tamper handling passed");
    return 0;
}catch(const std::exception& e){
    fprintf(stderr,"Update-ready test: %s\n",e.what());return 1;
}
