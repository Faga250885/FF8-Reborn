// On the supported EFIGS build, record the output of the game's own indexed
// battle projection. Never substitute a result or alter emulated registers.
// The call site belongs solely to 5099D0's mesh loop. The duplicate and
// range guards are also needed when a single asset is instanced twice.
namespace native_pose {
using Point=BoneSample;
static SRWLOCK lock=SRWLOCK_INIT;
static std::vector<Point> pending;
static uint32_t calls=0,accepted=0;
static bool overflow=false;
static void (__cdecl *original)(void*);
static void (__cdecl *stage_original)(void*);
static thread_local bool stage_drawing=false;
#include "stage_clip.h"
static void __cdecl draw_stage(void*cpu){
    bool previous=stage_drawing;stage_drawing=true;
    stage_original(cpu);stage_drawing=previous;
}
static void (__cdecl *world_original)(void*);
static void (__cdecl *foot_original)(void*);
static void* writable_guest(uint32_t address,size_t size){
    if(!address||size>4096-(address&4095))return nullptr;
    const auto slot=(const uint32_t*)((BYTE*)engine+0x188edd0)+(address>>12);
    if(!readable(slot,4)||!*slot)return nullptr;
    auto ptr=(BYTE*)(uintptr_t)(*slot)+(address&4095);
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(ptr,&info,sizeof(info))||info.State!=MEM_COMMIT||info.Protect!=PAGE_READWRITE)return nullptr;
    return ptr;
}
static bool fetch(uint32_t addr,void*out,size_t length){
    if(!addr||length>65536||uint64_t(addr)+length>0x100000000ULL)return false;
    auto dest=(BYTE*)out;
    while(length){
        const auto slot=(const uint32_t*)((BYTE*)engine+0x188edd0)+(addr>>12);
        if(!readable(slot,4)||!*slot)return false;
        const auto ptr=(const BYTE*)(uintptr_t)(*slot)+(addr&4095);
        size_t n=std::min(length,size_t(4096-(addr&4095)));
        if(!readable(ptr,n))return false;
        memcpy(dest,ptr,n);dest+=n;length-=n;addr+=(uint32_t)n;
    }
    return true;
}
static void __cdecl projected(void*cpu){
    uint32_t sp,args[3]{},source=0,buffer=0,tag=0;uint16_t count=0;
    memcpy(&sp,(BYTE*)cpu+0x2c,4);
    bool valid=sp<=UINT32_MAX-16&&fetch(sp+4,args,12)&&fetch(args[1],&source,4)&&
        fetch(args[2],&buffer,4)&&fetch(source,&count,2)&&count&&count<=4096&&
        source<=UINT32_MAX-2u-6u*count&&buffer<=UINT32_MAX-8u*count;
    // The original function sees the caller's context and stack unchanged.
    uint32_t saved[4]{};bool frame_ok=sp<=UINT32_MAX-40&&fetch(sp+20,saved,sizeof(saved));
    // The native projector bounds are in half-resolution pixels. Let its
    // normal clipping and near-plane handling run on a wider horizontal range.
    int16_t* clip=valid&&enabled&&display_settings.wide&&args[0]<=UINT32_MAX-28?(int16_t*)writable_guest(args[0]+20,8):nullptr;
    int16_t left=0,span=0;
    bool expanded=clip&&clip[2]>=300&&clip[2]<=1200&&clip[0]>=-640&&clip[0]<=32&&clip[3]>0&&clip[3]<=512;
    if(expanded){left=clip[0];span=clip[2];clip[0]=std::min<int>(left,-480);clip[2]=std::max<int>(left+span,800)-clip[0];}
    original(cpu);
    if(expanded){clip[0]=left;clip[2]=span;}
    // This marker is populated by EFIGS immediately before/after this call.
    memcpy(&tag,(BYTE*)engine+0x1771cd4,4);
    // EDI of the body caller is the actor row. Only accept exact bank
    // alignment, otherwise duplicate asset instances are rejected later.
    if(frame_ok && tag<0x10000 && saved[0]>=0x1d972c0 && saved[0]<0x1d972c0+7*156 &&
       (saved[0]-0x1d972c0)%156==0)tag|=(1u+(saved[0]-0x1d972c0)/156)<<16;
    struct Raw {int16_t x,y;uint16_t z,flags;};static_assert(sizeof(Raw)==8);
    thread_local std::array<Raw,4096> raw;
    uint32_t advanced=0;
    valid=valid&&fetch(args[2],&advanced,4)&&advanced==buffer+8u*count&&fetch(buffer,raw.data(),8u*count);
    // Only the verified battle-stage call uses this path. Preserve every
    // depth/near flag, winding test, material and the projected XY itself.
    if(valid&&stage_drawing&&enabled&&display_settings.wide&&height>0&&width>0){
        const int half=int(std::ceil(864.*width/height+4*turn_guard(width,height,432)))+8;
        for(uint32_t i=0;i<count;){
            const uint32_t addr=buffer+8*i;
            const uint32_t n=std::min(uint32_t(count)-i,(4096u-(addr&4095u))/8);
            Raw* dst=n?(Raw*)writable_guest(addr,n*8):nullptr;
            if(!dst)break;
            for(uint32_t j=0;j<n;j++){
                Raw& p=raw[i+j];
                p.flags=stage_outcode(p.x,p.y,1280-half,1280+half,-8,1736,p.flags);
                dst[j].flags=p.flags;
            }
            i+=n;
        }
    }
    AcquireSRWLockExclusive(&lock);
    calls++;
    if(valid&&!overflow)try{
        if(pending.size()+count>64000)overflow=true;
        else {for(unsigned i=0;i<count;i++){auto p=raw[i];pending.push_back({tag,source+2+6*i,p.x,p.y,p.z,p.flags});}accepted++;}
    }catch(...){overflow=true;}
    ReleaseSRWLockExclusive(&lock);
    service_at(5);
}
static void take(std::vector<Point>&out,unsigned&seen,unsigned&groups){
    AcquireSRWLockExclusive(&lock);
    out.clear();if(!overflow)out.swap(pending);pending.clear();
    seen=calls;groups=accepted;calls=accepted=0;overflow=false;
    ReleaseSRWLockExclusive(&lock);
}
static int world_margin(){
    return height>0?std::clamp(int(std::ceil(896.*width/height))-1280+8,0,8192):512;
}
// 651FB0 projects the controlled world model in bone-sized batches. Read
// its indexed screen positions before the engine discards back-facing faces.
static void __cdecl foot_projected(void* cpu){
    uint32_t sp,instance_slot,args[4]{},instance=0,player=0;
    memcpy(&sp,(BYTE*)cpu+0x2c,4);memcpy(&instance_slot,(BYTE*)cpu+0x30,4);
    bool capture=enabled&&display_settings.fps&&sp<=UINT32_MAX-20&&fetch(sp+4,args,sizeof(args))&&
        fetch(instance_slot,&instance,4)&&instance&&fetch(0x25030d8,&player,4)&&instance==player&&
        args[3]>0&&args[3]<=1024&&args[1]>=0x24fef48&&args[1]-0x24fef48<=16384&&
        args[2]==0x250315c+(args[1]-0x24fef48)/2;
    foot_original(cpu);
    if(!capture)return;
    thread_local std::array<std::array<int16_t,2>,1024> xy;
    thread_local std::array<uint16_t,1024> depth;
    if(!fetch(args[1],xy.data(),4*args[3])||!fetch(args[2],depth.data(),2*args[3]))return;
    AcquireSRWLockExclusive(&lock);
    try{
        if(pending.size()+args[3]>64000)overflow=true;
        if(!overflow)for(unsigned i=0;i<args[3];i++)
            pending.push_back({instance|0x80000000u,args[0]+8*i,xy[i][0],xy[i][1],depth[i],0});
    }catch(...){overflow=true;}
    ReleaseSRWLockExclusive(&lock);
}
static bool install_foot(){
    BYTE* sites[]={(BYTE*)engine+0xd771ce,(BYTE*)engine+0xd77455};
    BYTE* projector=(BYTE*)engine+0xd7f4a0;
    for(auto site:sites){
        int32_t offset;memcpy(&offset,site+1,4);
        if(site[0]!=0xe8||site[-1]!=0xfc||site+5+offset!=projector)return false;
    }
    foot_original=(void(__cdecl*)(void*))projector;
    for(auto site:sites){
        DWORD old,unused;
        if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old))return false;
        uint32_t offset=(uint32_t)(uintptr_t)&foot_projected-(uint32_t)(uintptr_t)(site+5);
        memcpy(site+1,&offset,4);FlushInstructionCache(GetCurrentProcess(),site,5);VirtualProtect(site,5,old,&unused);
    }
    return true;
}
static void __cdecl world_projected(void*cpu){
    uint32_t sp=0,count=0;memcpy(&sp,(BYTE*)cpu+0x2c,4);
    bool valid=enabled&&display_settings.wide&&sp<=UINT32_MAX-8&&fetch(sp+4,&count,4)&&count>0&&count<=4096;
    world_original(cpu);
    if(!valid)return;
    const int margin=world_margin()+int(std::ceil(4*turn_guard(width,height,448)));
    for(uint32_t i=0;i<count;){
        const uint32_t addr=0x1f9dc40u+8*i;
        const uint32_t n=std::min(count-i,(4096u-(addr&4095u))/8u);
        auto point=(int16_t*)writable_guest(addr,8*n);
        if(!point||!n)break;
        // Bit zero is the native horizontal visibility bit. Its original
        // interval is [0,2560); retain the original near and vertical flags.
        for(uint32_t j=0;j<n;j++,point+=4){const int x=point[0];
            if((x>=-margin&&x<0)||(x>=2560&&x<2560+margin))point[3]|=1;}
        i+=n;
    }
}
static void (__cdecl *sky_original)(void*);
static void __cdecl wide_sky(void*cpu){
    uint32_t sp,packet=0;memcpy(&sp,(BYTE*)cpu+0x2c,4);
    if(enabled&&display_settings.wide&&sp<=UINT32_MAX-12&&fetch(sp+8,&packet,4)&&
        (packet==0x204b390||packet==0x204b3b4)){
        auto q=(int16_t*)writable_guest(packet+8,28);
        if(q&&q[0]==0&&q[4]==2560&&q[8]==0&&q[12]==2560){
            const int margin=world_margin();
            q[0]=q[8]=-margin;q[4]=q[12]=2560+margin;
        }
    }
    sky_original(cpu);
}
static bool install_world(){
    // 53BB90 submits terrain; 53C750 projects the second block pass into
    // the same guest buffer. Both have two traversal paths.
    BYTE* sites[]={(BYTE*)engine+0x8acf0c,(BYTE*)engine+0x8ae977,
                   (BYTE*)engine+0x8afbdc,(BYTE*)engine+0x8b16b3};
    BYTE* target=(BYTE*)engine+0x79120;
    for(auto s:sites){int32_t rel;memcpy(&rel,s+1,4);
        if(s[0]!=0xe8||s+5+rel!=target||s[-1]!=0xfc||s[5]!=0x8b||s[6]!=0x4e)return false;}
    world_original=(void(__cdecl*)(void*))target;
    BYTE* sky_sites[]={(BYTE*)engine+0x91764b,(BYTE*)engine+0x917853};
    BYTE* sky=(BYTE*)engine+0x21e390;
    for(auto s:sky_sites){int32_t rel;memcpy(&rel,s+1,4);if(s[0]!=0xe8||s+5+rel!=sky)return false;}
    sky_original=(void(__cdecl*)(void*))sky;
    // Disassembly of guest 53d3c0+3a2/+3a8 and 5513a0+1d9:
    // skip the old horizontal rejection only. Vertical/depth branches follow.
    struct Branch{size_t rva;BYTE bytes[6];};
    const Branch legacy[]={{0x8b30e0,{0x0f,0x85,0x4a,1,0,0}},
        {0x8b3151,{0x0f,0x84,0xd9,0,0,0}},{0x906189,{0x0f,0x84,0x1b,0x15,0,0}}};
    for(auto&b:legacy)if(memcmp((BYTE*)engine+b.rva,b.bytes,6))return false;
    for(auto&b:legacy){auto p=(BYTE*)engine+b.rva;DWORD old,tmp;
        if(!VirtualProtect(p,6,PAGE_EXECUTE_READWRITE,&old))return false;
        memset(p,0x90,6);FlushInstructionCache(GetCurrentProcess(),p,6);VirtualProtect(p,6,old,&tmp);}
    for(auto s:sky_sites){DWORD old,tmp;if(!VirtualProtect(s,5,PAGE_EXECUTE_READWRITE,&old))return false;
        uint32_t rel=(uint32_t)(uintptr_t)&wide_sky-(uint32_t)(uintptr_t)(s+5);
        memcpy(s+1,&rel,4);FlushInstructionCache(GetCurrentProcess(),s,5);VirtualProtect(s,5,old,&tmp);}
    for(auto s:sites){DWORD prot;if(!VirtualProtect(s,5,PAGE_EXECUTE_READWRITE,&prot))return false;
        uint32_t rel=(uint32_t)(uintptr_t)&world_projected-(uint32_t)(uintptr_t)(s+5);
        memcpy(s+1,&rel,4);FlushInstructionCache(GetCurrentProcess(),s,5);DWORD ignored;VirtualProtect(s,5,prot,&ignored);}
    return true;
}
static bool install(){
    BYTE*site=(BYTE*)engine+0x7f9675;BYTE*target=(BYTE*)engine+0x811710;
    // Fail closed if either the original direct call or its surrounding
    // translated instructions differ. No executable on disk is changed.
    const BYTE suffix[]={0x6a,0xff,0xe8};int32_t old=0;
    memcpy(&old,site+1,4);
    if(site[-1]!=0x56||site[0]!=0xe8||site+5+old!=target||memcmp(site+5,suffix,sizeof(suffix)))return false;
    original=(void(__cdecl*)(void*))target;
    DWORD prot;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&prot))return false;
    uint32_t delta=(uint32_t)(uintptr_t)&projected-(uint32_t)(uintptr_t)(site+5);
    memcpy(site+1,&delta,4);FlushInstructionCache(GetCurrentProcess(),site,5);
    DWORD ignored;VirtualProtect(site,5,prot,&ignored);return true;
}
static bool install_stage(){
    BYTE* site=(BYTE*)engine+0x7d5543;
    BYTE* target=(BYTE*)engine+0x7f9310;
    int32_t displacement;memcpy(&displacement,site+1,4);
    const BYTE suffix[]={0x83,0x46,0x2c,0x14};
    if(site[-1]!=0xfc||site[0]!=0xe8||site+5+displacement!=target||
       memcmp(site+5,suffix,sizeof(suffix)))return false;
    DWORD protect;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protect))return false;
    stage_original=(void(__cdecl*)(void*))target;
    uint32_t delta=(uint32_t)(uintptr_t)&draw_stage-(uint32_t)(uintptr_t)(site+5);
    memcpy(site+1,&delta,4);FlushInstructionCache(GetCurrentProcess(),site,5);
    DWORD ignored;VirtualProtect(site,5,protect,&ignored);return true;
}
static void audit(const Frame&frame,const std::vector<Point>&points,unsigned seen,unsigned groups){
    static unsigned tick=0,saved=0;
    if(frame.mode!=3||!seen||++tick%61||saved>=6)return;
    char path[100];snprintf(path,sizeof(path),"FF8_bone_projection_%02u.bin",++saved);
    FILE*f=fopen(path,"wb");if(!f)return;
    uint32_t hdr[]={0x38425031u,seen,groups,(uint32_t)points.size(),0};fwrite(hdr,4,5,f);
    fwrite(points.data(),sizeof(Point),points.size(),f);
    for(const auto&op:frame.ops)if(projected_geometry(op))for(const auto&v:op.vertices)if(v.p[3]>0&&v.p[3]<.1f){
        fwrite(v.p,4,4,f);hdr[4]++;}
    fseek(f,16,SEEK_SET);fwrite(hdr+4,4,1,f);fclose(f);
}
}
