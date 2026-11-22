#define WIN32_LEAN_AND_MEAN
#include <memory_resource>
#include <windows.h>
#include <commctrl.h>
#include <xinput.h>
#include <GL/glew.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <array>
#include <string>
#include <cstddef>
#include <atomic>
#ifdef GEO_NATIVE_CAPTURE
#include <winhttp.h>
#include "update_version.h"
#include "update_feed.h"
#include "update_slots.h"
#endif

// Geometry replay, never optical flow. All GL work stays on the game's thread.
// Unsupported frames stay native. Replay must pass an image parity test first.
namespace geo {
#ifdef GEO_STEAM_DECK
static constexpr bool deck_renderer=true;
#else
static constexpr bool deck_renderer=false;
#endif
#define API_LIST(X) \
 X(PFNGLACTIVETEXTUREPROC,ActiveTexture) X(PFNGLCLIENTACTIVETEXTUREPROC,ClientActiveTexture) \
 X(PFNGLUSEPROGRAMPROC,UseProgram) X(PFNGLGETPROGRAMIVPROC,GetProgramiv) \
 X(PFNGLGETACTIVEATTRIBPROC,GetActiveAttrib) \
 X(PFNGLGETACTIVEUNIFORMPROC,GetActiveUniform) X(PFNGLGETUNIFORMLOCATIONPROC,GetUniformLocation) \
 X(PFNGLGETUNIFORMFVPROC,GetUniformfv) X(PFNGLGETUNIFORMIVPROC,GetUniformiv) \
 X(PFNGLUNIFORM1FVPROC,Uniform1fv) X(PFNGLUNIFORM2FVPROC,Uniform2fv) \
 X(PFNGLUNIFORM3FVPROC,Uniform3fv) X(PFNGLUNIFORM4FVPROC,Uniform4fv) \
 X(PFNGLUNIFORM1IVPROC,Uniform1iv) X(PFNGLUNIFORM2IVPROC,Uniform2iv) \
 X(PFNGLUNIFORM3IVPROC,Uniform3iv) X(PFNGLUNIFORM4IVPROC,Uniform4iv) \
 X(PFNGLUNIFORMMATRIX2FVPROC,UniformMatrix2fv) X(PFNGLUNIFORMMATRIX3FVPROC,UniformMatrix3fv) \
 X(PFNGLUNIFORMMATRIX4FVPROC,UniformMatrix4fv) X(PFNGLBINDBUFFERPROC,BindBuffer) \
 X(PFNGLGENBUFFERSPROC,GenBuffers) X(PFNGLBUFFERDATAPROC,BufferData) X(PFNGLGETBUFFERPARAMETERIVPROC,GetBufferParameteriv) \
 X(PFNGLGENFRAMEBUFFERSPROC,GenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC,BindFramebuffer) \
 X(PFNGLDELETEFRAMEBUFFERSPROC,DeleteFramebuffers) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,CheckFramebufferStatus) \
 X(PFNGLGENRENDERBUFFERSPROC,GenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC,BindRenderbuffer) \
 X(PFNGLDELETERENDERBUFFERSPROC,DeleteRenderbuffers) X(PFNGLRENDERBUFFERSTORAGEPROC,RenderbufferStorage) \
 X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,FramebufferRenderbuffer) X(PFNGLBLITFRAMEBUFFERPROC,BlitFramebuffer) \
 X(PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC,GetFramebufferAttachmentParameteriv) X(PFNGLFRAMEBUFFERTEXTURE2DPROC,FramebufferTexture2D) \
 X(PFNGLBLENDFUNCSEPARATEPROC,BlendFuncSeparate) X(PFNGLBLENDEQUATIONSEPARATEPROC,BlendEquationSeparate) \
 X(PFNGLBLENDCOLORPROC,BlendColor) X(PFNGLSTENCILFUNCSEPARATEPROC,StencilFuncSeparate) \
 X(PFNGLSTENCILOPSEPARATEPROC,StencilOpSeparate) X(PFNGLSTENCILMASKSEPARATEPROC,StencilMaskSeparate)
#define DECLARE(type,name) static type p##name;
API_LIST(DECLARE)
#undef DECLARE
static bool loaded=false,inside=false,enabled=true,can_present=false,accessibility_test=false,diagnostic_trace=false;
#include "retro_settings.h"
struct DisplaySettings {
    int fps=60;
    int fps_counter=0;
    bool automatic_fps=false;
    bool wide=true,atlas=true,field_sides=true;
    RetroSettings retro;
    void load(const wchar_t* path){
        const int value=GetPrivateProfileIntW(L"Display",L"FPS",-1,path);
        automatic_fps=value!=0&&value!=60&&value!=120;
        fps=value==0||value==120?value:60;
        if(deck_renderer){fps=value==60||value==90||value==120?60:0;automatic_fps=false;}
        wide=GetPrivateProfileIntW(L"Display",L"Wide",1,path)!=0;
        atlas=GetPrivateProfileIntW(L"Display",L"Atlas",1,path)!=0;
        field_sides=GetPrivateProfileIntW(L"Display",L"FieldSides",1,path)!=0;
        fps_counter=std::clamp(int(GetPrivateProfileIntW(L"Display",L"FPSCounter",0,path)),0,3);
        retro.load(path);
    }
} static display_settings;
static std::wstring retro_ini_path;
#include "retro_panel.h"
#ifdef GEO_NATIVE_CAPTURE
#include "update_menu.h"
#endif
static HGLRC context;
static HDC dc;
static std::atomic<DWORD> thread{0};
static HMODULE engine;
static BOOL (WINAPI *real_swap)(HDC)=SwapBuffers;
static BOOL (WINAPI *real_delete_context)(HGLRC)=wglDeleteContext;
static FARPROC (WINAPI *real_get_proc_address)(HMODULE,LPCSTR)=GetProcAddress;
static DWORD (WINAPI *real_wait)(HANDLE,DWORD)=WaitForSingleObject;
static VOID (WINAPI *real_sleep)(DWORD)=Sleep;
static BOOL (WINAPI *real_counter)(LARGE_INTEGER*)=QueryPerformanceCounter;
static void (APIENTRY *real_draw)(GLenum,GLsizei,GLenum,const void*)=glDrawElements;
static void (APIENTRY *real_clear)(GLbitfield)=glClear;
static void (APIENTRY *real_tex_image)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)=glTexImage2D;
static void (APIENTRY *real_tex_subimage)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*)=glTexSubImage2D;
static void (APIENTRY *real_copy_tex)(GLenum,GLint,GLenum,GLint,GLint,GLsizei,GLsizei,GLint)=glCopyTexImage2D;
static void (APIENTRY *real_delete_textures)(GLsizei,const GLuint*)=glDeleteTextures;
static PFNGLCOPYIMAGESUBDATAPROC copy_image;
static PFNGLGETPROGRAMRESOURCEIVPROC terrain_resource;
static LARGE_INTEGER frequency;
static double origin=0,period=1./30.,next_output=0,last_swap=0;
static unsigned sources=0,outputs=0,parity_ok=0,parity_fail=0,moving_draws=0;
static unsigned incoherent_draws=0;
static unsigned matched_candidates=0,singular_rejects=0;
static double replay_seconds=0,presentation_seconds=0;static unsigned replay_count=0,presentation_count=0;
static double capture_seconds=0,matching_seconds=0,parity_seconds=0;static unsigned capture_count=0,matching_count=0;
static unsigned waits=0;
static HANDLE output_timer=nullptr;
static int width=0,height=0,stencil_bits=0;
static int saved_interval=1;static bool interval_changed=false;
static BOOL (WINAPI *set_interval)(int);
// Keep the last verified wide layout briefly. The swap interval belongs to
// the display mode and can remain stable even after that layout expires.
struct DeckSwapInterval {
    int mode=0, w=0, h=0;
    unsigned grace=0, held=0, changes=0, failures=0;
    double until=0, retry_after=0;
    bool within(int scene,int width,int height,double time)const{
        return scene&&mode==scene&&width==w&&height==h&&grace&&time<until;
    }
    bool keep(int scene,int width,int height,bool valid,double time){
        if(valid&&scene){
            mode=scene;w=width;h=height;grace=3;until=time+.3;
        }else if(scene&&mode==scene&&w==width&&h==height){
            if(grace)--grace;
            ++held;return true;
        }else{
            mode=0;grace=0;
        }
        return valid;
    }
};
static DeckSwapInterval deck_interval;
static const unsigned MAX_VERTICES=300000,MAX_OPS=12000;

static double now() { LARGE_INTEGER t;QueryPerformanceCounter(&t);return double(t.QuadPart)/frequency.QuadPart; }
static void log(const char *s) {FILE*f=fopen("FF8_geometry.log","a");if(f){fputs(s,f);fclose(f);}}
static void demand(bool ok,const char *s) {if(!ok)throw std::runtime_error(s);}
static void cap(GLenum name,bool on) {if(on)glEnable(name);else glDisable(name);}
static void APIENTRY qa_gl_message(GLenum,GLenum type,GLuint,GLenum,GLsizei len,const GLchar*msg,const void*){
    if(type!=GL_DEBUG_TYPE_ERROR||len<=0)return;
    static std::vector<std::string> seen;std::string s(msg,(size_t)std::min(len,512));
    if(seen.size()>=24||std::find(seen.begin(),seen.end(),s)!=seen.end())return;seen.push_back(s);
    log("QA GL: ");log(s.c_str());log("\n");
}
static void initialize_deck_bindings();
static bool initialize_gl() {
#define LOAD(type,name) p##name=(type)wglGetProcAddress("gl" #name);if((uintptr_t)p##name<=3 || (uintptr_t)p##name==UINTPTR_MAX)return false;
    API_LIST(LOAD)
#undef LOAD
    QueryPerformanceFrequency(&frequency);context=wglGetCurrentContext();dc=wglGetCurrentDC();thread=GetCurrentThreadId();
    glGetIntegerv(GL_STENCIL_BITS,&stencil_bits);loaded=true;
    terrain_resource=nullptr;
    if(deck_renderer){
        initialize_deck_bindings();
        int major=0,minor=0;
        const char* version=(const char*)glGetString(GL_VERSION);
        if(version&&sscanf(version,"%d.%d",&major,&minor)==2&&(major>4||(major==4&&minor>=3))){
            auto proc=wglGetProcAddress("glGetProgramResourceiv");
            if((uintptr_t)proc>3&&(uintptr_t)proc!=UINTPTR_MAX)terrain_resource=(PFNGLGETPROGRAMRESOURCEIVPROC)proc;
        }
    }
    if(diagnostic_trace){auto callback=(PFNGLDEBUGMESSAGECALLBACKPROC)wglGetProcAddress("glDebugMessageCallback");
        if((uintptr_t)callback>3&&(uintptr_t)callback!=UINTPTR_MAX){glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);callback(qa_gl_message,nullptr);}}
    if(!output_timer)output_timer=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_MODIFY_STATE|SYNCHRONIZE);
    copy_image=(PFNGLCOPYIMAGESUBDATAPROC)wglGetProcAddress("glCopyImageSubData");
    set_interval=(BOOL(WINAPI*)(int))wglGetProcAddress("wglSwapIntervalEXT");
    auto get_interval=(int(WINAPI*)())wglGetProcAddress("wglGetSwapIntervalEXT");
    if((uintptr_t)get_interval>3&&(uintptr_t)get_interval!=UINTPTR_MAX)saved_interval=get_interval();
    return true;
}

#include "deck_world_bindings.h"
#include "deck_programs.h"
struct Uniform {
    GLint location;GLenum type;std::array<float,16> f{};std::array<GLint,4> i{};
};
struct UniformShape {unsigned floats,integers;};
static UniformShape uniform_shape(GLenum type){
    switch(type) {
    case GL_FLOAT:return {1,0};case GL_FLOAT_VEC2:return {2,0};case GL_FLOAT_VEC3:return {3,0};
    case GL_FLOAT_VEC4:case GL_FLOAT_MAT2:return {4,0};
    case GL_FLOAT_MAT3:return {9,0};case GL_FLOAT_MAT4:return {16,0};
    case GL_INT:case GL_BOOL:case GL_SAMPLER_2D:return {0,1};
    case GL_INT_VEC2:case GL_BOOL_VEC2:return {0,2};
    case GL_INT_VEC3:case GL_BOOL_VEC3:return {0,3};
    case GL_INT_VEC4:case GL_BOOL_VEC4:return {0,4};
    default:throw std::runtime_error("unsupported shader uniform type");}
}
static bool same_uniform(const Uniform& a,const Uniform& b){
    if(a.location!=b.location||a.type!=b.type)return false;
    const auto shape=uniform_shape(a.type);
    return std::equal(a.f.begin(),a.f.begin()+shape.floats,b.f.begin())&&
           std::equal(a.i.begin(),a.i.begin()+shape.integers,b.i.begin());
}
static void read_uniform(GLuint program,GLint location,GLenum type,std::vector<Uniform>& out){
    auto& u=out.emplace_back();u.location=location;u.type=type;
    if(uniform_shape(type).integers){pGetUniformiv(program,u.location,u.i.data());
        if(type==GL_SAMPLER_2D)demand(u.i[0]>=0&&u.i[0]<4,"sampler outside captured units");}
    else pGetUniformfv(program,u.location,u.f.data());
}
static std::vector<Uniform> uniforms(GLuint program,unsigned* inputs=nullptr,bool terrain=false) {
    if(inputs)*inputs=program?1u:127u;
    std::vector<Uniform> out;if(!program)return out;
    // Locations and input semantics belong to the link. Values (including
    // sampler unit selections) still belong to the individual draw.
    // Reflection describes the linked shader for every primitive type.
    // Fields submit many tile strips and quads through the same program.
    const bool cache=deck_renderer&&display_settings.fps&&inputs&&deck_programs::available();
    if(cache){
        auto entry=deck_programs::layouts.find(program);
        if(entry!=deck_programs::layouts.end()){
            *inputs=entry->second.inputs;out.reserve(entry->second.uniforms.size());
            for(const auto& spec:entry->second.uniforms)read_uniform(program,spec.first,spec.second,out);
            return out;
        }
    }
    GLint count=0,attrs=0;pGetProgramiv(program,GL_ACTIVE_UNIFORMS,&count);pGetProgramiv(program,GL_ACTIVE_ATTRIBUTES,&attrs);
    demand(count<=128 && attrs<=16,"shader attribute/uniform count unsupported");
    for(GLint j=0;j<attrs;j++) {char name[256];GLsizei len;GLint size;GLenum type;
        pGetActiveAttrib(program,j,sizeof(name),&len,&size,&type,name);
        unsigned bit=!strcmp(name,"gl_Vertex")?1u:!strcmp(name,"gl_Normal")?2u:!strcmp(name,"gl_Color")?4u:0u;
        static const char* const coordinates[]={"gl_MultiTexCoord0","gl_MultiTexCoord1","gl_MultiTexCoord2","gl_MultiTexCoord3"};
        if(!bit)for(int unit=0;unit<4;unit++)if(!strcmp(name,coordinates[unit])){bit=8u<<unit;break;}
        demand(bit!=0,"shader uses unsupported generic attributes");if(inputs)*inputs|=bit;}
    out.reserve(count);
    for(GLint j=0;j<count;j++) {
        // Query the linked interface on this draw so relinks and reused program
        // names are safe. Arrays retain the name-based per-element lookup.
        if(terrain&&terrain_resource){
            constexpr GLenum properties[]={GL_TYPE,GL_ARRAY_SIZE,GL_LOCATION};
            GLint values[3]={};GLsizei returned=0;
            terrain_resource(program,GL_UNIFORM,j,3,properties,3,&returned,values);
            demand(returned==3,"terrain uniform reflection failed");
            if(values[2]<0)continue;
            if(values[1]==1){read_uniform(program,values[2],values[0],out);continue;}
        }
        char name[256];GLsizei len=0;GLint size=0;GLenum type=0;
        pGetActiveUniform(program,j,sizeof(name),&len,&size,&type,name);
        demand(len>0 && len<255 && size>0 && size<=128,"uniform shape unsupported");
        for(GLint element=0;element<size;element++) {
            char indexed[300];const char *key=name;
            if(size>1) {char *bracket=strstr(name,"[0]");demand(bracket,"uniform array name unsupported");
                snprintf(indexed,sizeof(indexed),"%.*s[%d]%s",int(bracket-name),name,element,bracket+3);key=indexed;}
            const GLint location=pGetUniformLocation(program,key);
            if(location>=0)read_uniform(program,location,type,out);
        }
    }
    if(cache){
        deck_programs::Layout layout;layout.inputs=*inputs;layout.uniforms.reserve(out.size());
        for(const auto& u:out)layout.uniforms.emplace_back(u.location,u.type);
        deck_programs::layouts.emplace(program,std::move(layout));
    }
    return out;
}
static void apply_uniforms(GLuint program,const std::vector<Uniform>& values,const std::vector<Uniform>* previous=nullptr) {
    if(!previous)pUseProgram(program);
    for(size_t index=0;index<values.size();++index){
        const auto& u=values[index];
        if(previous&&index<previous->size()){
            const auto& before=(*previous)[index];
            if(same_uniform(u,before))continue;
        }
        const GLfloat*f=u.f.data();const GLint*i=u.i.data();
        switch(u.type){
        case GL_FLOAT:pUniform1fv(u.location,1,f);break;case GL_FLOAT_VEC2:pUniform2fv(u.location,1,f);break;
        case GL_FLOAT_VEC3:pUniform3fv(u.location,1,f);break;case GL_FLOAT_VEC4:pUniform4fv(u.location,1,f);break;
        case GL_FLOAT_MAT2:pUniformMatrix2fv(u.location,1,GL_FALSE,f);break;
        case GL_FLOAT_MAT3:pUniformMatrix3fv(u.location,1,GL_FALSE,f);break;
        case GL_FLOAT_MAT4:pUniformMatrix4fv(u.location,1,GL_FALSE,f);break;
        case GL_INT:case GL_BOOL:case GL_SAMPLER_2D:pUniform1iv(u.location,1,i);break;
        case GL_INT_VEC2:case GL_BOOL_VEC2:pUniform2iv(u.location,1,i);break;
        case GL_INT_VEC3:case GL_BOOL_VEC3:pUniform3iv(u.location,1,i);break;
        case GL_INT_VEC4:case GL_BOOL_VEC4:pUniform4iv(u.location,1,i);break;
        }
    }
}

struct Surface {GLuint fbo=0,color=0,depth=0;int w=0,h=0;bool texture=false,has_depth=false,has_stencil=false;GLenum format=0;};
static void release(Surface&s){
    if(s.fbo)pDeleteFramebuffers(1,&s.fbo);
    if(s.color){if(s.texture)glDeleteTextures(1,&s.color);else pDeleteRenderbuffers(1,&s.color);}
    if(s.depth)pDeleteRenderbuffers(1,&s.depth);s=Surface{};
}
static void resize(Surface&s,int w,int h,bool depth=true,bool texture=false,GLenum format=GL_RGBA8,int stencil=-1) {
    bool packed=depth&&(stencil<0?stencil_bits>0:stencil!=0);
    if(s.w==w&&s.h==h&&s.has_depth==depth&&s.has_stencil==packed&&s.format==format)return;
    GLint r,d,b,tex;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&r);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&d);glGetIntegerv(GL_RENDERBUFFER_BINDING,&b);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex);
    if(!s.fbo)pGenFramebuffers(1,&s.fbo);s.texture=texture;
    pBindFramebuffer(GL_FRAMEBUFFER,s.fbo);
    if(texture){if(!s.color)glGenTextures(1,&s.color);glBindTexture(GL_TEXTURE_2D,s.color);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        pFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.color,0);
    }else{if(!s.color)pGenRenderbuffers(1,&s.color);pBindRenderbuffer(GL_RENDERBUFFER,s.color);
        pRenderbufferStorage(GL_RENDERBUFFER,format,w,h);pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,s.color);}
    if(depth){if(!s.depth)pGenRenderbuffers(1,&s.depth);pBindRenderbuffer(GL_RENDERBUFFER,s.depth);
        pRenderbufferStorage(GL_RENDERBUFFER,packed?GL_DEPTH24_STENCIL8:GL_DEPTH_COMPONENT24,w,h);
        pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,s.depth);
        pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,packed?s.depth:0);}
    else {pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,0);pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);}
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    bool complete=pCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    pBindFramebuffer(GL_READ_FRAMEBUFFER,r);pBindFramebuffer(GL_DRAW_FRAMEBUFFER,d);pBindRenderbuffer(GL_RENDERBUFFER,b);glBindTexture(GL_TEXTURE_2D,tex);
    demand(complete,"framebuffer creation failed");s.w=w;s.h=h;s.has_depth=depth;s.has_stencil=packed;s.format=format;
}
static void blit(GLuint from,GLuint to,int w,int h,int tw,int th,GLbitfield bits,int inset_x=0,int inset_y=0,int dest_y=0,int dest_x=0) {
    GLint r,d,rb,db;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&r);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&d);
    glGetIntegerv(GL_READ_BUFFER,&rb);glGetIntegerv(GL_DRAW_BUFFER,&db);
    glPushAttrib(GL_SCISSOR_BIT);glDisable(GL_SCISSOR_TEST);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,from);pBindFramebuffer(GL_DRAW_FRAMEBUFFER,to);
    glReadBuffer(from?GL_COLOR_ATTACHMENT0:GL_BACK);glDrawBuffer(to?GL_COLOR_ATTACHMENT0:GL_BACK);
    pBlitFramebuffer(inset_x,inset_y,w-inset_x,h-inset_y,dest_x,dest_y,dest_x+tw,dest_y+th,bits,
                     (inset_x||inset_y)?GL_LINEAR:GL_NEAREST);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,r);pBindFramebuffer(GL_DRAW_FRAMEBUFFER,d);
    glReadBuffer(rb);glDrawBuffer(db);glPopAttrib();
}
static GLbitfield all_buffers(){return GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|(stencil_bits?GL_STENCIL_BUFFER_BIT:0);}
#include "retro_display.h"
struct TextureParameters {
    GLint min_filter,mag_filter,wrap_s,wrap_t,base_level,max_level;
    GLfloat border[4],min_lod,max_lod,lod_bias;
};
static TextureParameters texture_parameters(){
    TextureParameters p{};
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&p.min_filter);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&p.mag_filter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&p.wrap_s);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&p.wrap_t);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&p.base_level);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&p.max_level);
    glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,p.border);glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,&p.min_lod);
    glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,&p.max_lod);glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,&p.lod_bias);return p;
}
static void apply_texture_parameters(const TextureParameters&p){
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,p.min_filter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,p.mag_filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,p.wrap_s);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,p.wrap_t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,p.base_level);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,p.max_level);
    glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,p.border);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,p.min_lod);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,p.max_lod);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,p.lod_bias);
}
// Reuse only storage from released, single-level colour snapshots. Every
// checkout still copies all pixels from this native draw. At most 32 MiB.
struct SpareTexture {GLuint id=0;GLint w=0,h=0,format=0;};
static std::array<SpareTexture,32> spare_textures{};
static unsigned reused_storage=0;
struct TextureCopy {
    GLuint id=0;
    SpareTexture storage{};
    bool checked_map_detail=false,eligible_map_detail=false;
    ~TextureCopy(){if(context&&wglGetCurrentContext()==context){
        if(id&&storage.id)for(auto& slot:spare_textures)if(!slot.id){slot=storage;id=0;break;}
        if(id)glDeleteTextures(1,&id);
    }}
};
static std::unordered_map<GLuint,std::shared_ptr<TextureCopy>> texture_cache;
static unsigned texture_copies=0,texture_updates=0;
struct TextureState {GLint id=0,env=GL_MODULATE;bool enabled=false;float matrix[16];TextureParameters parameters{};std::shared_ptr<TextureCopy> copy;};
#include "texture_transfer.h"
#include "fps_counter.h"
#include "retro_panel_draw.h"
static std::shared_ptr<TextureCopy> freeze_texture(GLuint id){
    auto found=texture_cache.find(id);if(found!=texture_cache.end())return found->second;
    demand((uintptr_t)copy_image>3&&(uintptr_t)copy_image!=UINTPTR_MAX,"texture snapshots require copy image support");
    GLint old,pbo;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);
    auto snapshot=std::make_shared<TextureCopy>();
    SpareTexture reusable{};
    // Do not misattribute a game/driver error from an earlier draw to this copy.
    // Output validation still compares the full rendered scene on every native frame.
    GLenum prior=glGetError();if(prior&&accessibility_test){static bool once=false;if(!once){once=true;char msg[80];snprintf(msg,sizeof(msg),"QA error pending before texture copy: 0x%x\n",prior);log(msg);}}
    pBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    try{
        for(int level=0;level<15;level++){
            glBindTexture(GL_TEXTURE_2D,id);GLint w,h,format;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&h);
            if(!w||!h)break;
            demand(w<=16384&&h<=16384,"texture snapshot dimensions unsupported");
            glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_INTERNAL_FORMAT,&format);
            bool allocated=false;
            if(level==0){
                if(w<=512&&h<=512&&(format==GL_RGB8||format==GL_RGBA8)){
                    GLint mip=0,base=0;
                    glGetTexLevelParameteriv(GL_TEXTURE_2D,1,GL_TEXTURE_WIDTH,&mip);
                    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&base);
                    if(!mip&&base==0){
                        for(auto& slot:spare_textures)if(slot.id&&slot.w==w&&slot.h==h&&slot.format==format){
                            snapshot->id=slot.id;slot={};allocated=true;reused_storage++;break;
                        }
                        reusable={0,w,h,format};
                    }
                }
                if(!snapshot->id)glGenTextures(1,&snapshot->id);
            }
            glBindTexture(GL_TEXTURE_2D,snapshot->id);
            GLenum fmt,type;transfer_format(format,fmt,type);
            if(!allocated)real_tex_image(GL_TEXTURE_2D,level,format,w,h,0,fmt,type,nullptr);
            else{
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,1000);
            }
            // CopyImageSubData requires complete texture objects. A single
            // level filter avoids incompleteness while mip levels are allocated.
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            GLint min_filter;glBindTexture(GL_TEXTURE_2D,id);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&min_filter);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            copy_image(id,GL_TEXTURE_2D,level,0,0,0,snapshot->id,GL_TEXTURE_2D,level,0,0,0,w,h,1);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,min_filter);
            GLenum copy_error=glGetError();if(copy_error){
                if(accessibility_test){static int notice=0;if(notice++<8){char msg[160];snprintf(msg,sizeof(msg),"QA texture transfer id=%u format=%x level=%d %dx%d error=%x\n",id,format,level,w,h,copy_error);log(msg);}}
                copy_level_readback(id,snapshot->id,level,w,h,format,fmt,type);}
            if(w==1&&h==1)break;
        }
        if(!snapshot->id)glGenTextures(1,&snapshot->id);
        GLenum error=glGetError();demand(error==GL_NO_ERROR,"texture content snapshot failed");
    }catch(...){glBindTexture(GL_TEXTURE_2D,old);pBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);throw;}
    glBindTexture(GL_TEXTURE_2D,old);pBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    if(reusable.w){reusable.id=snapshot->id;snapshot->storage=reusable;}
    texture_cache[id]=snapshot;texture_copies++;return snapshot;
}
static void invalidate_texture(GLenum target){
    if(inside||!loaded||target!=GL_TEXTURE_2D||wglGetCurrentContext()!=context)return;
    if(texture_cache.empty())return;
    GLint id;glGetIntegerv(GL_TEXTURE_BINDING_2D,&id);if(texture_cache.erase(id))texture_updates++;
}
static void APIENTRY tex_image_hook(GLenum target,GLint level,GLint format,GLsizei w,GLsizei h,GLint border,GLenum pixel_format,GLenum type,const void*data){
    invalidate_texture(target);real_tex_image(target,level,format,w,h,border,pixel_format,type,data);
}
static void APIENTRY tex_subimage_hook(GLenum target,GLint level,GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,const void*data){
    invalidate_texture(target);real_tex_subimage(target,level,x,y,w,h,format,type,data);
}
static void APIENTRY copy_tex_hook(GLenum target,GLint level,GLenum format,GLint x,GLint y,GLsizei w,GLsizei h,GLint border){
    invalidate_texture(target);real_copy_tex(target,level,format,x,y,w,h,border);
}
static void APIENTRY delete_textures_hook(GLsizei count,const GLuint*ids){
    if(!inside&&loaded&&wglGetCurrentContext()==context)for(int i=0;i<count;i++)texture_cache.erase(ids[i]);
    real_delete_textures(count,ids);
}
#include "field_raster.h"
struct State {
    GLint viewport[4],scissor[4],depth_func,cull_face,front_face,alpha_func,shade,logic,polygon[2];
    GLint src_rgb,dst_rgb,src_alpha,dst_alpha,eq_rgb,eq_alpha;GLint program=0;
    bool depth,blend,cull,alpha,scissor_on,logic_on,offset_fill,srgb;
    GLboolean mask[4],depth_mask;GLdouble depth_range[2],clear_depth;
    GLfloat blend_color[4],clear_color[4],alpha_ref,offset_factor,offset_units,line_width,point_size;
    GLint clear_stencil;float model[16],projection[16];TextureState texture[4];std::vector<Uniform> uniforms;
};
static bool uses_blend_constant(GLint factor){
    return factor==GL_CONSTANT_COLOR||factor==GL_ONE_MINUS_CONSTANT_COLOR||
           factor==GL_CONSTANT_ALPHA||factor==GL_ONE_MINUS_CONSTANT_ALPHA;
}
static State get_state(RasterExtra* extra=nullptr,bool stencil_clear=false,unsigned* inputs=nullptr,bool terrain_draw=false) {
    State s{};GLint active;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    if(extra)read_extra(*extra,stencil_clear);
    for(int j=0;j<6;j++)demand(!glIsEnabled(GL_CLIP_PLANE0+j),"clip planes unsupported");
    glGetIntegerv(GL_VIEWPORT,s.viewport);glGetIntegerv(GL_SCISSOR_BOX,s.scissor);
    glGetIntegerv(GL_CURRENT_PROGRAM,&s.program);s.uniforms=uniforms(s.program,inputs,terrain_draw);
    if(extra){demand(!extra->lighting||s.program||stencil_clear,"fixed-function light parameters not captured");
        demand(!extra->fog||s.program||extra->fog_source==GL_FRAGMENT_DEPTH,"fixed-function fog coordinate array unsupported");}
    glGetFloatv(GL_MODELVIEW_MATRIX,s.model);glGetFloatv(GL_PROJECTION_MATRIX,s.projection);
    s.depth=glIsEnabled(GL_DEPTH_TEST);s.blend=glIsEnabled(GL_BLEND);s.cull=glIsEnabled(GL_CULL_FACE);
    s.alpha=glIsEnabled(GL_ALPHA_TEST);s.scissor_on=glIsEnabled(GL_SCISSOR_TEST);s.logic_on=glIsEnabled(GL_COLOR_LOGIC_OP);
    s.offset_fill=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    s.srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
#define GI(key,field) glGetIntegerv(key,&s.field)
    GI(GL_FRONT_FACE,front_face);GI(GL_SHADE_MODEL,shade);
    if(!terrain_draw||s.depth){GI(GL_DEPTH_FUNC,depth_func);}else s.depth_func=GL_LESS;
    if(!terrain_draw||s.cull){GI(GL_CULL_FACE_MODE,cull_face);}else s.cull_face=GL_BACK;
    if(!terrain_draw||s.alpha){GI(GL_ALPHA_TEST_FUNC,alpha_func);glGetFloatv(GL_ALPHA_TEST_REF,&s.alpha_ref);}else s.alpha_func=GL_ALWAYS;
    if(!terrain_draw||s.logic_on){GI(GL_LOGIC_OP_MODE,logic);}else s.logic=GL_COPY;
    if(!terrain_draw||s.blend){
        GI(GL_BLEND_SRC_RGB,src_rgb);GI(GL_BLEND_DST_RGB,dst_rgb);
        GI(GL_BLEND_SRC_ALPHA,src_alpha);GI(GL_BLEND_DST_ALPHA,dst_alpha);GI(GL_BLEND_EQUATION_RGB,eq_rgb);GI(GL_BLEND_EQUATION_ALPHA,eq_alpha);
        if(!terrain_draw||uses_blend_constant(s.src_rgb)||uses_blend_constant(s.dst_rgb)||
           uses_blend_constant(s.src_alpha)||uses_blend_constant(s.dst_alpha))
            glGetFloatv(GL_BLEND_COLOR,s.blend_color);
    }else{s.src_rgb=s.src_alpha=GL_ONE;s.dst_rgb=s.dst_alpha=GL_ZERO;s.eq_rgb=s.eq_alpha=GL_FUNC_ADD;}
    // A clear is recorded separately with its actual masks and clear values.
    if(!terrain_draw){
        GI(GL_STENCIL_CLEAR_VALUE,clear_stencil);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE,&s.clear_depth);glGetFloatv(GL_COLOR_CLEAR_VALUE,s.clear_color);
    }else s.clear_depth=1;
#undef GI
    glGetIntegerv(GL_POLYGON_MODE,s.polygon);glGetBooleanv(GL_COLOR_WRITEMASK,s.mask);glGetBooleanv(GL_DEPTH_WRITEMASK,&s.depth_mask);
    glGetDoublev(GL_DEPTH_RANGE,s.depth_range);
    if(!terrain_draw||s.offset_fill){
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&s.offset_factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&s.offset_units);
    }
    s.line_width=s.point_size=1;
    if(!terrain_draw||s.polygon[0]==GL_LINE||s.polygon[1]==GL_LINE)glGetFloatv(GL_LINE_WIDTH,&s.line_width);
    if(!terrain_draw||s.polygon[0]==GL_POINT||s.polygon[1]==GL_POINT)glGetFloatv(GL_POINT_SIZE,&s.point_size);
    // Query only units restored below; reject combining until implemented.
    unsigned sampled=0;
    for(const auto& u:s.uniforms)if(u.type==GL_SAMPLER_2D)sampled|=1u<<u.i[0];
    for(int j=0;j<4;j++){pActiveTexture(GL_TEXTURE0+j);auto&t=s.texture[j];
        const bool unused_sampler=deck_renderer&&inputs&&s.program&&!(sampled&(1u<<j));
        if(!unused_sampler)t.enabled=glIsEnabled(GL_TEXTURE_2D);
        const bool used=!inputs||(s.program?bool(sampled&(1u<<j)):t.enabled);
        if(used)glGetIntegerv(GL_TEXTURE_BINDING_2D,&t.id);
        if(t.id)t.parameters=texture_parameters();
        if(!deck_renderer||used)glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&t.env);
        glGetFloatv(GL_TEXTURE_MATRIX,t.matrix);
        if(!used){t.enabled=false;t.env=GL_MODULATE;}
        if(t.enabled&&t.env!=GL_MODULATE&&t.env!=GL_REPLACE&&t.env!=GL_DECAL){pActiveTexture(active);demand(false,"texture combining unsupported");}}
    pActiveTexture(active);return s;
}
static std::unordered_map<GLuint,GLuint> replay_textures;
static bool same_texture_units(const State& a,const State& b){
    for(int unit=0;unit<4;++unit){
        const auto& x=a.texture[unit];const auto& y=b.texture[unit];
        if(x.copy!=y.copy||memcmp(&x,&y,offsetof(TextureState,copy)))return false;
    }
    return true;
}
static bool same_uniforms(const State& a,const State& b){
    if(a.program!=b.program||a.uniforms.size()!=b.uniforms.size())return false;
    for(size_t i=0;i<a.uniforms.size();++i){
        const auto& x=a.uniforms[i];const auto& y=b.uniforms[i];
        if(!same_uniform(x,y))return false;
    }
    return true;
}
using SamplerState=std::unordered_map<GLuint,TextureParameters>;
static void set_state(const State&s,const State* prior,bool restore_view,SamplerState* samplers=nullptr) {
    // The wide camera overrides only viewport, scissor and projection.
    if(!prior||restore_view||memcmp(s.viewport,prior->viewport,sizeof(s.viewport)))glViewport(s.viewport[0],s.viewport[1],s.viewport[2],s.viewport[3]);
    if(!prior||restore_view||memcmp(s.scissor,prior->scissor,sizeof(s.scissor)))glScissor(s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]);
    if(!prior||restore_view||memcmp(s.projection,prior->projection,sizeof(s.projection))){glMatrixMode(GL_PROJECTION);glLoadMatrixf(s.projection);}
    if(!prior||memcmp(s.model,prior->model,sizeof(s.model))){glMatrixMode(GL_MODELVIEW);glLoadMatrixf(s.model);}
    // Viewport, scissor and program are handled separately. On Proton their
    // changes must not re-issue unrelated blend, mask and raster settings.
    const size_t raster_begin=deck_renderer?offsetof(State,depth_func):0;
    const bool raster_changed=!prior||(deck_renderer?
        memcmp(&s.depth_func,&prior->depth_func,offsetof(State,program)-raster_begin)||
        memcmp(&s.depth,&prior->depth,offsetof(State,model)-offsetof(State,depth)):
        memcmp(&s,prior,offsetof(State,model)));
    if(raster_changed&&samplers&&prior){
        // Terrain often changes one blending or depth setting between adjacent
        // triangles. Keep the rest of the compatibility state on the driver.
        auto toggle=[&](GLenum key,bool value,bool before){if(value!=before)cap(key,value);};
        const auto& p=*prior;
        toggle(GL_DEPTH_TEST,s.depth,p.depth);toggle(GL_BLEND,s.blend,p.blend);
        toggle(GL_CULL_FACE,s.cull,p.cull);toggle(GL_ALPHA_TEST,s.alpha,p.alpha);
        toggle(GL_SCISSOR_TEST,s.scissor_on,p.scissor_on);toggle(GL_COLOR_LOGIC_OP,s.logic_on,p.logic_on);
        toggle(GL_POLYGON_OFFSET_FILL,s.offset_fill,p.offset_fill);toggle(GL_FRAMEBUFFER_SRGB,s.srgb,p.srgb);
        if(s.depth_func!=p.depth_func)glDepthFunc(s.depth_func);
        if(s.depth_mask!=p.depth_mask)glDepthMask(s.depth_mask);
        if(memcmp(s.depth_range,p.depth_range,sizeof(s.depth_range)))glDepthRange(s.depth_range[0],s.depth_range[1]);
        if(s.cull_face!=p.cull_face)glCullFace(s.cull_face);
        if(s.front_face!=p.front_face)glFrontFace(s.front_face);
        if(s.alpha_func!=p.alpha_func||s.alpha_ref!=p.alpha_ref)glAlphaFunc(s.alpha_func,s.alpha_ref);
        if(s.shade!=p.shade)glShadeModel(s.shade);
        if(s.logic!=p.logic)glLogicOp(s.logic);
        if(s.polygon[0]!=p.polygon[0])glPolygonMode(GL_FRONT,s.polygon[0]);
        if(s.polygon[1]!=p.polygon[1])glPolygonMode(GL_BACK,s.polygon[1]);
        if(s.offset_factor!=p.offset_factor||s.offset_units!=p.offset_units)glPolygonOffset(s.offset_factor,s.offset_units);
        if(s.line_width!=p.line_width)glLineWidth(s.line_width);
        if(s.point_size!=p.point_size)glPointSize(s.point_size);
        if(memcmp(s.mask,p.mask,sizeof(s.mask)))glColorMask(s.mask[0],s.mask[1],s.mask[2],s.mask[3]);
        if(s.src_rgb!=p.src_rgb||s.dst_rgb!=p.dst_rgb||s.src_alpha!=p.src_alpha||s.dst_alpha!=p.dst_alpha)
            pBlendFuncSeparate(s.src_rgb,s.dst_rgb,s.src_alpha,s.dst_alpha);
        if(s.eq_rgb!=p.eq_rgb||s.eq_alpha!=p.eq_alpha)pBlendEquationSeparate(s.eq_rgb,s.eq_alpha);
        if(memcmp(s.blend_color,p.blend_color,sizeof(s.blend_color)))pBlendColor(s.blend_color[0],s.blend_color[1],s.blend_color[2],s.blend_color[3]);
        if(memcmp(s.clear_color,p.clear_color,sizeof(s.clear_color)))glClearColor(s.clear_color[0],s.clear_color[1],s.clear_color[2],s.clear_color[3]);
        if(s.clear_depth!=p.clear_depth)glClearDepth(s.clear_depth);
        if(s.clear_stencil!=p.clear_stencil)glClearStencil(s.clear_stencil);
    }else if(raster_changed){
    cap(GL_DEPTH_TEST,s.depth);cap(GL_BLEND,s.blend);cap(GL_CULL_FACE,s.cull);cap(GL_ALPHA_TEST,s.alpha);
    cap(GL_SCISSOR_TEST,s.scissor_on);cap(GL_COLOR_LOGIC_OP,s.logic_on);cap(GL_POLYGON_OFFSET_FILL,s.offset_fill);
    cap(GL_FRAMEBUFFER_SRGB,s.srgb);
    glDisable(GL_POLYGON_OFFSET_LINE);glDisable(GL_POLYGON_OFFSET_POINT);
    glDepthFunc(s.depth_func);glDepthMask(s.depth_mask);glDepthRange(s.depth_range[0],s.depth_range[1]);
    glCullFace(s.cull_face);glFrontFace(s.front_face);glAlphaFunc(s.alpha_func,s.alpha_ref);glShadeModel(s.shade);glLogicOp(s.logic);
    glPolygonMode(GL_FRONT,s.polygon[0]);glPolygonMode(GL_BACK,s.polygon[1]);glPolygonOffset(s.offset_factor,s.offset_units);
    glLineWidth(s.line_width);glPointSize(s.point_size);glColorMask(s.mask[0],s.mask[1],s.mask[2],s.mask[3]);
    pBlendFuncSeparate(s.src_rgb,s.dst_rgb,s.src_alpha,s.dst_alpha);pBlendEquationSeparate(s.eq_rgb,s.eq_alpha);
    pBlendColor(s.blend_color[0],s.blend_color[1],s.blend_color[2],s.blend_color[3]);
    glClearColor(s.clear_color[0],s.clear_color[1],s.clear_color[2],s.clear_color[3]);glClearDepth(s.clear_depth);glClearStencil(s.clear_stencil);
    }
    // Texture parameters belong to the texture object, even when several units
    // sample it. Restore the four units in native order whenever one changes.
    if(!prior||!same_texture_units(s,*prior))for(int j=0;j<4;j++){const auto&t=s.texture[j];
        if(deck_renderer&&prior&&!t.id&&!prior->texture[j].id&&
           !memcmp(&t,&prior->texture[j],offsetof(TextureState,copy))){
            if(j==3)pActiveTexture(GL_TEXTURE3);
            continue;
        }
        pActiveTexture(GL_TEXTURE0+j);
        const TextureState* previous_unit=samplers&&prior?&prior->texture[j]:nullptr;
        if(!previous_unit||t.enabled!=previous_unit->enabled)cap(GL_TEXTURE_2D,t.enabled);
        auto replacement=replay_textures.find(t.id);
        GLuint texture=t.copy?t.copy->id:(replacement!=replay_textures.end()?replacement->second:(glIsTexture(t.id)?t.id:0));
        glBindTexture(GL_TEXTURE_2D,texture);
        if(!previous_unit||t.env!=previous_unit->env)glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,t.env);
        if(texture){
            bool changed=true;
            if(samplers){
                auto entry=samplers->try_emplace(texture,t.parameters);
                changed=entry.second||memcmp(&entry.first->second,&t.parameters,sizeof(t.parameters));
                if(changed)entry.first->second=t.parameters;
            }
            if(changed)apply_texture_parameters(t.parameters);
        }
        if(!previous_unit||memcmp(t.matrix,previous_unit->matrix,sizeof(t.matrix))){
            glMatrixMode(GL_TEXTURE);glLoadMatrixf(t.matrix);
        }}
    if(samplers&&prior&&s.program==prior->program)apply_uniforms(s.program,s.uniforms,&prior->uniforms);
    else if(!prior||!same_uniforms(s,*prior))apply_uniforms(s.program,s.uniforms);
}
struct Vertex {float p[4],c[4],uv[4][4],n[3];};
#include "stride_curve.h"
struct FaceDepthBias { float prior=NAN,current=NAN; };
struct Op {bool indexed_complete=false;GLbitfield clear=0;GLenum mode=0;GLuint target=0;State state;RasterExtra raster;std::vector<Vertex> vertices,card_before;std::vector<std::array<float,4>> prior_positions,prior_colors;std::vector<StrideKnot> actor_mid;std::vector<BYTE> motion_valid,color_valid,front_face;std::vector<FaceDepthBias> depth_bias;uint64_t key=0;int match=-1;};
struct Target {Surface seed;GLuint texture=0;int w=0,h=0;bool active=false,depth=false,stencil=false;GLenum format=GL_RGBA8;GLint min_filter=GL_LINEAR,mag_filter=GL_LINEAR,wrap_s=GL_CLAMP_TO_EDGE,wrap_t=GL_CLAMP_TO_EDGE;};
#include "native_tracks.h"
struct MatchInput;
struct Frame {std::vector<BoneSample> bones;std::unordered_map<uint64_t,std::array<float,2>> stride_velocity;std::vector<Op> ops;std::unordered_map<GLuint,Target> targets;std::unordered_map<GLuint,GLuint> framebuffer_for_target;bool valid=true,started=false;unsigned vertex_count=0;int w=0,h=0,mode=-1,infirmary_room=-1,infirmary_top=-1;
    std::unordered_map<GLuint,GLuint> depth_texture_for_target;
    mutable std::unordered_map<GLuint,std::array<unsigned,3>> seed_clears;
    std::shared_ptr<MatchInput> match_input;
    int layout_override=-1,menu_edge=0;
    int raster_rows=240;
    enum class Travel { none, scene, fade };
    Travel travel=Travel::none;
    int menu_top=28,menu_bottom=448,menu_half_width=282;
    int layout()const{return layout_override<0?mode:layout_override;}
    StrideFollow stride_follow;float stride_dt=1.f/30;
    bool reactive_sides=false,camera_cut=false;};
static Frame building,previous,current;static Surface test_target,backup,reference_target,sample_a,sample_b;
static unsigned baked_passes=0,clock_presents=0,sleep_presents=0;
static uint64_t hash(uint64_t,const void*,size_t);
static std::unordered_map<GLuint,bool> recognized;
static bool native_texture_filter(GLuint program){
    if(!program)return false;
    auto found=recognized.find(program);if(found!=recognized.end())return found->second;
    auto attached=(PFNGLGETATTACHEDSHADERSPROC)wglGetProcAddress("glGetAttachedShaders");
    auto info=(PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    auto source=(PFNGLGETSHADERSOURCEPROC)wglGetProcAddress("glGetShaderSource");
    bool filter=false;
    if(attached&&info&&source){GLuint shaders[8];GLsizei count=0;attached(program,8,&count,shaders);
        for(int i=0;i<count;i++){GLint type=0,size=0;info(shaders[i],GL_SHADER_TYPE,&type);if(type!=GL_FRAGMENT_SHADER)continue;
            info(shaders[i],GL_SHADER_SOURCE_LENGTH,&size);if(size<=0||size>65536)continue;
            std::vector<char> buffer(size);GLsizei used=0;source(shaders[i],size,&used,buffer.data());
            filter=hash(1469598103934665603ULL,buffer.data(),used)==0x9fdab56610f3e40bULL;
        }
    }
    recognized.emplace(program,filter);return filter;
}
static bool bake_texture_pass(GLuint attachment){
    auto target=building.targets.find(attachment);
    if(!attachment||(target!=building.targets.end()&&target->second.active))return false;
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(!native_texture_filter(program))return false;
    GLint unit=0,active=0,source=0;GLint location=pGetUniformLocation(program,"tex");
    if(location<0)return false;
    pGetUniformiv(program,location,&unit);if(unit<0||unit>=4)return false;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);pActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&source);pActiveTexture(active);
    auto upstream=building.targets.find(source);
    if(upstream!=building.targets.end()&&upstream->second.active)return false;
    // The native pass is still executed. Consumers freeze its output on use.
    // Several different textures can be filtered by one scratch FBO in a frame.
    texture_cache.erase(attachment);baked_passes++;
    return true;
}
#ifdef GEO_AUTOTEST
static Surface previous_reference;
#endif
static std::unordered_map<GLuint,Surface> replay_targets;
// Sample the depth produced by this replay, not a frozen copy from an earlier
// foreground layer. FFVIII clears and reuses these textures within one frame.
struct ReplayDepth {GLuint texture=0;int w=0,h=0;bool stencil=false;};
static std::unordered_map<GLuint,ReplayDepth> replay_depth;
static void attach_replay_depth(GLuint key,Surface& surface,const Target& target,GLuint native_depth){
    auto found=replay_depth.find(key);
    if(!native_depth){
        if(found!=replay_depth.end()){
            glDeleteTextures(1,&found->second.texture);replay_depth.erase(found);
            pBindFramebuffer(GL_FRAMEBUFFER,surface.fbo);
            pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,surface.depth);
            pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,target.stencil?surface.depth:0);
        }
        return;
    }
    auto& depth=replay_depth[key];
    if(!depth.texture)glGenTextures(1,&depth.texture);
    if(depth.w!=target.w||depth.h!=target.h||depth.stencil!=target.stencil){
        glBindTexture(GL_TEXTURE_2D,depth.texture);
        real_tex_image(GL_TEXTURE_2D,0,target.stencil?GL_DEPTH24_STENCIL8:GL_DEPTH_COMPONENT24,target.w,target.h,0,
                       target.stencil?GL_DEPTH_STENCIL:GL_DEPTH_COMPONENT,target.stencil?GL_UNSIGNED_INT_24_8:GL_UNSIGNED_INT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        depth.w=target.w;depth.h=target.h;depth.stencil=target.stencil;
    }
    pBindFramebuffer(GL_FRAMEBUFFER,surface.fbo);
    pFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth.texture,0);
    if(target.stencil)pFramebufferTexture2D(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth.texture,0);
    demand(pCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"replayed depth texture incomplete");
    replay_textures[native_depth]=depth.texture;
}
static GLuint capture_target=0;
static std::vector<Vertex> scratch;
static void fail_frame(const char*s){static char last[200]="";if(building.valid&&strcmp(last,s)){char line[256];snprintf(line,sizeof(line),"native fallback: %s\n",s);log(line);snprintf(last,sizeof(last),"%s",s);}building.valid=false;}
static bool native_deck_frame(){return deck_renderer&&!display_settings.fps&&!display_settings.wide;}
struct ReadableRegion { uintptr_t begin=0,end=0; };
struct CaptureRegions { ReadableRegion indices,arrays[7]; };
static thread_local CaptureRegions capture_regions;
static bool capture_start() {
    if(inside)return false;
    if(!display_settings.fps&&!display_settings.wide&&(deck_renderer||!display_settings.atlas))return false;
    const auto active_context=wglGetCurrentContext();if(!active_context)return false;
    if(!loaded&&!initialize_gl())return false;
    if(GetCurrentThreadId()!=thread||active_context!=context)return false;
    if(!building.valid)return false;
    if(!building.started){RECT r{};demand(GetClientRect(WindowFromDC(dc),&r),"no drawable");
        building.w=r.right;building.h=r.bottom;demand(r.right>0&&r.bottom>0&&r.right<=8192&&r.bottom<=8192,"drawable extent unsupported");
        if(deck_renderer&&display_settings.fps){capture_regions={};world_bindings::begin_frame();}
        building.started=true;}
    GLint fbo=0;if(deck_renderer&&display_settings.fps)fbo=world_bindings::draw_target();
    else glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    // FFVIII reattaches multiple particle textures to the same scratch FBO
    // during one attack. Each texture retains its own image when detached.
    GLint attachment=0;
    if(fbo)pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attachment);
    demand(!fbo||attachment>0,"offscreen color missing");
    capture_target=(GLuint)attachment;
    if(bake_texture_pass(capture_target))return false;
    if(building.targets.size()>=32&&!building.targets.count(capture_target)){
        for(auto it=building.targets.begin();it!=building.targets.end();){
            if(!it->second.active){release(it->second.seed);it=building.targets.erase(it);}else ++it;
        }
        demand(building.targets.size()<48,"too many active render targets");
    }
    auto&target=building.targets[capture_target];
    if(!target.active){
        target.w=building.w;target.h=building.h;target.depth=true;target.stencil=stencil_bits>0;target.texture=0;target.format=GL_RGBA8;
        if(fbo){GLint type=0,name=attachment,level=0,depth_type=0;
            pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
            pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&depth_type);
            demand(type==GL_TEXTURE&&name>0,"offscreen color is not a texture");
            pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,&level);demand(level==0,"offscreen mip target unsupported");
            GLint st=0;pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&st);
            target.texture=name;target.depth=depth_type!=GL_NONE;target.stencil=st!=GL_NONE;demand(!target.stencil||target.depth,"stencil target without depth unsupported");
            if(depth_type==GL_TEXTURE){
                GLint depth_name=0;
                pGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&depth_name);
                demand(depth_name>0,"depth texture missing");
                building.depth_texture_for_target[capture_target]=GLuint(depth_name);
                texture_cache.erase(GLuint(depth_name));
            }
            if(target.depth)for(const auto&entry:building.framebuffer_for_target)
                demand(entry.second!=(GLuint)fbo||entry.first==capture_target,"reattached depth target unsupported");
            texture_cache.erase(name);
            GLint old_tex;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_tex);glBindTexture(GL_TEXTURE_2D,name);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&target.w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&target.h);
            GLint format;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);target.format=format;
            glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&target.min_filter);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&target.mag_filter);
            glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&target.wrap_s);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&target.wrap_t);
            glBindTexture(GL_TEXTURE_2D,old_tex);
            demand(target.w>0&&target.h>0&&target.w<=8192&&target.h<=8192,"offscreen texture dimensions unsupported");
            demand(format==GL_RGBA8||format==GL_RGB8||format==GL_RGBA||format==GL_RGB||format==GL_SRGB8_ALPHA8,"offscreen color format unsupported");
            if(format==GL_RGBA)target.format=GL_RGBA8;if(format==GL_RGB)target.format=GL_RGB8;
        }
        building.framebuffer_for_target[capture_target]=(GLuint)fbo;
        resize(target.seed,target.w,target.h,target.depth,false,target.format,target.stencil);
        GLbitfield bits=GL_COLOR_BUFFER_BIT|(target.depth?GL_DEPTH_BUFFER_BIT:0)|(target.stencil?GL_STENCIL_BUFFER_BIT:0);
        blit(fbo,target.seed.fbo,target.w,target.h,target.w,target.h,bits);target.active=true;
    }else demand(building.framebuffer_for_target.at(capture_target)==(GLuint)fbo,"texture used by multiple draw framebuffers");
    demand(building.ops.size()<MAX_OPS,"too many draw commands");return true;
}
// Attributes in an interleaved draw often share one committed region.
static bool readable(const void* pointer,size_t bytes,ReadableRegion* known=nullptr) {
    uintptr_t pos=reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end=pos+bytes;
    if(!pos||end<pos)return false;
    while(pos<end){
        if(known&&pos>=known->begin&&pos<known->end){
            pos=known->end;
            continue;
        }
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(pos),&info,sizeof(info))||
           info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto begin=reinterpret_cast<uintptr_t>(info.BaseAddress);
        const auto next=begin+info.RegionSize;
        if(next<=pos)return false;
        if(known)*known={begin,next};
        pos=next;
    }
    return true;
}
struct Array {const BYTE*p=nullptr;GLint type=GL_FLOAT,size=4,stride=0;bool enabled=false;};
static Array array(GLenum enable,GLenum pointer,GLenum size,GLenum type,GLenum stride,GLenum buffer) {
    Array a;a.enabled=glIsEnabled(enable);if(!a.enabled)return a;
    GLint binding=0;glGetIntegerv(buffer,&binding);demand(!binding,"VBO client arrays unsupported");
    glGetPointerv(pointer,(void**)&a.p);if(size)glGetIntegerv(size,&a.size);else a.size=3;
    glGetIntegerv(type,&a.type);glGetIntegerv(stride,&a.stride);
    demand(a.size>=1&&a.size<=4&&(a.type==GL_FLOAT||a.type==GL_UNSIGNED_BYTE),"client array format unsupported");
    if(!a.stride)a.stride=a.size*(a.type==GL_FLOAT?4:1);
    demand(a.stride>0&&a.stride<4096,"client array stride unsupported");return a;
}
static void read_array(const Array&a,unsigned index,float*out,bool color) {
    const BYTE*p=a.p+size_t(index)*a.stride;
    for(int j=0;j<a.size;j++){if(a.type==GL_FLOAT)memcpy(out+j,p+j*4,4);else out[j]=color?p[j]/255.f:float(p[j]);
        demand(std::isfinite(out[j]),"nonfinite vertex data");}
}
static uint64_t hash(uint64_t h,const void*p,size_t n){auto b=(const BYTE*)p;while(n--){h^=*b++;h*=1099511628211ULL;}return h;}
template<class Index,class Visitor>
static void visit_draw_indices(const BYTE* indices,GLsizei count,Visitor visit){
    for(GLsizei i=0;i<count;i++){
        Index index;memcpy(&index,indices+i*sizeof(Index),sizeof(Index));
        visit(i,unsigned(index));
    }
}
template<class Visitor>static void visit_draw_indices(GLenum type,const void* data,GLsizei count,Visitor visit){
    const auto* indices=static_cast<const BYTE*>(data);
    if(type==GL_UNSIGNED_SHORT)visit_draw_indices<GLushort>(indices,count,visit);
    else if(type==GL_UNSIGNED_INT)visit_draw_indices<GLuint>(indices,count,visit);
    else visit_draw_indices<GLubyte>(indices,count,visit);
}
static void read_draw_vertices(Op& op,unsigned inputs,GLsizei count,GLenum type,const void*indices,CaptureRegions* regions=nullptr){
    struct RestoreClient {GLint client;RestoreClient(){glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&client);}~RestoreClient(){pClientActiveTexture(client);}} restore_client;
    GLint element=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element);
    demand(!element,"index VBO unsupported");size_t bytes=type==GL_UNSIGNED_SHORT?2:type==GL_UNSIGNED_INT?4:type==GL_UNSIGNED_BYTE?1:0;
    demand(bytes&&readable(indices,size_t(count)*bytes,regions?&regions->indices:nullptr),"index memory invalid");
    unsigned maxid=0;
    visit_draw_indices(type,indices,count,[&](int,unsigned id){maxid=std::max(maxid,id);});
    demand(maxid<1000000,"index range too large");
    op.vertices.resize(count);
    auto pos=array(GL_VERTEX_ARRAY,GL_VERTEX_ARRAY_POINTER,GL_VERTEX_ARRAY_SIZE,GL_VERTEX_ARRAY_TYPE,GL_VERTEX_ARRAY_STRIDE,GL_VERTEX_ARRAY_BUFFER_BINDING);
    auto color=(inputs&4)?array(GL_COLOR_ARRAY,GL_COLOR_ARRAY_POINTER,GL_COLOR_ARRAY_SIZE,GL_COLOR_ARRAY_TYPE,GL_COLOR_ARRAY_STRIDE,GL_COLOR_ARRAY_BUFFER_BINDING):Array{};
    auto normal=(inputs&2)?array(GL_NORMAL_ARRAY,GL_NORMAL_ARRAY_POINTER,0,GL_NORMAL_ARRAY_TYPE,GL_NORMAL_ARRAY_STRIDE,GL_NORMAL_ARRAY_BUFFER_BINDING):Array{};
    Array uv[4];for(int j=0;j<4;j++)if(inputs&(8u<<j)){pClientActiveTexture(GL_TEXTURE0+j);uv[j]=array(GL_TEXTURE_COORD_ARRAY,GL_TEXTURE_COORD_ARRAY_POINTER,GL_TEXTURE_COORD_ARRAY_SIZE,GL_TEXTURE_COORD_ARRAY_TYPE,GL_TEXTURE_COORD_ARRAY_STRIDE,GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING);}
    demand(pos.enabled,"vertex array disabled");
    ReadableRegion local;
    auto validate=[&](const Array&a,int attribute){if(a.enabled)demand(readable(a.p,size_t(maxid)*a.stride+a.size*(a.type==GL_FLOAT?4:1),regions?&regions->arrays[attribute]:&local),"vertex memory invalid");};
    validate(pos,0);validate(color,1);validate(normal,2);for(int j=0;j<4;++j)validate(uv[j],3+j);
    float constant_color[4]={1,1,1,1},constant_normal[3]={0,0,1};
    if((inputs&4)&&!color.enabled)glGetFloatv(GL_CURRENT_COLOR,constant_color);
    if((inputs&2)&&!normal.enabled)glGetFloatv(GL_CURRENT_NORMAL,constant_normal);
    GLfloat constant_uv[4][4]={};
    GLint active=-1;
    for(int j=0;j<4;j++){
        constant_uv[j][3]=1;
        if(!(inputs&(8u<<j))||uv[j].enabled)continue;
        if(active<0)glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        pActiveTexture(GL_TEXTURE0+j);
        glGetFloatv(GL_CURRENT_TEXTURE_COORDS,constant_uv[j]);
    }
    if(active>=0)pActiveTexture(active);
    visit_draw_indices(type,indices,count,[&](int i,unsigned id){
        auto&v=op.vertices[i];v.p[3]=1;read_array(pos,id,v.p,false);
        memcpy(v.c,constant_color,sizeof(v.c));if(color.enabled){v.c[3]=1;read_array(color,id,v.c,true);}
        memcpy(v.n,constant_normal,sizeof(v.n));if(normal.enabled)read_array(normal,id,v.n,false);
        for(int j=0;j<4;j++){memcpy(v.uv[j],constant_uv[j],sizeof(v.uv[j]));if(uv[j].enabled)read_array(uv[j],id,v.uv[j],false);}
    });
}
static void record_draw(GLenum mode,GLsizei count,GLenum type,const void*indices) {
    if(!capture_start())return;
    demand(count>0&&unsigned(count)<=MAX_VERTICES&&building.vertex_count+count<=MAX_VERTICES,"vertex budget exceeded");
    demand(mode==GL_TRIANGLES||mode==GL_TRIANGLE_STRIP||mode==GL_TRIANGLE_FAN||mode==GL_LINES||mode==GL_POINTS||mode==GL_QUADS,"primitive mode unsupported");
    const bool compact_draw=deck_renderer&&mode==GL_TRIANGLES&&
        (display_settings.fps||(display_settings.wide&&current.mode==2&&capture_target));
    unsigned inputs=127;Op op;op.mode=mode;op.target=capture_target;op.state=get_state(&op.raster,false,&inputs,compact_draw);
    read_draw_vertices(op,inputs,count,type,indices,deck_renderer&&display_settings.fps?&capture_regions:nullptr);
    bool used_texture[4]={};
    for(int j=0;j<4;j++)used_texture[j]=!op.state.program&&op.state.texture[j].enabled;
    for(auto&u:op.state.uniforms)if(u.type==GL_SAMPLER_2D)used_texture[u.i[0]]=true;
    for(int j=0;j<4;j++)if(used_texture[j]&&op.state.texture[j].id){
        auto&t=op.state.texture[j];bool render_target=false;
        for(const auto&entry:building.targets)render_target|=entry.second.active&&entry.second.texture==(GLuint)t.id;
        for(const auto&entry:building.depth_texture_for_target)render_target|=entry.second==(GLuint)t.id;
        if(!render_target)t.copy=freeze_texture(t.id);
    }
#ifdef GEO_AUTOTEST
    // Full draw signatures are only consumed by diagnostic frame dumps.
    uint64_t key=hash(1469598103934665603ULL,&mode,sizeof(mode));
    key=hash(key,&count,sizeof(count));key=hash(key,&op.target,sizeof(op.target));key=hash(key,&op.state.program,sizeof(GLint));
    for(int j=0;j<4;j++)if(used_texture[j])key=hash(key,&op.state.texture[j].id,sizeof(GLint));
    for(const auto& v:op.vertices)for(int j=0;j<4;j++)if(used_texture[j])key=hash(key,v.uv[j],sizeof(v.uv[j]));
    op.key=key;
#endif
    building.vertex_count+=count;building.ops.push_back(std::move(op));
}
static int present_reason=0,previous_stage=0,gap_from=0,gap_to=0;
static double last_stage_time=0,maximum_gap=0;
static void qa_stage(int reason){
    if(!diagnostic_trace||GetCurrentThreadId()!=thread)return;
    double t=now(),gap=t-last_stage_time;
    if(last_stage_time&&gap>maximum_gap){maximum_gap=gap;gap_from=previous_stage;gap_to=reason;}
    previous_stage=reason;last_stage_time=t;
}
static void service_at(int,double earliest=next_output);
static void present_during_wait(bool force_previous=false);
static void pump_due(double earliest=next_output);
static double world_capture_time=0;
static bool draw_native_atlas(GLenum,GLsizei,GLenum,const void*);
static bool direct_world_frame();
static void direct_world_draw(GLenum,GLsizei,GLenum,const void*);
static void APIENTRY draw_hook(GLenum mode,GLsizei count,GLenum type,const void*indices){
    if(native_deck_frame()){
        if(enabled&&display_settings.atlas&&draw_native_atlas(mode,count,type,indices))return;
        real_draw(mode,count,type,indices);return;
    }
    if(deck_renderer&&direct_world_frame()){direct_world_draw(mode,count,type,indices);return;}
    double began=loaded?now():0;
    try{if(count>0)record_draw(mode,count,type,indices);}catch(const std::exception&e){fail_frame(e.what());}
    if(began&&!inside){
        const double elapsed=now()-began;
        capture_seconds+=elapsed;capture_count++;
        if(deck_renderer&&current.mode==2&&display_settings.wide&&!display_settings.fps)world_capture_time+=elapsed;
    }
    real_draw(mode,count,type,indices);
    service_at(1);
}
static void APIENTRY clear_hook(GLbitfield mask){
    if(native_deck_frame()||(deck_renderer&&direct_world_frame())){real_clear(mask);return;}
    try{if(capture_start()){Op op;op.clear=mask;op.target=capture_target;op.state=get_state(&op.raster,(mask&GL_STENCIL_BUFFER_BIT)!=0);building.ops.push_back(std::move(op));}}
    catch(const std::exception&e){fail_frame(e.what());}real_clear(mask);service_at(2);
}

struct Guard {
    GLint r,d,program,active,client,matrix;float model[16],projection[16],texture[4][16];
    std::unordered_map<GLuint,std::vector<Uniform>> values;
    std::unordered_map<GLuint,TextureParameters> texture_values;
    std::unordered_map<GLuint,unsigned> vertex_inputs;
    Guard(const Frame&f,bool inspect_inputs=false){
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&r);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&d);glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&client);glGetIntegerv(GL_MATRIX_MODE,&matrix);
        glGetFloatv(GL_MODELVIEW_MATRIX,model);glGetFloatv(GL_PROJECTION_MATRIX,projection);
        for(const auto&op:f.ops)if(op.state.program&&!values.count(op.state.program)){
            unsigned inputs=127;
            values.emplace(op.state.program,uniforms(op.state.program,inspect_inputs?&inputs:nullptr,
                deck_renderer&&inspect_inputs&&display_settings.fps==60));
            if(inspect_inputs)vertex_inputs.emplace(op.state.program,inputs);
        }
        for(int j=0;j<4;j++){pActiveTexture(GL_TEXTURE0+j);glGetFloatv(GL_TEXTURE_MATRIX,texture[j]);}pActiveTexture(active);
        glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);inside=true;
        // A frozen texture redirects every sampler mutation to our own copy.
        for(const auto&op:f.ops)for(const auto&t:op.state.texture)if(t.id&&!t.copy&&!texture_values.count(t.id)&&glIsTexture(t.id)){
            glBindTexture(GL_TEXTURE_2D,t.id);texture_values.emplace(t.id,texture_parameters());}
    }
    ~Guard(){
        for(const auto&entry:texture_values){glBindTexture(GL_TEXTURE_2D,entry.first);apply_texture_parameters(entry.second);}
        for(auto&v:values)apply_uniforms(v.first,v.second);pUseProgram(program);
        glMatrixMode(GL_MODELVIEW);glLoadMatrixf(model);glMatrixMode(GL_PROJECTION);glLoadMatrixf(projection);
        for(int j=0;j<4;j++){pActiveTexture(GL_TEXTURE0+j);glMatrixMode(GL_TEXTURE);glLoadMatrixf(texture[j]);}
        pBindFramebuffer(GL_READ_FRAMEBUFFER,r);pBindFramebuffer(GL_DRAW_FRAMEBUFFER,d);
        glPopClientAttrib();glPopAttrib();pActiveTexture(active);pClientActiveTexture(client);glMatrixMode(matrix);inside=false;
    }
    void protect(const State&s){
        if(s.program&&!values.count(s.program))values.emplace(s.program,uniforms(s.program));
        for(const auto&t:s.texture)if(t.id&&!t.copy&&!texture_values.count(t.id)&&glIsTexture(t.id)){
            glBindTexture(GL_TEXTURE_2D,t.id);texture_values.emplace(t.id,texture_parameters());}
    }
};
static void transform(const float*m,const float*p,double*out){for(int r=0;r<4;r++){out[r]=0;for(int c=0;c<4;c++)out[r]+=double(m[c*4+r])*p[c];}}
static bool invert_matrix(const float*m,float*out){
    double a[4][8]{};for(int r=0;r<4;r++)for(int c=0;c<4;c++){a[r][c]=m[c*4+r];a[r][c+4]=r==c;}
    for(int c=0;c<4;c++){int pivot=c;for(int r=c+1;r<4;r++)if(fabs(a[r][c])>fabs(a[pivot][c]))pivot=r;
        if(fabs(a[pivot][c])<1e-12)return false;for(int j=0;j<8;j++)std::swap(a[c][j],a[pivot][j]);
        double d=a[c][c];for(int j=0;j<8;j++)a[c][j]/=d;
        for(int r=0;r<4;r++)if(r!=c){d=a[r][c];for(int j=0;j<8;j++)a[r][j]-=d*a[c][j];}}
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)out[c*4+r]=(float)a[r][c+4];return true;
}
#include "wide_view.h"
#include "triangle_motion.h"
#include "menu_motion.h"
#ifdef GEO_NATIVE_CAPTURE
#include "native_capture.h"
#endif
#include "world_map_detail.h"
#include "deck_world_pass.h"
static void initialize_deck_bindings(){
    world_bindings::install();
    world_state::install_buffers();
    deck_programs::install();
}
#include "infirmary/presentation.h"
#include "field_blur.h"
#include "stride_trace.h"
#include "replay_seed.h"
#include "field_prefix.h"
static FieldPrefixCache field_prefix;
#include "vertex_stream_policy.h"
static VertexStreamPolicy vertex_policy;
static GLuint vertex_stream=0;
static float replay(GLuint target,float alpha,bool widescreen=false){
    if(current.camera_cut)alpha=1;
    WideView view(current,widescreen);
    if(view.full_map)alpha=1; // Keep every map label and the pointer on the same native tick.
    if(!display_settings.wide){view.outer_width=0;view.horizontal=1;}
    double began=now();
    // A linked program consumes the same attributes at every interpolation
    // phase. Reflect the inputs once per replay, including on intermediate poses.
    const bool deck_gameplay=deck_renderer&&current.layout()==current.mode&&
        (display_settings.fps==60?gameplay_layout(current.mode):current.mode==2&&alpha==1);
    Guard guard(current,deck_gameplay);
    if(alpha<1)for(const auto&draw:outgoing_draws)guard.protect(draw.op.state);
    const ReplaySeedPlan seed_plan(alpha);
    field_prefix.prepare(current,alpha,widescreen&&display_settings.wide);
    bool restore_prefix=field_prefix.ready;
    replay_textures.clear();
    for(auto it=replay_depth.begin();it!=replay_depth.end();){
        if(!current.depth_texture_for_target.count(it->first)){
            auto target_it=replay_targets.find(it->first);
            if(target_it!=replay_targets.end()){
                pBindFramebuffer(GL_FRAMEBUFFER,target_it->second.fbo);
                pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,target_it->second.depth);
                pFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,target_it->second.has_stencil?target_it->second.depth:0);
            }
            glDeleteTextures(1,&it->second.texture);it=replay_depth.erase(it);
        }else ++it;
    }
    for(auto it=replay_targets.begin();it!=replay_targets.end();){
        auto existing=current.targets.find(it->first);
        if(existing==current.targets.end()||!existing->second.active){release(it->second);it=replay_targets.erase(it);}else ++it;}
    for(auto&entry:current.targets){auto&t=entry.second;if(!t.active)continue;GLuint dest=target;
        if(entry.first){auto&copy=replay_targets[entry.first];resize(copy,t.w,t.h,t.depth,true,t.format,t.stencil);dest=copy.fbo;
            auto depth=current.depth_texture_for_target.find(entry.first);
            attach_replay_depth(entry.first,copy,t,depth==current.depth_texture_for_target.end()?0:depth->second);
            replay_textures[t.texture]=copy.color;glBindTexture(GL_TEXTURE_2D,copy.color);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,t.min_filter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,t.mag_filter);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,t.wrap_s);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,t.wrap_t);}
        if(restore_prefix&&entry.first==field_prefix.plan.target){
            if(field_prefix.restore(dest,t))continue;
            restore_prefix=false;
        }
        const GLbitfield seed=seed_plan.required(current,entry.first,t);
        if(seed)blit(t.seed.fbo,dest,t.w,t.h,t.w,t.h,seed);
    }
    pBindBuffer(GL_ARRAY_BUFFER,0);pBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    const auto& follow=current.stride_follow;
    const float actor_shift=widescreen?follow.start+(follow.end-follow.start)*alpha:0;
    const bool tracing=stride_trace::enabled&&target==0&&widescreen&&current.mode==2;
    if(tracing)stride_trace::begin(actor_shift);
    const Op* last_draw=nullptr;
    auto prepare=[&](const Op&op,GLsizei& vertex_count){
        const Vertex* vertices=op.vertices.data();
        if(!op.clear&&op.match>=0&&alpha<1){scratch=op.vertices;
            for(size_t i=0;i<scratch.size();i++)if(op.motion_valid[i])for(int k=0;k<4;k++)
                scratch[i].p[k]=op.prior_positions[i][k]+alpha*(op.vertices[i].p[k]-op.prior_positions[i][k]);
            for(size_t i=0;i<op.color_valid.size();i++)if(op.color_valid[i])for(int k=0;k<4;k++)
                scratch[i].c[k]=op.prior_colors[i][k]+alpha*(op.vertices[i].c[k]-op.prior_colors[i][k]);
            for(size_t i=0;i<op.actor_mid.size();i++)if(op.actor_mid[i].position[3]>0)
                op.actor_mid[i].sample(op.prior_positions[i].data(),op.vertices[i].p,alpha,scratch[i].p);
            interpolate_depth(op,scratch,alpha);suppress_backfaces(op,scratch);
            vertices=scratch.data();}
        if(actor_shift!=0&&!op.actor_mid.empty()){
            if(vertices==op.vertices.data())scratch=op.vertices;
            for(size_t i=0;i<op.actor_mid.size();i++)if(op.actor_mid[i].player)scratch[i].p[0]+=actor_shift;
            vertices=scratch.data();
        }
        vertices=view.position_hud(op,vertices,scratch);
        vertex_count=(GLsizei)op.vertices.size();
        static std::vector<Vertex> layer_vertices;
        vertices=view.screen_layers(op,vertices,layer_vertices,vertex_count);
        if(tracing&&!op.actor_mid.empty()&&vertex_count==(GLsizei)op.vertices.size())stride_trace::observe(op,vertices);
        return vertices;
    };
    // Preserve each draw boundary, vertex index and all native raster state.
    // Upload the prepared vertices together instead of making the driver
    // stage seven host-memory arrays at every small draw.
    struct Span { size_t offset; GLsizei count; };
    static std::vector<Vertex> packed;
    static std::vector<Span> spans;
    spans.clear();packed.clear();
    const auto stream_choice=vertex_policy.choose(current.mode,current.ops.size(),current.vertex_count,widescreen,alpha>=1,current.camera_cut);
    const bool stream=stream_choice.stream;
    const double submit_begin=stream_choice.timing?now():0;
    const size_t departures=alpha<1?outgoing_draws.size():0;
    if(departures>1){
        // Camera turns and attacks can leave many departing batches. Preserve
        // their original blend order at each insertion point on both passes.
        auto by_anchor=[](const OutgoingDraw& a,const OutgoingDraw& b){return a.after<b.after;};
        if(!std::is_sorted(outgoing_draws.begin(),outgoing_draws.end(),by_anchor))
            std::stable_sort(outgoing_draws.begin(),outgoing_draws.end(),by_anchor);
    }
    auto visit=[&](auto&& fn){
        size_t next_departure=0;
        for(unsigned i=0;i<current.ops.size();i++){
            if(!restore_prefix||i>field_prefix.plan.last||current.ops[i].target!=field_prefix.plan.target)fn(current.ops[i]);
            while(next_departure<departures&&outgoing_draws[next_departure].after==i)
                fn(outgoing_draws[next_departure++].op);
        }
    };
    bool uploaded=false;
    if(stream){
        visit([&](const Op& op){
            GLsizei count=0;
            const Vertex* vertices=prepare(op,count);
            spans.push_back({packed.size()*sizeof(Vertex),count});
            if(count)packed.insert(packed.end(),vertices,vertices+count);
        });
        if(!packed.empty()){
            demand(glGetError()==GL_NO_ERROR,"GL error before vertex stream upload");
            if(!vertex_stream)pGenBuffers(1,&vertex_stream);
            if(vertex_stream){
                pBindBuffer(GL_ARRAY_BUFFER,vertex_stream);
                const GLsizeiptr bytes=GLsizeiptr(packed.size()*sizeof(Vertex));
                pBufferData(GL_ARRAY_BUFFER,bytes,packed.data(),GL_STREAM_DRAW);
                GLint stored=0;pGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&stored);
                const GLenum upload_error=glGetError();
                uploaded=upload_error==GL_NO_ERROR&&stored==bytes;
            }else{
                glGetError();
            }
            if(!uploaded){
                pBindBuffer(GL_ARRAY_BUFFER,0);
                vertex_policy.upload_failed(stream_choice);
            }
        }
    }
    // The native client attributes are saved by Guard. Clears, texture
    // uploads and atlas transitions only change data pointers inside this loop.
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    for(int unit=0;unit<4;++unit){
        pClientActiveTexture(GL_TEXTURE0+unit);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    size_t span_index=0;
    bool packed_arrays_bound=false;
    unsigned enabled_inputs=127;
    // Only the renderer can change GL state between these calls. Forget the
    // cache before the game resumes or a custom atlas touches the bindings.
    const bool reuse_draw_state=deck_gameplay&&!view.full_map;
    const Op* atlas_draw=display_settings.atlas?view.full_map:nullptr;
    SamplerState replay_samplers;
    auto draw_op=[&](const Op&op){
        if(!last_draw||op.target!=last_draw->target){
            GLuint dest=op.target?replay_targets.at(op.target).fbo:target;
            pBindFramebuffer(GL_FRAMEBUFFER,dest);glDrawBuffer(dest?GL_COLOR_ATTACHMENT0:GL_BACK);
        }
        const Vertex* vertices=nullptr;
        GLsizei vertex_count=0;
        uintptr_t base=0;
        if(stream&&!packed.empty()){
            const Span& span=spans[span_index++];
            vertex_count=span.count;
            vertices=packed.data()+span.offset/sizeof(Vertex);
            base=uploaded?span.offset:reinterpret_cast<uintptr_t>(vertices);
        }else{
            vertices=prepare(op,vertex_count);
            base=reinterpret_cast<uintptr_t>(vertices);
        }
        const bool same_scene_view=reuse_draw_state&&view.outer_width&&last_draw&&
            op.target&&op.target==last_draw->target&&!op.clear&&!last_draw->clear&&
            op.state.scissor_on==last_draw->state.scissor_on&&
            !memcmp(op.state.viewport,last_draw->state.viewport,sizeof(op.state.viewport))&&
            !memcmp(op.state.scissor,last_draw->state.scissor,sizeof(op.state.scissor))&&
            !memcmp(op.state.projection,last_draw->state.projection,sizeof(op.state.projection))&&
            view.world_map_draw(op)==view.world_map_draw(*last_draw);
        set_state(op.state,last_draw?&last_draw->state:nullptr,view.outer_width&&!same_scene_view,reuse_draw_state?&replay_samplers:nullptr);
        if(!last_draw||memcmp(&op.raster,&last_draw->raster,sizeof(op.raster)))apply_extra(op.raster);
        last_draw=&op;
        if(!same_scene_view)view.apply(op);
        if(atlas_draw==&op){bind_world_map_detail(op.state.texture[0]);last_draw=nullptr;replay_samplers.clear();}
        if(op.clear){real_clear(op.clear);return;}
        // Card transitions intentionally switch between client arrays. Use
        // the packed CPU vertices for that draw and rebind the stream after it.
        const bool card=!op.card_before.empty()&&alpha<1;
        if(uploaded&&card){pBindBuffer(GL_ARRAY_BUFFER,0);base=reinterpret_cast<uintptr_t>(vertices);}
        struct Rebind {bool active; ~Rebind(){if(active)pBindBuffer(GL_ARRAY_BUFFER,vertex_stream);}} rebind{uploaded&&card};
        // A shader can read gl_VertexID. Keep its original first vertex.
        const bool shared=deck_renderer&&stream&&!packed.empty()&&!card&&!op.state.program;
        // Guard reflected these linked programs in this replay. Keep first=0
        // for shaders, and submit only the arrays their executable reads.
        // Departing geometry can refer to a program absent from the current
        // frame. Its original full set of arrays remains a valid fallback.
        unsigned inputs=127;
        if(deck_gameplay&&op.state.program){
            auto active=guard.vertex_inputs.find(op.state.program);
            if(active!=guard.vertex_inputs.end())inputs=active->second;
        }
        if(inputs!=enabled_inputs){
            auto toggle=[&](unsigned bit,GLenum array){
                if(inputs&bit)glEnableClientState(array);else glDisableClientState(array);
            };
            const unsigned changed=inputs^enabled_inputs;
            if(changed&2)toggle(2,GL_NORMAL_ARRAY);
            if(changed&4)toggle(4,GL_COLOR_ARRAY);
            for(int unit=0;unit<4;++unit)if(changed&(8u<<unit)){
                pClientActiveTexture(GL_TEXTURE0+unit);toggle(8u<<unit,GL_TEXTURE_COORD_ARRAY);
            }
            enabled_inputs=inputs;
        }
        GLint first=0;
        if(shared){
            first=GLint(spans[span_index-1].offset/sizeof(Vertex));
            base=uploaded?0:reinterpret_cast<uintptr_t>(packed.data());
        }
        if(!shared||!packed_arrays_bound){
            auto address=[&](size_t offset){return reinterpret_cast<const void*>(base+offset);};
            glVertexPointer(4,GL_FLOAT,sizeof(Vertex),address(offsetof(Vertex,p)));
            if(inputs&4)glColorPointer(4,GL_FLOAT,sizeof(Vertex),address(offsetof(Vertex,c)));
            if(inputs&2)glNormalPointer(GL_FLOAT,sizeof(Vertex),address(offsetof(Vertex,n)));
            for(int j=0;j<4;j++)if(inputs&(8u<<j)){pClientActiveTexture(GL_TEXTURE0+j);
                glTexCoordPointer(4,GL_FLOAT,sizeof(Vertex),address(offsetof(Vertex,uv)+j*sizeof(vertices[0].uv[0])));}
        }
        packed_arrays_bound=shared;
        if(card){
            last_draw=nullptr;
            // Positions stay fixed across the two draws: there is no ghost of
            // the character or its background. Only the atlas image changes.
            const Vertex*old=op.card_before.data();
            pClientActiveTexture(GL_TEXTURE0);glTexCoordPointer(4,GL_FLOAT,sizeof(Vertex),old[0].uv[0]);glColorPointer(4,GL_FLOAT,sizeof(Vertex),old[0].c);
            if(alpha<=0){glDrawArrays(op.mode,0,(GLsizei)op.vertices.size());return;}
            const auto&s=op.state;const bool alpha_linear=s.src_alpha==GL_ONE&&s.dst_alpha==GL_ONE&&(s.eq_alpha==GL_FUNC_ADD||s.eq_alpha==GL_FUNC_REVERSE_SUBTRACT);
            if(!alpha_linear)glColorMask(s.mask[0],s.mask[1],s.mask[2],GL_FALSE);
            pBlendFuncSeparate(GL_CONSTANT_COLOR,GL_ONE,GL_CONSTANT_ALPHA,GL_ONE);
            float w=1-alpha;pBlendColor(w,w,w,w);glDrawArrays(op.mode,0,(GLsizei)op.vertices.size());
            glTexCoordPointer(4,GL_FLOAT,sizeof(Vertex),vertices[0].uv[0]);glColorPointer(4,GL_FLOAT,sizeof(Vertex),vertices[0].c);
            pBlendColor(alpha,alpha,alpha,alpha);glDrawArrays(op.mode,0,(GLsizei)op.vertices.size());
            if(!alpha_linear&&s.mask[3]){glColorMask(0,0,0,1);pBlendFuncSeparate(s.src_rgb,s.dst_rgb,s.src_alpha,s.dst_alpha);
                if(alpha<.5f){glTexCoordPointer(4,GL_FLOAT,sizeof(Vertex),old[0].uv[0]);glColorPointer(4,GL_FLOAT,sizeof(Vertex),old[0].c);}
                glDrawArrays(op.mode,0,(GLsizei)op.vertices.size());}
            return;
        }
        glDrawArrays(op.mode,first,vertex_count);
    };
    visit([&](const Op& op){
        draw_op(op);
        if(!restore_prefix&&field_prefix.plan.target && &op==&current.ops[field_prefix.plan.last]){
            const GLuint key=field_prefix.plan.target;
            field_prefix.save(replay_targets.at(key).fbo,current.targets.at(key));
        }
    });
    if(widescreen&&display_settings.wide&&display_settings.field_sides&&current.layout()==1)draw_field_sides(current,target,view);
    if(widescreen&&display_settings.wide&&display_settings.field_sides&&view.full_map)field_blur::draw_margins(current,target,view.atlas_width);
    const double finished=now();
    if(stream_choice.timing&&(!stream||uploaded))VertexStreamPolicy::measured(stream_choice,finished-submit_begin);
    replay_seconds+=finished-began;replay_count++;
    return alpha;
}
static int game_mode();
static int presentation_mode=-1;
static std::vector<OutgoingDraw> checkpoint_outgoing;
#include "presentation_clock.h"
static double output_period(){
    const double requested=1./std::max(display_settings.fps,1);
    return deck_renderer?std::max(requested,presentation_clock.refresh_period):requested;
}
// A long tick followed by a short one can still deliver the full native rate.
// Reserve the rendering thread when the average slips, or immediately on a
// major stall. The scheduler can still present during actual idle waits.
struct DeckSourceBudget {
    double relative_period=1;
    bool busy=false;
    void observe(double interval,double nominal){
        const double ratio=interval/nominal;
        relative_period+=.25*(std::clamp(ratio,0.,2.)-relative_period);
        busy=ratio>=1.5||relative_period>(busy?1.06:1.12);
    }
};
static DeckSourceBudget deck_source_budget;
static bool deck_native_layout_wait=false;
static void track_source_budget(int mode,double interval,bool continuous){
    if(!deck_renderer||!continuous){deck_source_budget={};return;}
    const double native_period=mode==3?1./15.:mode==6?1./60.:1./30.;
    deck_source_budget.observe(interval,native_period);
}
static double previous_origin=0,previous_period=1./30.;
static bool prepared_previous=false;
static unsigned displayed_source=0;
static float replay_presentation(GLuint target,bool force_previous=false){
    double t=now();bool old=force_previous||(prepared_previous&&t<origin);
    presentation_clock.draw_started=t;
    if(field_prefix.active){
        const double same_pair_end=old?std::min(origin,previous_origin+previous_period):origin+period;
        field_prefix.next(same_pair_end,output_period(),presentation_clock.replay_cost);
    }
    float alpha=(float)std::clamp((t-(old?previous_origin:origin))/(old?previous_period:period),0.,1.);
    if(old){std::swap(current,previous);std::swap(outgoing_draws,checkpoint_outgoing);}
    try{alpha=replay(target,alpha,true);}
    catch(...){if(old){std::swap(outgoing_draws,checkpoint_outgoing);std::swap(current,previous);}throw;}
    if(old){std::swap(outgoing_draws,checkpoint_outgoing);std::swap(current,previous);}
    displayed_source=sources-(old&&sources?1:0);return alpha;
}
static void advance_output(){
    next_output=presentation_clock.next_slot(next_output,now(),output_period());
}
static void matching_pump(){
    if(deck_source_budget.busy)return;
    present_reason=6;qa_stage(6);
    // Publish is pending: previous still contains the fully prepared pair
    // shown on the preceding tick. Temporarily replay it at CPU checkpoints.
    double t=now();if(t<next_output||t-origin>period*1.5)return;
    present_during_wait(true);advance_output();
}
static bool test_capture_requested=false;
#include "test_input.h"
#ifdef GEO_AUTOTEST
static bool test_fast_motion(int mode){
    // Bounded unattended evidence: manual capture often arrives after a fast
    // attack has ended. Observe existing geometry only; never drive inputs.
    static unsigned captures=0;static double last=0;
    if(mode!=3||captures>=6||now()-last<4)return false;
    unsigned moving=0;double maximum=0;
    for(const auto&op:current.ops)if(projected_geometry(op))for(unsigned i=0;i<op.vertices.size();i++)
        if(op.motion_valid[i]&&op.vertices[i].p[3]>0&&op.vertices[i].p[3]<.1f){
            double delta=std::hypot(double(op.vertices[i].p[0])-op.prior_positions[i][0],double(op.vertices[i].p[1])-op.prior_positions[i][1]);
            moving+=delta>8;maximum=std::max(maximum,delta);}
    if(moving<150||maximum<28)return false;
    captures++;last=now();char line[160];snprintf(line,sizeof(line),"automatic fast-motion comparison %u: moving references %u max displacement %.3f\n",captures,moving,maximum);log(line);return true;
}
static void test_timeline(double arrival,double interval,bool continuous){
    static FILE*f=nullptr;static unsigned count=0;if(count>=20000)return;
    if(!f){f=fopen("FF8_geometry_timeline.csv","w");if(f)fputs("source,mode,arrival,interval,continuous,origin,period,nominal\n",f);}
    if(f){fprintf(f,"%u,%d,%.9f,%.9f,%d,%.9f,%.9f,%.9f\n",sources,game_mode(),arrival,interval,continuous,origin,period,presentation_clock.nominal);if(++count%60==0)fflush(f);}
}
static void test_cadence(double time,float alpha){
    static FILE*f=nullptr;static unsigned count=0;if(count>=40000)return;
    if(!f){f=fopen("FF8_geometry_cadence.csv","w");if(f)fputs("time,source,mode,alpha,period\n",f);}
    if(f){fprintf(f,"%.9f,%u,%d,%.6f,%.9f\n",time,alpha<0?sources:displayed_source,game_mode(),alpha,period);if(++count%120==0)fflush(f);}
}
#endif
#ifndef GEO_AUTOTEST
static void qa_native_image(int,int);
#endif
#include "deck_cadence.h"
#include "deck_output_audit.h"
static void track_display_refresh(HDC handle,double time){
    static double next_check=0;
    if(time<next_check)return;
    next_check=time+2.;
    const double previous_refresh=presentation_clock.refresh_period;
    const double query_start=deck_renderer?now():0;
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    HMONITOR nearest=MonitorFromWindow(WindowFromDC(handle),MONITOR_DEFAULTTONEAREST);
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(GetMonitorInfoW(nearest,&monitor)&&EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1){
        presentation_clock.refresh_period=1./mode.dmDisplayFrequency;
        if(display_settings.automatic_fps)display_settings.fps=mode.dmDisplayFrequency>60?120:60;
    }
    else presentation_clock.refresh_period=0;
    if(deck_renderer&&display_settings.fps==60){
        const double finished=now(),duration=finished-query_start;
        if(duration>=.04||previous_refresh!=presentation_clock.refresh_period){
            deck_cadence.record({DeckCadence::EventKind::display_query,finished,duration,0,sources,current.mode,
                presentation_clock.refresh_period>0?int(1./presentation_clock.refresh_period+.5):0,0});
        }
    }
}
static bool begin_retro_frame(HDC handle,int& w,int& h){
    const bool panel=retro_panel::is_open();
    const int counter=display_settings.fps_counter;
    if(!counter)fps_counter::reset_meter();
    if(!panel&&!counter&&(!enabled||!retro_display::active(display_settings.retro))&&!retro_display::valid_front)return false;
    RECT rect{};
    if(!GetClientRect(WindowFromDC(handle),&rect)){retro_display::valid_front=false;return false;}
    w=rect.right;h=rect.bottom;
    RetroSettings filter=enabled?display_settings.retro:RetroSettings{};
    filter.lines=filter.visible_lines(current.raster_rows);
    const int scale=fps_counter::scale(counter,w,h);
    if(!retro_display::begin_window(filter,handle,w,h,panel||scale,
        panel||!scale?w:fps_counter::tile_w*scale,panel||!scale?h:fps_counter::tile_h*scale))return false;
    if(scale&&!retro_display::retiring_front)fps_counter::draw(w,h,scale);
    if(panel&&!retro_display::retiring_front)
        retro_panel_draw::draw(display_settings.retro,retro_panel::selected(),0,w,h,!enabled);
    return true;
}
static BOOL timed_swap(HDC handle,float alpha=-1,bool outer_restores=false,double* filter_seconds=nullptr,bool native_replayed=false){double began=now();
    if(stride_trace::enabled&&game_mode()==2)stride_trace::present(began,alpha<0?sources:displayed_source,alpha);
    int retro_w=0,retro_h=0;
    const double filter_start=filter_seconds?now():0;
    const bool filtered=begin_retro_frame(handle,retro_w,retro_h);
    if(filtered&&filter_seconds)*filter_seconds=now()-filter_start;
#ifndef GEO_AUTOTEST
    if(accessibility_test&&test_capture_requested)qa_native_image(width,height);
#endif
    if(diagnostic_trace&&presentation_mode==3&&alpha>=0){static FILE*f=nullptr;static unsigned rows=0;
        if(!rows&&!f){f=fopen("FF8_geometry_QA_pacing.csv","w");if(f)fputs("time,source,reason,alpha,lateness,gap,from,to\n",f);}
        if(f){fprintf(f,"%.9f,%u,%d,%.6f,%.6f,%.6f,%d,%d\n",began,sources,present_reason,alpha,began-next_output,maximum_gap,gap_from,gap_to);
            if(++rows%120==0)fflush(f);if(rows==9000){fclose(f);f=nullptr;}}}
    maximum_gap=0;qa_stage(12);
    if(!deck_renderer||display_settings.fps)track_display_refresh(handle,began);
    if(alpha>=0){
        presentation_clock.submitted(now());
    }
    const bool probed=deck_renderer&&deck_output_diagnostics&&display_settings.fps==60&&deck_output.before_swap(alpha,began,outer_restores,native_replayed);
    const double swap_start=deck_renderer&&display_settings.fps==60?now():0;
    BOOL result=real_swap(handle);
    if(probed&&!result)--deck_output.count;
    if(deck_renderer){
        if(result&&swap_start){
            const double finished=now();
            deck_cadence.present(current.mode,finished,alpha,sources,finished-swap_start,
                presentation_clock.replay_cost,deck_source_budget.busy);
        }
        else deck_cadence.last_mode=0;
    }
    if(result&&display_settings.fps_counter)fps_counter::presented(now());
    if(filtered){
        const double restore_start=filter_seconds?now():0;
        retro_display::end_window(result!=FALSE,outer_restores,retro_w,retro_h);
        if(filter_seconds)*filter_seconds+=now()-restore_start;
    }
    presentation_seconds+=now()-began;presentation_count++;
#ifdef GEO_AUTOTEST
    test_cadence(now(),alpha);
#else
    (void)alpha;
#endif
    return result;}
static void correspondence_audit(const char*name="FF8_geometry_correspondence.bin"){
    FILE*f=fopen(name,"wb");if(!f)return;
    uint32_t magic=0x3847454f,version=2;fwrite(&magic,4,1,f);fwrite(&version,4,1,f);
    for(const Frame*frame:{&previous,&current}){uint32_t count=(uint32_t)frame->ops.size();fwrite(&count,4,1,f);
        for(const auto&op:frame->ops){uint32_t fields[]={op.clear,op.mode,op.target,(uint32_t)op.state.program,(uint32_t)op.vertices.size(),(uint32_t)op.match};
            fwrite(fields,sizeof(fields),1,f);fwrite(&op.key,8,1,f);fwrite(op.state.model,sizeof(op.state.model),1,f);fwrite(op.state.projection,sizeof(op.state.projection),1,f);
            GLint ids[4];uint32_t used=0;for(int j=0;j<4;j++){ids[j]=op.state.texture[j].id;if(!op.state.program&&op.state.texture[j].enabled)used|=1u<<j;}
            for(const auto&u:op.state.uniforms)if(u.type==GL_SAMPLER_2D)used|=1u<<u.i[0];
            fwrite(ids,sizeof(ids),1,f);fwrite(&used,4,1,f);uint32_t state[]={uint32_t(op.state.blend),uint32_t(op.state.src_rgb),uint32_t(op.state.dst_rgb)};
            fwrite(state,sizeof(state),1,f);fwrite(op.state.viewport,sizeof(op.state.viewport),1,f);
            for(const auto&v:op.vertices){fwrite(v.p,sizeof(v.p),1,f);fwrite(v.uv,sizeof(v.uv),1,f);}}}
    fclose(f);log("bounded correspondence audit saved: ");log(name);log("\n");
}
static void comparison_image(const BYTE* a,const BYTE* b,int mode,unsigned serial,int sw=160,int sh=90){
    const int w=3*sw,h=sh;std::vector<BYTE> pixels(w*h*3);
    for(int y=0;y<h;y++)for(int x=0;x<sw;x++)for(int ch=0;ch<3;ch++){
        int at=4*(y*sw+x)+2-ch;pixels[3*(y*w+x)+ch]=a[at];pixels[3*(y*w+x+sw)+ch]=b[at];
        pixels[3*(y*w+x+2*sw)+ch]=(BYTE)std::min(255,8*abs(int(a[at])-int(b[at])));}
    BITMAPFILEHEADER header{};BITMAPINFOHEADER info{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info);header.bfSize=header.bfOffBits+(DWORD)pixels.size();
    info.biSize=sizeof(info);info.biWidth=w;info.biHeight=h;info.biPlanes=1;info.biBitCount=24;info.biCompression=BI_RGB;
    char name[100];snprintf(name,sizeof(name),"FF8_geometry_compare_mode%d_%u.bmp",mode,serial);FILE*f=fopen(name,"wb");
    if(f){fwrite(&header,sizeof(header),1,f);fwrite(&info,sizeof(info),1,f);fwrite(pixels.data(),pixels.size(),1,f);fclose(f);}
    char line[240];snprintf(line,sizeof(line),"comparison %s: left native, center replay, right difference x8\n",name);log(line);
    for(auto&entry:current.targets)if(entry.second.active){auto&t=entry.second;
        snprintf(line,sizeof(line)," target %u texture %u extent %dx%d depth %d format 0x%x\n",entry.first,t.texture,t.w,t.h,t.depth,t.format);log(line);}
}
static void target_comparisons(int mode){
    // Bounded, once per mode: compare each render stage to locate the first
    // divergence, instead of repeatedly asking for subjective screenshots.
    Guard guard(current);Surface a,b;const int w=640,h=480;
    resize(a,w,h,false);resize(b,w,h,false);
    pBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    std::vector<BYTE> left(w*h*4),right(left.size());
    for(const auto&entry:current.targets){auto&t=entry.second;if(!t.active)continue;
        GLuint reconstructed=entry.first?replay_targets.at(entry.first).fbo:test_target.fbo;
        GLuint original=current.framebuffer_for_target.at(entry.first);
        if(original){GLint old=0,attachment=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old);pBindFramebuffer(GL_READ_FRAMEBUFFER,original);
            pGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attachment);
            pBindFramebuffer(GL_READ_FRAMEBUFFER,old);if((GLuint)attachment!=t.texture)continue;}
        blit(original,a.fbo,t.w,t.h,w,h,GL_COLOR_BUFFER_BIT);blit(reconstructed,b.fbo,t.w,t.h,w,h,GL_COLOR_BUFFER_BIT);
        pBindFramebuffer(GL_READ_FRAMEBUFFER,a.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,left.data());
        pBindFramebuffer(GL_READ_FRAMEBUFFER,b.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,right.data());
        comparison_image(left.data(),right.data(),mode,1000+entry.first,w,h);
        double error=0;unsigned bad=0;for(size_t i=0;i<left.size();i++)if(i%4!=3){int delta=abs(int(left[i])-int(right[i]));error+=delta;bad+=delta>8;}
        char line[160];snprintf(line,sizeof(line),"stage %u mean %.5f bad %u of %d\n",entry.first,error/(w*h*3),bad,w*h*3);log(line);
    }
    release(a);release(b);
    char name[100];snprintf(name,sizeof(name),"FF8_geometry_draws_mode%d.txt",mode);FILE*f=fopen(name,"w");
    if(f){unsigned n=0;for(const auto&op:current.ops){
        fprintf(f,"%u target=%u clear=%x mode=%x vertices=%zu shader=%d depth=%d/%x blend=%d/%x/%x viewport=%d,%d,%d,%d\n",n++,op.target,op.clear,op.mode,op.vertices.size(),op.state.program,op.state.depth,op.state.depth_func,op.state.blend,op.state.src_rgb,op.state.dst_rgb,op.state.viewport[0],op.state.viewport[1],op.state.viewport[2],op.state.viewport[3]);
        for(int j=0;j<4;j++){auto&t=op.state.texture[j];if(t.id)fprintf(f," unit%d id=%d snapshot=%u enable=%d filter=%x/%x wrap=%x/%x\n",j,t.id,t.copy?t.copy->id:0,t.enabled,t.parameters.min_filter,t.parameters.mag_filter,t.parameters.wrap_s,t.parameters.wrap_t);}
    }fclose(f);}
}
#ifndef GEO_AUTOTEST
static Surface qa_tiny;
static void qa_native_image(int w,int h){
    if(!test_capture_requested||w<1||h<1)return;
    test_capture_requested=false;auto&tiny=qa_tiny;resize(tiny,640,360,false);blit(0,tiny.fbo,w,h,640,360,GL_COLOR_BUFFER_BIT);
    GLint r,rb,pbo;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&r);glGetIntegerv(GL_READ_BUFFER,&rb);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pbo);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);pBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,tiny.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<BYTE> pixels(640*360*3);glReadPixels(0,0,640,360,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    pBindFramebuffer(GL_READ_FRAMEBUFFER,r);glReadBuffer(rb);glPopClientAttrib();pBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    FILE*f=fopen("FF8_geometry_QA.ppm","wb");if(f){fputs("P6\n640 360\n255\n",f);for(int y=359;y>=0;y--)fwrite(pixels.data()+640*3*y,1,640*3,f);fclose(f);}
}
#endif
#include "world_readback.h"
struct WorldParityTimes {
    double replay=0, copy=0, samples=0, flush=0, collect=0;
    unsigned count=0;
};
static WorldParityTimes world_parity_times;
static void capture_native_reference(GLuint source){
    resize(reference_target,width,height,false);
    blit(source,reference_target.fbo,width,height,width,height,GL_COLOR_BUFFER_BIT);
}
static void submit_parity_samples(GLuint native_source=0,WorldParityTimes* times=nullptr,bool native_captured=false){
    const double begin=times?now():0;
    resize(test_target,width,height);
    resize(sample_a,160,deck_renderer?(deck_output_diagnostics?180+DeckOutputAudit::rows:180):90,false);
    if(!deck_renderer)resize(sample_b,160,90,false);
    replay(test_target.fbo,1);
    const double drawn=times?now():0;
    // Normalize both sources to the same offscreen orientation before scaling.
    // At integral downscale ratios, default-FBO vs FBO nearest sampling can
    // pick adjacent source rows at texel-boundary ties on the NVIDIA driver.
    if(!native_captured)capture_native_reference(native_source);
    const double copied=times?now():0;
    blit(reference_target.fbo,sample_a.fbo,width,height,160,90,GL_COLOR_BUFFER_BIT);
    blit(test_target.fbo,deck_renderer?sample_a.fbo:sample_b.fbo,width,height,160,90,GL_COLOR_BUFFER_BIT,0,0,deck_renderer?90:0);
    if(deck_renderer)deck_output.submit(sample_a.fbo);
    if(times){
        times->replay+=drawn-begin;times->copy+=copied-drawn;times->samples+=now()-copied;
    }
}
// A lit endpoint reconstructed from this tick can disagree with an entirely
// black native backbuffer. Retain the last displayed image for at most one
// native tick, and only when the accepted scene still matches the new replay.
struct DeckBlackEndpoint {
    unsigned tick=0,held=0;
    int mode=0,w=0,h=0;
    BYTE thumbnail[12*8*3]{};
    bool suspect=false;
    void sample(bool ok,GLenum error,const BYTE* native,const BYTE* reconstructed){
        suspect=false;
        if(display_settings.fps!=60||current.mode<1||current.mode>3||current.layout()!=current.mode||
           current.menu_edge||error){tick=0;return;}
        const bool consecutive=tick&&tick+1==sources&&mode==current.mode&&w==width&&h==height;
        if(!ok&&!consecutive){tick=0;return;}
        int changes[8][3]{};
        unsigned previous_lit=0,current_lit=0;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<12;++x){
            const unsigned pixel=((5+11*y)*160+7+13*x)*4,i=(y*12+x)*3;
            for(unsigned c=0;c<3;++c){
                if(ok)thumbnail[i+c]=native[pixel+c];
                else changes[(y/4)*4+x/3][c]+=int(thumbnail[i+c])-reconstructed[pixel+c];
            }
            if(!ok){
                previous_lit+=std::max({thumbnail[i],thumbnail[i+1],thumbnail[i+2]})>16;
                current_lit+=std::max({reconstructed[pixel],reconstructed[pixel+1],reconstructed[pixel+2]})>16;
            }
        }
        if(ok){tick=sources;mode=current.mode;w=width;h=height;return;}
        tick=0;
        unsigned delta=0;
        for(const auto& block:changes)for(int component:block)delta+=std::abs(component);
        if(previous_lit<32||current_lit<32||delta>sizeof(thumbnail)*12)return;
        // Dark clear colors can sit above digital black. Require a dim
        // whole frame and a low mean; even a single lit patch disqualifies it.
        unsigned brightness=0;
        for(size_t i=0;i<parity_plane_bytes;i+=4){
            BYTE peak=std::max({native[i],native[i+1],native[i+2]});
            if(peak>16)return;
            brightness+=native[i]+native[i+1]+native[i+2];
        }
        if(brightness>size_t(parity_plane_bytes/4)*3*8)return;
        suspect=true;
    }
};
static DeckBlackEndpoint deck_black;
static bool collect_parity(GLenum submission_error=GL_NO_ERROR,bool queued=false){
    static std::vector<BYTE> samples(parity_storage_bytes);
    BYTE* a=samples.data();
    BYTE* b=a+parity_plane_bytes;
    bool copied=false;
    if(queued){
        const GLenum checkpoint_error=glGetError();
        if(!submission_error)submission_error=checkpoint_error;
        copied=world_readback.copy(a);
    }
    if(!copied){
        ParityReadState guard;
        pBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        ParityReadState::read(sample_a.fbo,a,parity_read_rows());
        if(!deck_renderer)ParityReadState::read(sample_b.fbo,b);
    }
    double error=0;unsigned bad=0;for(size_t i=0;i<parity_plane_bytes;i++)if(i%4!=3){int d=abs(int(a[i])-int(b[i]));error+=d;bad+=d>8;}
    error/=160*90*3;GLenum error_code=glGetError();if(submission_error)error_code=submission_error;bool ok=!error_code&&error<0.75&&bad<160*90*3/100;
    if(deck_renderer){
        if(!error_code)deck_output.collect(b+parity_plane_bytes);
        deck_black.sample(ok,error_code,a,b);
    }
    current.infirmary_room=current.mode==1?infirmary::room_id():-1;
    current.infirmary_top=current.infirmary_room>=0?infirmary::camera_top(current.infirmary_room,samples):-1;
    field_blur::prepare(current);
    if(!ok){char line[180];snprintf(line,sizeof(line),"parity rejected: mean %.4f bad %u ops %zu vertices %u GL 0x%x\n",error,bad,current.ops.size(),current.vertex_count,error_code);
        if(parity_fail%30==0||(deck_renderer&&parity_fail<8)){
            log(line);
            if(deck_renderer){
                unsigned native_lit=0,replay_lit=0,native_peak=0,native_sum=0;
                for(size_t i=0;i<parity_plane_bytes;i+=4){
                    const unsigned peak=std::max({a[i],a[i+1],a[i+2]});
                    native_peak=std::max(native_peak,peak);native_sum+=a[i]+a[i+1]+a[i+2];
                    native_lit+=peak>16;
                    replay_lit+=std::max({b[i],b[i+1],b[i+2]})>16;
                }
                snprintf(line,sizeof(line),"Deck rejected endpoint | mode %d | game tick %u | lit pixels native %u replay %u of %zu | native peak %u mean %.2f\n",
                         current.mode,sources,native_lit,replay_lit,size_t(parity_plane_bytes/4),native_peak,native_sum/(160.*90*3));
                log(line);
            }
        }
        #ifdef GEO_AUTOTEST
        static unsigned captures[4]={};int mode=game_mode();
        if(mode>=1&&mode<=3&&current.vertex_count>(mode==3?10000:3000)){unsigned n=++captures[mode];if(n==1||n==30)comparison_image(a,b,mode,n);if(n==30)target_comparisons(mode);}
#endif
    }
    return ok;
}
static bool parity(bool native_captured=false,GLenum capture_error=GL_NO_ERROR){
    submit_parity_samples(0,nullptr,native_captured);
    return collect_parity(capture_error);
}
// The native samples and the wide candidate belong to this same game tick.
// Submit the candidate while the GPU transfers the samples. Only commit it
// after validation, so a rejected frame leaves the game's drawable intact.
static bool present_native_world(GLuint native=0,GLuint output=0){
    submit_parity_samples(native);
    const GLenum submitted=glGetError();
    if(submitted||!world_readback.available()||!world_readback.enqueue()){
        if(!collect_parity(submitted))return false;
        replay(output,1,true);
        return true;
    }
    replay(test_target.fbo,1,true);
    if(!collect_parity(GL_NO_ERROR,true))return false;
    blit(test_target.fbo,output,width,height,width,height,all_buffers());
    if(glGetError()!=GL_NO_ERROR){
        // Some pixel formats cannot receive a packed depth/stencil blit.
        // Present the validated scene directly on that drawable.
        world_readback.cooldown=600;
        replay(output,1,true);
    }
    return true;
}
static void reset_building(){
    building.match_input.reset();
    building.seed_clears.clear();
    building.depth_texture_for_target.clear();
    building.bones.clear();building.stride_velocity.clear();building.stride_follow={};building.ops.clear();building.framebuffer_for_target.clear();building.valid=true;building.started=false;building.vertex_count=0;building.camera_cut=false;
    for(auto it=building.targets.begin();it!=building.targets.end();){
        if(!it->second.active){release(it->second.seed);it=building.targets.erase(it);}else{it->second.active=false;++it;}}
}
#ifdef GEO_AUTOTEST
static Frame test_older;
#endif
static void publish(){
#ifdef GEO_AUTOTEST
    std::swap(previous,test_older);
#endif
    std::swap(previous,current);std::swap(current,building);reset_building();width=current.w;height=current.h;
}
static unsigned frame_waits=0;
static double requested_wait=0,reclaimed_wait=0;
struct WorldFrameStats {
    unsigned frames=0,draws=0;
    unsigned direct_frames=0,cached_targets=0,cached_arrays=0;
    double intervals=0,capture=0,layout=0,filter=0,swap=0;
};
static WorldFrameStats world_frame_stats;
static unsigned world_original_frames=0;
static double world_original_intervals=0;
static void stats(){static double last=0;static unsigned ps=0,po=0;
    double t=now();if(t-last<5)return;
    std::unique_ptr<FILE,decltype(&fclose)> stream(fopen("FF8_geometry.log","a"),fclose);
    auto write_stats=[&](const char* message){if(stream)fputs(message,stream.get());};
    char line[320];snprintf(line,sizeof(line),
        "source %.2f/s | geometry presents %.2f/s | moved draws %u | parity pass %u fail %u | waits %u | %s\n",
        (sources-ps)/(t-last),(outputs-po)/(t-last),moving_draws,parity_ok,parity_fail,waits,can_present?"geometry":"native");
    write_stats(line);ps=sources;po=outputs;last=t;
    if(deck_renderer){
        deck_cadence.report(write_stats);
        auto& budget=deck_wait_recovery;
        if(budget.missed_windows||budget.probes||budget.failed_probes){
            snprintf(line,sizeof(line),"Deck idle windows | %u blocked by estimated draw cost | %u lower estimates tested | %u exceeded\n",
                     budget.missed_windows,budget.probes,budget.failed_probes);
            write_stats(line);budget.missed_windows=budget.probes=budget.failed_probes=0;
        }
        deck_output.report(write_stats);
        if(deck_black.held){
            snprintf(line,sizeof(line),"Deck black endpoint | %u single presentations held after native/replay mismatch\n",deck_black.held);
            write_stats(line);deck_black.held=0;
        }
        if(deck_interval.held||deck_interval.changes||deck_interval.failures){
            snprintf(line,sizeof(line),"Deck swap interval | %u changes | %u transient native frames | %u driver failures\n",
                     deck_interval.changes,deck_interval.held,deck_interval.failures);write_stats(line);
            deck_interval.held=deck_interval.changes=deck_interval.failures=0;
        }
    }
    if(world_original_frames){
        snprintf(line,sizeof(line),"Deck world Original 4:3 | %.1f FPS | %u travel frames\n",
            world_original_frames/world_original_intervals,world_original_frames);
        write_stats(line);world_original_frames=0;world_original_intervals=0;
    }
    if(world_frame_stats.frames){
        const auto& w=world_frame_stats;
        snprintf(line,sizeof(line),"Deck world native projection: %u of %u travel frames | cached target %u | cached arrays %u\n",
            w.direct_frames,w.frames,w.cached_targets,w.cached_arrays);write_stats(line);
        snprintf(line,sizeof(line),"Deck world Original widescreen | %.1f FPS | %.0f commands/frame | capture or projection setup %.2f ms | validation and layout %.2f ms | filter CPU %.2f ms | swap and other %.2f ms | drawable %dx%d\n",
            w.frames/w.intervals,double(w.draws)/w.frames,1000*w.capture/w.frames,1000*w.layout/w.frames,1000*w.filter/w.frames,1000*w.swap/w.frames,width,height);
        write_stats(line);world_frame_stats={};
    }
    if(frame_waits){
        snprintf(line,sizeof(line),"frame limiter %u waits | requested %.3f ms | drawing credited %.3f ms per wait\n",
                 frame_waits,1000*requested_wait/frame_waits,1000*reclaimed_wait/frame_waits);write_stats(line);
        frame_waits=0;requested_wait=reclaimed_wait=0;
    }
      if(registered_after){snprintf(line,sizeof(line),"indexed native battle vertices, prior %u current %u\n",registered_before,registered_after);write_stats(line);}
    snprintf(line,sizeof(line),"native texture filter passes %u | clock deadline presents %u | sleep deadline presents %u\n",baked_passes,clock_presents,sleep_presents);write_stats(line);
    snprintf(line,sizeof(line),"texture content snapshots %u | changed versions %u | cached %zu | storage reused %u\n",texture_copies,texture_updates,texture_cache.size(),reused_storage);write_stats(line);
    snprintf(line,sizeof(line),"matched triangles %u | singular rejects %u | connected triangles held native %u | high resolution scheduler %d\n",matched_candidates,singular_rejects,incoherent_draws,output_timer!=nullptr);write_stats(line);
    snprintf(line,sizeof(line),"shared points %u | repaired conflicts %u | filled vertices %u | world camera %d confidence %.4f points %u | departing triangles %u\n",coherent_nodes,coherent_conflicts,coherent_filled,camera_motion.valid,camera_motion.confidence,camera_motion.points,outgoing_triangles);write_stats(line);
    snprintf(line,sizeof(line),"topology anchors %u | partial anchors %u | corrected references %u\n",topology_anchors,topology_partial,topology_corrected);write_stats(line);
    snprintf(line,sizeof(line),"CPU replay average %.3f ms | SwapBuffers average %.3f ms\n",replay_count?1000*replay_seconds/replay_count:0,presentation_count?1000*presentation_seconds/presentation_count:0);write_stats(line);
    snprintf(line,sizeof(line),"CPU capture/draw %.3f ms (%u draws) | match/frame %.3f ms | parity/frame %.3f ms (%u frames)\n",capture_count?1000*capture_seconds/capture_count:0,capture_count,matching_count?1000*matching_seconds/matching_count:0,matching_count?1000*parity_seconds/matching_count:0,matching_count);write_stats(line);
    capture_seconds=matching_seconds=parity_seconds=0;capture_count=matching_count=0;
    if(world_parity_times.count){
        const double factor=1000./world_parity_times.count;
        snprintf(line,sizeof(line),"world parity ms | replay %.3f | native blit %.3f | downsample %.3f | flush %.3f | collect %.3f | frames %u\n",
            world_parity_times.replay*factor,world_parity_times.copy*factor,world_parity_times.samples*factor,
            world_parity_times.flush*factor,world_parity_times.collect*factor,world_parity_times.count);
        write_stats(line);world_parity_times={};world_readback.report(write_stats);
    }
    replay_seconds=presentation_seconds=0;replay_count=presentation_count=0;
}
// The guest mode word is shared by the encounter effect and active combat.
// The installed main-loop callback (game object + B40) distinguishes them.
static constexpr int encounter_transition=3003;
static int game_mode(){
    if(!engine)return -1;const unsigned guest=0x1cd8fc6;
    auto entry=(const unsigned*)((BYTE*)engine+0x188edd0+(guest>>12)*4);
    if(!readable(entry,4))return -1;const void*p=(const void*)(uintptr_t)(*entry+(guest&4095));
    if(!readable(p,2))return -1;unsigned short value;memcpy(&value,p,2);
#ifdef GEO_NATIVE_CAPTURE
    if(display_settings.wide){
        uint32_t obj=0,loop=0;
        if(native_pose::fetch(0x1a79d88,&obj,4)&&obj&&obj<0x10000000&&
           native_pose::fetch(obj+0xb40,&loop,4)&&loop==0x559890)return encounter_transition;
    }
#endif
    return value;
}
static Surface encounter_native;
// When a scene changes, parity or the next interpolation pair may not be
// available yet. Scale this swap's actual image, never an old parity sample.
static bool present_native_layout(const Frame& f,const WideView& v,GLuint target=0,bool transient_3d=false){
    const bool field=f.layout()==1;
    if(!transient_3d&&!v.menu&&!v.outer_width&&!f.menu_edge&&f.travel==Frame::Travel::none)return false;
    if(!v.menu&&!gameplay_layout(f.layout()))return false;
    const int w=f.w,h=f.h;if(w<=0||h<=0)return false;
    Guard guard(Frame{});
    resize(encounter_native,w,h,false);
    blit(target,encounter_native.fbo,w,h,w,h,GL_COLOR_BUFFER_BIT);
    const int sw=std::min(w,int(std::lround(h*4./3))),sh=std::min(h,int(std::lround(w*3./4)));
    const int sx=(w-sw)/2,sy=(h-sh)/2;
    const int lines=f.layout()==3?432:448;
    const int dh=v.outer_height?v.outer_height:int(std::lround(sh*480./lines));
    const bool native_sides=(transient_3d||(f.travel!=Frame::Travel::none&&f.layout()==2))&&!v.full_map;
    const int natural_width=std::min(w,int(std::lround(sw*480./lines)));
    const int dw=native_sides?natural_width:(v.outer_width?v.outer_width:(field?natural_width:w));
    const int dx=(w-dw)/2,dy=v.menu?v.menu_y:(h-dh)/2;
    glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);glDrawBuffer(target?GL_COLOR_ATTACHMENT0:GL_BACK);
    glClearColor(0,0,0,1);real_clear(GL_COLOR_BUFFER_BIT);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,encounter_native.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    pBlitFramebuffer(sx,sy,sx+sw,sy+sh,dx,dy,dx+dw,dy+dh,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    if(native_sides&&display_settings.field_sides)field_blur::draw_margins(f,target,dw);
    else if(field&&display_settings.field_sides){
        if(field_blur::preferred)field_blur::draw_margins(f,target,dw);
        else draw_field_sides(f,target,v);
    }
    return true;
}
static void present_encounter(int w,int h,int lines){
    const int native_width=int(std::lround(h*4./3.));
    if(w<=native_width+2||h<1)return;
    auto&native=encounter_native;
    resize(native,w,h,false);
    blit(0,native.fbo,w,h,w,h,GL_COLOR_BUFFER_BIT);
    blit(native.fbo,0,w,h,w,h,GL_COLOR_BUFFER_BIT,(w-native_width)/2,
         int(std::lround(h*(480-lines)/960.)));
}
#ifdef GEO_AUTOTEST
#include "test_capture.h"
#endif
static BOOL WINAPI swap_hook(HDC handle){
    if(inside||!wglGetCurrentContext()||(!loaded&&!initialize_gl())||
       GetCurrentThreadId()!=thread||wglGetCurrentContext()!=context)return real_swap(handle);
    const bool direct_world=deck_renderer&&deck_world.active;
    const bool direct_complete=direct_world&&deck_world.scene&&deck_world.output;
    retro_panel::poll(WindowFromDC(handle),retro_ini_path.c_str(),display_settings.retro);
#ifdef GEO_AUTOTEST
    test_input_poll();
#else
    if(accessibility_test)test_input_poll();
#endif
    qa_stage(7);
    double t=now(),dt=last_swap?t-last_swap:0;last_swap=t;sources++;bool was_presenting=can_present;can_present=false;bool saved=false;
    const double pair_work_start=deck_renderer?matching_seconds+parity_seconds:0;
    previous_origin=origin;previous_period=period;prepared_previous=false;
    float displayed_alpha=-1;bool output_due=true,native_endpoint=false,published=false,native_scaled=false;
    double source_replay_start=0,source_backup_time=0;
    bool measure_world=false;
    bool hold_display=false;
    int deck_scene=0;
    try {
        int mode=game_mode();
        if(deck_renderer)deck_cadence.source(display_settings.fps==60?mode:0,t);
        bool candidate=enabled&&!native_deck_frame()&&building.valid&&building.started&&!building.ops.empty();
        if(engine&&(mode<1||(mode>3&&mode!=6)))candidate=false;
        building.stride_dt=float(dt);building.mode=mode;building.infirmary_room=-1;building.infirmary_top=-1;building.reactive_sides=false;
        if(!building.started&&display_settings.wide){
            RECT r{};if(GetClientRect(WindowFromDC(handle),&r)){building.w=r.right;building.h=r.bottom;}
        }
        publish();published=true;
        if(display_settings.wide&&!(direct_complete&&mode==2))resolve_menu_layout(current,previous);
        else {current.layout_override=-1;current.menu_edge=0;}
        WideView transition(current,display_settings.wide);
        current.travel=Frame::Travel::none;
        if(display_settings.wide&&resolve_travel_layout(current,previous,transition))
            transition=WideView(current,true);
        if(direct_complete&&mode==2){current.travel=Frame::Travel::scene;native_endpoint=true;}
        current.raster_rows=240;
        if(display_settings.wide&&!transition.full_map){
            if(current.travel!=Frame::Travel::none)current.raster_rows=224;
            else if(transition.battle&&transition.outer_width)current.raster_rows=216;
        }
        bool pair=previous.valid&&previous.started&&previous.w==width&&previous.h==height&&previous.mode==mode&&previous.layout()==current.layout()&&previous.travel==current.travel;
        if(deck_renderer&&(display_settings.fps!=60||previous.mode!=mode||previous.w!=width||previous.h!=height))
            deck_wait_recovery={};
        if(candidate&&(current.menu_edge||current.travel!=Frame::Travel::none)){
            candidate=transition.menu?transition.menu_composite(current.ops.back()):transition.outer_width!=0;
        }
#ifdef GEO_NATIVE_CAPTURE
        {unsigned seen,groups;
         native_pose::take(current.bones,seen,groups);
#ifdef GEO_AUTOTEST
         native_pose::audit(current,current.bones,seen,groups);
#endif
        }
#endif
#ifdef GEO_AUTOTEST
        std::swap(reference_target,previous_reference);
#endif
        checkpoint_outgoing=std::move(outgoing_draws);
        if(enabled&&display_settings.wide&&mode==encounter_transition){
            int lines=480;
#ifdef GEO_NATIVE_CAPTURE
            int region=0;
            if(native_pose::fetch(0x204db24,&region,4)&&(region==448||region==224))lines=448;
#endif
            RECT area{};
            if(GetClientRect(WindowFromDC(handle),&area))present_encounter(area.right,area.bottom,lines);
        }
        const bool atlas_open=candidate&&mode==2&&(display_settings.wide?transition.full_map:WideView(current,true).full_map);
        if(deck_renderer&&enabled&&display_settings.fps==60&&mode>=1&&mode<=3&&
           current.layout()==mode&&!current.menu_edge&&!atlas_open)deck_scene=mode;
        measure_world=deck_renderer&&candidate&&display_settings.wide&&!display_settings.fps&&mode==2&&
            previous.mode==2&&current.layout()==2&&transition.outer_width&&!atlas_open&&!current.menu_edge&&
            current.travel==Frame::Travel::scene&&dt>0&&dt<.25;
        // The atlas is a 2D interface inside world mode. Never match an atlas
        // capture with terrain, or replay a stale pointer during a native wait.
        if(pair && mode==2 && !was_presenting && !atlas_open &&
           WideView(previous,true).full_map)pair=false;
        const bool scene_entry=candidate&&display_settings.wide&&(current.layout()!=mode||!pair||dt<0.008||dt>0.15);
        track_source_budget(mode,dt,pair&&display_settings.fps!=0);
        // A prerendered field has a fixed camera. At the original frame rate
        // the game already composed the actors, masks and animated tiles.
        if(deck_renderer&&candidate&&display_settings.fps==0&&display_settings.wide&&
           current.layout()==1&&pair&&!current.menu_edge&&transition.outer_width&&field_blur::preferred){
            field_blur::prepare(current);
            if(current.reactive_sides){
                native_scaled=present_native_layout(current,transition);
                native_endpoint=native_scaled;
            }
        }
        if(!native_endpoint&&candidate&&(display_settings.fps==0||atlas_open||scene_entry)){
            // One endpoint per game swap. Keep the native wait and swap interval.
            if(measure_world&&pair){
                native_endpoint=present_native_world();
            }else if((display_settings.wide||(display_settings.atlas&&atlas_open))&&parity()){
                replay(0,1,true);native_endpoint=true;
            }
        }else if(!native_endpoint&&candidate&&dt>=0.008&&dt<=0.15){
            // Own a copy of the game's exact backbuffer before any extra
            // swap for the outgoing pair or any offscreen validation draw.
            const bool captured=deck_renderer&&display_settings.fps==60&&mode>=1&&mode<=3;
            double native_copy_cost=0;
            GLenum capture_error=GL_NO_ERROR;
            if(captured){
                const double begin=now();capture_native_reference(0);
                capture_error=glGetError();
                native_copy_cost=now()-begin;
                parity_seconds+=native_copy_cost;
            }
            // A source tick can arrive at the same time as a 60 Hz slot.
            // Present the already validated pair before queuing the next
            // endpoint's GPU work; doing it after the readback adds jitter.
            if(deck_renderer&&display_settings.fps==60&&(mode==1||mode==2)&&!deck_source_budget.busy&&
               pair&&was_presenting&&presentation_mode==mode&&!current.menu_edge&&!atlas_open){
                const double time=now();
                if(time>=next_output&&time-origin<period*1.5){
                    present_during_wait(true);advance_output();
                }
            }
            // At alpha=1 battle replay uses captured vertices. Its depth
            // corrections and departing faces apply only to intermediate
            // poses. Queue validation of this endpoint before CPU matching.
            const bool early_samples=pair&&(mode==2||(deck_renderer&&display_settings.fps==60&&(mode==1||mode==3)))&&
                world_readback.available();
            FieldMatchPreparation field_work(current,early_samples&&mode==1,
                previous.match_input?previous.match_input->field_topology.get():nullptr);
            bool queued_world=false;
            GLenum submit_error=GL_NO_ERROR;
            if(early_samples){
                const double began=now();
                submit_parity_samples(0,&world_parity_times,captured);
                submit_error=glGetError();
                if(capture_error)submit_error=capture_error;
                world_parity_times.copy+=native_copy_cost;
                if(!submit_error)queued_world=world_readback.enqueue();
                const double flushing=now();
                glFlush();
                const GLenum queue_state_error=glGetError();
                if(!submit_error)submit_error=queue_state_error;
                world_parity_times.flush+=now()-flushing;
                parity_seconds+=now()-began;
            }
            matching_checkpoint=was_presenting&&pair&&presentation_mode==mode?matching_pump:nullptr;
            double phase=now();if(mode==6)match_menu_frames();else match_frames(mode,&field_work);matching_seconds+=now()-phase;matching_count++;
            if(matching_checkpoint)matching_checkpoint();matching_checkpoint=nullptr;
            #ifdef GEO_AUTOTEST
            static unsigned battle_audit=0;if(mode==3&&current.vertex_count>10000&&++battle_audit==30)correspondence_audit();
#endif
            // A GPU readback can span the next deadline. Finish one prepared
            // intermediate frame before blocking for the endpoint comparison.
            if(!deck_source_budget.busy&&was_presenting&&pair&&presentation_mode==mode&&now()+.002>=next_output&&now()-origin<period*1.5){
                present_reason=9;qa_stage(9);present_during_wait(true);advance_output();}
            qa_stage(10);phase=now();
            const bool valid=early_samples?collect_parity(submit_error,queued_world):parity(captured,capture_error);
            hold_display=!valid&&captured&&deck_black.suspect&&was_presenting&&pair&&
                presentation_mode==mode&&!current.menu_edge&&!atlas_open&&current.layout()==mode;
            if(hold_display)++deck_black.held;
            if(early_samples){world_parity_times.collect+=now()-phase;++world_parity_times.count;}
            if(valid){
                parity_ok++;
                can_present=pair&&(moving_draws>0||matched_candidates>0||(mode==6&&menu_pairs>0)||(was_presenting&&presentation_mode==mode));
                if(!can_present&&display_settings.wide){
                    replay(0,1,true);
                    native_endpoint=true;
                }
            }else parity_fail++;parity_seconds+=now()-phase;
#ifdef GEO_AUTOTEST
            if(can_present&&(test_capture_requested||test_fast_motion(mode))){test_capture_requested=false;test_capture_pair(mode);}
#endif
        }
        if(deck_renderer){
            if(!enabled||display_settings.fps||!display_settings.wide||mode!=2||current.menu_edge||atlas_open||
               (direct_world&&(!direct_complete||deck_world.failed)))deck_world.view.reset();
            else if(!direct_world){
                deck_world.view.reset();
                if(native_endpoint&&transition.scene_width&&transition.horizontal<1&&!transition.full_map&&
                   current.travel==Frame::Travel::scene&&!native_atlas_possible()){
                    deck_world.view=std::make_unique<WideView>(transition);
                    deck_world.selector=native_atlas_view;
                }
            }
        }
        if(can_present){
            prepared_previous=was_presenting&&pair&&presentation_mode==mode;
            presentation_clock.update(t,dt,prepared_previous);origin=presentation_clock.origin;period=presentation_clock.period;
#ifdef GEO_AUTOTEST
            test_timeline(t,dt,prepared_previous);
#endif
            presentation_mode=mode;
            if(!was_presenting)next_output=t;
            output_due=now()>=next_output;
            if(output_due){
                if(deck_renderer&&display_settings.fps==60&&prepared_previous&&mode>=1&&mode<=3&&
                   deck_source_budget.busy&&deck_wait_recovery.needs_sample(presentation_clock,t))source_replay_start=now();
                resize(backup,width,height);blit(0,backup.fbo,width,height,width,height,all_buffers());saved=true;
                if(source_replay_start)source_backup_time=now()-source_replay_start;
                displayed_alpha=replay_presentation(0);}
        }
    }catch(const std::exception&e){
        deck_world.view.reset();
        if(saved)blit(backup.fbo,0,width,height,width,height,all_buffers());
        can_present=false;char line[220];snprintf(line,sizeof(line),"replay fallback: %s\n",e.what());log(line);
    }
    matching_checkpoint=nullptr;if(!can_present){prepared_previous=false;checkpoint_outgoing.clear();}
    if(deck_renderer&&!can_present){deck_wait_recovery.clear_samples();deck_wait_recovery.missed_window=0;}
    const bool transient_3d=deck_scene>=2&&deck_interval.within(deck_scene,current.w,current.h,t);
    if(published&&enabled&&display_settings.wide&&!native_endpoint&&!can_present&&!hold_display&&
       (current.layout()==6||current.menu_edge||current.layout()==1||current.travel!=Frame::Travel::none||transient_3d)){
        try{
            WideView fallback(current,true);
            if(transient_3d&&!fallback.outer_width&&previous.mode==deck_scene&&
               previous.w==current.w&&previous.h==current.h)fallback=WideView(previous,true);
            native_scaled=present_native_layout(current,fallback,0,transient_3d);
        }
        catch(const std::exception& e){log(e.what());log("\n");}
    }
    const bool unpaced=deck_renderer?deck_interval.keep(deck_scene,current.w,current.h,can_present,t):can_present;
    if((uintptr_t)set_interval>3&&(uintptr_t)set_interval!=UINTPTR_MAX){
        if(unpaced==interval_changed){
            if(deck_renderer)deck_interval.retry_after=0;
        }else if(!deck_renderer||t>=deck_interval.retry_after){
            if(deck_renderer&&saved_interval==0)interval_changed=unpaced;
            else{
                const int requested=unpaced?0:saved_interval;
                const double sync_start=deck_renderer?now():0;
                const BOOL applied=set_interval(requested);
                if(deck_renderer){
                    const double finished=now();
                    if(applied){++deck_interval.changes;interval_changed=unpaced;deck_interval.retry_after=0;}
                    else{++deck_interval.failures;deck_interval.retry_after=finished+1.;}
                    deck_cadence.record({DeckCadence::EventKind::swap_interval,finished,finished-sync_start,
                        0,sources,current.mode,requested,0,applied!=FALSE});
                }else interval_changed=unpaced;
            }
        }
    }
    present_reason=7;qa_stage(11);BOOL result=TRUE;
    const double layout_seconds=deck_renderer?now()-t:0;
    double filter_seconds=0;
    const bool time_filter=deck_renderer&&!display_settings.fps&&display_settings.wide;
    if(!hold_display&&(!can_present||output_due)){
        result=timed_swap(handle,can_present?displayed_alpha:-1.f,native_scaled,time_filter?&filter_seconds:nullptr,native_endpoint);
        if(source_replay_start&&can_present&&result){
            const double finished=now();
            deck_wait_recovery.source(presentation_clock,finished,finished-source_replay_start,source_backup_time);
        }
        if(native_scaled)blit(encounter_native.fbo,0,width,height,width,height,GL_COLOR_BUFFER_BIT);
        if(can_present){outputs++;advance_output();}
    }
    deck_native_layout_wait=deck_renderer&&!can_present&&!display_settings.fps&&
        display_settings.wide&&(native_endpoint||native_scaled)&&(current.mode>=1&&current.mode<=3||current.mode==6);
    if(deck_native_layout_wait)
        // Only measured compositor work earns credit against an overdue native
        // delay. Waiting for vertical sync inside SwapBuffers earns none.
        // Some engine ticks request their delay after swapping. Carry only
        // projection work that was not already credited before this swap.
        native_frame_wait.start(current.mode,t,dt,layout_seconds+filter_seconds+
            (direct_complete&&current.mode==2?deck_world.wait_credit:0.),true);
    else {
        native_frame_wait.start(presentation_mode,t,dt,now()-t,can_present&&prepared_previous);
        // The limiter delay belongs to this tick. The previous tick may have
        // been punctual even if today's matching/validation used most of the
        // budget. Only the portion that overruns this tick's deadline can be
        // reclaimed, at most once; unrelated game/driver time earns no credit.
        if(deck_renderer&&display_settings.fps==60&&can_present&&prepared_previous&&
           current.mode>=1&&current.mode<=3){
            const double pair_work=matching_seconds+parity_seconds-pair_work_start;
            native_frame_wait.draw_credit=std::max(native_frame_wait.draw_credit,pair_work);
        }
    }
    if(measure_world||(direct_complete&&dt>0&&dt<.25)){
        auto& w=world_frame_stats;
        ++w.frames;w.direct_frames+=direct_complete;w.draws+=direct_complete?deck_world.draws:unsigned(current.ops.size());w.intervals+=dt;
        w.cached_targets+=direct_complete&&world_bindings::tracked;
        w.cached_arrays+=direct_complete&&world_state::cached_buffers;
        w.capture+=world_capture_time+deck_world.setup_seconds;w.layout+=layout_seconds;w.filter+=filter_seconds;w.swap+=now()-t-layout_seconds-filter_seconds;
    }
    if(native_deck_frame()&&current.mode==2&&previous.mode==2&&dt>0&&dt<.25&&!native_atlas_possible()){
        ++world_original_frames;world_original_intervals+=dt;
    }
    world_capture_time=0;
    deck_world.begin();native_atlas_view=unread_atlas_view;
    stats();return result;
}
// Users can also choose original frame rate, original aspect and original
// atlas with only a retro filter. That path needs no scene capture or timers.
static BOOL WINAPI retro_only_swap(HDC handle){
    if(!wglGetCurrentContext()||(!loaded&&!initialize_gl())||
       GetCurrentThreadId()!=thread||wglGetCurrentContext()!=context)return real_swap(handle);
    retro_panel::poll(WindowFromDC(handle),retro_ini_path.c_str(),display_settings.retro);

    int w=0,h=0;
    const bool filtered=begin_retro_frame(handle,w,h);
    const BOOL result=real_swap(handle);
    if(result&&display_settings.fps_counter)fps_counter::presented(now());
    if(filtered)retro_display::end_window(result!=FALSE,false,w,h);
    return result;
}
// The native renderer cannot submit new drawing on this thread until its
// sleep/wait returns. All intermediate presents in that wait can restore
// from the same original color, depth and stencil image.
struct DeckIdleBackup {
    static inline thread_local DeckIdleBackup* active=nullptr;
    DeckIdleBackup* parent=active;
    bool saved=false;
    DeckIdleBackup(){if(deck_renderer&&display_settings.fps==60)active=this;}
    ~DeckIdleBackup(){active=parent;}
};
static void present_during_wait(bool force_previous){
    // Snapshot and restore the entire native BACK buffer (including depth),
    // so extra swaps never become input to incremental game rendering.
    const double started=now();
    auto window=DeckIdleBackup::active;
    if(!window||!window->saved){
        resize(backup,width,height);blit(0,backup.fbo,width,height,width,height,all_buffers());
        if(window)window->saved=true;
    }
    BOOL swapped=FALSE;
    try{float alpha=replay_presentation(0,force_previous);swapped=timed_swap(dc,alpha,true);outputs++;}
    catch(...){blit(backup.fbo,0,width,height,width,height,all_buffers());throw;}
    blit(backup.fbo,0,width,height,width,height,all_buffers());
    const double finished=now(),elapsed=finished-started;
    presentation_clock.measured_replay(elapsed);
    if(deck_renderer&&display_settings.fps==60&&swapped)deck_wait_recovery.extra(finished,elapsed);
}
static void pump_due(double earliest){
    if(inside||!enabled||!loaded||!can_present||GetCurrentThreadId()!=thread)return;
    double t=now();if(t<earliest||t-origin>period*1.5)return;
    if(wglGetCurrentContext()!=context)return;
    if(engine&&game_mode()!=presentation_mode){can_present=false;return;}
    try{present_during_wait();advance_output();}
    catch(const std::exception&e){can_present=false;log("mid-frame presentation fallback: ");log(e.what());log("\n");}
}
static void service_at(int reason,double earliest){
    if(deck_source_budget.busy&&reason!=4)return;
    if(GetCurrentThreadId()!=thread||!loaded||inside)return;
    present_reason=reason;qa_stage(reason);pump_due(earliest);
}
static bool pacing_callback=false;
static BOOL WINAPI counter_hook(LARGE_INTEGER*value){
    BOOL result=real_counter(value);
    if(!result||GetCurrentThreadId()!=thread||!can_present||inside||pacing_callback||
       value->QuadPart<double(frequency.QuadPart)*next_output)return result;
    pacing_callback=true;unsigned prior=outputs;service_at(3);clock_presents+=outputs-prior;pacing_callback=false;
    return real_counter(value);
}
// Service the compositor inside the time requested by either the OS sleep
// or the game's frame limiter. Both delays end on the same wall-clock grid.
static bool draw_within_wait(DWORD milliseconds,bool game_limiter=false){
    if(!milliseconds||milliseconds>150||GetCurrentThreadId()!=thread||!can_present||inside||pacing_callback||
       !output_timer||wglGetCurrentContext()!=context)return false;
    const double start=now(),requested=milliseconds*.001;
    double duration=requested;
    if(game_limiter){
        duration=native_frame_wait.remaining(start,requested);
        requested_wait+=requested;reclaimed_wait+=requested-duration;++frame_waits;
    }
    const double end=start+duration;
    DeckIdleBackup idle_backup;
    const bool repeated_field=presentation_mode==1&&duration>=output_period()+2*presentation_clock.replay_cost+.0005;
    FieldPrefixCache::Window prefix_window(field_prefix,end,repeated_field);
    pacing_callback=true;
    while(can_present){
        double time=now();if(time>=end)break;
        // Towns and world travel have short waits within a 30 Hz native
        // tick. Battles already schedule several outputs in a 15 Hz tick.
        const bool short_scene_wait=deck_renderer&&display_settings.fps==60&&deck_source_budget.busy&&
            (presentation_mode==1||presentation_mode==2);
        const double slot=short_scene_wait?presentation_clock.wait_slot(next_output,time,end,output_period()):next_output;
        const bool can_draw=presentation_clock.fits_wait(std::max(time,slot),end);
        if(!can_draw&&short_scene_wait)
            deck_wait_recovery.missed(next_output,time,end);
        double wake=can_draw?slot:end;
        if(wake>time){LARGE_INTEGER due;due.QuadPart=-(LONGLONG)std::max(1.,std::ceil((wake-time)*1e7));
            if(!SetWaitableTimer(output_timer,&due,0,nullptr,nullptr,FALSE))break;
            if(real_wait(output_timer,INFINITE)!=WAIT_OBJECT_0)break;}
        if(!can_draw||!presentation_clock.fits_wait(now(),end))break;
        unsigned prior=outputs;service_at(4,slot);sleep_presents+=outputs-prior;
        if(prior==outputs)break;
    }
    // An early or late timer wake can leave less than one Windows Sleep tick.
    // Use the existing high resolution timer for that tail whenever possible.
    double remaining=end-now();if(remaining>0){
        LARGE_INTEGER due;due.QuadPart=-(LONGLONG)std::max(1.,std::ceil(remaining*1e7));
        if(!SetWaitableTimer(output_timer,&due,0,nullptr,nullptr,FALSE)||real_wait(output_timer,INFINITE)!=WAIT_OBJECT_0){
            remaining=end-now();if(remaining>0)real_sleep((DWORD)std::ceil(remaining*1000.));
        }
    }
    pacing_callback=false;
    return true;
}
static VOID WINAPI sleep_hook(DWORD milliseconds){
    if(!draw_within_wait(milliseconds))real_sleep(milliseconds);
}
#ifdef GEO_NATIVE_CAPTURE
static void (__cdecl *native_delay_ms)(unsigned);
static void __cdecl draw_within_delay(unsigned milliseconds){
    // Widescreen still draws an endpoint at original FPS. Credit only the
    // measured layout work that overruns the game's own next deadline.
    if(deck_native_layout_wait&&!can_present&&loaded&&GetCurrentThreadId()==thread&&milliseconds&&milliseconds<=150){
        double requested=milliseconds*.001;
        if(deck_world.active&&deck_world.wait_credit>0&&game_mode()==2){
            // The projection work was paid before SwapBuffers, in this native
            // tick. remaining() can reclaim only time past its 30 Hz deadline.
            native_frame_wait.draw_credit+=deck_world.wait_credit;
            deck_world.wait_credit=0;
        }
        const double remaining=native_frame_wait.remaining(now(),requested);
        if(remaining<requested){
            const unsigned adjusted=static_cast<unsigned>(std::ceil(remaining*1000.));
            if(adjusted)native_delay_ms(adjusted);
            return;
        }
    }
    if(!draw_within_wait(milliseconds,true))native_delay_ms(milliseconds);
}
static bool install_game_delay(){
    auto base=(BYTE*)engine;
    BYTE* site=base+0x15926c6;auto target=base+0x597c0;
    const BYTE before[]={0x8b,0x45,0x08,0x50},after[]={0x83,0xc4,0x04,0x5d,0xc3};
    int32_t rel;memcpy(&rel,site+1,4);
    if(memcmp(site-4,before,4)||*site!=0xe8||site+5+rel!=target||memcmp(site+5,after,sizeof(after)))return false;
    native_delay_ms=(void(__cdecl*)(unsigned))target;
    DWORD protect,ignored;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protect))return false;
    uint32_t displacement=(uintptr_t)&draw_within_delay-(uintptr_t)(site+5);
    memcpy(site+1,&displacement,4);FlushInstructionCache(GetCurrentProcess(),site,5);
    VirtualProtect(site,5,protect,&ignored);return true;
}
#endif
static DWORD WINAPI wait_hook(HANDLE handle,DWORD timeout){
    if(GetCurrentThreadId()!=thread||!engine||!loaded||inside||!can_present||wglGetCurrentContext()!=context||
       handle!=*(HANDLE*)((BYTE*)engine+0x1c94680))return real_wait(handle,timeout);
    waits++;present_reason=8;qa_stage(8);double start=now();
    const double end=start+timeout*.001;
    DeckIdleBackup idle_backup;
    auto remaining_ms=[&]()->DWORD{return timeout==INFINITE?INFINITE:
        (DWORD)std::max(0.,std::ceil((end-now())*1000.));};
    if(output_timer){
        HANDLE handles[]={handle,output_timer};
        for(;;){double t=now();DWORD remaining=INFINITE;
            if(timeout!=INFINITE){
                remaining=remaining_ms();
                if(!presentation_clock.fits_wait(std::max(t,next_output),end))return real_wait(handle,remaining);
            }
            LARGE_INTEGER due;due.QuadPart=-(LONGLONG)std::max(1.,std::ceil((next_output-t)*10000000.));
            if(!SetWaitableTimer(output_timer,&due,0,nullptr,nullptr,FALSE))break;
            DWORD result=WaitForMultipleObjects(2,handles,FALSE,remaining);
            if(result==WAIT_OBJECT_0)return WAIT_OBJECT_0;
            if(result==WAIT_TIMEOUT||result==WAIT_FAILED)return result;
            if(result!=WAIT_OBJECT_0+1)return WAIT_FAILED;
            if(timeout!=INFINITE&&!presentation_clock.fits_wait(now(),end))return real_wait(handle,remaining_ms());
            try{present_during_wait();}catch(const std::exception&e){log(e.what());log("\n");can_present=false;
                return real_wait(handle,remaining_ms());}
            advance_output();
        }
    }
    for(;;){double t=now();DWORD slice=(DWORD)std::clamp((next_output-t)*1000.,1.,8.);
        if(timeout!=INFINITE){
            DWORD remaining=remaining_ms();
            if(!presentation_clock.fits_wait(std::max(t,next_output),end))return real_wait(handle,remaining);
            slice=std::min(slice,remaining);
        }
        DWORD result=real_wait(handle,slice);if(result!=WAIT_TIMEOUT)return result;
        if(timeout!=INFINITE&&!presentation_clock.fits_wait(now(),end))return real_wait(handle,remaining_ms());
        if(now()>=next_output){try{present_during_wait();}catch(const std::exception&e){log(e.what());log("\n");can_present=false;return real_wait(handle,remaining_ms());}
            advance_output();}
    }
}
// FFVIII's in-game Exit recreates its GL context while retaining the process.
// The driver has already destroyed the old objects when this is called. Drop
// their names without GL deletes: a new context may reuse exactly those names.
static void forget_deleted_context(){
    retro_panel_draw::forget();
    fps_counter::forget();
    atlas_detail_texture=0;atlas_allocation_attempted=false;native_atlas_view=unread_atlas_view;deck_world={};world_state::state={};world_state::tracking=false;
    retro_display::forget();
    deck_source_budget={};deck_native_layout_wait=false;world_capture_time=0;world_frame_stats={};
    world_original_frames=0;world_original_intervals=0;
    if(deck_renderer&&deck_cadence.event_count)deck_cadence.report();
    deck_cadence={};deck_black={};deck_output={};
    world_readback={};
    vertex_policy={};
    field_prefix={};vertex_stream=0;
    can_present=false;loaded=false;context=nullptr;dc=nullptr;thread=0;
    building={};previous={};current={};texture_cache.clear();recognized.clear();spare_textures={};
    replay_targets.clear();replay_depth.clear();replay_textures.clear();outgoing_draws.clear();checkpoint_outgoing.clear();
    test_target={};backup={};reference_target={};sample_a={};sample_b={};encounter_native={};
    for(auto&s:field_blur::levels)s={};field_blur::isolated_edge={};for(auto&s:infirmary::sheets)s={};
#ifdef GEO_AUTOTEST
    test_older={};previous_reference={};
#else
    qa_tiny={};
#endif
#ifdef GEO_NATIVE_CAPTURE
    AcquireSRWLockExclusive(&native_pose::lock);
    native_pose::pending.clear();native_pose::calls=0;native_pose::accepted=0;native_pose::overflow=false;
    ReleaseSRWLockExclusive(&native_pose::lock);
#endif
    camera_motion={};presentation_clock={};deck_wait_recovery={};prepared_previous=false;matching_checkpoint=nullptr;
    presentation_mode=-1;capture_target=0;test_capture_requested=false;
    origin=next_output=last_swap=previous_origin=0;period=previous_period=1./30.;
    width=height=0;interval_changed=false;set_interval=nullptr;deck_interval={};
    log("render context deleted; discarded old scene and GPU caches, ready to reinitialize\n");
}
static BOOL WINAPI delete_context_hook(HGLRC rc){
    const bool owned=loaded&&rc==context;
    const BOOL result=real_delete_context(rc);
    if(result&&owned)forget_deleted_context();
    return result;
}
static FARPROC WINAPI get_proc_address_hook(HMODULE module,LPCSTR name){
    FARPROC result=real_get_proc_address(module,name);
    if(result&&(uintptr_t)name>65535&&!strcmp(name,"XInputGetState")&&
       (module==GetModuleHandleW(L"xinput1_4.dll")||module==GetModuleHandleW(L"xinput1_3.dll")||
        module==GetModuleHandleW(L"xinput9_1_0.dll")||module==GetModuleHandleW(L"xinput1_2.dll")||
        module==GetModuleHandleW(L"xinput1_1.dll")))return (FARPROC)(uintptr_t)retro_panel::glfw_pad;
    // GLFW loads WGL dynamically on this build; other builds may import it.
    if(result&&(uintptr_t)name>65535&&module==GetModuleHandleA("opengl32.dll")&&
       !strcmp(name,"wglDeleteContext")){
        real_delete_context=(BOOL(WINAPI*)(HGLRC))result;
        log("observing dynamically loaded WGL context deletion\n");
        return (FARPROC)(uintptr_t)delete_context_hook;
    }
    return result;
}
static bool hook(HMODULE module,const char*dll,const char*function,void*replacement,void**original){
    BYTE*base=(BYTE*)module;auto dos=(IMAGE_DOS_HEADER*)base;demand(dos->e_magic==IMAGE_DOS_SIGNATURE,"invalid module");
    auto nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);demand(nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC,"expected32bit module");
    auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;desc->Name;desc++){if(_stricmp((char*)base+desc->Name,dll))continue;
        auto lookup=(IMAGE_THUNK_DATA32*)(base+desc->OriginalFirstThunk);auto iat=(IMAGE_THUNK_DATA32*)(base+desc->FirstThunk);
        for(;lookup->u1.AddressOfData;lookup++,iat++){if(IMAGE_SNAP_BY_ORDINAL32(lookup->u1.Ordinal))continue;
            auto name=(IMAGE_IMPORT_BY_NAME*)(base+lookup->u1.AddressOfData);if(strcmp((char*)name->Name,function))continue;
            DWORD old;if(!VirtualProtect(iat,sizeof(*iat),PAGE_READWRITE,&old))return false;
            *original=(void*)(uintptr_t)iat->u1.Function;InterlockedExchange((LONG*)&iat->u1.Function,(LONG)(uintptr_t)replacement);
            DWORD ignored;VirtualProtect(iat,sizeof(*iat),old,&ignored);return true;}}
    return false;
}
#include "steam_license.h"
static DWORD WINAPI initialize(void* module){
    char path[MAX_PATH];GetModuleFileNameA(NULL,path,MAX_PATH);char*leaf=strrchr(path,'\\');if(!leaf||_stricmp(leaf+1,"FFVIII.exe"))return 0;
#ifdef GEO_NATIVE_CAPTURE
    if(update_slots::load_newer(static_cast<HMODULE>(module),update_version::channel,update_version::build))return 0;
#endif
    for(int i=0;i<200&&!engine;i++){engine=GetModuleHandleA("FFVIII_EFIGS.dll");if(!engine)Sleep(50);}
    FILE*f=fopen("FF8_geometry.log","w");if(f){fputs("FFVIII 120 FPS - indexed geometry, verified native source frames; F8 opens live settings\n",f);fclose(f);}
#ifdef GEO_NATIVE_CAPTURE
    log("FF8 Reborn ");log(update_version::channel);log(" ");log(update_version::tag);log("\n");
#endif
    if(!engine)return 0;
    SteamLicense license=steam_license();
    for(int retry=0;license==SteamLicense::waiting&&retry<240;++retry){
        Sleep(250);license=steam_license();
    }
    if(license!=SteamLicense::owned){
        log("FF8 Reborn inactive: launch the Steam edition with the account that owns FINAL FANTASY VIII Remastered.\n");
        return 0;
    }
#ifdef GEO_NATIVE_CAPTURE
    mod_update::setup(static_cast<HMODULE>(module));
#endif
    wchar_t exePath[MAX_PATH]={};
    const DWORD length=GetModuleFileNameW(NULL,exePath,MAX_PATH);
    if(length>0&&length<MAX_PATH){
        retro_ini_path=exePath;
        retro_ini_path.resize(retro_ini_path.find_last_of(L"\\/")+1);
        retro_ini_path+=L"FF8-Horizonte.ini";
        display_settings.load(retro_ini_path.c_str());
        retro_panel::update_factory_resolution(retro_ini_path.c_str(),display_settings.retro);
    }
    char config[160];snprintf(config,sizeof(config),"Horizonte: FPS=%d Wide=%d Atlas=%d FieldSides=%d\n",display_settings.fps,display_settings.wide,display_settings.atlas,display_settings.field_sides);log(config);
    if(display_settings.retro.active())log(display_settings.retro.mode==2?"Retro CRT filter enabled\n":"Retro pixel filter enabled\n");

    diagnostic_trace=GetFileAttributesA("FF8_geometry_trace.enabled")!=INVALID_FILE_ATTRIBUTES;
    if(deck_renderer)
        deck_output_diagnostics=GetFileAttributesA("FF8_geometry_output_probe.enabled")!=INVALID_FILE_ATTRIBUTES;
    accessibility_test=GetFileAttributesA("FF8_geometry_test_input.enabled")!=INVALID_FILE_ATTRIBUTES;
    // An explicitly armed capture runs for this launch only.
    stride_trace::enabled=DeleteFileA("FF8_geometry_stride_test.enabled")!=FALSE;
    // Version guard before installing the import hooks and the guarded battle observer.
    const BYTE expected[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x2c};
    if(memcmp((BYTE*)engine+0x59560,expected,sizeof(expected))){log("unsupported engine timer signature; no hooks installed\n");return 0;}
#ifdef GEO_NATIVE_CAPTURE
    log(install_game_delay()?"native delay budget enabled\n":"native delay unchanged\n");
    log(native_pose::install()?"native battle projection observer enabled\n":"native projection call unrecognized; observer disabled\n");
    log(native_pose::install_foot()?"world character projection observer enabled\n":"world character projection unavailable\n");
    {
        log(native_pose::install_stage()?"battle stage direction-aware widescreen clip enabled\n":"battle stage call unrecognized\n");
        log(native_pose::install_world()?"world wide clipping enabled\n":"world wide clipping unavailable\n");
    }
#endif
    try{
        if(deck_renderer){
            using namespace world_state;
            observing=
                hook(engine,"OPENGL32.dll","glViewport",(void*)world_state::viewport,(void**)&viewport_original)&&
                hook(engine,"OPENGL32.dll","glScissor",(void*)world_state::scissor,(void**)&scissor_original)&&
                hook(engine,"OPENGL32.dll","glMatrixMode",(void*)world_state::mode,(void**)&mode_original)&&
                hook(engine,"OPENGL32.dll","glLoadMatrixf",(void*)world_state::load,(void**)&load_original)&&
                hook(engine,"OPENGL32.dll","glLoadIdentity",(void*)world_state::identity,(void**)&identity_original)&&
                hook(engine,"OPENGL32.dll","glOrtho",(void*)world_state::ortho,(void**)&ortho_original)&&
                hook(engine,"OPENGL32.dll","glPopMatrix",(void*)world_state::pop,(void**)&pop_original)&&
                hook(engine,"OPENGL32.dll","glPopAttrib",(void*)world_state::attrib,(void**)&attrib_original)&&
                hook(engine,"OPENGL32.dll","glEnable",(void*)world_state::enable,(void**)&enable_original)&&
                hook(engine,"OPENGL32.dll","glDisable",(void*)world_state::disable,(void**)&disable_original)&&
                hook(engine,"OPENGL32.dll","glEnableClientState",(void*)world_state::client_enable,(void**)&client_enable_original)&&
                hook(engine,"OPENGL32.dll","glVertexPointer",(void*)world_state::vertex,(void**)&vertex_original);
        }
        demand(hook(engine,"OPENGL32.dll","glDrawElements",(void*)draw_hook,(void**)&real_draw),"draw import missing");
        demand(hook(engine,"OPENGL32.dll","glClear",(void*)clear_hook,(void**)&real_clear),"clear import missing");
        demand(hook(engine,"OPENGL32.dll","glTexImage2D",(void*)tex_image_hook,(void**)&real_tex_image),"texture image import missing");
        demand(hook(engine,"OPENGL32.dll","glTexSubImage2D",(void*)tex_subimage_hook,(void**)&real_tex_subimage),"texture update import missing");
        demand(hook(engine,"OPENGL32.dll","glCopyTexImage2D",(void*)copy_tex_hook,(void**)&real_copy_tex),"texture copy import missing");
        demand(hook(engine,"OPENGL32.dll","glDeleteTextures",(void*)delete_textures_hook,(void**)&real_delete_textures),"texture delete import missing");
        hook(engine,"XINPUT9_1_0.dll","XInputGetState",(void*)retro_panel::game_pad,(void**)&retro_panel::game_read);
        hook(engine,"OPENGL32.dll","wglDeleteContext",(void*)delete_context_hook,(void**)&real_delete_context);
            demand(hook(engine,"KERNEL32.dll","GetProcAddress",(void*)get_proc_address_hook,(void**)&real_get_proc_address),"context lifecycle resolver missing");
        demand(hook(engine,"GDI32.dll","SwapBuffers",(void*)swap_hook,(void**)&real_swap),"swap import missing");
        demand(hook(engine,"KERNEL32.dll","WaitForSingleObject",(void*)wait_hook,(void**)&real_wait),"timer import missing");
        demand(hook(engine,"KERNEL32.dll","Sleep",(void*)sleep_hook,(void**)&real_sleep),"sleep import missing");
        demand(hook(engine,"KERNEL32.dll","QueryPerformanceCounter",(void*)counter_hook,(void**)&real_counter),"clock import missing");
#ifdef GEO_AUTOTEST
        demand(hook(engine,"USER32.dll","PeekMessageW",(void*)test_peek,(void**)&test_real_peek),"test message observation import missing");
#else
        // Optional local accessibility bridge for input with no scan code.
        // In normal play no input hook is installed and no captures are made.
        if(accessibility_test)demand(hook(engine,"USER32.dll","PeekMessageW",(void*)test_peek,(void**)&test_real_peek),"optional accessibility input unavailable");
#endif
        log("geometry hooks ready\n");
    }catch(const std::exception&e){enabled=false;log(e.what());log("\n");}return 0;
}
}
#ifndef GEO_TEST
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,geo::initialize,module,0,nullptr);
        if(!thread)return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
#endif
