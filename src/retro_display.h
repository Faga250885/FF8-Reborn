// Final display pass, in the game's compatibility context. Scene replays,
// validation and next-frame inputs always use the original colour surface.
namespace retro_display {
struct Pipeline {
    GLuint program=0;
    GLint size=-1, grid=-1, strengths=-1, tube=-1, screen=-1;
    bool inspected=false,attempted=false, modern=false, srgb=false;
    PFNGLBINDSAMPLERPROC bind_sampler=nullptr;
};
static Pipeline pipeline,television_pipeline;
#include "crt_phosphors.h"
static bool television(const RetroSettings& s){return s.mode==2&&s.phosphors!=RetroSettings::standard;}
static bool active(const RetroSettings& s){return s.active()||(enabled&&s.tv_screen&&s.phosphor_strength);}
static Surface source,previous_front;
static bool valid_front=false,retiring_front=false;
static HDC swap_dc=nullptr;
static bool exchanges_front=false;
static int failed_width=0,failed_height=0;

template<class T> static T procedure(const char* name){
    const PROC p=wglGetProcAddress(name);
    return uintptr_t(p)>3 && uintptr_t(p)!=UINTPTR_MAX ? reinterpret_cast<T>(p) : nullptr;
}
static bool extension(const char* extensions,const char* wanted){
    if(!extensions)return false;
    const size_t len=strlen(wanted);
    for(const char* s=extensions;(s=strstr(s,wanted))!=nullptr;s+=len)
        if((s==extensions||s[-1]==' ') && (s[len]==' '||s[len]==0))return true;
    return false;
}
static void inspect_context(){
    if(pipeline.inspected)return;
    pipeline.inspected=true;
    int major=0,minor=0;
    const auto version=(const char*)glGetString(GL_VERSION);
    if(version)sscanf(version,"%d.%d",&major,&minor);
    const auto extensions=(const char*)glGetString(GL_EXTENSIONS);
    pipeline.modern=major>=3;
    pipeline.srgb=pipeline.modern||extension(extensions,"GL_EXT_framebuffer_sRGB")||extension(extensions,"GL_ARB_framebuffer_sRGB");
    if(major>3||(major==3&&minor>=3)||extension(extensions,"GL_ARB_sampler_objects"))
        pipeline.bind_sampler=procedure<PFNGLBINDSAMPLERPROC>("glBindSampler");
}
static bool prepare(bool tv=false){
    Pipeline& effect=tv?television_pipeline:pipeline;
    if(effect.attempted)return effect.program!=0;
    effect.attempted=true;
    auto create_shader=procedure<PFNGLCREATESHADERPROC>("glCreateShader");
    auto set_source=procedure<PFNGLSHADERSOURCEPROC>("glShaderSource");
    auto compile=procedure<PFNGLCOMPILESHADERPROC>("glCompileShader");
    auto shader_iv=procedure<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    auto delete_shader=procedure<PFNGLDELETESHADERPROC>("glDeleteShader");
    auto create_program=procedure<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    auto attach=procedure<PFNGLATTACHSHADERPROC>("glAttachShader");
    auto link=procedure<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    auto delete_program=procedure<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    if(!create_shader||!set_source||!compile||!shader_iv||!delete_shader||
       !create_program||!attach||!link||!delete_program){
        log("Retro filter unavailable: shader functions missing\n");return false;
    }
    inspect_context();
    const char* vertex="#version 120\nvoid main(){gl_Position=gl_Vertex;}\n";
    const char* fragment=tv?television_fragment:R"GLSL(
uniform sampler2D sourceImage;
uniform vec2 size;
uniform vec2 grid;
uniform vec4 strengths; // pixelation, dithering, scanlines, mask
uniform vec4 tube;      // glow, integrated scanline amplitude, edge softness, raster style (-1 HD, 0 regular, 1 PS1)
float bayer2(vec2 p) {
    p=mod(p,2.0);
    return 2.0*mod(p.x+p.y,2.0)+p.y;
}
void main(){
    vec2 uv=gl_FragCoord.xy/size;
    vec2 cell=uv*grid;
    vec2 center=(floor(cell)+0.5)/grid;
    vec2 sampleUV=mix(uv,center,strengths.x);
    // A compact box in each virtual pixel retains thin original details.
    vec2 extent=0.23*strengths.x*max(1.0/grid-1.0/size,vec2(0.0));
    vec3 c=texture2D(sourceImage,sampleUV+extent).rgb;
    c+=texture2D(sourceImage,sampleUV-extent).rgb;
    c+=texture2D(sourceImage,sampleUV+vec2(extent.x,-extent.y)).rgb;
    c+=texture2D(sourceImage,sampleUV+vec2(-extent.x,extent.y)).rgb;
    c*=0.25;
    vec3 neighborhood=c;
    if(tube.x>0.0 || tube.z>0.0){
        vec2 spread=(tube.w<0.0?2.1:1.25)/grid;
        vec3 left=texture2D(sourceImage,sampleUV-vec2(spread.x,0)).rgb;
        vec3 right=texture2D(sourceImage,sampleUV+vec2(spread.x,0)).rgb;
        vec3 down=texture2D(sourceImage,sampleUV-vec2(0,spread.y)).rgb;
        vec3 up=texture2D(sourceImage,sampleUV+vec2(0,spread.y)).rgb;
        neighborhood=0.25*(left+right+down+up);
        if(tube.z>0.0 && tube.w<0.0){
            // Compare two scales. A smooth painted gradient has almost no local residual,
            // while a hard silhouette has a residual even within the smaller footprint.
            vec2 nearStep=0.72/grid;
            vec3 local=0.25*(texture2D(sourceImage,sampleUV+vec2(nearStep.x,0)).rgb
                            +texture2D(sourceImage,sampleUV-vec2(nearStep.x,0)).rgb
                            +texture2D(sourceImage,sampleUV+vec2(0,nearStep.y)).rgb
                            +texture2D(sourceImage,sampleUV-vec2(0,nearStep.y)).rgb);
            vec3 fine=abs(local-c);
            float amount=tube.z*smoothstep(0.008,0.075,max(fine.r,max(fine.g,fine.b)));
            vec3 shift=mix(local,neighborhood,0.46)-c;
            float light=dot(shift,vec3(0.2126,0.7152,0.0722));
            // Keep dark strokes and small highlights legible while easing hard color boundaries.
            float boundedLight=light/(1.0+abs(light)/0.20);
            c=clamp(c+amount*(boundedLight+clamp(shift-light,-0.22,0.22)),0.0,1.0);
        }else if(tube.z>0.0){
            vec3 delta=max(max(abs(left-c),abs(right-c)),max(abs(up-c),abs(down-c)));
            float contrast=max(delta.r,max(delta.g,delta.b));
            c=mix(c,neighborhood,0.82*tube.z*smoothstep(0.06,0.28,contrast));
        }
    }
    if(strengths.y>0.0){
        vec2 p=floor(cell);
        vec3 quantized;
        if(tube.w>0.5){
            // PS1 GPU offset matrix, indexed from the top-left of VRAM.
            vec2 q=mod(vec2(p.x,grid.y-1.0-p.y),4.0);
            float offset=-4.0+4.0*mod(q.x,2.0)+6.0*mod(q.y,2.0)
                         -8.0*mod(q.x,2.0)*mod(q.y,2.0)+mod(floor(q.x/2.0)+floor(q.y/2.0),2.0);
            quantized=floor(clamp(floor(c*255.0+0.5)+offset,0.0,255.0)/8.0)/31.0;
        }else{
            float threshold=(4.0*bayer2(p)+bayer2(floor(p/2.0))+0.5)/16.0;
            quantized=floor(c*31.0+threshold)/31.0;
        }
        c=mix(c,quantized,strengths.y);
    }
    if(tube.x>0.0)c+=(1.0-c)*max(neighborhood-0.20,vec3(0))*0.45*tube.x;
    float wave=0.5+0.5*tube.y*cos(6.2831853*fract(cell.y));
    c*=1.0-0.65*strengths.z*wave;
    float column=mod(floor(gl_FragCoord.x),3.0);
    vec3 legacyMask=vec3(column==0.0,column==1.0,column==2.0);
    if(tvSettings.x>0.0)c*=sqrt(max(vec3(0.0),1.0+0.455*tvSettings.y*phosphor(gl_FragCoord.xy,size.y,tvSettings.x)));
    else c*=1.0-0.35*strengths.w*(1.0-legacyMask);
    gl_FragColor=vec4(c,1.0);
}
)GLSL";
    GLuint program=create_program(),shaders[2]={};
    bool linked=program!=0;
    for(int i=0;i<2&&linked;i++){
        shaders[i]=create_shader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
        if(!shaders[i]){linked=false;break;}
        const char* parts[]={"#version 120\n",mask_fragment,fragment};
        set_source(shaders[i],i?3:1,i?parts:&vertex,nullptr);compile(shaders[i]);
        GLint ok=0;shader_iv(shaders[i],GL_COMPILE_STATUS,&ok);
        if(ok)attach(program,shaders[i]);else linked=false;
    }
    if(linked){link(program);GLint ok=0;pGetProgramiv(program,GL_LINK_STATUS,&ok);linked=ok!=0;}
    for(const GLuint sh:shaders)if(sh)delete_shader(sh);
    if(!linked){
        if(program)delete_program(program);
        log("Retro filter unavailable: GPU rejected shader\n");return false;
    }
    // A new program's sampler uniforms default to unit zero.
    effect.size=pGetUniformLocation(program,"size");effect.grid=pGetUniformLocation(program,"grid");
    effect.strengths=pGetUniformLocation(program,"strengths");effect.tube=pGetUniformLocation(program,"tube");
    effect.screen=pGetUniformLocation(program,"tvSettings");effect.program=program;
    return true;
}

static void allocate(Surface& surface,int w,int h){
    if(surface.w==w && surface.h==h)return;
    GLint unpack=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    pBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    try{resize(surface,w,h,false,true);}
    catch(...){pBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);throw;}
    pBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
    // A failed texture reallocation may leave the previous, still complete,
    // FBO attached. Never sample a cropped or stale frame in that case.
    GLint texture,actual_w,actual_h;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glBindTexture(GL_TEXTURE_2D,surface.color);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&actual_w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&actual_h);
    glBindTexture(GL_TEXTURE_2D,texture);
    if(actual_w!=w||actual_h!=h){surface.w=surface.h=0;throw std::runtime_error("retro texture size unavailable");}
}

// Buffer selectors belong to the target framebuffer. Push/pop them with
// that target current even when the engine originally had another FBO bound.
struct TargetState{
    GLuint target;
    GLint read_fbo,draw_fbo,read_buffer;
    explicit TargetState(GLuint framebuffer,bool drawing):target(framebuffer){
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fbo);
        pBindFramebuffer(GL_FRAMEBUFFER,target);
        glGetIntegerv(GL_READ_BUFFER,&read_buffer);
        GLbitfield mask=GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT;
        if(drawing)mask|=GL_POLYGON_BIT|GL_VIEWPORT_BIT;
        glPushAttrib(mask);
        glDisable(GL_SCISSOR_TEST);
        if(pipeline.srgb)glDisable(GL_FRAMEBUFFER_SRGB);
        const GLenum buffer=target?GL_COLOR_ATTACHMENT0:GL_BACK;
        glReadBuffer(buffer);glDrawBuffer(buffer);
    }
    ~TargetState(){
        pBindFramebuffer(GL_FRAMEBUFFER,target);
        glReadBuffer(read_buffer);glPopAttrib();
        pBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);pBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_fbo);
    }
};

static void copy_color(int w,int h){
    pBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
}
static bool apply(const RetroSettings& settings,GLuint target,int w,int h){
    if(!active(settings)||w<=0||h<=0||w>8192||h>8192||
       (failed_width==w&&failed_height==h)||!prepare(television(settings)))return false;
    try{allocate(source,w,h);}
    catch(const std::exception&){
        failed_width=w;failed_height=h;log("Retro filter unavailable at this drawable size\n");return false;
    }
    TargetState state(target,true);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,source.fbo);copy_color(w,h);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);
    GLint program,active,texture,sampler=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    pActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    if(pipeline.bind_sampler){
        glGetIntegerv(GL_SAMPLER_BINDING,&sampler);pipeline.bind_sampler(0,0);
    }
    for(GLenum flag:{GL_BLEND,GL_COLOR_LOGIC_OP,GL_ALPHA_TEST,GL_DEPTH_TEST,GL_STENCIL_TEST,
        GL_CULL_FACE,GL_POLYGON_STIPPLE,GL_POLYGON_SMOOTH,GL_DITHER,
        GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_ALPHA_TO_ONE,GL_SAMPLE_COVERAGE})
        glDisable(flag);
    for(int i=0;i<6;i++)glDisable(GL_CLIP_PLANE0+i);
    if(pipeline.modern)glDisable(GL_RASTERIZER_DISCARD);
    glViewport(0,0,w,h);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    const Pipeline& effect=television(settings)?television_pipeline:pipeline;
    pUseProgram(effect.program);glBindTexture(GL_TEXTURE_2D,source.color);
    const GLfloat rows=GLfloat(std::min(h,settings.lines));
    const GLfloat size[]={GLfloat(w),GLfloat(h)},grid[]={std::max(1.f,std::round(rows*w/h)),rows};
    const bool crt=settings.mode==2;
    const GLfloat strengths[]={settings.mode?settings.pixelation*.01f:0.f,settings.mode?settings.dither*.01f:0.f,
                               crt?settings.scanlines*.01f:0.f,crt?settings.mask*.01f:0.f};
    const GLfloat phase=3.14159265f*rows/h,tube[]={crt?settings.glow*.01f:0.f,std::sin(phase)/phase,settings.mode?settings.edge_softness*.01f:0.f,
        television(settings)?GLfloat(settings.phosphors):settings.ps1_raster?1.f:settings.hd_blend?-1.f:0.f};
    const GLfloat maskSettings[]={enabled?GLfloat(settings.tv_screen):0.f,settings.phosphor_strength*.01f,settings.phosphor_sharpness*.01f};
    pUniform3fv(effect.screen,1,maskSettings);
    pUniform2fv(effect.size,1,size);pUniform2fv(effect.grid,1,grid);
    pUniform4fv(effect.strengths,1,strengths);pUniform4fv(effect.tube,1,tube);
    glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
    glBindTexture(GL_TEXTURE_2D,texture);
    if(pipeline.bind_sampler)pipeline.bind_sampler(0,sampler);
    pActiveTexture(active);pUseProgram(program);
    return true;
}
static void restore_from(const Surface& surface,GLuint target,int w,int h){
    TargetState state(target,false);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,surface.fbo);copy_color(w,h);
}
static void restore(GLuint target,int w,int h){restore_from(source,target,w,h);}
static void inspect_swap_method(HDC handle){
    if(handle==swap_dc)return;
    swap_dc=handle;valid_front=false;
    PIXELFORMATDESCRIPTOR format{};
    const int index=GetPixelFormat(handle);
    exchanges_front=DescribePixelFormat(handle,index,sizeof(format),&format) && (format.dwFlags&PFD_SWAP_EXCHANGE);
    using GetFormat=BOOL (WINAPI*)(HDC,int,int,UINT,const int*,int*);
    if(auto get=procedure<GetFormat>("wglGetPixelFormatAttribivARB")){
        const int attribute=0x2007; // WGL_SWAP_METHOD_ARB
        int method=0;
        if(get(handle,index,0,1,&attribute,&method))exchanges_front=method==0x2028; // WGL_SWAP_EXCHANGE_ARB
    }
}
// Drivers using page exchange expose the previous front page as the new
// back page. Keep its unfiltered counterpart until that exchange takes place.
static bool retire_front(HDC handle,int w,int h){
    // The first swap after closing an unfiltered panel (or bypassing the
    // effect) still exchanges the old decorated front into the game's back.
    retiring_front=valid_front&&exchanges_front&&handle==swap_dc&&
                   previous_front.w==w&&previous_front.h==h;
    if(!retiring_front)valid_front=false;
    return retiring_front;
}
static bool begin_window(const RetroSettings& settings,HDC handle,int w,int h,bool overlay=false){
    retiring_front=false;
    if((!active(settings)&&!overlay)||w<=0||h<=0||w>8192||h>8192||
       (failed_width==w&&failed_height==h))return retire_front(handle,w,h);
    inspect_context();
    const bool filter=active(settings)&&prepare(television(settings));
    if(!filter&&!overlay)return retire_front(handle,w,h);
    inspect_swap_method(handle);
    if(exchanges_front && (!valid_front||previous_front.w!=w||previous_front.h!=h)){
        try{allocate(previous_front,w,h);}
        catch(const std::exception&){failed_width=w;failed_height=h;valid_front=false;return false;}
        TargetState state(0,false);glReadBuffer(GL_FRONT);
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,previous_front.fbo);copy_color(w,h);
        valid_front=true;
    }
    if(filter&&apply(settings,0,w,h))return true;
    if(overlay){
        // Opening the editor with the original image still requires an
        // unmodified copy for engines which draw the next frame incrementally.
        try{allocate(source,w,h);}
        catch(const std::exception&){failed_width=w;failed_height=h;valid_front=false;return false;}
        TargetState state(0,false);
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,source.fbo);copy_color(w,h);
        return true;
    }
    valid_front=false;return false;
}
static void end_window(bool swapped,bool outer_restores,int w,int h){
    if(retiring_front){
        if(swapped&&!outer_restores)restore_from(previous_front,0,w,h);
        if(swapped)valid_front=false;
        retiring_front=false;
        return;
    }
    if(swapped&&exchanges_front){
        if(!outer_restores)restore_from(previous_front,0,w,h);
        std::swap(source,previous_front);valid_front=true;
    }else if(!outer_restores)restore(0,w,h);
}
static void forget(){
    pipeline={};television_pipeline={};source={};previous_front={};failed_width=failed_height=0;
    valid_front=retiring_front=false;swap_dc=nullptr;exchanges_front=false;
}
}
