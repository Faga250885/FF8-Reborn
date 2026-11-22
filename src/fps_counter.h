// Counts completed window swaps, including successfully presented intermediate
// frames. The bitmap is uploaded only when the displayed integer changes.
namespace fps_counter {
constexpr int tile_w=44,tile_h=11;
static Surface tile;
static double started=0;
static unsigned intervals=0;
static int rate=-1,uploaded=-2;
static void reset_meter(){started=0;intervals=0;rate=-1;}
static void forget(){reset_meter();tile={};uploaded=-2;}
static void presented(double time){
    if(!started||time<=started||time-started>5){started=time;intervals=0;rate=-1;return;}
    ++intervals;
    const double elapsed=time-started;
    if(elapsed>=.5){
        rate=std::clamp(int(std::lround(intervals/elapsed)),0,999);
        started=time;intervals=0;
    }
}
static int scale(int size,int w,int h){
    if(!size)return 0;
    return std::min({w/tile_w,h/tile_h,std::max(1,int(std::lround((size+1)*h/800.)))});
}
static bool draw(int w,int h,int zoom){
    if(!zoom||w<tile_w*zoom||h<tile_h*zoom)return false;
    static constexpr BYTE glyphs[][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{31,16,16,30,16,16,16},{30,17,17,30,16,16,16},
        {15,16,16,14,1,1,30},{0,0,0,14,0,0,0}
    };
    if(!tile.fbo||uploaded!=rate){
        try{retro_display::allocate(tile,tile_w,tile_h);}catch(const std::exception&){return false;}
        std::array<uint32_t,tile_w*tile_h> pixels;
        pixels.fill(0xff24201c);
        char label[9];
        if(rate<0)strcpy(label," -- FPS");else snprintf(label,sizeof(label),"%3d FPS",rate);
        for(int i=0;label[i];++i){
            const char c=label[i];
            if(c==' ')continue;
            const int g=c=='F'?10:c=='P'?11:c=='S'?12:c=='-'?13:c-'0';
            for(int y=0;y<7;++y)for(int x=0;x<5;++x)if(glyphs[g][y]&(16>>x))
                pixels[(tile_h-3-y)*tile_w+2+6*i+x]=0xfff3f6f2;
        }
        PixelTransferGuard transfer;
        GLint old=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);
        glBindTexture(GL_TEXTURE_2D,tile.color);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,tile_w,tile_h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        glBindTexture(GL_TEXTURE_2D,old);uploaded=rate;
    }
    retro_display::TargetState state(0,false);
    pBindFramebuffer(GL_READ_FRAMEBUFFER,tile.fbo);
    pBlitFramebuffer(0,0,tile_w,tile_h,0,h-tile_h*zoom,tile_w*zoom,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    return true;
}
}
