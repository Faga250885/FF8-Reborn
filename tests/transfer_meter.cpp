#define GEO_TEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include "../src/update_feed.h"
#include "../src/update_slots.h"
#include "../src/geometry.cpp"

// Exercise the real streaming loop with a bounded in-memory HTTP response.
namespace transport {
using namespace geo;
using namespace geo::retro_panel;
static std::vector<unsigned char> body;
static DWORD length=0,position=0;
static bool known=true;
static std::vector<unsigned> observed;
static void observe();
static HINTERNET WinHttpOpen(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){return (HINTERNET)1;}
static BOOL WinHttpSetTimeouts(HINTERNET,int,int,int,int){return TRUE;}
static HINTERNET WinHttpConnect(HINTERNET,LPCWSTR,INTERNET_PORT,DWORD){return (HINTERNET)2;}
static HINTERNET WinHttpOpenRequest(HINTERNET,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR*,DWORD){
    position=0;observed.clear();return (HINTERNET)3;
}
static BOOL WinHttpCloseHandle(HINTERNET){return TRUE;}
static BOOL WinHttpSetOption(HINTERNET,DWORD,LPVOID,DWORD){return TRUE;}
static BOOL WinHttpSendRequest(HINTERNET,LPCWSTR,DWORD,LPVOID,DWORD,DWORD,DWORD_PTR){return TRUE;}
static BOOL WinHttpReceiveResponse(HINTERNET,LPVOID){return TRUE;}
static BOOL WinHttpQueryHeaders(HINTERNET,DWORD what,LPCWSTR,LPVOID result,LPDWORD,LPDWORD){
    if(what==(WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER)){*(DWORD*)result=200;return TRUE;}
    if(!known)return FALSE;
    *(DWORD*)result=length;return TRUE;
}
static BOOL WinHttpReadData(HINTERNET,LPVOID dst,DWORD capacity,LPDWORD bytes){
    observe();
    *bytes=std::min<DWORD>({capacity,4096,DWORD(body.size())-position});
    if(*bytes)memcpy(dst,body.data()+position,*bytes);
    position+=*bytes;return TRUE;
}
// Check that the phase labels bracket verification and the atomic save. The
// real staging implementation independently checks the file and its digest.
namespace update_slots {
static bool pending(HMODULE,const char*,unsigned){return false;}
static bool stage(HMODULE,const char*,unsigned,unsigned,const std::string&,const std::vector<unsigned char>&,void(*verified)()){
    demand(update_status()==update_verifying,"must show verification before validating");
    verified();
    demand(update_status()==update_saving,"must show saving after validation");
    return true;
}
}
#include "../src/update_menu.h"
static void observe(){
    unsigned received,total;update_bytes(received,total);
    demand(received==position&&total==(known?length:0),"reported bytes or total mismatch");
    observed.push_back(received);
}
static void run(DWORD advertised,DWORD actual,bool has_total,UpdateState expected){
    body.assign(actual,'a');length=advertised;known=has_total;observed.clear();
    mod_update::received_bytes=0;mod_update::total_bytes=0;
    mod_update::say(update_downloading,L"");
    mod_update::work(nullptr);
    demand(update_status()==expected,"wrong final installation state");
}
}

int main(){
    using namespace geo;
    using namespace geo::retro_panel;
    using namespace transport;
    mod_update::setup(GetModuleHandleW(nullptr));
    run(12288,12288,true,update_ready);
    demand(observed==std::vector<unsigned>({0,4096,8192,12288}),"known length progress");
    auto meter=retro_panel_draw::update_meter(update_downloading,0);
    demand(meter.percent==100,"received full response");
    run(0,12288,false,update_ready);
    demand(observed==std::vector<unsigned>({0,4096,8192,12288}),"streamed bytes");
    meter=retro_panel_draw::update_meter(update_downloading,150);
    demand(meter.percent<0&&meter.transferred_kib==12&&meter.sweep!=0,"indeterminate progress");
    run(0,0,true,update_error);
    run(0,0,false,update_error);
    run(12289,12288,true,update_error);
    run(4095,4096,true,update_error);
    for(auto phase:{update_verifying,update_saving}){
        demand(!update_actionable(phase)&&retro_panel_draw::update_meter(phase,300).percent<0,"busy phase");
    }
    demand(retro_panel_draw::update_meter(update_ready,0).percent==100,"ready bar");
    puts("Update progress: known and streamed downloads, exact bytes, verification, saving, restart and failure OK");
}
