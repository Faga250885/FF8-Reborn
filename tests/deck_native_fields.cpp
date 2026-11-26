#define GEO_TEST
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;

int main(){
    std::vector<BYTE> module(0x1897000);
    BYTE mode_page[4096]{},object_page[4096]{},loop_page[4096]{};
    engine=reinterpret_cast<HMODULE>(module.data());
    auto guest_page=[&](unsigned address,BYTE* page){
        auto table=reinterpret_cast<DWORD*>(module.data()+0x188edd0);
        table[address>>12]=DWORD(uintptr_t(page));
        return page+(address&4095);
    };
    const unsigned object=0x10000;
    auto mode_location=guest_page(0x1cd8fc6,mode_page);
    memcpy(guest_page(0x1a79d88,object_page),&object,sizeof(object));
    auto callback=guest_page(object+0xb40,loop_page);
    display_settings.fps=30;display_settings.wide=false;
    for(const short scene:{1,2,3,6}){
        memcpy(mode_location,&scene,sizeof(scene));
        deck_draw_mode=unknown_draw_mode;
        assert(native_deck_frame()==(scene!=3));
    }
#ifdef GEO_NATIVE_CAPTURE
    for(int y:{-80,1792,1871}){
        assert(native_pose::terrain_visibility(5,100,y,250,80)==7);
        assert(native_pose::terrain_visibility(4,-10,y,250,80)==7);
        assert(native_pose::terrain_visibility(0x105,100,y,250,80)==0x105);
        assert(native_pose::terrain_visibility(1,100,y,250,80)==1);
        assert(native_pose::terrain_visibility(4,-251,y,250,80)==4);
    }
    for(int y:{-81,1872})assert(native_pose::terrain_visibility(5,100,y,250,80)==5);
    const short battle=3;
    const unsigned encounter=0x559890;
    memcpy(mode_location,&battle,sizeof(battle));
    memcpy(callback,&encounter,sizeof(encounter));
    deck_draw_mode=unknown_draw_mode;
    assert(game_mode()==encounter_transition&&native_deck_frame());
    memset(callback,0,sizeof(encounter));
#endif
    const short field=1;
    memcpy(mode_location,&field,sizeof(field));deck_draw_mode=unknown_draw_mode;
    display_settings.wide=true;
    current.mode=1;current.reactive_sides=true;
    const GLuint scene_target=9;
    auto& target=building.targets[scene_target];target.w=640;target.h=480;
    Op tile;tile.target=scene_target;tile.mode=GL_TRIANGLES;
    auto& state=tile.state;
    state.viewport[2]=640;state.viewport[3]=480;
    state.scissor_on=true;state.scissor[2]=640;state.scissor[3]=448;
    state.texture[0].id=1;state.projection[0]=2.f/640;
    tile.vertices.resize(36);tile.vertices.front().p[3]=.05f;
    // Title cards, untextured triangles, menu handoffs and isolated actors
    // cannot enable the field compositor.
    current.menu_edge=2;recognize_native_field_target(tile);
    assert(!building.native_field_target);
    current.menu_edge=0;state.texture[0].id=0;recognize_native_field_target(tile);
    assert(!building.native_field_target);
    state.texture[0].id=1;tile.vertices.resize(6);recognize_native_field_target(tile);
    assert(!building.native_field_target);
    tile.vertices.resize(36);building.ops.push_back(tile);
    recognize_native_field_target(building.ops.back());
    assert(building.native_field_target==scene_target);
    building.vertex_count=100;
    assert(!field_draw_is_native(scene_target));
    building.vertex_count=180;
    assert(!field_draw_is_native(0)&&!field_draw_is_native(scene_target+1));
    assert(field_draw_is_native(scene_target)&&building.field_layout_only);
    // A later offscreen operation must never be mistaken for a final
    // drawable composite when the capture contains layout metadata only.
    building.ops.emplace_back().target=0;
    assert(field_draw_is_native(scene_target)&&building.ops.back().target==scene_target);
    reset_building();
    assert(!building.field_layout_only&&!building.native_field_target);
    display_settings.select_fps(60);
    recognize_native_field_target(tile);
    assert(!building.native_field_target&&!building.field_layout_only);
    for(int scene:{1,2,3})assert(!native_rate_for_mode(scene));
    std::puts("Native gameplay routing and field draw capture OK");
}
