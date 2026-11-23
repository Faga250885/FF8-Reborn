namespace update_notice {
// Hold for ten seconds, then fade for two. Start the clock and cue only
// when the first frame carrying this release's notice reaches the screen.
struct Lifetime {
    unsigned last_shown=0;
    double began=0;

    float opacity(unsigned offered,double time) const {
        if(!offered||offered<last_shown)return 0;
        if(offered>last_shown)return 1;
        return float(std::clamp((began+12.-time)/2.,0.,1.));
    }
    bool presented(unsigned build,double time){
        if(build<=last_shown)return false;
        last_shown=build;
        began=time;
        return true;
    }
};
static Lifetime lifetime;
static void after_swap(unsigned drawn_build,bool swapped){
    if(swapped&&drawn_build&&lifetime.presented(drawn_build,now()))
        update_sound::play();
}

static Surface tile;
constexpr wchar_t title[]=L"FF8 Reborn update available";
constexpr wchar_t hint[]=L"Open the FF8 Reborn menu for details.";
constexpr int height=39;
static int width(){
    static const int logical_width=18+std::max(retro_panel_draw::text_width(title),retro_panel_draw::text_width(hint));
    return logical_width;
}
struct Placement { int x,y,w,h; };
static Placement place_tile(int w,int h,int occupied_height,int logical_width,int logical_height){
    const int preferred=std::max(1,int(std::lround(h/400.0)));
    const double fit=std::min(double(w)/(logical_width+12),double(h-occupied_height)/(logical_height+12));
    if(fit<=0)return {};
    const double scale=fit<1?fit:std::min<double>(preferred,std::floor(fit));
    const int margin=int(std::lround(6*scale)),tw=int(std::lround(logical_width*scale)),th=int(std::lround(logical_height*scale));
    return {margin,h-occupied_height-margin-th,tw,th};
}
static Placement place(int w,int h,int counter_height){return place_tile(w,h,counter_height,width(),height);}
static bool draw(GLuint target,Placement p,float alpha){
    if(alpha<=0||p.w<=0||p.h<=0)return false;
    if(tile.w!=width()||tile.h!=height){
        using namespace retro_panel_draw;
        const int w=width();
        Canvas c(w,height,w);
        if(!c.pixels)return false;
        c.window(0,0,w-1,height-2);
        c.label(title,9,4,w-8,18,0,ink);
        c.label(hint,9,19,w-8,34,0,mint);
        if(!upload(tile,c))return false;
    }
    return retro_panel_draw::blit(tile,target,p.x,p.y,p.w,p.h,alpha);
}
}

namespace filter_notice {
using update_notice::Placement;
constexpr int height=24;
static Surface tile;
static int cached_profile=-1;
static int width(int id){
    static const auto widths=[](){
        std::array<int,retro_panel::profile_count> result{};
        const int inset=18+retro_panel_draw::text_width(L"Filter: ");
        for(int i=0;i<retro_panel::profile_count;++i)result[i]=inset+retro_panel_draw::text_width(retro_panel::names[i]);
        return result;
    }();
    return widths[id];
}
static Placement place(int w,int h,int occupied_height,int id){
    if(id<0||id>=retro_panel::profile_count)return {};
    return update_notice::place_tile(w,h,occupied_height,width(id),height);
}
static bool draw(GLuint target,Placement p,int id,float alpha){
    if(id<0||id>=retro_panel::profile_count||alpha<=0||p.w<=0||p.h<=0)return false;
    if(id!=cached_profile||!tile.color){
        using namespace retro_panel_draw;
        const int w=width(id);
        Canvas c(w,height,w);
        if(!c.pixels)return false;
        c.window(0,0,w-1,height-2);
        const int value_x=9+text_width(L"Filter: ");
        c.label(L"Filter:",9,3,value_x,height-5,0,mint);
        c.label(retro_panel::names[id],value_x,3,w-7,height-5,0,ink);
        if(!upload(tile,c))return false;
        cached_profile=id;
    }
    return retro_panel_draw::blit(tile,target,p.x,p.y,p.w,p.h,alpha);
}
}
