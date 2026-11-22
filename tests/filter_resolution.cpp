#define GEO_TEST
#undef NDEBUG
#include "../src/geometry.cpp"
#include <cassert>

using namespace geo;
namespace fr=filter_resolution;
namespace rd=retro_display;

static void fill(Surface& out){
    pBindFramebuffer(GL_FRAMEBUFFER,out.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);glClearColor(.5f,.5f,.5f,1);glClear(GL_COLOR_BUFFER_BIT);
}

static std::array<BYTE,144*4> read_strip(const Surface& out){
    std::array<BYTE,144*4> pixels{};
    pBindFramebuffer(GL_READ_FRAMEBUFFER,out.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,out.h/2,144,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    return pixels;
}

int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    using Tier=fr::Tier;
    static_assert(fr::detect(0,1080)==Tier::unknown&&fr::detect(1280,0)==Tier::unknown);
    static_assert(fr::detect(1280,720)==Tier::handheld&&fr::detect(1280,800)==Tier::handheld);
    static_assert(fr::detect(2560,1080)==Tier::full_hd&&fr::detect(3440,1440)==Tier::qhd);
    static_assert(fr::detect(4096,2160)==Tier::uhd);
    HWND win=CreateWindowA("STATIC","Adaptive filter samples",WS_POPUP,0,0,256,256,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    HDC dc=GetDC(win);
    PIXELFORMATDESCRIPTOR fmt{sizeof(fmt),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&fmt),&fmt));
    assert(wglMakeCurrent(dc,wglCreateContext(dc))&&initialize_gl());
    RetroSettings rgb{};
    rgb.mode=2;rgb.lines=320;rgb.pixelation=rgb.dither=rgb.glow=rgb.scanlines=0;rgb.mask=100;
    Surface out;
    for(const auto& dimensions:{std::pair{1280,720},std::pair{1280,800},std::pair{1920,1080},
                               std::pair{2560,1440},std::pair{3840,2160},std::pair{1280,800}}){
        const auto [w,h]=dimensions;
        resize(out,w,h,false);
        fill(out);
        const auto unfiltered=read_strip(out);
        assert(!rd::apply(RetroSettings{},out.fbo,w,h)&&read_strip(out)==unfiltered);
        assert(rd::apply(rgb,out.fbo,w,h));
        const auto stripe=read_strip(out);
        int sum[3]{},lo=255,hi=0;
        for(int x=0;x<144;x++){
            for(int c=0;c<3;c++)sum[c]+=stripe[4*x+c];
            lo=std::min(lo,int(stripe[4*x]));hi=std::max(hi,int(stripe[4*x]));
            const int period=h==2160?9:3;
            if(x>=period)for(int c=0;c<3;c++)assert(stripe[4*x+c]==stripe[4*(x-period)+c]);
        }
        assert(hi-lo>=18);
        assert(*std::max_element(sum,sum+3)-*std::min_element(sum,sum+3)<=144);
        if(h==1440)assert(std::abs(hi-int(unfiltered[0]))<=1&&std::abs(lo-int(std::lround(.65f*unfiltered[0])))<=1);
        if(h<=800)assert(hi-lo<32);
        if(h==2160)assert(stripe[4]>lo&&stripe[4]<hi); // A physical pixel spans two virtual phosphors.
        rd::restore(out.fbo,w,h);assert(read_strip(out)==unfiltered);

        if(h==800||h==1440){
            auto crt=rgb;crt.mask=0;crt.scanlines=100;
            for(int rows:{315,320,325}){
                crt.lines=rows;
                assert(rd::apply(crt,out.fbo,w,h));
                std::vector<BYTE> column(size_t(h)*4);
                pBindFramebuffer(GL_READ_FRAMEBUFFER,out.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
                glReadPixels(w/2,0,1,h,GL_RGBA,GL_UNSIGNED_BYTE,column.data());
                int darkest=255,lightest=0;
                for(int y=0;y<h;y++){
                    darkest=std::min(darkest,int(column[4*y]));lightest=std::max(lightest,int(column[4*y]));
                }
                const int middle=(darkest+lightest)/2;
                int scanlines=0;
                for(int y=0;y<h;y++)
                    if(column[4*y]<middle&&column[4*((y+1)%h)]>=middle)++scanlines;
                assert(scanlines==rows);
                rd::restore(out.fbo,w,h);
            }
        }
        RetroSettings tv=rgb;tv.mode=0;tv.tv_screen=RetroSettings::aperture_grille;
        assert(rd::apply(tv,out.fbo,w,h));
        const auto grille=read_strip(out);
        lo=255;hi=0;
        for(int i=0;i<144;i++){lo=std::min(lo,int(grille[i*4]));hi=std::max(hi,int(grille[i*4]));}
        assert(hi-lo>10); // The handheld grille must not disappear at the Nyquist limit.
        rd::restore(out.fbo,w,h);

        glEnable(GL_SCISSOR_TEST);
        for(int x=1;x<144;x+=3){
            glScissor(x,h/2-10,1,24);glClearColor(1,.1f,.5f,1);glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
        auto signal=RetroSettings::recommended();signal.lines=320;
        for(bool use_tv:{false,true}){
            if(use_tv)signal.phosphors=RetroSettings::slot_mask;
            assert(rd::apply(signal,out.fbo,w,h));
            const auto split=read_strip(out);
            rd::restore(out.fbo,w,h);
            rd::failed_signal_w=use_tv?w:int(std::round(float(signal.lines)*w/h));
            rd::failed_signal_h=signal.lines+(use_tv?2:0);
            assert(rd::apply(signal,out.fbo,w,h));
            const auto single=read_strip(out);
            for(size_t i=0;i<split.size();i++)assert(std::abs(int(split[i])-int(single[i]))<=1);
            rd::restore(out.fbo,w,h);
            rd::failed_signal_w=rd::failed_signal_h=0;
        }
        assert(glGetError()==GL_NO_ERROR);
        printf("%dx%d: balanced RGB, visible grille, original restore, signal paths match.\n",w,h);
    }
    return 0;
}
