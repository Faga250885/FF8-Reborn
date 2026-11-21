// Field backgrounds mask actors with the stencil buffer. A test is per draw,
// but the write mask also applies to a stencil clear while testing is off.
struct RasterExtra {
    bool stencil=false,lighting=false,fog=false,mask_known=false;
    GLint face[2][7]={};
    GLint fog_mode=GL_EXP,fog_source=GL_FRAGMENT_DEPTH;
    GLfloat fog_params[3]={1,0,1},fog_color[4]={};
};
static void read_extra(RasterExtra& e,bool stencil_clear){
    e.stencil=glIsEnabled(GL_STENCIL_TEST);e.fog=glIsEnabled(GL_FOG);e.lighting=glIsEnabled(GL_LIGHTING);
    e.mask_known=e.stencil||stencil_clear;
    const GLenum keys[2][7]={{GL_STENCIL_FUNC,GL_STENCIL_REF,GL_STENCIL_VALUE_MASK,GL_STENCIL_FAIL,GL_STENCIL_PASS_DEPTH_FAIL,GL_STENCIL_PASS_DEPTH_PASS,GL_STENCIL_WRITEMASK},
      {GL_STENCIL_BACK_FUNC,GL_STENCIL_BACK_REF,GL_STENCIL_BACK_VALUE_MASK,GL_STENCIL_BACK_FAIL,GL_STENCIL_BACK_PASS_DEPTH_FAIL,GL_STENCIL_BACK_PASS_DEPTH_PASS,GL_STENCIL_BACK_WRITEMASK}};
    if(e.mask_known)for(int j=0;j<2;j++)for(int k=e.stencil?0:6;k<7;k++)glGetIntegerv(keys[j][k],&e.face[j][k]);
    if(e.fog){glGetIntegerv(GL_FOG_MODE,&e.fog_mode);glGetIntegerv(GL_FOG_COORDINATE_SOURCE,&e.fog_source);
        glGetFloatv(GL_FOG_DENSITY,e.fog_params);glGetFloatv(GL_FOG_START,e.fog_params+1);glGetFloatv(GL_FOG_END,e.fog_params+2);glGetFloatv(GL_FOG_COLOR,e.fog_color);}
}
static void apply_extra(const RasterExtra&e){
    cap(GL_STENCIL_TEST,e.stencil);cap(GL_FOG,e.fog);cap(GL_LIGHTING,e.lighting);
    if(e.mask_known)for(int j=0;j<2;j++){GLenum side=j?GL_BACK:GL_FRONT;auto*p=e.face[j];
        pStencilMaskSeparate(side,(GLuint)p[6]);if(e.stencil){pStencilFuncSeparate(side,p[0],p[1],(GLuint)p[2]);pStencilOpSeparate(side,p[3],p[4],p[5]);}}
    if(e.fog){glFogi(GL_FOG_MODE,e.fog_mode);glFogi(GL_FOG_COORDINATE_SOURCE,e.fog_source);glFogf(GL_FOG_DENSITY,e.fog_params[0]);glFogf(GL_FOG_START,e.fog_params[1]);glFogf(GL_FOG_END,e.fog_params[2]);glFogfv(GL_FOG_COLOR,e.fog_color);}
}
