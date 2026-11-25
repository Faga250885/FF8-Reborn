// Final display pass, in the game's compatibility context. Scene replays,
// validation and next-frame inputs always use the original colour surface.
#include "filter_resolution.h"
namespace retro_display {
struct FilterUniforms{
    GLfloat size[2],grid[2],strengths[4],tube[4],mask[3],output[3],sampling[2];
};
struct Pipeline {
    GLuint program=0;
    GLint size=-1, grid=-1, strengths=-1, tube=-1, screen=-1, response=-1,output=-1,sampling=-1;
    bool inspected=false,attempted=false, modern=false, srgb=false;
    PFNGLBINDSAMPLERPROC bind_sampler=nullptr;
    FilterUniforms last{};
    bool configured=false;
};
enum class Stage { full, cells, screen, tv_rows, tv_screen, motion, notice, title_fire };
static Pipeline pipeline,television_pipeline,cell_pipeline,screen_pipeline,tv_rows_pipeline,tv_screen_pipeline;
static Pipeline motion_pipeline,notice_pipeline;
static Pipeline title_fire_pipeline;
static Surface title_fire_surface;
#include "motion_blur.h"
static Surface signal_surface;
static int failed_signal_w=0,failed_signal_h=0;
static Pipeline& select_pipeline(bool tv,Stage stage){
    switch(stage){
        case Stage::title_fire:return title_fire_pipeline;
        case Stage::notice:return notice_pipeline;
        case Stage::motion:return motion_pipeline;
        case Stage::cells:return cell_pipeline;
        case Stage::screen:return screen_pipeline;
        case Stage::tv_rows:return tv_rows_pipeline;
        case Stage::tv_screen:return tv_screen_pipeline;
        default:return tv?television_pipeline:pipeline;
    }
}
#include "crt_phosphors.h"
static bool television(const RetroSettings& s){return s.tv_active()&&s.mode==2&&s.phosphors!=RetroSettings::standard;}
static Surface source,previous_front;
static bool valid_front=false,retiring_front=false;
static int front_window_w=0,front_window_h=0;
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
static bool prepare(bool tv=false,Stage stage=Stage::full){
    Pipeline& effect=select_pipeline(tv,stage);
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
    const char* notice_fragment="uniform vec2 size,origin;uniform sampler2D sourceImage;"
        "void main(){vec4 pixel=texture2D(sourceImage,(gl_FragCoord.xy-origin)/size);"
        "if(pixel.a==0.0)discard;gl_FragColor=pixel;}";
    const char* title_fire_fragment=R"GLSL(
uniform sampler2D sourceImage;
uniform vec2 size,origin,grid;
void main(){
    vec2 p=(gl_FragCoord.xy-origin)*vec2(640.0,480.0)/size;
    float y=480.0-p.y;
    float edge=min(p.x,640.0-p.x);
    // In the 1672x941 painting the centered native viewport starts at x=209.
    // Match the original at the join. Narrow screens crop the outer tips.
    float margin=max(origin.x*640.0/size.x,73.0);
    float x=edge<26.0 ? (edge+margin)*259.94375/(margin+26.0) : 209.0+edge*1.959375;
    float wing=grid.x*0.5;
    float u=clamp(x,0.5,wing-0.5)+(p.x<320.0?0.0:wing);
    float v=clamp(y*(941.0/480.0)-32.0,0.5,grid.y-1.5);
    vec3 fire=texture2D(sourceImage,vec2(u,v)/grid).rgb;
    vec3 black=texture2D(sourceImage,vec2(0.5,grid.y-0.5)/grid).rgb;
    vec3 white=texture2D(sourceImage,vec2(1.5,grid.y-0.5)/grid).rgb;
    gl_FragColor=vec4(mix(black,white,fire),1.0-smoothstep(32.0,48.0,edge));
}
)GLSL";
    const char* fragment=stage==Stage::title_fire?title_fire_fragment:stage==Stage::notice?notice_fragment:stage==Stage::motion?motion_fragment:tv?television_fragment:R"GLSL(
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
#ifdef CELL_PASS
    cell=gl_FragCoord.xy;
    center=cell/grid;
    vec2 sampleUV=center;
#elif defined(SCREEN_PASS)
    vec3 c=texture2D(sourceImage,center).rgb;
#else
    vec2 sampleUV=mix(uv,center,strengths.x);
#endif
#ifndef SCREEN_PASS
    vec2 footprint=strengths.x*max(1.0/grid-1.0/size,vec2(0.0));
    vec3 c;
    if(sourceSampling.y>0.5 && strengths.x==1.0){
        // A 4K signal cell covers many source pixels. Integrate nine
        // locations once per cell to catch thin highlights and dark edges.
        c=vec3(0.0);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
            c+=texture2D(sourceImage,sampleUV+vec2(float(x),float(y))*footprint/3.0).rgb;
        c/=9.0;
    }else{
        vec2 extent=sourceSampling.x*footprint;
        c=texture2D(sourceImage,sampleUV+extent).rgb;
        c+=texture2D(sourceImage,sampleUV-extent).rgb;
        c+=texture2D(sourceImage,sampleUV+vec2(extent.x,-extent.y)).rgb;
        c+=texture2D(sourceImage,sampleUV+vec2(-extent.x,extent.y)).rgb;
        c*=0.25;
    }
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
#endif
#ifdef CELL_PASS
    gl_FragColor=vec4(c,1.0);
#else
    float wave=0.5+0.5*tube.y*cos(6.2831853*fract(cell.y));
    c*=1.0-0.65*strengths.z*wave;
    if(tvSettings.x>0.0)c*=sqrt(max(vec3(0.0),1.0+0.455*tvSettings.y*phosphor(gl_FragCoord.xy,tvSettings.x)));
    else if(strengths.w>0.0)c*=1.0-0.35*strengths.w*(1.0-rgbMask(gl_FragCoord.x));
    gl_FragColor=vec4(c,1.0);
#endif
}
)GLSL";
    GLuint program=create_program(),shaders[2]={};
    bool linked=program!=0;
    for(int i=0;i<2&&linked;i++){
        shaders[i]=create_shader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
        if(!shaders[i]){linked=false;break;}
        const char* variant=stage==Stage::cells?"#define CELL_PASS\n":stage==Stage::screen?"#define SCREEN_PASS\n":
            stage==Stage::tv_rows?"#define TV_ROWS\n":stage==Stage::tv_screen?"#define TV_SCREEN\n":"";
        const char* parts[]={"#version 120\n",variant,mask_fragment,fragment};
        set_source(shaders[i],i?4:1,i?parts:&vertex,nullptr);compile(shaders[i]);
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
    effect.output=pGetUniformLocation(program,"outputGeometry");effect.sampling=pGetUniformLocation(program,"sourceSampling");
    if(stage==Stage::motion)effect.response=pGetUniformLocation(program,"response");
    if(stage==Stage::notice||stage==Stage::title_fire)effect.response=pGetUniformLocation(program,"origin");
    if(stage==Stage::tv_screen||stage==Stage::motion){
        GLint previous;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
        const GLint unit=1;
        const char* second=stage==Stage::motion?"historyImage":"rowImage";
        pUseProgram(program);pUniform1iv(pGetUniformLocation(program,second),1,&unit);pUseProgram(previous);
    }
    return true;
}

static void allocate(Surface& surface,int w,int h,GLenum format=GL_RGBA8){
    if(surface.w==w && surface.h==h)return;
    GLint unpack=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    pBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    try{resize(surface,w,h,false,true,format);}
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
static void fullscreen_triangle(){
    glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
}
static void draw_filter(const RetroSettings& settings,GLuint target,int w,int h){
    const GLfloat rows=GLfloat(std::min(h,settings.lines));
    const bool crt=settings.mode==2,tv=television(settings);
    // A tube preset supplies its own calibrated mask strength when using
    // the native model. An explicit model also works on ordinary filters.
    const bool mask_override=settings.tv_active()&&(!tv||settings.tv_screen!=RetroSettings::standard);
    const auto calibration=filter_resolution::calibrate(w,h);
    const GLfloat frequency=rows/h,phase=3.14159265f*frequency;
    // Fade unresolved scanlines before they can alias into wide moving bands.
    const GLfloat alias=std::clamp((frequency-.34f)/(.5f-.34f),0.f,1.f);
    const GLfloat beam=std::sin(phase)/phase*(1.f-alias*alias*(3.f-2.f*alias));
    const FilterUniforms params={
        {GLfloat(w),GLfloat(h)}, {std::max(1.f,std::round(rows*w/h)),rows},
        {settings.mode?settings.pixelation*.01f:0.f,settings.mode?settings.dither*.01f:0.f,
         crt?settings.scanlines*.01f:0.f,crt?settings.mask*.01f:0.f},
        {crt?settings.glow*.01f:0.f,beam,settings.mode?settings.edge_softness*.01f:0.f,
         tv?GLfloat(settings.phosphors):settings.ps1_raster?1.f:settings.hd_blend?-1.f:0.f},
        {mask_override?GLfloat(settings.selected_tube()):0.f,settings.phosphor_strength*.01f,settings.phosphor_sharpness*.01f},
        {calibration.rgb_pitch,calibration.rgb_contrast,calibration.tv_height},
        {calibration.sample_spread,calibration.dense_signal?1.f:0.f}
    };
    auto draw=[&](Stage stage){
        Pipeline& effect=select_pipeline(tv,stage);
        pUseProgram(effect.program);
        // The mod owns these programs. A resize or live edit changes the key.
        if(!effect.configured||memcmp(&effect.last,&params,sizeof(params))){
            pUniform3fv(effect.screen,1,params.mask);
            pUniform2fv(effect.size,1,params.size);pUniform2fv(effect.grid,1,params.grid);
            pUniform4fv(effect.strengths,1,params.strengths);pUniform4fv(effect.tube,1,params.tube);
            pUniform3fv(effect.output,1,params.output);pUniform2fv(effect.sampling,1,params.sampling);
            effect.last=params;effect.configured=true;
        }
        fullscreen_triangle();
    };
    // At full pixelation the colour signal is constant inside each virtual
    // cell. TV sets instead share one horizontal beam sample per native x and
    // virtual scanline. Keep the mask and beam phase at the drawable size.
    const bool cells=settings.mode&&settings.pixelation==100&&!tv;
    const int signal_w=tv?w:int(params.grid[0]),signal_h=tv?int(rows)+2:int(rows);
    bool split=(tv||cells)&&signal_h*2<=h&&
               (failed_signal_w!=signal_w||failed_signal_h!=signal_h)&&
               prepare(tv,tv?Stage::tv_rows:Stage::cells)&&prepare(tv,tv?Stage::tv_screen:Stage::screen);
    if(split){
        try{allocate(signal_surface,signal_w,signal_h,GL_RGBA16F);}
        catch(const std::exception&){
            failed_signal_w=signal_w;failed_signal_h=signal_h;split=false;
            log("Retro signal texture unavailable, using single-pass filter\n");
        }
    }
    if(split){
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,signal_surface.fbo);
        glViewport(0,0,signal_w,signal_h);draw(tv?Stage::tv_rows:Stage::cells);
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);glViewport(0,0,w,h);
        GLint unit1_texture=0,unit1_sampler=0;
        if(tv){
            pActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&unit1_texture);
            if(pipeline.bind_sampler){glGetIntegerv(GL_SAMPLER_BINDING,&unit1_sampler);pipeline.bind_sampler(1,0);}
        }
        glBindTexture(GL_TEXTURE_2D,signal_surface.color);draw(tv?Stage::tv_screen:Stage::screen);
        if(tv){
            glBindTexture(GL_TEXTURE_2D,unit1_texture);
            if(pipeline.bind_sampler)pipeline.bind_sampler(1,unit1_sampler);
            pActiveTexture(GL_TEXTURE0);
        }
    }else draw(Stage::full);
}
static void fullscreen_draw_state(int x,int y,int w,int h){
    for(GLenum flag:{GL_BLEND,GL_COLOR_LOGIC_OP,GL_ALPHA_TEST,GL_DEPTH_TEST,GL_STENCIL_TEST,
        GL_CULL_FACE,GL_POLYGON_STIPPLE,GL_POLYGON_SMOOTH,GL_DITHER,
        GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_ALPHA_TO_ONE,GL_SAMPLE_COVERAGE})
        glDisable(flag);
    for(int i=0;i<6;i++)glDisable(GL_CLIP_PLANE0+i);
    if(pipeline.modern)glDisable(GL_RASTERIZER_DISCARD);
    glViewport(x,y,w,h);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
}
static bool apply(const RetroSettings& settings,GLuint target,int w,int h){
    if(!settings.filter_enabled())return false;
    const bool filter=settings.active()&&prepare(television(settings));
    auto& history=motion_history;
    const bool motion=history.strength&&prepare(false,Stage::motion);
    if((!filter&&!motion)||w<=0||h<=0||w>8192||h>8192||
       (failed_width==w&&failed_height==h))return false;
    try{allocate(source,w,h);}
    catch(const std::exception&){
        failed_width=w;failed_height=h;log("Retro filter unavailable at this drawable size\n");return false;
    }
    TargetState state(target,true);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,source.fbo);copy_color(w,h);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);
    history.pending=motion;
    const auto& prior=exchanges_front?previous_front:history.raw;
    bool blend=motion&&history.exposure>0&&prior.w==w&&prior.h==h&&prior.color;
    if(blend&&filter){
        if(history.failed_signal_w==w&&history.failed_signal_h==h)blend=false;
        else try{allocate(history.signal,w,h);}
        catch(const std::exception&){
            history.failed_signal_w=w;history.failed_signal_h=h;blend=false;
            log("Motion blur buffer unavailable at this size; using the selected filter\n");
        }
    }
    if(!filter&&!blend)return true; // First frame seeds history without changing the picture.
    GLint program,active,texture,sampler=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    pActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    if(pipeline.bind_sampler){
        glGetIntegerv(GL_SAMPLER_BINDING,&sampler);pipeline.bind_sampler(0,0);
    }
    fullscreen_draw_state(0,0,w,h);
    glBindTexture(GL_TEXTURE_2D,source.color);
    if(blend){
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,filter?history.signal.fbo:target);
        GLint last_texture,last_sampler=0;
        pActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&last_texture);
        if(pipeline.bind_sampler){
            glGetIntegerv(GL_SAMPLER_BINDING,&last_sampler);pipeline.bind_sampler(1,0);
        }
        glBindTexture(GL_TEXTURE_2D,prior.color);
        pUseProgram(motion_pipeline.program);
        const GLfloat extent[]={float(w),float(h)},response[]={history.exposure,.01f*history.strength};
        pUniform2fv(motion_pipeline.size,1,extent);
        pUniform2fv(motion_pipeline.response,1,response);
        fullscreen_triangle();
        glBindTexture(GL_TEXTURE_2D,last_texture);
        if(pipeline.bind_sampler)pipeline.bind_sampler(1,last_sampler);
        pActiveTexture(GL_TEXTURE0);
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);
        if(filter)glBindTexture(GL_TEXTURE_2D,history.signal.color);
    }
    if(filter)draw_filter(settings,target,w,h);
    glBindTexture(GL_TEXTURE_2D,texture);
    if(pipeline.bind_sampler)pipeline.bind_sampler(0,sampler);
    pActiveTexture(active);pUseProgram(program);
    return true;
}
static void restore_from(const Surface& surface,GLuint target,int w,int h){
    TargetState state(target,false);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,surface.fbo);
    const int cw=std::min(w,surface.w),ch=std::min(h,surface.h);
    pBlitFramebuffer(0,0,cw,ch,0,h-ch,cw,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
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
                   front_window_w==w&&front_window_h==h;
    if(!retiring_front)valid_front=false;
    return retiring_front;
}
static bool begin_window(const RetroSettings& settings,HDC handle,int w,int h,bool overlay=false,int overlay_w=0,int overlay_h=0,bool outer_restores=false){
    retiring_front=false;
    if(!settings.filter_enabled())motion_history.reset();
    if((!settings.active()&&!motion_history.strength&&!overlay)||w<=0||h<=0||w>8192||h>8192||
       (failed_width==w&&failed_height==h))return retire_front(handle,w,h);
    inspect_context();
    const bool filter=(settings.active()&&prepare(television(settings)))||
        (motion_history.strength&&prepare(false,Stage::motion));
    if(!filter&&!overlay)return retire_front(handle,w,h);
    inspect_swap_method(handle);
    const int cw=!filter&&overlay_w>0?std::min(w,overlay_w):w;
    const int ch=!filter&&overlay_h>0?std::min(h,overlay_h):h;
    auto save_corner=[&](const Surface& dst){
        pBindFramebuffer(GL_DRAW_FRAMEBUFFER,dst.fbo);
        pBlitFramebuffer(0,h-ch,cw,h,0,0,cw,ch,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    };
    if(exchanges_front && (!valid_front||front_window_w!=w||front_window_h!=h)){
        try{allocate(previous_front,cw,ch);}
        catch(const std::exception&){failed_width=w;failed_height=h;valid_front=false;return false;}
        TargetState state(0,false);glReadBuffer(GL_FRONT);
        save_corner(previous_front);
        valid_front=true;front_window_w=w;front_window_h=h;
    }
    if(filter&&apply(settings,0,w,h))return true;
    if(overlay){
        if(outer_restores&&!exchanges_front)return true;
        // Opening the editor with the original image still requires an
        // unmodified copy for engines which draw the next frame incrementally.
        try{allocate(source,cw,ch);}
        catch(const std::exception&){failed_width=w;failed_height=h;valid_front=false;return false;}
        TargetState state(0,false);
        save_corner(source);
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
    }else{
        if(!outer_restores)restore(0,w,h);
        if(swapped&&motion_history.pending)std::swap(source,motion_history.raw);
    }
    motion_history.commit(swapped);
}
static void forget(){
    title_fire_pipeline={};title_fire_surface={};
    motion_pipeline={};notice_pipeline={};motion_history={};
    pipeline={};television_pipeline={};cell_pipeline={};screen_pipeline={};tv_rows_pipeline={};tv_screen_pipeline={};signal_surface={};source={};previous_front={};failed_width=failed_height=0;
    failed_signal_w=failed_signal_h=0;valid_front=retiring_front=false;swap_dc=nullptr;exchanges_front=false;
}
}
