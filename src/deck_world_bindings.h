// The supported renderers submit framebuffer changes through GLEW's
// EXT dispatch entries. Observe changes there, including late mod startup.
// If another renderer owns an entry, use the ordinary GL query on each draw.
namespace world_bindings {
static HMODULE observed_engine=nullptr;
static thread_local bool tracked=false;
static thread_local GLint cached_target=-1;
template<class Fn> static bool slot_is(size_t rva,Fn callback){
    return observed_engine&&observed_engine==engine&&
        *reinterpret_cast<Fn volatile*>(reinterpret_cast<BYTE*>(engine)+rva)==callback;
}
template<class Fn> static bool observe(size_t rva,Fn expected,Fn callback,Fn& original){
    if(!observed_engine||uintptr_t(expected)<=3||uintptr_t(expected)==UINTPTR_MAX)return false;
    auto slot=reinterpret_cast<void* volatile*>(reinterpret_cast<BYTE*>(engine)+rva);
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<BYTE*>(engine)+rva,&info,sizeof(info))||info.AllocationBase!=engine||info.Type!=MEM_IMAGE||
       info.State!=MEM_COMMIT||(info.Protect!=PAGE_READWRITE&&info.Protect!=PAGE_WRITECOPY))return false;
    if(slot_is(rva,callback))return original==expected;
    if(!slot_is(rva,expected))return false;
    original=expected;
    return InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(callback),
        reinterpret_cast<void*>(expected))==reinterpret_cast<void*>(expected);
}
static PFNGLBINDFRAMEBUFFERPROC original_bind;
static PFNGLDELETEFRAMEBUFFERSPROC original_delete;
static bool installed=false;
static void binding_changed(GLenum target){
    if(target==GL_FRAMEBUFFER||target==GL_DRAW_FRAMEBUFFER)cached_target=-1;
}
static void APIENTRY bind(GLenum target,GLuint id){
    original_bind(target,id);
    binding_changed(target);
}
static void APIENTRY erase(GLsizei count,const GLuint* ids){
    original_delete(count,ids);
    cached_target=-1;
}
static void install(){
    tracked=installed=false;cached_target=-1;
    observed_engine=engine;
    if(!engine)return;
    const auto bind_ext=reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(wglGetProcAddress("glBindFramebufferEXT"));
    const auto delete_ext=reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(wglGetProcAddress("glDeleteFramebuffersEXT"));
    installed=observe(engine_layout->framebuffer_bind,bind_ext,bind,original_bind)&&
        observe(engine_layout->framebuffer_delete,delete_ext,erase,original_delete);
}
static void begin_frame(){
    tracked=installed&&slot_is(engine_layout->framebuffer_bind,bind)&&
        slot_is(engine_layout->framebuffer_delete,erase);
    cached_target=-1;
}
static GLuint draw_target(){
    if(!tracked||cached_target<0)glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&cached_target);
    return GLuint(cached_target);
}
}
