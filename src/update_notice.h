// Durations start on the first drawable presentation after the check completes.
namespace update_notice {
struct Lifetime {
    double began=-1;
    float opacity(bool available,double time){
        if(!available)return 0;
        if(began<0)began=time;
        return float(std::clamp((began+12.-time)/2.,0.,1.));
    }
};
static Lifetime lifetime;
static Surface tile;
struct Placement { int x,y,w,h; };
static Placement place(int w,int h,int counter_height){
    const double scale=std::min({std::clamp(h/850.,.85,2.5),w/420.,(h-counter_height)/62.});
    const int margin=int(std::lround(8*scale)),tw=int(std::lround(404*scale)),th=int(std::lround(46*scale));
    return {margin,h-counter_height-margin-th,tw,th};
}
static bool draw(GLuint target,Placement p,float alpha){
    if(alpha<=0||p.w<=0||p.h<=0)return false;
    if(tile.w!=p.w||tile.h!=p.h){
        retro_panel_draw::Canvas c(p.w,p.h);
        if(!c.pixels)return false;
        c.box(0,0,404,46,RGB(23,31,35));c.box(0,0,4,46,RGB(125,226,200));
        c.label(L"FF8 Reborn update available",13,0,400,26,1,RGB(238,243,243));
        c.label(L"Open settings with F8 or View + Menu to download.",13,24,400,45,0,RGB(181,201,199));
        GdiFlush();
        for(size_t at=3;at<size_t(p.w)*p.h*4;at+=4)c.pixels[at]=255;
        try{retro_display::allocate(tile,p.w,p.h);}catch(const std::exception&){return false;}
        PixelTransferGuard transfer;
        GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glBindTexture(GL_TEXTURE_2D,tile.color);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,p.w,p.h,GL_BGRA,GL_UNSIGNED_BYTE,c.pixels);
        glBindTexture(GL_TEXTURE_2D,texture);
    }
    retro_display::inspect_context();
    retro_display::TargetState state(target,true);
    if(!retro_display::prepare(false,retro_display::Stage::notice))return false;
    GLint program,unit,texture,sampler=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);
    pActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    auto bind_sampler=retro_display::pipeline.bind_sampler;
    if(bind_sampler){glGetIntegerv(GL_SAMPLER_BINDING,&sampler);bind_sampler(0,0);}
    retro_display::fullscreen_draw_state(p.x,p.y,p.w,p.h);
    // glBlitFramebuffer ignores channel masks. Draw RGB for the opaque
    // hold as well as the fade so the game's alpha survives both phases.
    if(alpha<1){
        glEnable(GL_BLEND);pBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        pBlendColor(0,0,0,alpha);pBlendFuncSeparate(GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_ZERO,GL_ONE);
    }
    const auto& shader=retro_display::notice_pipeline;
    pUseProgram(shader.program);const GLfloat origin[]={float(p.x),float(p.y)},size[]={float(p.w),float(p.h)};
    pUniform2fv(shader.response,1,origin);pUniform2fv(shader.size,1,size);
    glBindTexture(GL_TEXTURE_2D,tile.color);retro_display::fullscreen_triangle();
    glBindTexture(GL_TEXTURE_2D,texture);if(bind_sampler)bind_sampler(0,sampler);
    pActiveTexture(unit);pUseProgram(program);
    return true;
}
}
