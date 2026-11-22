// The supported EFIGS renderer uses these two core entries to link and
// delete programs. Uniform values and sampler units are read on each draw.
namespace deck_programs {
struct Layout {
    unsigned inputs=0;
    std::vector<std::pair<GLint,GLenum>> uniforms;
};
static thread_local std::unordered_map<GLuint,Layout> layouts;
static std::atomic<unsigned> generation{0};
static thread_local unsigned last_generation=0;
static bool installed=false;
static constexpr size_t link_slot=0x178b89c,delete_slot=0x178e0e4;
static PFNGLLINKPROGRAMPROC original_link;
static PFNGLDELETEPROGRAMPROC original_delete;
static void APIENTRY link(GLuint program){
    original_link(program);++generation;
}
static void APIENTRY erase(GLuint program){
    original_delete(program);++generation;
}
static void install(){
    ++generation; // A replacement GL context can reuse program names.
    installed=world_bindings::observe(link_slot,reinterpret_cast<PFNGLLINKPROGRAMPROC>(wglGetProcAddress("glLinkProgram")),link,original_link)&&
              world_bindings::observe(delete_slot,reinterpret_cast<PFNGLDELETEPROGRAMPROC>(wglGetProcAddress("glDeleteProgram")),erase,original_delete);
}
static bool available(){
    if(!installed)return false;
    if(!world_bindings::slot_is(link_slot,link)||!world_bindings::slot_is(delete_slot,erase)){
        installed=false;layouts.clear();return false;
    }
    const unsigned version=generation.load();
    if(last_generation!=version){layouts.clear();last_generation=version;}
    return true;
}
}
