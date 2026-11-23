namespace update_sound {
// First call from the Final Fantasy VII "Wark" recording on FF Compendium.
// Store the final preview WAV directly in the renderer on both platforms.
alignas(4) static constexpr unsigned char chocobo_wave[]={
#embed "audio/chocobo-vii.wav"
};

static DWORD WINAPI playback_thread(void* module){
    HMODULE multimedia=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(multimedia){
        using Play=BOOL (WINAPI*)(LPCWSTR,HMODULE,DWORD);
        auto sound=reinterpret_cast<Play>(GetProcAddress(multimedia,"PlaySoundW"));
        // SND_MEMORY, SND_NODEFAULT and SND_NOSTOP. Synchronous on this worker
        // keeps the module alive until playback completes and leaves other cues alone.
        constexpr DWORD flags=0x0004|0x0002|0x0010;
        if(sound)sound(reinterpret_cast<LPCWSTR>(chocobo_wave),nullptr,flags);
        FreeLibrary(multimedia);
    }
    FreeLibraryAndExitThread(static_cast<HMODULE>(module),0);
}
static void play(){
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(chocobo_wave),&self))return;
    HANDLE thread=CreateThread(nullptr,0,playback_thread,self,0,nullptr);
    if(thread)CloseHandle(thread);
    else FreeLibrary(self);
}
}
