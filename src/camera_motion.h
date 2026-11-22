// Common camera motion in homogeneous screen coordinates, estimated only
// from unique primitive identities. Independent actor motion is downweighted.
struct CameraMotion {double matrix[4][3]{};double confidence=0;unsigned points=0;bool valid=false;};
static CameraMotion camera_motion;
static bool camera_predict(const float*p,float*out){
    memcpy(out,p,4*sizeof(float));if(!camera_motion.valid||p[3]<=0||p[3]>=.1f)return false;
    double source[]={p[0]/double(p[3]),p[1]/double(p[3]),1./p[3],1},dest[3]{};
    for(int j=0;j<3;j++)for(int k=0;k<4;k++)dest[j]+=source[k]*camera_motion.matrix[k][j];
    if(!std::isfinite(dest[2])||dest[2]<=1)return false;
    out[0]=(float)(dest[0]/dest[2]);out[1]=(float)(dest[1]/dest[2]);out[3]=(float)(1/dest[2]);
    return std::isfinite(out[0])&&std::isfinite(out[1]);
}
static bool solve_camera(double normal[4][4],double rhs[4][3],double solution[4][3]){
    double a[4][7];for(int i=0;i<4;i++){for(int j=0;j<4;j++)a[i][j]=normal[i][j];for(int j=0;j<3;j++)a[i][j+4]=rhs[i][j];}
    for(int c=0;c<4;c++){int pivot=c;for(int r=c+1;r<4;r++)if(fabs(a[r][c])>fabs(a[pivot][c]))pivot=r;
        if(fabs(a[pivot][c])<1e-8)return false;for(int j=0;j<7;j++)std::swap(a[c][j],a[pivot][j]);
        double factor=a[c][c];for(int j=0;j<7;j++)a[c][j]/=factor;
        for(int r=0;r<4;r++)if(r!=c){factor=a[r][c];for(int j=0;j<7;j++)a[r][j]-=factor*a[c][j];}}
    for(int i=0;i<4;i++)for(int j=0;j<3;j++)solution[i][j]=a[i][j+4];return true;
}
template<class Groups>
static void fit_camera_motion(const std::vector<Primitive>&old,const std::vector<Primitive>&cur,
    const Groups&old_groups,const Groups&new_groups){
    camera_motion=CameraMotion{};
    struct Sample{double source[3],target[3],design[4],weight,robust=1,error=1e9;float screen[4];};
    std::vector<Sample> samples;
    for(const auto&group:new_groups){if(group.second.size()!=1)continue;auto found=old_groups.find(group.first);if(found==old_groups.end()||found->second.size()!=1)continue;
        const auto&a=old[found->second[0]],&b=cur[group.second[0]];const auto&before=previous.ops[a.op];const auto&after=current.ops[b.op];
        if(!before.state.program||!after.state.program||!zero_model(before.state)||!zero_model(after.state))continue;
        const Vertex*p=before.vertices.data()+a.start,*q=after.vertices.data()+b.start;bool valid=true;
        for(int i=0;i<3;i++)valid&=p[i].p[3]>0&&p[i].p[3]<.1f&&q[i].p[3]>0&&q[i].p[3]<.1f;if(!valid)continue;
        double area=fabs(double(q[1].p[0]-q[0].p[0])*(q[2].p[1]-q[0].p[1])-double(q[1].p[1]-q[0].p[1])*(q[2].p[0]-q[0].p[0]))*.5;
        if(area<.25)continue;
        for(int i=0;i<3;i++){Sample sample{};sample.source[0]=q[i].p[0]/double(q[i].p[3]);sample.source[1]=q[i].p[1]/double(q[i].p[3]);sample.source[2]=1./q[i].p[3];
            sample.target[0]=p[i].p[0]/double(p[i].p[3]);sample.target[1]=p[i].p[1]/double(p[i].p[3]);sample.target[2]=1./p[i].p[3];
            sample.weight=std::min(area,10000.)/3;sample.robust=1;memcpy(sample.screen,p[i].p,sizeof(sample.screen));samples.push_back(sample);}
    }
    if(samples.size()<36)return;
    // Bound the fitting cost while favouring large visible surfaces.
    if(samples.size()>2048){std::stable_sort(samples.begin(),samples.end(),[](const Sample&a,const Sample&b){return a.weight>b.weight;});samples.resize(2048);}
    double center[3]{},scale[3]{};
    for(const auto&s:samples)for(int j=0;j<3;j++)center[j]+=s.source[j]/samples.size();
    for(const auto&s:samples)for(int j=0;j<3;j++){double d=s.source[j]-center[j];scale[j]+=d*d/samples.size();}
    for(int j=0;j<3;j++){scale[j]=std::sqrt(scale[j]);if(scale[j]<1e-8)return;}
    for(auto&s:samples){for(int j=0;j<3;j++)s.design[j]=(s.source[j]-center[j])/scale[j];s.design[3]=1;}
    // Adjacent triangles often repeat both endpoints of a corner. Their
    // fit weights still contribute individually in the original order, but
    // their projected error is identical on every solver iteration.
    std::vector<unsigned> owner(samples.size()),unique;
    size_t capacity=2;while(capacity<2*samples.size())capacity*=2;
    std::vector<unsigned> slots(capacity);
    for(unsigned i=0;i<samples.size();i++){
        const auto& s=samples[i];
        uint64_t key=hash(1469598103934665603ULL,s.source,sizeof(s.source));
        key=hash(key,s.screen,sizeof(s.screen));
        size_t slot=(key^(key>>32))&(capacity-1);
        while(slots[slot]){
            const auto& other=samples[slots[slot]-1];
            if(!memcmp(other.source,s.source,sizeof(s.source))&&!memcmp(other.screen,s.screen,sizeof(s.screen)))break;
            slot=(slot+1)&(capacity-1);
        }
        if(!slots[slot]){slots[slot]=i+1;unique.push_back(i);}
        owner[i]=slots[slot]-1;
    }
    double solution[4][3]{};
    for(int iteration=0;iteration<24;iteration++){
        double normal[4][4]{},rhs[4][3]{};
        for(unsigned n=0;n<samples.size();n++){const auto& s=samples[n];double weight=s.weight*samples[owner[n]].robust;for(int i=0;i<4;i++){
            double term=weight*s.design[i];for(int j=0;j<4;j++)normal[i][j]+=term*s.design[j];for(int j=0;j<3;j++)rhs[i][j]+=term*s.target[j];}}
        double last_solution[4][3];
        memcpy(last_solution,solution,sizeof(solution));
        if(!solve_camera(normal,rhs,solution))return;
        // Identical coefficients give identical residuals and robust weights.
        // Once the floating-point solution stops changing, every remaining
        // iteration would repeat the same normal equations.
        if(iteration&&memcmp(last_solution,solution,sizeof(solution))==0)break;
        for(unsigned index:unique){auto& s=samples[index];double dest[3]{};for(int j=0;j<3;j++)for(int k=0;k<4;k++)dest[j]+=s.design[k]*solution[k][j];
            if(dest[2]<=1){s.error=1e6;s.robust=1e-12;continue;}
            double dx=dest[0]/dest[2]-s.screen[0],dy=dest[1]/dest[2]-s.screen[1],depth=250*std::log((1/dest[2])/s.screen[3]);
            s.error=std::sqrt(dx*dx+dy*dy+depth*depth);s.robust=1/(1+(s.error/.75)*(s.error/.75));}
    }
    double total=0,inliers=0;for(unsigned i=0;i<samples.size();i++){total+=samples[i].weight;if(samples[owner[i]].error<1)inliers+=samples[i].weight;}
    camera_motion.confidence=inliers/total;camera_motion.points=(unsigned)samples.size();
    if(camera_motion.confidence<.85)return;
    for(int j=0;j<3;j++){camera_motion.matrix[3][j]=solution[3][j];for(int k=0;k<3;k++){
        camera_motion.matrix[k][j]=solution[k][j]/scale[k];camera_motion.matrix[3][j]-=center[k]*camera_motion.matrix[k][j];}}
    camera_motion.valid=true;
}
