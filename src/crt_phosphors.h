// GLSL 1.20 compatibility shader. Raster coordinates follow the video signal.
// The phosphor mask stays in screen coordinates as the game camera moves.
static constexpr const char* mask_fragment=R"GLSL(
uniform vec3 tvSettings;
const float tau=6.2831853;
// Approximate spatial integration over one output pixel. Suppress structure
// above the display's Nyquist limit rather than aliasing it into false stripes.
float visible(float cycles){
    float a=3.14159265*cycles;
    return sin(a)/max(a,0.0001)*(1.0-smoothstep(0.32,0.50,cycles));
}
vec3 phosphor(vec2 frag,float height,float model){
    float freq=(model<1.5?375.0:model<2.5?320.0:305.0)/height;
    vec2 p=frag*freq;
    vec3 phase=tau*(p.x-vec3(0.0,1.0/3.0,2.0/3.0));
    // Gaussian blur affects the mask alone, with greater attenuation at higher harmonics.
    float blur=mix(0.29,0.025,tvSettings.z);
    float fundamental=exp(-19.7392*blur*blur);
    vec3 stripe=cos(phase)*visible(freq)*fundamental
        +0.18*cos(2.0*phase)*visible(2.0*freq)*pow(fundamental,4.0);
    if(model<1.5)return stripe; // continuous aperture grille
    if(model<2.5){
        // Bridges interrupt the slots, staggered between adjacent columns.
        float y=tau*p.y*0.68;
        vec3 stagger=cos(0.5*phase);
        return 0.80*stripe-0.28*stagger*cos(y)*fundamental*visible(freq*0.68)*visible(freq*0.5);
    }
    // Three reciprocal-lattice directions give round peaks on a triangular
    // lattice. Offset the RGB peaks to the corners of each phosphor trio.
    vec3 v=tau*(p.y*0.8660254-vec3(0.0,0.0,0.5));
    return (cos(phase)+cos(0.5*phase+v)+cos(0.5*phase-v))*(fundamental*visible(freq)/2.0);
}
)GLSL";
static constexpr const char* television_fragment=R"GLSL(
uniform sampler2D sourceImage;
#ifdef TV_SCREEN
uniform sampler2D rowImage;
#endif
uniform vec2 size,grid;
uniform vec4 strengths; // raster sampling, dither, beam contrast, mask
uniform vec4 tube;      // glow, reserved, horizontal softness, tube model


vec3 signalSample(vec2 at){
    vec3 c=texture2D(sourceImage,(at+0.5)/grid).rgb;
    vec2 q=mod(vec2(at.x,grid.y-1.0-at.y),4.0);
    vec2 p=mod(q,2.0);
    float offset=-4.0+4.0*p.x+6.0*p.y-8.0*p.x*p.y+mod(floor(q.x/2.0)+floor(q.y/2.0),2.0);
    vec3 quant=floor(clamp(floor(c*255.0+0.5)+offset,0.0,255.0)/8.0)/31.0;
    c=mix(c,quant,strengths.y);
    return c*c;
}

// Continuous horizontal video bandwidth, with a wider spot on bright lines.
vec3 scan(vec2 pos,float row){
    float x=floor(pos.x),f=pos.x-x;
    vec3 delta=vec3(f+1.0,f,f-1.0);
    float sigma=0.46+0.30*tube.z;
    vec3 w=exp(-0.5*delta*delta/(sigma*sigma));
    float tail=exp(-0.5*(f-2.0)*(f-2.0)/(sigma*sigma));
    return (signalSample(vec2(x-1.0,row))*w.x+signalSample(vec2(x,row))*w.y+
            signalSample(vec2(x+1.0,row))*w.z+signalSample(vec2(x+2.0,row))*tail)/(w.x+w.y+w.z+tail);
}

void main(){
    vec2 uv=gl_FragCoord.xy/size,pos=uv*grid-0.5;
#ifdef TV_ROWS
    gl_FragColor=vec4(scan(pos,floor(gl_FragCoord.y)-1.0),1.0);
#else
    float base=floor(pos.y+0.5);
    vec3 emitted=vec3(0.0);
    float total=0.0;
    for(int i=-1;i<=1;i++){
        float row=base+float(i),dy=pos.y-row;
#ifdef TV_SCREEN
        vec3 signal=texture2D(rowImage,vec2(uv.x,(row+1.5)/(grid.y+2.0))).rgb;
#else
        vec3 signal=scan(pos,row);
#endif
        float luma=dot(signal,vec3(0.2126,0.7152,0.0722));
        float sigma=0.28+0.13*sqrt(luma)+tube.x*0.09;
        float variance=sigma*sigma+grid.y*grid.y/(12.0*size.y*size.y);
        float beam=exp(-0.5*dy*dy/variance)/sqrt(6.2831853*variance);
        emitted+=signal*beam;
        total+=beam;
    }
    vec3 native=texture2D(sourceImage,uv).rgb;
    vec3 light=mix(native*native,emitted/max(total,0.01),strengths.x);
    light*=mix(1.0,total,strengths.z*visible(grid.y/size.y));
    float model=tvSettings.x>0.0?tvSettings.x:tube.w;
    float amount=tvSettings.x>0.0?0.65:strengths.w;
    light*=1.0+0.70*amount*tvSettings.y*phosphor(gl_FragCoord.xy,size.y,model);
    gl_FragColor=vec4(sqrt(clamp(light,0.0,1.0)),1.0);
#endif
}
)GLSL";
