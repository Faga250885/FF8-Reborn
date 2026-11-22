// Original-FPS world draws share the engine's camera for long runs. Observe
// its compatibility state setters so getters need not drain the driver queue
// on every triangle. Only the active Deck path reads this thread-local cache.
namespace world_state {
struct Cache {
    bool viewport_known=false,scissor_known=false,projection_known=false;
    GLint mode=0,viewport[4]{},scissor[4]{};
    int blend=-1,scissor_test=-1;
    GLfloat projection[16]{};
    Array position{};
    bool position_known=false;
    GLint array_binding=-1,position_binding=-1,position_enabled=-1,element_binding=-1;
};
static thread_local Cache state;
static bool observing=false;
static thread_local bool tracking=false;
static decltype(&glViewport) viewport_original;
static decltype(&glScissor) scissor_original;
static decltype(&glMatrixMode) mode_original;
static decltype(&glLoadMatrixf) load_original;
static decltype(&glLoadIdentity) identity_original;
static decltype(&glOrtho) ortho_original;
static decltype(&glPopMatrix) pop_original;
static decltype(&glPopAttrib) attrib_original;
static decltype(&glEnable) enable_original;
static decltype(&glDisable) disable_original;
static decltype(&glVertexPointer) vertex_original;
static decltype(&glEnableClientState) client_enable_original;
#include "deck_client_buffers.h"
static void APIENTRY client_enable(GLenum key){
    client_enable_original(key);
    if(tracking&&key==GL_VERTEX_ARRAY&&state.position_enabled==0)state.position_enabled=-1;
}
static void APIENTRY vertex(GLint size,GLenum type,GLsizei stride,const void* ptr){
    vertex_original(size,type,stride,ptr);
    if(!tracking)return;
    state.position_known=GetCurrentThreadId()==thread&&wglGetCurrentContext()==context&&size==4&&type==GL_FLOAT&&stride>=0&&stride<4096;
    if(state.position_known)state.position={static_cast<const BYTE*>(ptr),GL_FLOAT,4,stride?stride:16,true};
    if(cached_buffers&&state.position_known){
        if(state.array_binding<0)glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&state.array_binding);
        state.position_binding=state.array_binding;
    }
}
static void APIENTRY viewport(GLint x,GLint y,GLsizei w,GLsizei h){
    viewport_original(x,y,w,h);state.viewport_known=false;
}
static void APIENTRY scissor(GLint x,GLint y,GLsizei w,GLsizei h){
    scissor_original(x,y,w,h);state.scissor_known=false;
}
static void APIENTRY mode(GLenum m){mode_original(m);state.mode=0;}
static void changed_matrix(){if(!state.mode||state.mode==GL_PROJECTION)state.projection_known=false;}
static void APIENTRY load(const GLfloat* m){load_original(m);changed_matrix();}
static void APIENTRY identity(){identity_original();changed_matrix();}
static void APIENTRY ortho(GLdouble l,GLdouble r,GLdouble b,GLdouble t,GLdouble n,GLdouble f){ortho_original(l,r,b,t,n,f);changed_matrix();}
static void APIENTRY pop(){pop_original();changed_matrix();}
static void APIENTRY attrib(){attrib_original();state={};}
static void changed_cap(GLenum key){
    if(key==GL_BLEND)state.blend=-1;
    if(key==GL_SCISSOR_TEST)state.scissor_test=-1;
}
static void APIENTRY enable(GLenum key){enable_original(key);changed_cap(key);}
static void APIENTRY disable(GLenum key){disable_original(key);changed_cap(key);}
static void rect(GLenum key,GLint* dst){
    const bool vp=key==GL_VIEWPORT;
    bool& known=vp?state.viewport_known:state.scissor_known;
    GLint* data=vp?state.viewport:state.scissor;
    if(!known||!observing){glGetIntegerv(key,data);known=true;}
    memcpy(dst,data,4*sizeof(GLint));
}
static bool cap_enabled(GLenum key){
    int& value=key==GL_BLEND?state.blend:state.scissor_test;
    if(value<0||!observing)value=glIsEnabled(key);
    return value!=0;
}
static GLint matrix_mode(){
    if(!state.mode||!observing)glGetIntegerv(GL_MATRIX_MODE,&state.mode);
    return state.mode;
}
static void projection(GLfloat* dst){
    if(!state.projection_known||!observing){
        glGetFloatv(GL_PROJECTION_MATRIX,state.projection);state.projection_known=true;
    }
    memcpy(dst,state.projection,sizeof(state.projection));
}
}
