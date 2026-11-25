#pragma once

// Host offsets are DLL RVAs. Guest addresses belong to the corresponding
// game's emulated address space and must never be shared across editions.
struct EngineLayout {
    const wchar_t* file;
    const char* label;
    DWORD timestamp, image_size;
    uint32_t timer, timer_handle, delay_call, delay_target;
    uint32_t guest_pages, mode, field, main_object, encounter_loop, display_region;
    uint32_t framebuffer_bind, framebuffer_delete;
    uint32_t battle_call, battle_target, stage_call, stage_target, battle_marker, actors;
    uint32_t foot_calls[2], foot_target, world_player, foot_xy, foot_depth;
    uint32_t terrain_calls[4], terrain_target, terrain_points;
    uint32_t sky_calls[2], sky_target, sky_packets[2];
    struct Branch {uint32_t rva; BYTE bytes[6];} terrain_branches[3];
};

static constexpr EngineLayout engine_layouts[]={
    {
        L"FFVIII_EFIGS.dll","EFIGS",0x61ec43a9,0x1df4000,
        0x59560,0x1c94680,0x15926c6,0x597c0,
        0x188edd0,0x1cd8fc6,0x1cd2fc0,0x1a79d88,0x559890,0x204db24,
        0x178c654,0x178c66c,
        0x7f9675,0x811710,0x7d5543,0x7f9310,0x1771cd4,0x1d972c0,
        {0xd771ce,0xd77455},0xd7f4a0,0x25030d8,0x24fef48,0x250315c,
        {0x8acf0c,0x8ae977,0x8afbdc,0x8b16b3},0x79120,0x1f9dc40,
        {0x91764b,0x917853},0x21e390,{0x204b390,0x204b3b4},
        {{0x8b30e0,{0x0f,0x85,0x4a,1,0,0}},
         {0x8b3151,{0x0f,0x84,0xd9,0,0,0}},{0x906189,{0x0f,0x84,0x1b,0x15,0,0}}}
    },
    {
        L"FFVIII_JP.dll","Japanese",0x61ec4421,0x1e06000,
        0x59910,0x1ca61d0,0x15c4006,0x59b70,
        0x18a09a0,0x21def5e,0x21d8f58,0x1f7fd20,0x55e1b0,0x2551c04,
        0x179e18c,0x179e1a4,
        0x833ba5,0x84bc50,0x80fa73,0x833840,0,0x229b3a0,
        {0xdfbd7e,0xdfc005},0xe04050,0x2a071a8,0x2a03018,0x2a0722c,
        {0x8e7b7c,0x8e958c,0x8ea76c,0x8ec1cd},0x72720,0x24a1d20,
        {0x950bf2,0x950dfa},0x21f020,{0x254f470,0x254f494},
        {{0x8edbc8,{0x0f,0x85,0x5d,1,0,0}},
         {0x8edc3e,{0x0f,0x84,0xe7,0,0,0}},{0x93fc0e,{0x0f,0x84,0xf4,0x14,0,0}}}
    },
};

static const EngineLayout* engine_layout=&engine_layouts[0];

static bool matches_engine(const BYTE* base,const EngineLayout& layout){
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||
       dos->e_lfanew>4096-static_cast<LONG>(sizeof(IMAGE_NT_HEADERS32)))return false;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const BYTE timer_bytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x2c};
    return nt->Signature==IMAGE_NT_SIGNATURE&&
        nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&
        nt->FileHeader.TimeDateStamp==layout.timestamp&&
        nt->OptionalHeader.SizeOfImage==layout.image_size&&
        !memcmp(base+layout.timer,timer_bytes,sizeof(timer_bytes));
}
