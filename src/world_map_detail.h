// Shared atlas UVs in the game address the upper three quarters of a square.
// Upload the painting at its native resolution and keep the marker/UI passes native.
static GLuint atlas_detail_texture=0;
static bool atlas_allocation_attempted=false;
// Call with texture unit zero active. The caller owns the binding.
static void ensure_atlas_detail(){
    if(!atlas_allocation_attempted){
        atlas_allocation_attempted=true;
        static const BYTE painting[]={
#embed "mapart/horizonte/atlas.rgb"
        };
        constexpr int side=2048,rows=1536;
        static_assert(sizeof(painting)==side*rows*3);
        PixelTransferGuard transfer;
        GLuint id=0;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
        // The stock map layer uses 127/255 alpha, including the ocean.
        glPixelTransferf(GL_ALPHA_SCALE,0.f);glPixelTransferf(GL_ALPHA_BIAS,127.f/255);
        real_tex_image(GL_TEXTURE_2D,0,GL_RGBA8,side,side,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
        GLint stored=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&stored);
        if(stored==side){
            real_tex_subimage(GL_TEXTURE_2D,0,0,0,side,rows,GL_RGB,GL_UNSIGNED_BYTE,painting);
            atlas_detail_texture=id;
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }else glDeleteTextures(1,&id);
    }
}
static void bind_world_map_detail(const TextureState& texture){
    auto copy=texture.copy;
    if(!copy)return;
    pActiveTexture(GL_TEXTURE0);
    if(!copy->checked_map_detail){
        copy->checked_map_detail=true;
        GLint w=0,h=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        copy->eligible_map_detail=w==256&&h==256;
    }
    if(copy->eligible_map_detail){
        ensure_atlas_detail();
        glBindTexture(GL_TEXTURE_2D,atlas_detail_texture?atlas_detail_texture:copy->id);
    }
    pActiveTexture(GL_TEXTURE3);
}

// The supported world renderer dispatches its four display modes at
// 5403CB. Only selector 3 submits the full atlas (5404AE..540525).
// Read the current selector on the first candidate draw, after world input.
static constexpr int unread_atlas_view=-2;
static int native_atlas_view=unread_atlas_view;
static bool native_atlas_possible(){
    if(native_atlas_view==unread_atlas_view){
        native_atlas_view=-1;
#ifdef GEO_NATIVE_CAPTURE
        int view=-1;
        if(engine&&native_pose::fetch(0xc75cf8,&view,sizeof(view))&&view>=0&&view<=3)
            native_atlas_view=view;
#endif
    }
    // An unavailable selector falls back to verifying the actual draw.
    return native_atlas_view<0||native_atlas_view==3;
}

// Original FPS and framing need only this one native draw. Reject other
// geometry before capturing any frame, offscreen surface, or texture pixels.
static bool draw_native_atlas(GLenum mode,GLsizei count,GLenum type,const void* indices){
    if(mode!=GL_TRIANGLES||count!=6||current.mode!=2||!loaded||inside||
       GetCurrentThreadId()!=thread||!native_atlas_possible()||wglGetCurrentContext()!=context)return false;
    const size_t index_size=type==GL_UNSIGNED_SHORT?2:type==GL_UNSIGNED_INT?4:type==GL_UNSIGNED_BYTE?1:0;
    if(!index_size)return false;
    GLint element=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element);
    if(element||!readable(indices,6*index_size))return false;
    GLint vbo=0;glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&vbo);
    if(vbo||!glIsEnabled(GL_VERTEX_ARRAY))return false;
    GLint size=0,format=0,stride=0;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE,&size);glGetIntegerv(GL_VERTEX_ARRAY_TYPE,&format);
    if(size!=4||format!=GL_FLOAT)return false;
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE,&stride);if(!stride)stride=4*sizeof(float);
    if(stride<0||stride>4096)return false;
    void* data=nullptr;glGetPointerv(GL_VERTEX_ARRAY_POINTER,&data);
    if(!data)return false;
    ReadableRegion region;unsigned corners=0;
    for(unsigned i=0;i<6;++i){
        unsigned idx=0;memcpy(&idx,(const BYTE*)indices+i*index_size,index_size);
        const uint64_t address=uint64_t(uintptr_t(data))+uint64_t(idx)*unsigned(stride);
        if(address>UINTPTR_MAX-4*sizeof(float))return false;
        const void* vertex=(const void*)uintptr_t(address);
        if(!readable(vertex,4*sizeof(float),&region))return false;
        float pos[4];memcpy(pos,vertex,sizeof(pos));
        if((pos[0]!=64&&pos[0]!=576)||(pos[1]!=48&&pos[1]!=432)||
           !(std::abs(pos[3]-1.f/3)<.00001f&&std::abs(pos[2]-.00155f)<.00001f))return false;
        corners|=1u<<((pos[0]==576?1:0)+(pos[1]==432?2:0));
    }
    if(corners!=15)return false;
    GLint fbo=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    GLfloat projection[16];glGetFloatv(GL_PROJECTION_MATRIX,projection);
    if(!fbo||std::abs(projection[0]-2.f/640)>.00001f)return false;
    GLint active,original,w,h;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);pActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&original);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
    bool substituted=false;
    if(original&&w==256&&h==256){
        ensure_atlas_detail();
        if(atlas_detail_texture){
            glBindTexture(GL_TEXTURE_2D,atlas_detail_texture);
            real_draw(mode,count,type,indices);
            substituted=true;
        }
        glBindTexture(GL_TEXTURE_2D,original);
    }
    pActiveTexture(active);
    return substituted;
}
