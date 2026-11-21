// Rare fallback for legacy / compressed textures that the driver will sample
// but cannot copy with CopyImageSubData. Preserve native transfer state.
struct PixelTransferGuard{
    GLint pack,unpack;
    PixelTransferGuard(){
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPushAttrib(GL_PIXEL_MODE_BIT);
        pBindBuffer(GL_PIXEL_PACK_BUFFER,0);pBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(auto p:{GL_PACK_ALIGNMENT,GL_UNPACK_ALIGNMENT})glPixelStorei(p,1);
        for(auto p:{GL_PACK_ROW_LENGTH,GL_UNPACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_UNPACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_UNPACK_SKIP_PIXELS,
            GL_PACK_SWAP_BYTES,GL_UNPACK_SWAP_BYTES,GL_PACK_LSB_FIRST,GL_UNPACK_LSB_FIRST})glPixelStorei(p,0);
        for(auto p:{GL_RED_SCALE,GL_GREEN_SCALE,GL_BLUE_SCALE,GL_ALPHA_SCALE,GL_DEPTH_SCALE})glPixelTransferf(p,1);
        for(auto p:{GL_RED_BIAS,GL_GREEN_BIAS,GL_BLUE_BIAS,GL_ALPHA_BIAS,GL_DEPTH_BIAS})glPixelTransferf(p,0);
        glPixelTransferi(GL_MAP_COLOR,GL_FALSE);glPixelTransferi(GL_MAP_STENCIL,GL_FALSE);
    }
    ~PixelTransferGuard(){glPopAttrib();glPopClientAttrib();pBindBuffer(GL_PIXEL_PACK_BUFFER,pack);pBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);}
};
static void transfer_format(GLint f,GLenum&fmt,GLenum&type){
    fmt=GL_RGBA;type=GL_UNSIGNED_BYTE;
    switch(f){
    case GL_DEPTH_COMPONENT:case GL_DEPTH_COMPONENT16:case GL_DEPTH_COMPONENT24:case GL_DEPTH_COMPONENT32:case GL_DEPTH_COMPONENT32F:fmt=GL_DEPTH_COMPONENT;type=GL_FLOAT;break;
    case GL_DEPTH_STENCIL:case GL_DEPTH24_STENCIL8:fmt=GL_DEPTH_STENCIL;type=GL_UNSIGNED_INT_24_8;break;
    case GL_DEPTH32F_STENCIL8:fmt=GL_DEPTH_STENCIL;type=GL_FLOAT_32_UNSIGNED_INT_24_8_REV;break;
    case GL_LUMINANCE:case GL_LUMINANCE8:case 1:fmt=GL_LUMINANCE;break;
    case GL_LUMINANCE_ALPHA:case GL_LUMINANCE8_ALPHA8:case 2:fmt=GL_LUMINANCE_ALPHA;break;
    case GL_ALPHA:case GL_ALPHA8:fmt=GL_ALPHA;break;
    }
}
static void copy_level_readback(GLuint source,GLuint dest,GLint level,GLint w,GLint h,GLint format,GLenum fmt,GLenum type){
    PixelTransferGuard transfer;glBindTexture(GL_TEXTURE_2D,source);GLint compressed=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_COMPRESSED,&compressed);
    if(compressed){GLint size=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&size);
        demand(size>0&&size<=256*1024*1024,"compressed image size unsupported");std::vector<BYTE> bytes(size);
        auto get=(PFNGLGETCOMPRESSEDTEXIMAGEPROC)wglGetProcAddress("glGetCompressedTexImage");
        auto put=(PFNGLCOMPRESSEDTEXIMAGE2DPROC)wglGetProcAddress("glCompressedTexImage2D");
        demand((uintptr_t)get>3&&(uintptr_t)get!=UINTPTR_MAX&&(uintptr_t)put>3&&(uintptr_t)put!=UINTPTR_MAX,"compressed transfer unavailable");
        get(GL_TEXTURE_2D,level,bytes.data());glBindTexture(GL_TEXTURE_2D,dest);put(GL_TEXTURE_2D,level,format,w,h,0,size,bytes.data());
    }else{
        demand(size_t(w)*h<=32*1024*1024,"texture readback size unsupported");
        std::vector<BYTE> bytes(size_t(w)*h*(type==GL_FLOAT_32_UNSIGNED_INT_24_8_REV?8:4));glGetTexImage(GL_TEXTURE_2D,level,fmt,type,bytes.data());
        glBindTexture(GL_TEXTURE_2D,dest);real_tex_image(GL_TEXTURE_2D,level,format,w,h,0,fmt,type,bytes.data());
    }
    demand(glGetError()==GL_NO_ERROR,"texture readback fallback failed");
}
