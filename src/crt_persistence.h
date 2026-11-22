// A short phosphor tail, integrated over one displayed frame. The history
// always holds the last successful presentation's unfiltered source.
struct PhosphorHistory {
    Surface raw, signal;
    int strength=0, scene=-1, previous_scene=-1, w=0, h=0;
    int failed_signal_w=0,failed_signal_h=0;
    HDC window=nullptr;
    double time=0, previous_time=0;
    float carry=0;
    bool pending=false;

    void begin(int amount,int key,HDC handle,int width,int height,double stamp){
        strength=key>=0?amount:0;
        const double dt=stamp-previous_time;
        carry=0;
        if(strength&&previous_time>0&&dt>0&&dt<.10&&key==previous_scene&&
           handle==window&&width==w&&height==h){
            constexpr double decay=.0035;
            carry=float(.01*strength*decay/dt*(-std::expm1(-dt/decay)));
        }
        previous_time=0;pending=false;
        scene=key;window=handle;w=width;h=height;time=stamp;
    }
    void commit(bool swapped){
        if(pending&&swapped){previous_time=time;previous_scene=scene;}
        pending=false;
    }
};
static PhosphorHistory phosphor_history;

static const char* persistence_fragment=R"GLSL(
uniform sampler2D sourceImage;
uniform sampler2D historyImage;
uniform vec2 size;
uniform vec2 response; // integrated emission, intensity
void main(){
    vec2 uv=gl_FragCoord.xy/size;
    vec2 stepUV=vec2(max(1.0,size.y/480.0))/size;
    vec3 c=texture2D(sourceImage,uv).rgb;
    vec3 hi=max(max(texture2D(sourceImage,uv+vec2(stepUV.x,0.0)).rgb,
                    texture2D(sourceImage,uv-vec2(stepUV.x,0.0)).rgb),
                max(texture2D(sourceImage,uv+vec2(0.0,stepUV.y)).rgb,
                    texture2D(sourceImage,uv-vec2(0.0,stepUV.y)).rgb));
    // Confine the residual light to this frame's edges. Solid black cuts
    // stay black and passing objects cannot leave a detached silhouette.
    vec3 last=min(hi,texture2D(historyImage,uv).rgb);
    vec3 residual=max(last*last-c*c,vec3(0.0));
    vec3 emitted=sqrt(c*c+response.x*residual);
    gl_FragColor=vec4(min(emitted,c+vec3(0.12*response.y)),1.0);
}
)GLSL";
