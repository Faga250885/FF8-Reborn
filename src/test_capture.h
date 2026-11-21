// Bounded renderer diagnostics, only in the temporary integration-test build.
// Capture native current plus reconstructed previous/mid/current endpoints.
#include "test_snapshot.h"
static void test_capture_pair(int mode){
    static unsigned serial=0;if(serial>=12)return;++serial;
    static bool shaders_saved=false;if(!shaders_saved&&mode==3){shaders_saved=true;
        auto attached=(PFNGLGETATTACHEDSHADERSPROC)wglGetProcAddress("glGetAttachedShaders");
        auto shader_info=(PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
        auto shader_source=(PFNGLGETSHADERSOURCEPROC)wglGetProcAddress("glGetShaderSource");
        FILE*f=fopen("FF8_geometry_shaders.txt","w");if(f&&attached&&shader_info&&shader_source){std::unordered_map<GLint,bool> seen;
            for(const auto&op:current.ops)if(op.state.program&&!seen[op.state.program]){seen[op.state.program]=true;GLuint shaders[16];GLsizei count=0;attached(op.state.program,16,&count,shaders);
                for(int i=0;i<count;i++){GLint length=0,type=0;shader_info(shaders[i],GL_SHADER_SOURCE_LENGTH,&length);shader_info(shaders[i],GL_SHADER_TYPE,&type);
                    if(length<=0||length>1000000)continue;std::vector<char> source(length+1);shader_source(shaders[i],length,nullptr,source.data());fprintf(f,"program %d type %x\n%s\n",op.state.program,type,source.data());}}}
        if(f)fclose(f);}
    Guard guard(current);const int w=1280,h=720;Surface sample;
    // Keep independent bounded fixtures for fields, world and battles. A busy
    // battle must not consume all opportunities to reproduce a field problem.
    static unsigned full[4]={};
    if(mode>=1&&mode<=3&&current.vertex_count>0&&full[mode]<3){char path[128];
        unsigned number=++full[mode];
        snprintf(path,sizeof(path),"FF8_geometry_snapshot_mode%d_%02u.bin",mode,number);
        try{snapshot::pair(path,false);log("full native scene snapshot saved\n");}catch(const std::exception&e){log(e.what());log("\n");}
        if(test_older.valid&&test_older.started&&test_older.mode==mode&&test_older.w==current.w&&test_older.h==current.h){
            snprintf(path,sizeof(path),"FF8_geometry_holdout_mode%d_%02u.bin",mode,number);
            std::swap(test_older,previous);
            try{snapshot::pair(path,false);log("holdout endpoints saved; native middle is step-2\n");}catch(const std::exception&e){log(e.what());log("\n");}
            std::swap(test_older,previous);
        }}
    resize(sample,w,h,false);std::vector<BYTE> pixels(w*h*4);
    pBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    char name[140];snprintf(name,sizeof(name),"FF8_geometry_test_%02u_mode%d.bin",serial,mode);correspondence_audit(name);
    for(int step=-2;step<=9;step++){
        if(step==-2&&(!previous_reference.fbo||previous_reference.w!=width||previous_reference.h!=height))continue;
        if(step==6){partial_topology_enabled=false;battle_visibility_enabled=false;battle_depth_enabled=false;match_frames(mode);replay(test_target.fbo,0);}
        else if(step==7)replay(test_target.fbo,.5f);
        else if(step==8){partial_topology_enabled=true;battle_visibility_enabled=true;match_frames(mode);replay(test_target.fbo,0);}
        else if(step==9)replay(test_target.fbo,.5f);
        else if(step==5){auto saved=std::move(outgoing_draws);replay(test_target.fbo,.5f);outgoing_draws=std::move(saved);}
        else if(step>=0)replay(test_target.fbo,step*.25f);
        GLuint src=step==-2?previous_reference.fbo:step==-1?reference_target.fbo:test_target.fbo;
        blit(src,sample.fbo,width,height,w,h,GL_COLOR_BUFFER_BIT);
        pBindFramebuffer(GL_READ_FRAMEBUFFER,sample.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,w,h,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
        BITMAPFILEHEADER head{};BITMAPINFOHEADER info{};
        head.bfType=0x4d42;head.bfOffBits=sizeof(head)+sizeof(info);head.bfSize=head.bfOffBits+(DWORD)pixels.size();
        info.biSize=sizeof(info);info.biWidth=w;info.biHeight=h;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        snprintf(name,sizeof(name),"FF8_geometry_test_%02u_mode%d_step%d.bmp",serial,mode,step);
        FILE*f=fopen(name,"wb");if(f){fwrite(&head,sizeof(head),1,f);fwrite(&info,sizeof(info),1,f);fwrite(pixels.data(),pixels.size(),1,f);fclose(f);}
    }
    partial_topology_enabled=true;battle_visibility_enabled=true;battle_depth_enabled=true;match_frames(mode);
    release(sample);log("test capture endpoints, intermediate positions, and same-pair baseline without partial topology saved\n");
}
