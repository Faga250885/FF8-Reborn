// Tangents are screen pixels per native animation step. Use the same tangent
// on both sides of a joint in time and stop at each actual reversal.
// The character's projection can oscillate about its ground contact over
// several native ticks. Keep camera motion and damp just this relative offset.
struct StrideFollow {
    float start=0,end=0;
    void update(float previous,float torso_dx,float ground_dx,float seconds,bool walking){
        start=previous;
        float retain=std::exp(-std::clamp(seconds,.008f,.15f)/.085f);
        float delta=walking?torso_dx-ground_dx:0;
        end=std::clamp((start-delta)*retain,-4.f,4.f);
        if(std::abs(end)<.01f)end=0;
    }
};
struct StrideKnot {
    bool player=false;
    std::array<float,4> position{};
    std::array<std::array<float,2>,3> slopes{};
    static float joining(float before,float after){
        return before*after>0 ? 2*before*after/(before+after) : 0;
    }
    static float limit(float slope,float distance){
        if(slope*distance<=0)return 0;
        return std::copysign(std::min(std::abs(slope),2*std::abs(distance)),distance);
    }
    void sample(const float* start,const float* finish,float alpha,float* out) const {
        unsigned half=alpha>=.5f;
        float t=2*alpha-half, u=1-t;
        const float* a=half?position.data():start;
        const float* b=half?finish:position.data();
        for(unsigned k=0;k<2;k++){
            float delta=b[k]-a[k];
            out[k]=a[k]+t*delta+t*u*(u*(slopes[half][k]-delta)+t*(delta-slopes[half+1][k]));
        }
        for(unsigned k=2;k<4;k++)out[k]=u*a[k]+t*b[k];
    }
};
