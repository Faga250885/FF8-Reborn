// Private, bounded native-scene snapshots for repeatable renderer regression.
// Local diagnostics only: these files contain game assets and are not releases.
namespace snapshot {
struct File {
    FILE*f;bool reading;size_t total=0;unsigned version=4;
    File(const char*path,bool read):f(fopen(path,read?"rb":"wb")),reading(read){demand(f,"snapshot open failed");}
    ~File(){fclose(f);}
    void bytes(void*p,size_t n){total+=n;demand(total<768u*1024u*1024u,"snapshot size limit");
        demand((reading?fread(p,1,n,f):fwrite(p,1,n,f))==n,"snapshot IO failed");}
    template<class T>void item(T&v){bytes(&v,sizeof(v));}
    template<class T>void list(std::vector<T>&v,unsigned limit){unsigned n=(unsigned)v.size();item(n);demand(n<=limit,"snapshot list limit");if(reading)v.resize(n);if(n)bytes(v.data(),n*sizeof(T));}
    template<class T,size_t Capacity>void uniform_values(std::array<T,Capacity>&v,unsigned count){
        unsigned n=count;item(n);demand(n==count&&n<=Capacity,"snapshot uniform size");if(n)bytes(v.data(),n*sizeof(T));
    }
    void string(std::string&s){std::vector<char> value(s.begin(),s.end());list(value,1000000);if(reading)s.assign(value.begin(),value.end());}
};
static void pixel_state(){pBindBuffer(GL_PIXEL_PACK_BUFFER,0);pBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    for(GLenum base:{GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS})glPixelStorei(base,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);}
static std::string uniform_name(GLuint program,GLint location){
    GLint count;pGetProgramiv(program,GL_ACTIVE_UNIFORMS,&count);
    for(int j=0;j<count;j++){char name[256];GLsizei len;GLint size;GLenum type;pGetActiveUniform(program,j,256,&len,&size,&type,name);
        for(int e=0;e<size;e++){std::string key=name;auto bracket=key.find("[0]");if(size>1&&bracket!=std::string::npos)key.replace(bracket,3,"["+std::to_string(e)+"]");
            if(pGetUniformLocation(program,key.c_str())==location)return key;}}
    demand(false,"snapshot uniform not found");return {};
}
static GLuint program(File&file,GLuint id){
    auto attached=(PFNGLGETATTACHEDSHADERSPROC)wglGetProcAddress("glGetAttachedShaders");
    auto info=(PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    auto source=(PFNGLGETSHADERSOURCEPROC)wglGetProcAddress("glGetShaderSource");
    auto create=(PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    auto setsource=(PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    auto compile=(PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    auto make=(PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    auto attach=(PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    auto link=(PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    GLuint shaders[16]{};GLsizei n=0;if(!file.reading)attached(id,16,&n,shaders);file.item(n);demand(n>0&&n<=16,"shader count");
    GLuint result=file.reading?make():id;
    for(int j=0;j<n;j++){GLint type=0;std::string text;
        if(!file.reading){GLint length;info(shaders[j],GL_SHADER_TYPE,&type);info(shaders[j],GL_SHADER_SOURCE_LENGTH,&length);demand(length>0&&length<1000000,"shader length");
            std::vector<char> raw(length+1);source(shaders[j],length,nullptr,raw.data());text=raw.data();}
        file.item(type);file.string(text);
        if(file.reading){GLuint shader=create(type);const char*s=text.c_str();setsource(shader,1,&s,nullptr);compile(shader);GLint ok=0;info(shader,GL_COMPILE_STATUS,&ok);demand(ok,"snapshot shader compile");attach(result,shader);}}
    if(file.reading){link(result);GLint ok;pGetProgramiv(result,GL_LINK_STATUS,&ok);demand(ok,"snapshot shader link");}return result;
}
static GLuint texture(File&file,GLuint original){
    GLuint result=original;if(file.reading)glGenTextures(1,&result);glBindTexture(GL_TEXTURE_2D,result);
    for(int level=0;level<15;level++){
        GLint w=0,h=0,format=0;if(!file.reading){glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&h);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_INTERNAL_FORMAT,&format);}
        file.item(w);file.item(h);file.item(format);if(!w||!h)break;demand(w>0&&h>0&&w<=8192&&h<=8192,"snapshot texture dimensions");
        GLenum fmt=GL_RGBA,type=GL_UNSIGNED_BYTE;GLint compressed=0;unsigned bytes=unsigned(size_t(w)*h*4);
        if(file.version>=4){
            if(!file.reading){
                transfer_format(format,fmt,type);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_COMPRESSED,&compressed);
                if(compressed){GLint size=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&size);demand(size>0,"snapshot compressed size");bytes=unsigned(size);}
                else bytes=unsigned(size_t(w)*h*(type==GL_FLOAT_32_UNSIGNED_INT_24_8_REV?8:4));
            }
            file.item(fmt);file.item(type);file.item(compressed);file.item(bytes);
            demand(bytes>0&&bytes<=256u*1024u*1024u,"snapshot texture byte limit");
            GLenum expected_fmt,expected_type;transfer_format(format,expected_fmt,expected_type);
            demand(fmt==expected_fmt&&type==expected_type&&(compressed==0||compressed==1),"snapshot transfer format");
            if(!compressed)demand(bytes==size_t(w)*h*(type==GL_FLOAT_32_UNSIGNED_INT_24_8_REV?8:4),"snapshot transfer size");
        }
        std::vector<BYTE> pixels(bytes);
        if(!file.reading){
            if(compressed){auto get=(PFNGLGETCOMPRESSEDTEXIMAGEPROC)wglGetProcAddress("glGetCompressedTexImage");demand(get!=nullptr,"snapshot compressed read unavailable");get(GL_TEXTURE_2D,level,pixels.data());}
            else glGetTexImage(GL_TEXTURE_2D,level,fmt,type,pixels.data());
            demand(glGetError()==GL_NO_ERROR,"snapshot texture read failed");
        }
        file.bytes(pixels.data(),pixels.size());
        if(file.reading){
            if(compressed){auto put=(PFNGLCOMPRESSEDTEXIMAGE2DPROC)wglGetProcAddress("glCompressedTexImage2D");demand(put!=nullptr,"snapshot compressed write unavailable");put(GL_TEXTURE_2D,level,format,w,h,0,bytes,pixels.data());}
            else real_tex_image(GL_TEXTURE_2D,level,format,w,h,0,fmt,type,pixels.data());
            demand(glGetError()==GL_NO_ERROR,"snapshot texture upload failed");
        }
        if(w==1&&h==1)break;
    }return result;
}
static void seed(File&file,Target&t){
    if(!t.active)return;demand(t.w>0&&t.h>0&&t.w<=8192&&t.h<=8192,"snapshot target dimensions");
    std::vector<BYTE> color(size_t(t.w)*t.h*4);std::vector<uint32_t> depth(t.depth?size_t(t.w)*t.h:0);
    GLenum df=t.stencil?GL_DEPTH_STENCIL:GL_DEPTH_COMPONENT,dt=t.stencil?GL_UNSIGNED_INT_24_8:GL_FLOAT;
    if(!file.reading){pBindFramebuffer(GL_READ_FRAMEBUFFER,t.seed.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,t.w,t.h,GL_RGBA,GL_UNSIGNED_BYTE,color.data());
        if(t.depth)glReadPixels(0,0,t.w,t.h,df,dt,depth.data());}
    file.bytes(color.data(),color.size());if(t.depth)file.bytes(depth.data(),depth.size()*4);
    if(file.reading){t.seed={};resize(t.seed,t.w,t.h,t.depth,false,t.format,t.stencil);GLuint fbo,textures[2];pGenFramebuffers(1,&fbo);glGenTextures(2,textures);pBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glBindTexture(GL_TEXTURE_2D,textures[0]);real_tex_image(GL_TEXTURE_2D,0,t.format,t.w,t.h,0,GL_RGBA,GL_UNSIGNED_BYTE,color.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        pFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
        if(t.depth){glBindTexture(GL_TEXTURE_2D,textures[1]);real_tex_image(GL_TEXTURE_2D,0,t.stencil?GL_DEPTH24_STENCIL8:GL_DEPTH_COMPONENT24,t.w,t.h,0,df,dt,depth.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);pFramebufferTexture2D(GL_FRAMEBUFFER,t.stencil?GL_DEPTH_STENCIL_ATTACHMENT:GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,textures[1],0);}
        demand(pCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"snapshot seed framebuffer");
        blit(fbo,t.seed.fbo,t.w,t.h,t.w,t.h,GL_COLOR_BUFFER_BIT|(t.depth?GL_DEPTH_BUFFER_BIT:0)|(t.depth&&t.stencil?GL_STENCIL_BUFFER_BIT:0));
        pBindFramebuffer(GL_FRAMEBUFFER,0);pDeleteFramebuffers(1,&fbo);glDeleteTextures(2,textures);}
}
static void pair(const char*path,bool reading){
    File file(path,reading);uint32_t header[]={0x38534e50,5,sizeof(State),sizeof(Vertex),sizeof(TextureState),sizeof(Target)};
    if(reading){uint32_t got[6];file.bytes(got,sizeof(got));if(got[1]>=1&&got[1]<=5)header[1]=got[1];demand(!memcmp(got,header,sizeof(got)),"snapshot ABI/version mismatch");}else file.bytes(header,sizeof(header));
    file.version=header[1];
    struct TransferState{
        GLint read=0,draw=0;PixelTransferGuard pixels;
        TransferState(){glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);}
        ~TransferState(){pBindFramebuffer(GL_READ_FRAMEBUFFER,read);pBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);}
    } transfer;
    int recorded_stencil=stencil_bits;
    file.item(width);file.item(height);file.item(recorded_stencil);pixel_state();
    std::unordered_map<GLuint,GLuint> programs,textures;
    if(!reading)for(const Frame*f:{&previous,&current})for(const auto&op:f->ops){if(op.state.program)programs[op.state.program]=op.state.program;
        for(const auto&t:op.state.texture)if(t.copy)textures[t.copy->id]=t.copy->id;}
    unsigned n=(unsigned)programs.size();file.item(n);demand(n<=64,"snapshot program limit");
    if(reading){for(unsigned i=0;i<n;i++){GLuint id;file.item(id);programs[id]=program(file,0);}}
    else for(auto entry:programs){GLuint id=entry.first;file.item(id);program(file,id);}
    n=(unsigned)textures.size();file.item(n);demand(n<=256,"snapshot texture limit");
    if(reading){for(unsigned i=0;i<n;i++){GLuint id;file.item(id);textures[id]=texture(file,0);}}
    else for(auto entry:textures){GLuint id=entry.first;file.item(id);texture(file,id);}
    std::unordered_map<GLuint,std::shared_ptr<TextureCopy>> copies;
    for(Frame*f:{&previous,&current}){
        if(reading){f->match_input.reset();f->seed_clears.clear();}
        if(header[1]>=2)file.list(f->bones,64000);else if(reading)f->bones.clear();
        file.item(f->valid);file.item(f->started);file.item(f->vertex_count);file.item(f->w);file.item(f->h);file.item(f->mode);
        n=(unsigned)f->targets.size();file.item(n);demand(n<=32,"snapshot target limit");
        if(reading){f->targets.clear();for(unsigned j=0;j<n;j++){GLuint id;Target t;file.item(id);file.item(t);if(header[1]<3)t.stencil=t.depth&&recorded_stencil;seed(file,t);f->targets.emplace(id,t);}}
        else for(auto entry:f->targets){GLuint id=entry.first;file.item(id);file.item(entry.second);seed(file,entry.second);}
        if(reading)f->depth_texture_for_target.clear();
        if(header[1]>=5){
            unsigned count=(unsigned)f->depth_texture_for_target.size();file.item(count);demand(count<=32,"depth target limit");
            if(reading)for(unsigned j=0;j<count;j++){GLuint target,texture;file.item(target);file.item(texture);f->depth_texture_for_target[target]=texture;}
            else for(auto entry:f->depth_texture_for_target){GLuint target=entry.first;file.item(target);file.item(entry.second);}
        }
        n=(unsigned)f->ops.size();file.item(n);demand(n<=12000,"snapshot op limit");if(reading)f->ops.resize(n);
        for(auto&op:f->ops){file.item(op.clear);file.item(op.mode);file.item(op.target);file.item(op.key);file.list(op.vertices,MAX_VERTICES);
            if(header[1]>=3)file.item(op.raster);else if(reading)op.raster=RasterExtra{};
            State&s=op.state;file.bytes(&s,offsetof(State,texture));GLuint old_program=s.program;if(reading&&s.program)s.program=programs.at(old_program);
            for(auto&t:s.texture){file.bytes(&t,offsetof(TextureState,copy));GLuint copy=t.copy?t.copy->id:0;file.item(copy);
                if(reading){t.copy.reset();if(copy){auto&ref=copies[copy];if(!ref){ref=std::make_shared<TextureCopy>();ref->id=textures.at(copy);}t.copy=ref;}}}
            unsigned count=(unsigned)s.uniforms.size();file.item(count);demand(count<=1024,"snapshot uniform limit");if(reading)s.uniforms.resize(count);
            for(auto&u:s.uniforms){std::string name;if(!reading)name=uniform_name(s.program,u.location);file.string(name);file.item(u.type);
                const auto shape=uniform_shape(u.type);file.uniform_values(u.f,shape.floats);file.uniform_values(u.i,shape.integers);
                if(reading)u.location=pGetUniformLocation(s.program,name.c_str());}
        }
    }
    demand(glGetError()==GL_NO_ERROR,"snapshot GL state error");
}
}
