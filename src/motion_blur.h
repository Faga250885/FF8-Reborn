// Retain one unfiltered, successfully presented image. Keeping the raw frame
// prevents repeated presentations or a stopped camera from accumulating ghosts.
struct MotionHistory {
    Surface raw, signal;
    int strength=0, scene=-1, previous_scene=-1, w=0, h=0;
    int failed_signal_w=0,failed_signal_h=0;
    HDC window=nullptr;
    double time=0, previous_time=0;
    float exposure=0;
    bool pending=false;

    void reset(){
        strength=0;previous_time=0;pending=false;exposure=0;
    }
    void begin(int amount,int key,HDC handle,int width,int height,double stamp){
        const double dt=stamp-previous_time;
        const bool consecutive=previous_time>0&&dt>0&&dt<.10&&key==previous_scene&&
                               handle==window&&width==w&&height==h;
        reset();
        strength=key>=0?amount:0;
        if(strength&&consecutive){
            const double level=.01*strength;
            // Integrate a short exposure over the actual presentation interval.
            // A low setting stays light while the high end can span most of a
            // 60 Hz refresh. Never give the older image more weight than this one.
            const double decay=.004+.016*level;
            exposure=float(std::min(.46,level*decay*(-std::expm1(-dt/decay))/dt));
        }
        scene=key;window=handle;w=width;h=height;time=stamp;
    }
    void commit(bool swapped){
        if(pending&&swapped){previous_time=time;previous_scene=scene;}
        pending=false;
    }
};
static MotionHistory motion_history;

static constexpr char motion_fragment[]=R"GLSL(
uniform sampler2D sourceImage;
uniform sampler2D historyImage;
uniform vec2 size;
uniform vec2 response; // exposure, strength
void main(){
    vec2 uv=gl_FragCoord.xy/size;
    vec3 c=texture2D(sourceImage,uv).rgb;
    vec3 last=texture2D(historyImage,uv).rgb;
    vec3 difference=abs(last-c);
    // Preserve all stationary pixels (including text and HUD) exactly and
    // avoid fetching their neighborhood on a stable area of the screen.
    if(max(difference.r,max(difference.g,difference.b))<.001){
        gl_FragColor=vec4(c,1.);return;
    }
    float level=response.y;
    float radius=max(1.,size.y/480.)*mix(.8,2.8,level);
    vec2 stepUV=vec2(radius)/size;
    vec3 a=texture2D(sourceImage,uv+vec2(stepUV.x,0.)).rgb;
    vec3 b=texture2D(sourceImage,uv-vec2(stepUV.x,0.)).rgb;
    vec3 d=texture2D(sourceImage,uv+vec2(0.,stepUV.y)).rgb;
    vec3 e=texture2D(sourceImage,uv-vec2(0.,stepUV.y)).rgb;
    vec3 lo=min(c,min(min(a,b),min(d,e)));
    vec3 hi=max(c,max(max(a,b),max(d,e)));
    // Clip the old sample to the current neighborhood: an edge that left
    // this area cannot reappear, and a cut to a solid color is immediate.
    // Clip along the color difference vector to avoid inventing new hues.
    vec3 range=mix(c-lo,hi-c,step(c,last));
    vec3 limit=range/max(difference,vec3(.0001));
    limit=mix(vec3(1.),limit,step(vec3(.0001),difference));
    last=mix(c,last,min(1.,min(limit.r,min(limit.g,limit.b))));
    vec3 blended=sqrt(mix(c*c,last*last,response.x));
    float peak=max(abs(blended.r-c.r),max(abs(blended.g-c.g),abs(blended.b-c.b)));
    float cap=.42*level;
    gl_FragColor=vec4(mix(c,blended,min(1.,cap/max(.0001,peak))),1.);
}
)GLSL";
