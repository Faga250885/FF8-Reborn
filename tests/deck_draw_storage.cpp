#define GEO_TEST
#include "../src/geometry.cpp"
using namespace geo;

int main(){
    HWND window=CreateWindowA("STATIC","FF8 draw storage",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC display=GetDC(window);
    PIXELFORMATDESCRIPTOR format{sizeof(format),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    if(!SetPixelFormat(display,ChoosePixelFormat(display,&format),&format)||
       !wglMakeCurrent(display,wglCreateContext(display))||!initialize_gl())return 1;

    DeckDrawStorage pool;
    Frame retired;
    auto& previous=retired.ops.emplace_back();
    previous.vertices.resize(6);
    memset(previous.vertices.data(),0x3f,6*sizeof(Vertex));
    previous.prior_positions.resize(6,{1,2,3,4});
    previous.motion_valid.assign(6,1);
    const Vertex* storage=previous.vertices.data();
    pool.retire(retired,true);
    Op next;
    pool.take(next,6);
    demand(next.vertices.data()==storage&&next.motion_valid.empty()&&next.prior_positions.empty(),"CPU storage reuse");
    const float pos[][2]={{2,3},{4,5},{6,7}};
    const GLubyte order[]={2,0,1,1,2,0};
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2,GL_FLOAT,0,pos);
    read_draw_vertices(next,1,6,GL_UNSIGNED_BYTE,order);
    Op fresh;
    read_draw_vertices(fresh,1,6,GL_UNSIGNED_BYTE,order);
    demand(!memcmp(next.vertices.data(),fresh.vertices.data(),6*sizeof(Vertex)),"stale attributes on recycled storage");
    for(auto& v:next.vertices)demand(v.p[2]==0&&v.p[3]==1&&v.c[3]==1&&v.n[2]==1&&v.uv[3][3]==1,"native defaults");

    retired.ops.clear();
    retired.ops.push_back(std::move(next));
    pool.retire(retired,false);
    Op disabled;
    pool.take(disabled,6);
    demand(!disabled.vertices.capacity(),"storage retained across disabling frame generation");
    demand(glGetError()==GL_NO_ERROR,"GL error");
    puts("Recycled and fresh client arrays matched in every component.");
}
