// Bitmap UI shared by the settings panel and update notice. All coordinates are
// on the SNES-style pixel grid. A row of pixels is stored bottom-up for OpenGL.
#include "ffvi_menu_art.h"
constexpr COLORREF ink=RGB(248,248,239),mint=RGB(162,245,255),secondary=ink;
constexpr COLORREF muted=RGB(181,180,206),warning=RGB(255,208,147),track=RGB(28,28,74);
constexpr COLORREF text_shadow=RGB(16,16,42);

static const Glyph& glyph(wchar_t ch){
    auto it=std::lower_bound(std::begin(glyphs),std::end(glyphs),ch,
                             [](const Glyph& g,wchar_t key){return g.code<key;});
    return it!=std::end(glyphs)&&it->code==ch?*it:glyphs[L'?'-L' '];
}
static int text_width(const wchar_t* text){
    int width=0;
    for(;*text;++text)width+=glyph(*text).advance;
    return width;
}
struct Canvas {
    int width,height;
    double scale;
    std::unique_ptr<uint32_t[]> storage;
    unsigned char* pixels;
    Canvas(int w,int h,int logical_width=404):width(w),height(h),scale(double(w)/logical_width),
        storage(new(std::nothrow) uint32_t[size_t(w)*h]{}),pixels(reinterpret_cast<unsigned char*>(storage.get())){}
    int px(int unit) const{return int(std::lround(unit*scale));}
    void box(int x,int y,int r,int b,COLORREF colour){
        const int x0=std::clamp(px(x),0,width),x1=std::clamp(px(r),0,width);
        const int y0=std::clamp(px(y),0,height),y1=std::clamp(px(b),0,height);
        if(x1<=x0)return;
        const uint32_t bgra=0xff000000u|(uint32_t(GetRValue(colour))<<16)|
                            (uint32_t(GetGValue(colour))<<8)|GetBValue(colour);
        for(int row=y0;row<y1;++row){
            auto* scan=storage.get()+size_t(height-1-row)*width;
            std::fill(scan+x0,scan+x1,bgra);
        }
    }
    void blend_box(int x,int y,int r,int b,COLORREF colour,unsigned opacity){
        const int x0=std::clamp(px(x),0,width),x1=std::clamp(px(r),0,width);
        const int y0=std::clamp(px(y),0,height),y1=std::clamp(px(b),0,height);
        opacity=std::min(opacity,255u);
        for(int row=y0;row<y1;++row){
            auto* scan=storage.get()+size_t(height-1-row)*width;
            for(int col=x0;col<x1;++col){
                const uint32_t old=scan[col];
                const auto mix=[&](unsigned previous,unsigned next){return (previous*(255-opacity)+next*opacity+127)/255;};
                scan[col]=0xff000000u|(mix((old>>16)&255,GetRValue(colour))<<16)|
                          (mix((old>>8)&255,GetGValue(colour))<<8)|mix(old&255,GetBValue(colour));
            }
        }
    }
    void stepped_box(int x,int y,int w,int h,COLORREF colour){
        box(x+3,y,x+w-3,y+h,colour);
        box(x+1,y+1,x+w-1,y+h-1,colour);
        box(x,y+3,x+w,y+h-3,colour);
    }
    void window(int x,int y,int w,int h,bool selected=false,bool shadow=true){
        static constexpr int blues[][2]={{131,206},{123,200},{115,194},{104,184},{89,174},{80,166},
            {70,160},{61,153},{49,146},{35,137},{22,131},{0,123},{0,115},{0,107},{0,99},{0,87}};
        if(shadow)stepped_box(x+1,y+2,w,h,RGB(8,8,25));
        stepped_box(x,y,w,h,RGB(27,27,51));
        stepped_box(x+1,y+1,w-2,h-2,RGB(116,116,131));
        box(x+3,y+1,x+w-4,y+2,RGB(222,222,229));
        box(x+1,y+3,x+2,y+h-4,RGB(222,222,229));
        box(x+2,y+2,x+4,y+3,RGB(182,182,192));
        box(x+w-4,y+2,x+w-2,y+3,RGB(182,182,192));
        stepped_box(x+2,y+2,w-4,h-4,RGB(49,49,74));
        for(int i=0;i<h-6;++i){
            const int cut=i==0||i==h-7?1:0;
            const int* shade=blues[i*std::size(blues)/(h-6)];
            const COLORREF fill=selected?RGB(std::min(255,shade[0]+32),std::min(255,shade[0]+55),std::min(255,shade[1]+36)):
                                               RGB(shade[0],shade[0],shade[1]);
            box(x+3+cut,y+3+i,x+w-3-cut,y+4+i,fill);
        }
    }
    void cursor(int x,int y){
        for(int row=0;row<16;++row)for(int col=0;col<16;++col){
            const int index=hand[row][col]-'0';
            if(index)box(x+col,y+row,x+col+1,y+row+1,hand_palette[index]);
        }
    }
    void label(const wchar_t* str,int x,int y,int r,int b,int /*font*/,COLORREF colour,bool align_right=false){
        const int available=r-x;
        if(available<=0||!str||!*str)return;
        int span=text_width(str);
        std::wstring trimmed;
        if(span>available){
            const int ellipsis=glyph(L'…').advance;
            span=0;
            while(*str&&span+glyph(*str).advance+ellipsis<=available){
                span+=glyph(*str).advance;trimmed+=*str++;
            }
            if(ellipsis<=available){trimmed+=L'…';span+=ellipsis;}
            str=trimmed.c_str();
        }
        const int start=align_right?r-span:x,top=y+(b-y-11)/2;
        auto pass=[&](int dx,int dy,COLORREF col){
            int pen=start;
            for(const wchar_t* p=str;*p;++p){
                const Glyph& g=glyph(*p);
                for(int row=0;row<11;++row){
                    for(int i=0;i<g.width;++i)if(g.rows[row]&(1u<<(g.width-1-i)))
                        box(pen+i+dx,top+row+dy,pen+i+dx+1,top+row+dy+1,col);
                }
                pen+=g.advance;
            }
        };
        pass(1,1,text_shadow);pass(0,0,colour);
    }
    void paragraph(const std::wstring& text,int x,int y,int r,int lines,COLORREF colour){
        size_t first=0;
        for(int line=0;line<lines&&first<text.size();++line){
            if(line==lines-1){label(text.c_str()+first,x,y,r,y+13,0,colour);break;}
            size_t end=first,last_break=first;int width=0;
            while(end<text.size()&&width+glyph(text[end]).advance<=r-x){
                if(text[end]==L' ')last_break=end;
                width+=glyph(text[end++]).advance;
            }
            if(end<text.size()&&text[end]!=L' '&&last_break>first)end=last_break;
            label(text.substr(first,end-first).c_str(),x,y,r,y+13,0,colour);
            first=end;while(first<text.size()&&text[first]==L' ')++first;
            y+=13;
        }
    }
};

static bool upload(Surface& surface,const Canvas& c){
    if(!c.pixels)return false;
    try{retro_display::allocate(surface,c.width,c.height);}catch(const std::exception&){return false;}
    PixelTransferGuard transfer;
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glBindTexture(GL_TEXTURE_2D,surface.color);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,c.width,c.height,GL_BGRA,GL_UNSIGNED_BYTE,c.pixels);
    glBindTexture(GL_TEXTURE_2D,texture);
    return true;
}
// Coverage is binary on this grid. The overlay shader skips transparent texels,
// and the constant blend factor fades all covered pixels without touching game alpha.
static bool blit(const Surface& tile,GLuint target,int x,int y,int w,int h,float opacity){
    if(opacity<=0||w<=0||h<=0||!tile.color)return false;
    retro_display::inspect_context();
    retro_display::TargetState state(target,true);
    if(!retro_display::prepare(false,retro_display::Stage::notice))return false;
    GLint program,unit,texture,sampler=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);
    pActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    auto bind_sampler=retro_display::pipeline.bind_sampler;
    if(bind_sampler){glGetIntegerv(GL_SAMPLER_BINDING,&sampler);bind_sampler(0,0);}
    retro_display::fullscreen_draw_state(x,y,w,h);
    if(opacity<1.f){
        glEnable(GL_BLEND);pBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        pBlendColor(0,0,0,opacity);pBlendFuncSeparate(GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_ZERO,GL_ONE);
    }
    const auto& shader=retro_display::notice_pipeline;
    pUseProgram(shader.program);
    const GLfloat origin[]={float(x),float(y)},size[]={float(w),float(h)};
    pUniform2fv(shader.response,1,origin);pUniform2fv(shader.size,1,size);
    glBindTexture(GL_TEXTURE_2D,tile.color);retro_display::fullscreen_triangle();
    glBindTexture(GL_TEXTURE_2D,texture);if(bind_sampler)bind_sampler(0,sampler);
    pActiveTexture(unit);pUseProgram(program);
    return true;
}
