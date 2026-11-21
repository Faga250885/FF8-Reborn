// Use the Steam session initialized by FFVIII. Do not start or shut down Steam
// from the mod, and never persist account IDs.
enum class SteamLicense { waiting, owned, unavailable };
static SteamLicense steam_license(){
    HMODULE api=GetModuleHandleW(L"steam_api.dll");
    if(!api)return SteamLicense::waiting;
    using Handle=int (__cdecl*)();
    using Client=void* (__cdecl*)(const char*);
    using Interface=void* (__cdecl*)(void*,int,int,const char*);
    using Utils=void* (__cdecl*)(void*,int,const char*);
    using AppId=unsigned (__cdecl*)(void*);
    using Subscribed=bool (__cdecl*)(void*,unsigned);
    using FreeWeekend=bool (__cdecl*)(void*);
    using Account=uint64_t (__cdecl*)(void*);
    auto userHandle=(Handle)GetProcAddress(api,"SteamAPI_GetHSteamUser");
    auto pipeHandle=(Handle)GetProcAddress(api,"SteamAPI_GetHSteamPipe");
    auto clientFor=(Client)GetProcAddress(api,"SteamInternal_CreateInterface");
    auto appsFor=(Interface)GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamApps");
    auto userFor=(Interface)GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamUser");
    auto utilsFor=(Utils)GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamUtils");
    auto appId=(AppId)GetProcAddress(api,"SteamAPI_ISteamUtils_GetAppID");
    auto subscribed=(Subscribed)GetProcAddress(api,"SteamAPI_ISteamApps_BIsSubscribedApp");
    auto weekend=(FreeWeekend)GetProcAddress(api,"SteamAPI_ISteamApps_BIsSubscribedFromFreeWeekend");
    auto owner=(Account)GetProcAddress(api,"SteamAPI_ISteamApps_GetAppOwner");
    auto currentUser=(Account)GetProcAddress(api,"SteamAPI_ISteamUser_GetSteamID");
    if(!userHandle||!pipeHandle||!clientFor||!appsFor||!userFor||!utilsFor||
       !appId||!subscribed||!weekend||!owner||!currentUser)return SteamLicense::unavailable;
    int user=userHandle(),pipe=pipeHandle();
    if(!user||!pipe)return SteamLicense::waiting;
    void* client=clientFor("SteamClient017");
    if(!client)return SteamLicense::waiting;
    void* utils=utilsFor(client,pipe,"SteamUtils009");
    void* apps=appsFor(client,user,pipe,"STEAMAPPS_INTERFACE_VERSION008");
    void* account=userFor(client,user,pipe,"SteamUser019");
    if(!utils||!apps||!account)return SteamLicense::waiting;
    constexpr unsigned ffviii=1026680;
    uint64_t session=currentUser(account);
    if(!session||!appId(utils))return SteamLicense::waiting;
    return appId(utils)==ffviii&&subscribed(apps,ffviii)&&!weekend(apps)&&owner(apps)==session
        ?SteamLicense::owned:SteamLicense::unavailable;
}
