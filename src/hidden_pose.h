// Locally rigid completion of a partially visible mesh. Native corners fix the
// boundary. Solve only the small region next to vertices missing at the other
// endpoint. The world coordinate basis follows the measured battle projection.
using Point3=std::array<double,3>;
using Rotation=std::array<double,4>; // (w,x,y,z)
static Point3 view_point(const std::array<float,4>&p){
    double z=1./p[3];
    return {(p[0]-320.)*z/1024.,(p[1]-240.)*z/1024.,z};
}
static Point3 diff3(const Point3&a,const Point3&b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
static double dot3(const Point3&a,const Point3&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static Point3 cross3(const Point3&a,const Point3&b){
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
static Point3 rotate3(const Rotation&q,const Point3&p){
    Point3 v={q[1],q[2],q[3]},x=cross3(v,p),y=cross3(v,x);
    return {p[0]+2*(q[0]*x[0]+y[0]),p[1]+2*(q[0]*x[1]+y[1]),p[2]+2*(q[0]*x[2]+y[2])};
}
struct PoseEdge{unsigned other;Point3 rest;double weight;};
static Rotation fitted_rotation(const std::vector<PoseEdge>&edges,const std::vector<Point3>&position,unsigned i,Rotation q){
    double s[3][3]{};
    for(const auto&e:edges){
        Point3 d=diff3(position[i],position[e.other]);
        for(int j=0;j<3;j++)for(int k=0;k<3;k++)s[j][k]+=e.weight*e.rest[j]*d[k];
    }
    double trace=s[0][0]+s[1][1]+s[2][2];
    double n[4][4]={{trace,s[1][2]-s[2][1],s[2][0]-s[0][2],s[0][1]-s[1][0]}};
    for(int j=0;j<3;j++){
        n[j+1][0]=n[0][j+1];
        for(int k=0;k<3;k++)n[j+1][k+1]=s[j][k]+s[k][j]-(j==k?trace:0.);
    }
    double bound=0;
    for(const auto&row:n){double sum=0;for(double value:row)sum+=fabs(value);bound=std::max(bound,sum);}
    if(bound<1e-12)return q;
    for(int iteration=0;iteration<6;iteration++){
        Rotation next{};double length=0;
        for(int j=0;j<4;j++){next[j]=bound*q[j];for(int k=0;k<4;k++)next[j]+=n[j][k]*q[k];length+=next[j]*next[j];}
        if(length<1e-16)break;
        double scale=1/std::sqrt(length);for(int j=0;j<4;j++)q[j]=next[j]*scale;
    }
    return q;
}
static void complete_hidden_pose(SurfaceGraph&graph){
    const unsigned count=(unsigned)graph.nodes.size();
    std::vector<Point3> rest(count),position(count);
    std::vector<unsigned> queue,unknown,active;
    std::vector<BYTE> reached(count),needs_rotation(count);
    for(unsigned i=0;i<count;i++){
        const auto&node=graph.nodes[i];rest[i]=view_point(node.point);position[i]=rest[i];
        if(node.partner>=0||node.native){position[i]=view_point(node.destination);queue.push_back(i);reached[i]=1;}
    }
    for(size_t at=0;at<queue.size();at++){
        unsigned i=queue[at];auto delta=diff3(position[i],rest[i]);
        for(unsigned j:graph.nodes[i].adjacent)if(!reached[j]){
            for(int k=0;k<3;k++)position[j][k]=rest[j][k]+delta[k];
            queue.push_back(j);reached[j]=1;
        }
    }
    for(unsigned i=0;i<count;i++)if(reached[i]&&graph.nodes[i].partner<0&&!graph.nodes[i].native){
        unknown.push_back(i);needs_rotation[i]=1;
        for(unsigned j:graph.nodes[i].adjacent)needs_rotation[j]=1;
        if(graph.nodes[i].predicted)position[i]=view_point(graph.nodes[i].destination);
    }
    if(unknown.empty())return;
    std::vector<std::vector<PoseEdge>> edges(count);
    for(unsigned i=0;i<count;i++)if(needs_rotation[i]){
        active.push_back(i);
        for(unsigned j:graph.nodes[i].adjacent){
            Point3 d=diff3(rest[i],rest[j]);double length=std::sqrt(dot3(d,d));
            if(length>.001)edges[i].push_back({j,d,1./length});
        }
    }
    std::vector<Rotation> rotations(count,Rotation{1,0,0,0});
    std::vector<Point3> offset(count);
    // Alternating local rotations and constrained position relaxation.
    // Anchored native positions never participate in the position update.
    for(int pass=0;pass<7;pass++){
        if(matching_checkpoint)matching_checkpoint();
        for(unsigned i:active)rotations[i]=fitted_rotation(edges[i],position,i,rotations[i]);
        for(unsigned i:unknown){
            offset[i]={};
            for(const auto&e:edges[i]){
                auto a=rotate3(rotations[i],e.rest),b=rotate3(rotations[e.other],e.rest);
                for(int k=0;k<3;k++)offset[i][k]+=.5*e.weight*(a[k]+b[k]);
            }
        }
        for(int iteration=0;iteration<4;iteration++)for(unsigned i:unknown){
            Point3 sum=offset[i];double weights=0;
            for(const auto&e:edges[i]){
                for(int k=0;k<3;k++)sum[k]+=e.weight*position[e.other][k];
                weights+=e.weight;
            }
            if(weights>0)for(int k=0;k<3;k++)position[i][k]=sum[k]/weights;
        }
    }
    for(unsigned i:unknown){
        auto&node=graph.nodes[i];const auto&p=position[i];
        if(p[2]<1||!std::isfinite(p[2]))continue;
        double x=320.+1024.*p[0]/p[2],y=240.+1024.*p[1]/p[2];
        if(!std::isfinite(x)||!std::isfinite(y)||std::hypot(x-node.point[0],y-node.point[1])>220)continue;
        node.destination={(float)x,(float)y,node.point[2],(float)(1./p[2])};node.predicted=true;
    }
}
