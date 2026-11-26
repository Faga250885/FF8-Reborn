// Common camera motion in homogeneous screen coordinates, estimated only
// from unique primitive identities. Independent actor motion is downweighted.
struct CameraMotion {double matrix[4][3]{};double confidence=0;unsigned points=0;bool valid=false,identity=false;};
static CameraMotion camera_motion;
static bool camera_predict(const float*p,float*out,const CameraMotion& motion=camera_motion){
    memcpy(out,p,4*sizeof(float));if(!motion.valid||p[3]<=0||p[3]>=.1f)return false;
    double source[]={p[0]/double(p[3]),p[1]/double(p[3]),1./p[3],1},dest[3]{};
    for(int j=0;j<3;j++)for(int k=0;k<4;k++)dest[j]+=source[k]*motion.matrix[k][j];
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
    const Groups&old_groups,const Groups&new_groups,const Frame& before_frame,const Frame& after_frame,CameraMotion& motion){
    motion=CameraMotion{};
    struct Sample{double source[3],target[3],design[4],weight,robust=1,error=1e9;float screen[4];};
    constexpr unsigned max_samples=2048;
    struct TriangleSample {
        const Vertex* previous;
        const Vertex* current;
        double weight;
        unsigned order;
    };
    std::vector<TriangleSample> triangles;
    triangles.reserve(std::min<size_t>(new_groups.size(),max_samples));
    for(const auto&group:new_groups){if(group.second.size()!=1)continue;auto found=old_groups.find(group.first);if(found==old_groups.end()||found->second.size()!=1)continue;
        const auto&a=old[found->second[0]],&b=cur[group.second[0]];const auto&before=before_frame.ops[a.op];const auto&after=after_frame.ops[b.op];
        if(!before.state.program||!after.state.program||!zero_model(before.state)||!zero_model(after.state))continue;
        const Vertex*p=before.vertices.data()+a.start,*q=after.vertices.data()+b.start;bool valid=true;
        for(int i=0;i<3;i++)valid&=p[i].p[3]>0&&p[i].p[3]<.1f&&q[i].p[3]>0&&q[i].p[3]<.1f;if(!valid)continue;
        double area=fabs(double(q[1].p[0]-q[0].p[0])*(q[2].p[1]-q[0].p[1])-double(q[1].p[1]-q[0].p[1])*(q[2].p[0]-q[0].p[0]))*.5;
        if(area<.25)continue;
        triangles.push_back({p,q,std::min(area,10000.)/3,unsigned(triangles.size())});
    }
    if(triangles.size()<12)return;
    if(triangles.size()*3>max_samples){
        // All three corners have the same weight. Keep precisely the largest
        // original samples, including the encounter order at the cutoff.
        auto heavier=[](const TriangleSample& a,const TriangleSample& b){
            if(a.weight!=b.weight)return a.weight>b.weight;
            return a.order<b.order;
        };
        auto cutoff=triangles.begin()+(max_samples+2)/3;
        std::nth_element(triangles.begin(),cutoff,triangles.end(),heavier);
        triangles.erase(cutoff,triangles.end());
        std::sort(triangles.begin(),triangles.end(),heavier);
    }
    std::vector<Sample> samples;
    samples.reserve(std::min<size_t>(max_samples,3*triangles.size()));
    for(const auto& triangle:triangles){
        const auto* p=triangle.previous;
        const auto* q=triangle.current;
        for(int i=0;i<3&&samples.size()<max_samples;i++){Sample sample{};sample.source[0]=q[i].p[0]/double(q[i].p[3]);sample.source[1]=q[i].p[1]/double(q[i].p[3]);sample.source[2]=1./q[i].p[3];
            sample.target[0]=p[i].p[0]/double(p[i].p[3]);sample.target[1]=p[i].p[1]/double(p[i].p[3]);sample.target[2]=1./p[i].p[3];
            sample.weight=triangle.weight;sample.robust=1;memcpy(sample.screen,p[i].p,sizeof(sample.screen));samples.push_back(sample);}
    }
    double center[3]{},scale[3]{};
    for(const auto&s:samples)for(int j=0;j<3;j++)center[j]+=s.source[j]/samples.size();
    for(const auto&s:samples)for(int j=0;j<3;j++){double d=s.source[j]-center[j];scale[j]+=d*d/samples.size();}
    for(int j=0;j<3;j++){scale[j]=std::sqrt(scale[j]);if(scale[j]<1e-8)return;}
    for(auto&s:samples){for(int j=0;j<3;j++)s.design[j]=(s.source[j]-center[j])/scale[j];s.design[3]=1;}
    // A parked world camera often supplies exactly the same landmarks at both
    // endpoints. Verify the same weighted system has full rank before accepting
    // an identity transform. Coplanar terrain alone cannot identify a camera.
    if(deck_renderer&&display_settings.fps==60&&std::all_of(samples.begin(),samples.end(),[](const Sample& s){
        return s.source[0]==s.target[0]&&s.source[1]==s.target[1]&&s.source[2]==s.target[2]&&
               std::isfinite(s.source[0])&&std::isfinite(s.source[1])&&std::isfinite(s.weight);
    })){
        double normal[4][4]{},rhs[4][3]{},solution[4][3]{};
        for(const auto& sample:samples)for(unsigned i=0;i<4;i++){
            const double weight=sample.weight*sample.design[i];
            for(unsigned j=0;j<4;j++)normal[i][j]+=weight*sample.design[j];
        }
        if(!solve_camera(normal,rhs,solution))return;
        for(unsigned i=0;i<3;i++)motion.matrix[i][i]=1.;
        motion.points=unsigned(samples.size());motion.confidence=1.;
        motion.valid=motion.identity=true;
        return;
    }
    // Adjacent triangles often repeat both endpoints of a corner. Their
    // fit weights still contribute individually in the original order, but
    // their projected error is identical on every solver iteration.
    std::vector<unsigned> owner(samples.size()),unique;
    size_t capacity=2;while(capacity<2*samples.size())capacity*=2;
    std::vector<unsigned> slots(capacity);
    for(unsigned i=0;i<samples.size();i++){
        const auto& s=samples[i];
        uint32_t key=2166136261u;
        auto mix_words=[&](const void* data,unsigned size){
            const auto* bytes=static_cast<const unsigned char*>(data);
            for(unsigned j=0;j<size;j+=4){
                uint32_t word;memcpy(&word,bytes+j,sizeof(word));
                key=(key^word)*16777619u;
                key^=key>>16;
            }
        };
        mix_words(s.source,sizeof(s.source));mix_words(s.screen,sizeof(s.screen));
        size_t slot=key&(capacity-1);
        while(slots[slot]){
            const auto& other=samples[slots[slot]-1];
            if(!memcmp(other.source,s.source,sizeof(s.source))&&!memcmp(other.screen,s.screen,sizeof(s.screen)))break;
            slot=(slot+1)&(capacity-1);
        }
        if(!slots[slot]){slots[slot]=i+1;unique.push_back(i);}
        owner[i]=slots[slot]-1;
    }
    // Each iteration uses the same four design columns and three targets.
    // Pack them together once. The eighth lane pads the seven sums to pairs
    // of doubles for SSE2. Each sum still visits samples in original order.
    struct Equation {double columns[8];};
    std::vector<Equation> equations(samples.size());
    std::vector<double> weights(samples.size());
    for(unsigned n=0;n<samples.size();n++){
        auto& c=equations[n].columns;
        std::copy_n(samples[n].design,4,c);
        std::copy_n(samples[n].target,3,c+4);
        c[7]=0;
    }
    double solution[4][3]{};
    unsigned stable_iterations=0;
    for(int iteration=0;iteration<24;iteration++){
        double normal[4][4]{},rhs[4][3]{};
        for(unsigned n=0;n<samples.size();n++)
            weights[n]=samples[n].weight*samples[owner[n]].robust;
        for(int i=0;i<4;i++){
            double sum[8]{};
            for(unsigned n=0;n<equations.size();n++){
                const auto& c=equations[n].columns;
                double term=weights[n]*c[i];
                for(int j=0;j<8;j++)sum[j]+=term*c[j];
            }
            std::copy_n(sum,4,normal[i]);
            std::copy_n(sum+4,3,rhs[i]);
        }
        double last_solution[4][3];
        memcpy(last_solution,solution,sizeof(solution));
        if(!solve_camera(normal,rhs,solution))return;
        // Identical coefficients give identical residuals and robust weights.
        // Once the floating-point solution stops changing, every remaining
        // iteration would repeat the same normal equations.
        if(iteration&&memcmp(last_solution,solution,sizeof(solution))==0)break;
        double weight_change=0;
        for(unsigned index:unique){auto& s=samples[index];double last_weight=s.robust;
            double dest[3]{};for(int j=0;j<3;j++)for(int k=0;k<4;k++)dest[j]+=s.design[k]*solution[k][j];
            if(dest[2]<=1){s.error=1e6;s.robust=1e-12;weight_change=std::max(weight_change,fabs(s.robust-last_weight));continue;}
            double dx=dest[0]/dest[2]-s.screen[0],dy=dest[1]/dest[2]-s.screen[1],depth=250*std::log((1/dest[2])/s.screen[3]);
            s.error=std::sqrt(dx*dx+dy*dy+depth*depth);s.robust=1/(1+(s.error/.75)*(s.error/.75));
            weight_change=std::max(weight_change,fabs(s.robust-last_weight));}
        // Stop when every contributing corner has kept its weight to twelve
        // decimal places for two successive fits. Outliers still get all
        // 24 iterations while their influence on the camera is changing.
        if(deck_renderer&&weight_change<1e-12){if(++stable_iterations==2)break;}
        else stable_iterations=0;
    }
    double total=0,inliers=0;for(unsigned i=0;i<samples.size();i++){total+=samples[i].weight;if(samples[owner[i]].error<1)inliers+=samples[i].weight;}
    motion.confidence=inliers/total;motion.points=(unsigned)samples.size();
    if(motion.confidence<.85)return;
    for(int j=0;j<3;j++){motion.matrix[3][j]=solution[3][j];for(int k=0;k<3;k++){
        motion.matrix[k][j]=solution[k][j]/scale[k];motion.matrix[3][j]-=center[k]*motion.matrix[k][j];}}
    motion.valid=true;
}
