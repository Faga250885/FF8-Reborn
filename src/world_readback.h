// The buffers contain only one pair of samples. No result from an earlier
// native tick may grant permission to present the current pair.
static constexpr GLsizeiptr parity_plane_bytes=160*90*4;
struct ParityReadState {
    GLint fbo,buffer,pack;
    ParityReadState(){
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&fbo);glGetIntegerv(GL_READ_BUFFER,&buffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    }
    ~ParityReadState(){
        pBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glReadBuffer(buffer);
        glPopClientAttrib();pBindBuffer(GL_PIXEL_PACK_BUFFER,pack);
    }
    static void read(GLuint src,void* out){
        pBindFramebuffer(GL_READ_FRAMEBUFFER,src);glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,160,90,GL_RGBA,GL_UNSIGNED_BYTE,out);
    }
};
struct WorldReadback {
    GLuint buffer=0;
    bool checked=false,allocated=false;
    PFNGLMAPBUFFERRANGEPROC map=nullptr;
    PFNGLUNMAPBUFFERPROC unmap=nullptr;
    unsigned cooldown=0,slow_queues=0;
    double queue_time=0,map_time=0;
    unsigned queues=0,maps=0,failures=0,backoffs=0;
    template<class Proc> static Proc optional(const char* name){
        const auto p=wglGetProcAddress(name);
        return uintptr_t(p)>3&&uintptr_t(p)!=UINTPTR_MAX?reinterpret_cast<Proc>(p):nullptr;
    }
    bool available(){
        if(!checked){
            map=optional<PFNGLMAPBUFFERRANGEPROC>("glMapBufferRange");
            unmap=optional<PFNGLUNMAPBUFFERPROC>("glUnmapBuffer");
            checked=true;
        }
        if(!map||!unmap)return false;
        if(cooldown){--cooldown;return false;}
        return true;
    }
    bool enqueue(){
        if(!map||!unmap||cooldown)return false;
        const bool was_allocated=allocated;
        const double begin=now();
        bool okay=false;
        {
            ParityReadState guard;
            if(!buffer)pGenBuffers(1,&buffer);
            if(buffer){
                pBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
                if(!allocated){
                    pBufferData(GL_PIXEL_PACK_BUFFER,2*parity_plane_bytes,nullptr,GL_STREAM_READ);
                    GLint size=0;pGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_SIZE,&size);
                    allocated=size==2*parity_plane_bytes;
                }
                if(allocated){
                    ParityReadState::read(sample_a.fbo,nullptr);
                    ParityReadState::read(sample_b.fbo,reinterpret_cast<void*>(parity_plane_bytes));
                }
            }
            const GLenum error=glGetError();
            okay=allocated&&error==GL_NO_ERROR;
        }
        const double elapsed=now()-begin;queue_time+=elapsed;++queues;
        if(!okay){++failures;cooldown=600;return false;}
        // A driver is allowed to synchronously wait even for a pack buffer.
        // Exclude first allocation. Exit this path if it repeatedly stalls.
        if(was_allocated){
            if(elapsed>.0015)++slow_queues;
            else slow_queues=0;
            if(slow_queues>=3){++backoffs;slow_queues=0;cooldown=600;}
        }
        return true;
    }
    bool copy(BYTE* a,BYTE* b){
        const double begin=now();
        GLint old;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&old);pBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
        const BYTE* ptr=static_cast<const BYTE*>(map(GL_PIXEL_PACK_BUFFER,0,2*parity_plane_bytes,GL_MAP_READ_BIT));
        bool okay=false;
        if(ptr){
            memcpy(a,ptr,parity_plane_bytes);memcpy(b,ptr+parity_plane_bytes,parity_plane_bytes);
            okay=unmap(GL_PIXEL_PACK_BUFFER)==GL_TRUE;
        }
        const GLenum error=glGetError();
        pBindBuffer(GL_PIXEL_PACK_BUFFER,old);
        map_time+=now()-begin;++maps;
        if(!okay||error!=GL_NO_ERROR){++failures;cooldown=600;return false;}
        return true;
    }
    void report(){
        if(!queues&&!maps)return;
        char line[192];snprintf(line,sizeof(line),"world PBO | queue %.3f ms (%u) | map %.3f ms (%u) | errors %u | slow queues disabled %u\n",
            queues?1000*queue_time/queues:0,queues,maps?1000*map_time/maps:0,maps,failures,backoffs);
        log(line);queue_time=map_time=0;queues=maps=failures=backoffs=0;
    }
};
static WorldReadback world_readback;
