// Included in namespace geo. Use the game drawable rather than the desktop:
// a window, an external Deck monitor and an ultrawide can have different sizes.
namespace filter_resolution {
enum class Tier { unknown, handheld, full_hd, qhd, uhd };

constexpr Tier detect(int width,int height){
    if(width<=0||height<=0)return Tier::unknown;
    if(height<940)return Tier::handheld;
    if(height<1260)return Tier::full_hd;
    if(height<1800)return Tier::qhd;
    return Tier::uhd;
}

constexpr const wchar_t* label(Tier tier){
    switch(tier){
        case Tier::handheld:return L"720p / 800p";
        case Tier::full_hd:return L"1080p";
        case Tier::qhd:return L"1440p (2K)";
        case Tier::uhd:return L"2160p (4K)";
        default:return L"Unknown";
    }
}

struct Calibration {
    float rgb_pitch, rgb_contrast, tv_height, sample_spread;
    bool dense_signal;
};
inline Calibration calibrate(int w,int h){
    const Tier tier=detect(w,h);
    // The reference RGB triplet spans three display pixels at 1440p. A
    // smaller drawable cannot resolve three independently colored stripes
    // at a smaller pitch, so keep the triplet and reduce its contrast.
    const float height=float(std::max(1,h));
    const float rgb_pitch=std::max(3.f,height/480.f);
    const float rgb_contrast=std::min(1.f,height/1440.f);
    // The finest television grille has 375 triplets per picture height.
    // Reserve at least three samples per triplet on the handheld screen.
    const float tv_height=std::max(1125.f,height);
    // Source samples already include bilinear filtering over one texel.
    // Increase coverage on small non-integer cells, and on the more widely
    // spaced samples in a 4K cell. Preserve the original 1440p tuning.
    const float spread=tier==Tier::handheld?.30f:tier==Tier::full_hd?.26f:tier==Tier::uhd?.28f:.23f;
    return {rgb_pitch,rgb_contrast,tv_height,spread,tier==Tier::uhd};
}
}
