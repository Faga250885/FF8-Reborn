// The first game presentation starts a background feed check. Downloading
// and staging an offered build require an explicit action in the panel.
namespace mod_update {
using State=retro_panel::UpdateState;
static std::atomic<State> state{retro_panel::update_idle};
static SRWLOCK detail_lock=SRWLOCK_INIT;
static wchar_t message[128]{};
static std::atomic<unsigned> received_bytes{0},total_bytes{0};
static HMODULE renderer=nullptr;
static update_feed::Release offered;
static bool startup_checked=false;
static wchar_t available_tag[33]{};

static void say(State value,const std::wstring& text){
    AcquireSRWLockExclusive(&detail_lock);
    lstrcpynW(message,text.c_str(),int(sizeof(message)/sizeof(*message)));
    ReleaseSRWLockExclusive(&detail_lock);
    state.store(value,std::memory_order_release);
}
static State status(){return state.load(std::memory_order_acquire);}
// The worker publishes offered before setting update_available. A new check
// or download is started only by the same game thread which reads it here.
static unsigned available_build(){return status()==retro_panel::update_available?offered.build:0;}
static void download_bytes(unsigned& received,unsigned& total){
    received=received_bytes.load(std::memory_order_relaxed);
    total=total_bytes.load(std::memory_order_relaxed);
}
static void detail(wchar_t* out,size_t capacity){
    if(!out||!capacity)return;
    AcquireSRWLockShared(&detail_lock);
    lstrcpynW(out,message,int(std::min<size_t>(capacity,128)));
    ReleaseSRWLockShared(&detail_lock);
}
static std::wstring wide(const std::string& ascii){return {ascii.begin(),ascii.end()};}
static std::wstring versions(){
    wchar_t tag[33]{};
    AcquireSRWLockShared(&detail_lock);
    lstrcpynW(tag,available_tag,33);
    ReleaseSRWLockShared(&detail_lock);
    auto line=L"Installed "+wide(update_version::tag);
    if(tag[0])line+=std::wstring(L"   ·   Available ")+tag;
    return line;
}

struct HttpHandle {
    HINTERNET h;
    explicit HttpHandle(HINTERNET handle):h(handle){}
    ~HttpHandle(){if(h)WinHttpCloseHandle(h);}
    HttpHandle(const HttpHandle&)=delete;
    HttpHandle& operator=(const HttpHandle&)=delete;
};
static std::vector<unsigned char> fetch(const wchar_t* host,const std::wstring& path,size_t limit,bool show_progress=false){
    HttpHandle session(WinHttpOpen(L"FF8-Reborn-Updater/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.h||!WinHttpSetTimeouts(session.h,5000,8000,8000,15000))
        throw std::runtime_error("Network unavailable");
    HttpHandle server(WinHttpConnect(session.h,host,INTERNET_DEFAULT_HTTPS_PORT,0));
    if(!server.h)throw std::runtime_error("Could not connect to GitHub");
    HttpHandle request(WinHttpOpenRequest(server.h,L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE|WINHTTP_FLAG_REFRESH));
    if(!request.h)throw std::runtime_error("Could not create HTTPS request");
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if(!WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects))||
       !WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||
       !WinHttpReceiveResponse(request.h,nullptr))throw std::runtime_error("Check your connection to GitHub");
    DWORD status_code=0,size=sizeof(status_code);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status_code,&size,WINHTTP_NO_HEADER_INDEX)||status_code!=200)
        throw std::runtime_error("Update unavailable on GitHub");
    DWORD length=0;size=sizeof(length);
    bool known_size=WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&length,&size,WINHTTP_NO_HEADER_INDEX)!=FALSE;
    if(known_size&&(!length||length>limit))throw std::runtime_error("Unexpected update size");
    if(show_progress)total_bytes.store(known_size?length:0,std::memory_order_relaxed);
    std::vector<unsigned char> result;
    if(known_size)result.reserve(length);
    unsigned char buffer[16*1024];
    const auto started=GetTickCount64();
    for(;;){
        DWORD bytes=0;
        if(GetTickCount64()-started>180000||!WinHttpReadData(request.h,buffer,sizeof(buffer),&bytes))
            throw std::runtime_error("Download interrupted. Try again.");
        if(!bytes)break;
        if(bytes>limit-result.size())throw std::runtime_error("Unexpected update size");
        if(known_size&&bytes>length-result.size())throw std::runtime_error("Invalid download length. Try again.");
        result.insert(result.end(),buffer,buffer+bytes);
        if(show_progress)received_bytes.store(unsigned(result.size()),std::memory_order_relaxed);
    }
    if(result.empty()||(known_size&&result.size()!=length))throw std::runtime_error("Incomplete download. Try again.");
    return result;
}

static DWORD WINAPI work(void*){
    try{
        if(status()==retro_panel::update_checking){
            if(update_slots::pending(renderer,update_version::channel,update_version::build)){
                say(retro_panel::update_ready,L"Reopen FFVIII to use your update.");return 0;
            }
            const auto bytes=fetch(L"raw.githubusercontent.com",
                L"/Faga250885/FF8-Reborn/main/updates/"+wide(update_version::channel)+L".txt",512);
            update_feed::Release latest;
            if(!update_feed::parse({reinterpret_cast<const char*>(bytes.data()),bytes.size()},update_version::channel,latest))
                throw std::runtime_error("Invalid release information");
            if(latest.build<=update_version::build){
                say(retro_panel::update_current,L"Installed: "+wide(update_version::tag));return 0;
            }
            offered=std::move(latest);
            const auto tag=wide(offered.tag);
            AcquireSRWLockExclusive(&detail_lock);
            lstrcpynW(available_tag,tag.c_str(),33);
            ReleaseSRWLockExclusive(&detail_lock);
            say(retro_panel::update_available,wide(update_version::tag)+L"  →  "+wide(offered.tag));
        }else{
            const std::wstring path=L"/Faga250885/FF8-Reborn/releases/download/"+wide(offered.tag)+
                L"/FF8-Reborn-"+wide(update_version::channel)+L".dll";
            const auto bytes=fetch(L"github.com",path,32*1024*1024,true);
            say(retro_panel::update_verifying,L"Checking the downloaded file and its checksum.");
            update_slots::stage(renderer,update_version::channel,update_version::build,offered.build,offered.sha256,bytes,
                [](){say(retro_panel::update_saving,L"Preparing the verified update for the next launch.");});
            say(retro_panel::update_ready,L"Reopen FFVIII to use "+wide(offered.tag)+L".");
        }
    }catch(const std::exception& error){
        log("Update: ");log(error.what());log("\n");
        say(retro_panel::update_error,wide(error.what()));
    }catch(...){
        say(retro_panel::update_error,L"Update failed. You can try again.");
    }
    return 0;
}
static void action(){
    const State before=status();
    if(before==retro_panel::update_available){
        received_bytes.store(0,std::memory_order_relaxed);
        total_bytes.store(0,std::memory_order_relaxed);
        say(retro_panel::update_downloading,L"Downloading "+wide(offered.tag)+L"…");
    }
    else if(before==retro_panel::update_idle||before==retro_panel::update_current||before==retro_panel::update_error){
        AcquireSRWLockExclusive(&detail_lock);
        available_tag[0]=0;
        ReleaseSRWLockExclusive(&detail_lock);
        say(retro_panel::update_checking,L"Checking GitHub for updates…");
    }
    else return;
    HANDLE worker=CreateThread(nullptr,0,work,nullptr,0,nullptr);
    if(worker)CloseHandle(worker);
    else say(retro_panel::update_error,L"Could not start the update check.");
}
static void on_first_present(){
    if(startup_checked||!renderer)return;
    startup_checked=true;
    if(status()==retro_panel::update_idle)action();
}
static void setup(HMODULE self){
    renderer=self;
    say(retro_panel::update_idle,L"Installed: "+wide(update_version::tag));
    retro_panel::update_action=action;
    retro_panel::update_status=status;
    retro_panel::update_detail=detail;
    retro_panel::update_bytes=download_bytes;
}
}
