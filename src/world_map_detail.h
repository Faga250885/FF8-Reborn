// Shared atlas UVs in the game address the upper three quarters of a square.
// Upload the painting at its native resolution and keep the marker/UI passes native.
static GLuint atlas_detail_texture=0;
static bool atlas_allocation_attempted=false;
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
            }else{glDeleteTextures(1,&id);glBindTexture(GL_TEXTURE_2D,copy->id);}
        }
        if(atlas_detail_texture)glBindTexture(GL_TEXTURE_2D,atlas_detail_texture);
    }
    pActiveTexture(GL_TEXTURE3);
}
