// Included in world_state. The supported EFIGS renderer uses these four
// core dispatch slots for buffer/VAO state. Query the driver whenever a
// slot is unavailable or has changed since installation.
static thread_local bool cached_buffers=false;
static bool buffers_installed=false;
static PFNGLBINDBUFFERPROC bind_buffer_original;
static PFNGLDELETEBUFFERSPROC delete_buffers_original;
static PFNGLBINDVERTEXARRAYPROC bind_vao_original;
static PFNGLDELETEVERTEXARRAYSPROC delete_vaos_original;
static constexpr size_t bind_buffer_slot=0x178dfa4,delete_buffers_slot=0x178dfd0;
static constexpr size_t bind_vao_slot=0x178d6cc,delete_vaos_slot=0x178d6d0;

static void invalidate_client_array(){
    state.position_binding=state.position_enabled=-1;
    state.element_binding=-1;
    state.position_known=false;
}
static void APIENTRY bind_buffer(GLenum target,GLuint name){
    bind_buffer_original(target,name);
    if(tracking&&target==GL_ARRAY_BUFFER)state.array_binding=-1;
    if(tracking&&target==GL_ELEMENT_ARRAY_BUFFER)state.element_binding=-1;
}
static void APIENTRY delete_buffers(GLsizei n,const GLuint* names){
    delete_buffers_original(n,names);
    if(tracking){state.array_binding=-1;invalidate_client_array();}
}
static void APIENTRY bind_vao(GLuint name){
    bind_vao_original(name);
    if(tracking)invalidate_client_array();
}
static void APIENTRY delete_vaos(GLsizei n,const GLuint* names){
    delete_vaos_original(n,names);
    if(tracking)invalidate_client_array();
}
static void install_buffers(){
    buffers_installed=
        world_bindings::observe(bind_buffer_slot,pBindBuffer,bind_buffer,bind_buffer_original)&&
        world_bindings::observe(delete_buffers_slot,reinterpret_cast<PFNGLDELETEBUFFERSPROC>(wglGetProcAddress("glDeleteBuffers")),delete_buffers,delete_buffers_original)&&
        world_bindings::observe(bind_vao_slot,reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(wglGetProcAddress("glBindVertexArray")),bind_vao,bind_vao_original)&&
        world_bindings::observe(delete_vaos_slot,reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(wglGetProcAddress("glDeleteVertexArrays")),delete_vaos,delete_vaos_original);
}
static void begin_buffers(){
    cached_buffers=buffers_installed&&observing&&world_bindings::slot_is(bind_buffer_slot,bind_buffer)&&
        world_bindings::slot_is(delete_buffers_slot,delete_buffers)&&
        world_bindings::slot_is(bind_vao_slot,bind_vao)&&world_bindings::slot_is(delete_vaos_slot,delete_vaos);
}
