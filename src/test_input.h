// Optional integration-test support. GLFW's own sticky-key API preserves a
// quick press until the game polls it. No key events are synthesized here.
// Only compile with GEO_AUTOTEST and enable with a local marker file.
static BOOL (WINAPI *test_real_peek)(LPMSG,HWND,UINT,UINT,UINT)=PeekMessageW;
static BOOL WINAPI test_peek(LPMSG message,HWND window,UINT first,UINT last,UINT remove){
    static MSG pending{};static double release_at=0;static double received_down[256]={};
    if(release_at){
        if(now()<release_at)return FALSE;
        *message=pending;if(remove&PM_REMOVE)release_at=0;return TRUE;
    }
    BOOL result=test_real_peek(message,window,first,last,remove);
    if(result&&(remove&PM_REMOVE)&&message->message>=WM_KEYFIRST&&message->message<=WM_KEYLAST){
        // Both scan-code and virtual-key input can request a QA capture.
        // This observes an actual key message; it never generates input.
        if((message->message==WM_KEYDOWN||message->message==WM_SYSKEYDOWN)&&message->wParam==VK_F10){test_capture_requested=true;log("test F10 image comparison requested\n");}
        // Some accessibility input arrives with a virtual key but no scan code.
        // GLFW 3 maps the scan code, so normalize only actual received messages.
        // This does not generate presses, releases or a scripted input stream.
        if((message->message==WM_KEYDOWN||message->message==WM_KEYUP||message->message==WM_SYSKEYDOWN||message->message==WM_SYSKEYUP)
           && !(message->lParam&0x00ff0000) && GetFileAttributesA("FF8_geometry_test_input.enabled")!=INVALID_FILE_ATTRIBUTES){
            UINT scan=MapVirtualKeyW((UINT)message->wParam,MAPVK_VK_TO_VSC_EX);
            if(scan){message->lParam|=(LPARAM)((scan&0xff)<<16);if(scan&0xff00)message->lParam|=0x01000000;}
            // The engine's callback stores level state and does not poll GLFW
            // sticky keys. Preserve an actual short accessibility press for a
            // source tick by delaying only its already-received release.
            if(message->wParam<256){
                auto&down_at=received_down[message->wParam];
                if(message->message==WM_KEYDOWN)down_at=now();
                if(message->message==WM_KEYUP&&down_at&&now()<down_at+.12){
                    pending=*message;release_at=down_at+.12;down_at=0;return FALSE;
                }
            }
        }
        static unsigned count=0;if(count++<80){char line[180];snprintf(line,sizeof(line),"test keyboard message %x key %zx data %zx\n",message->message,(size_t)message->wParam,(size_t)message->lParam);log(line);}
    }
    return result;
}
static void test_input_poll(){
    static double checked=0;static BYTE*modified=nullptr;static int original=0;
    if(!engine||now()-checked<1)return;checked=now();
    bool requested=GetFileAttributesA("FF8_geometry_test_input.enabled")!=INVALID_FILE_ATTRIBUTES;
    auto set_mode=(void(__cdecl*)(void*,int,int))((BYTE*)engine+0x161e6d0);
    const BYTE signature[]={0x8b,0x4d,0x0c,0x8b,0xc1,0x56,0x2d,0x01,0x30,0x03,0x00};
    if(memcmp((BYTE*)engine+0x161e6ed,signature,sizeof(signature)))return;
    if(!requested){if(modified&&readable(modified,0x284)){set_mode(modified,0x33002,original);modified=nullptr;log("test input: restored GLFW sticky-key mode\n");}return;}
    if(modified)return;
    BYTE*window=*(BYTE**)((BYTE*)engine+0x1c9473c);HWND active=WindowFromDC(dc);
    for(int i=0;i<8&&window&&readable(window,0x284);i++,window=*(BYTE**)window){
        if(*(HWND*)(window+0x280)!=active)continue;
        original=*(int*)(window+0x54);if(original!=0&&original!=1)return;
        set_mode(window,0x33002,1);modified=window;
        log("test input: GLFW sticky keys enabled on verified rendering window\n");return;
    }
}
