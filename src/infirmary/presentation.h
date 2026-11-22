namespace infirmary {
#include "pixels.inc"
static constexpr int panel=40, atlasWidth=2*panel, atlasHeight=240;
static Surface sheets[3];

// Same guest word used by the field transition dispatcher at 4706B0+21C.
static int field_id(){
    if(!engine)return -1;
    constexpr unsigned address=0x1cd2fc0;
    const auto slot=(const uint32_t*)((BYTE*)engine+0x188edd0)+(address>>12);
    if(!readable(slot,4)||!*slot)return -1;
    auto word=(const uint16_t*)(uintptr_t)(*slot+(address&4095));
    if(!readable(word,2))return -1;
    uint16_t id;memcpy(&id,word,2);
    return id;
}
static int room_id(){
    const int id=field_id();
    return id>=179&&id<=181?id-179:-1;
}
// Reuse the native parity readback. Only background landmarks vote, so a
// character or a dialogue over a few tiles does not move the reconstruction.
static int camera_top(int room,const std::vector<BYTE>&sample){
    constexpr int points=sizeof(cols)*sizeof(rows);
    int best=INT_MAX,answer=-1;
    for(int top=0;top<=16;top++){
        const BYTE* ref=anchors+(room*17+top)*points*3;
        int loss=0,support=0;
        for(auto y:rows)for(auto x:cols){
            const BYTE* live=&sample[4*((86-y)*160+20+x)];
            int delta=0;for(int c=0;c<3;c++)delta+=std::abs(int(live[c])-int(*ref++));
            support+=delta<70;loss+=std::min(delta,100);
        }
        if(support>=points/2&&loss<best){best=loss;answer=top;}
    }
    return answer;
}
static void draw(int room,int top,GLuint target,const WideView&view){
    if(room<0||top<0||!view.outer_width)return;
    int left=(view.drawable_width-view.outer_width)/2;
    if(left<=0)return;
    Surface& sheet=sheets[room];
    if(!sheet.fbo){
        GLint pbo;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);
        pBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
        resize(sheet,atlasWidth,atlasHeight,false,true);
        glBindTexture(GL_TEXTURE_2D,sheet.color);
        real_tex_subimage(GL_TEXTURE_2D,0,0,0,atlasWidth,atlasHeight,GL_RGB,GL_UNSIGNED_BYTE,
                          art+room*atlasWidth*atlasHeight*3);
        glPopClientAttrib();pBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    }
    glDisable(GL_SCISSOR_TEST);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,sheet.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    pBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);glDrawBuffer(target?GL_COLOR_ATTACHMENT0:GL_BACK);
    // Texture row zero is the top of the artwork. Clip oversized sides at
    // the physical display boundary, preserving scale on narrower screens.
    int width=int(std::lround(view.outer_width*panel/320.0)),right=left+view.outer_width;
    pBlitFramebuffer(0,top+224,panel,top,left-width,0,left,view.drawable_height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    pBlitFramebuffer(panel,top+224,atlasWidth,top,right,0,right+width,view.drawable_height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
}
}
