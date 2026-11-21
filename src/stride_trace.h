// Opt-in local comparison of actual submitted world-player vertices. This
// samples the draw stream, without a GPU readback or a change to the timer.
namespace stride_trace {
static bool enabled=false;
static float x[3],shift;
static unsigned vertices;
static void begin(float offset){
    vertices=0;shift=offset;std::fill(x,x+3,NAN);
}
static void observe(const Op& op,const Vertex* drawn){
    const int u[]={50,37,44},v[]={12,63,33};
    for(unsigned i=0;i<op.actor_mid.size();i++)if(op.actor_mid[i].player){
        vertices++;const auto& uv=op.vertices[i].uv[0];
        for(int j=0;j<3;j++)if(!std::isfinite(x[j])&&uv[0]*256==u[j]&&uv[1]*256==v[j])
            x[j]=drawn[i].p[0];
    }
}
static void present(double t,unsigned source,float alpha){
    static FILE* file=nullptr;static unsigned rows=0;
    if(!file){file=fopen("FF8_stride_trace.csv","w");if(!file){enabled=false;return;}
        fputs("time,source,alpha,actor_corners,shift,torso1,torso2,torso3\n",file);}
    fprintf(file,"%.7f,%u,%.5f,%u,%.6f,%.6f,%.6f,%.6f\n",t,source,alpha,
        alpha<0?0:vertices,shift,x[0],x[1],x[2]);
    if(++rows%120==0)fflush(file);
    if(rows==36000){fclose(file);enabled=false;}
}
}
