// World character faces use one ordering depth for all three corners. The
// world projector supplies a separate indexed XY and depth for each corner.
// Match in packet coordinates, then carry the face's depth bias with the pose.
namespace world_actor {
struct Corner {const BoneSample* here;const BoneSample* there;const BoneSample* mid;std::array<std::array<float,2>,3> slopes{};float offset=0;};
using ByXY=std::unordered_map<uint32_t,std::vector<Corner>>;
static uint32_t xy(int x,int y){return uint16_t(x)|(uint32_t(uint16_t(y))<<16);}
using Samples=std::vector<const BoneSample*>;
// The simulation can project another animation step before the GL swap. Its
// coordinates belong to the same indices but not necessarily to this picture.
// Vote on complete submitted triangles, then use a single pass even for the
// hidden corners that have no screen evidence of their own.
struct Poses {std::vector<Samples> passes;unsigned shown=0;};
static Poses displayed_pose(const Frame& frame){
    Poses result;auto& passes=result.passes;
    std::unordered_map<uint64_t,unsigned> occurrences;
    for(const auto& p:frame.bones)if(p.group&0x80000000u){
        unsigned n=occurrences[(uint64_t(p.group)<<32)|p.address]++;
        if(n>=8)return {};
        if(n>=passes.size())passes.resize(n+1);
        passes[n].push_back(&p);
    }
    if(passes.empty())return {};
    if(passes.size()==1)return result;
    unsigned best=0,winner=0;
    for(unsigned n=0;n<passes.size();n++){
        std::unordered_map<uint32_t,std::vector<unsigned>> depths;
        for(auto p:passes[n])if(p->z)depths[xy(p->x,p->y)].push_back(p->z);
        unsigned votes=0;
        for(const auto& op:frame.ops)if(opaque_projected(op)&&WideView::uses_texture(op.state)){
            for(unsigned i=0;i+2<op.vertices.size();i+=3){
                const auto* v=&op.vertices[i];
                if(v[0].p[3]<=0||v[0].p[3]>=.1f||v[1].p[3]!=v[0].p[3]||v[2].p[3]!=v[0].p[3])continue;
                double depth=8./v[0].p[3];
                unsigned hits=0;
                for(;hits<3;hits++){
                    float x=v[hits].p[0]*4,y=(v[hits].p[1]-16)*4;
                    if(x<INT16_MIN||x>INT16_MAX||y<INT16_MIN||y>INT16_MAX||x!=int(x)||y!=int(y))break;
                    auto f=depths.find(xy(int(x),int(y)));
                    if(f==depths.end()||std::none_of(f->second.begin(),f->second.end(),
                        [&](unsigned z){return std::abs(z-depth)<512;}))break;
                }
                if(hits==3)++votes;
            }
        }
        if(votes>best){best=votes;winner=n;}
    }
    if(best<4)return {};
    result.shown=winner;return result;
}
static unsigned link_count=0,departing=0;
// Work in screen space, where the chase camera has already been applied.
// Accumulate the same indexed torso points in all poses, including hidden
// ones. Visibility changes at the silhouette must not move the reference.
struct LateralStride {
    double first=0,second=0;
    std::array<double,3> velocity{};
    unsigned count=0;
    float foot_min=0,foot_max=0;
    void observe(float y,float a,float b,const Corner& c){
        if(y>.65f){foot_min=std::min(foot_min,a+b);foot_max=std::max(foot_max,a+b);}
        if(y<.20f||y>.48f)return;
        first+=a;second+=b;count++;
        for(unsigned i=0;i<3;i++)velocity[i]+=c.slopes[i][0];
    }
    void apply(ByXY& points) const {
        if(count<12||foot_max-foot_min<4||first*second>=0)return;
        const float speed=float((first+second)/(2.0*count));
        const float offset=speed-float(first/count);
        if(std::abs(offset)<.125f)return;
        for(auto& entry:points)for(auto& c:entry.second)if(c.mid){
            c.offset=offset;
            for(unsigned i=0;i<3;i++)c.slopes[i][0]+=speed-float(velocity[i]/count);
        }
    }
};
// Two subtractive strips under the player's feet. The game recycles their
// texture handle. A terrain-camera prediction makes the shadow slide sideways
// during interpolation and snap back on the very next native picture.
static bool shadow_strip(const Op& op){
    const auto& s=op.state;
    if((op.vertices.size()!=6&&op.vertices.size()!=12)||!projected_geometry(op)||
       !s.blend||s.depth_mask||s.src_rgb!=GL_ONE||s.dst_rgb!=GL_ONE||
       s.eq_rgb!=GL_FUNC_REVERSE_SUBTRACT||!WideView::uses_texture(s))return false;
    const int us[]={192,207,192,207,207,192}, vs[]={7,0,0,7,0,7};
    for(size_t i=0;i<op.vertices.size();i++){
        const auto& v=op.vertices[i];const auto& first=op.vertices[i-i%6];
        const float base=256*first.uv[0][1]-7;
        if((base!=120&&base!=112)||256*v.uv[0][0]!=us[i%6]||256*v.uv[0][1]!=base+vs[i%6]||
           v.p[3]<=0||v.p[3]>=.1f||v.p[3]!=first.p[3])return false;
    }
    return true;
}
static bool track_shadow(float& ground_dx){
    unsigned corners=0;ground_dx=0;
    const Op* old=nullptr;Op* cur=nullptr;
    for(const auto& op:previous.ops)if(shadow_strip(op)){if(old)return false;old=&op;}
    for(auto& op:current.ops)if(shadow_strip(op)){if(cur)return false;cur=&op;}
    if(!old||!cur||old->target!=cur->target||old->state.program!=cur->state.program)return false;
    for(size_t i=0;i<cur->vertices.size();i+=6){
        for(size_t j=0;j<old->vertices.size();j+=6){
            const auto* a=&old->vertices[j];const auto* b=&cur->vertices[i];
            if(a->uv[0][1]!=b->uv[0][1])continue;
            bool nearby=true;
            for(unsigned k=0;k<6;k++)nearby&=std::abs(a[k].p[0]-b[k].p[0])<48&&std::abs(a[k].p[1]-b[k].p[1])<48;
            if(!nearby)continue;
            for(unsigned k=0;k<6;k++){
                ground_dx+=b[k].p[0]-a[k].p[0];++corners;
                memcpy(cur->prior_positions[i+k].data(),a[k].p,sizeof(a[k].p));
                memcpy(cur->prior_colors[i+k].data(),a[k].c,sizeof(a[k].c));
                cur->motion_valid[i+k]=cur->color_valid[i+k]=1;
            }
            break;
        }
    }
    if(corners)ground_dx/=corners;
    return corners!=0;
}
static bool face(const Vertex* v,const ByXY& points,Vertex* target,std::array<uint32_t,3>& ids,StrideKnot* mid){
    if(v[0].p[3]<=0||v[0].p[3]>=.1f||v[1].p[3]!=v[0].p[3]||v[2].p[3]!=v[0].p[3])return false;
    const double depth=1./v[0].p[3];double change=0,mid_change=0;bool complete=true;
    for(int j=0;j<3;j++){
        float x=v[j].p[0]*4,y=(v[j].p[1]-16)*4;
        if(x<INT16_MIN||x>INT16_MAX||y<INT16_MIN||y>INT16_MAX||x!=int(x)||y!=int(y))return false;
        auto it=points.find(xy(int(x),int(y)));if(it==points.end())return false;
        const Corner* best=nullptr;double error=64.;
        for(const Corner& c:it->second){
            double diff=std::abs(c.here->z/8.-depth);
            if(diff<error){best=&c;error=diff;}
        }
        if(!best)return false;
        const auto& a=*best->here;const auto& b=*best->there;
        ids[j]=a.address;target[j]=v[j];mid[j].player=true;
        target[j].p[0]=b.x*.25f;target[j].p[1]=16+b.y*.25f;
        change+=(int(b.z)-int(a.z))/24.;
        if(best->mid){
            mid[j].slopes=best->slopes;
            mid[j].position[0]=best->mid->x*.25f+best->offset;mid[j].position[1]=16+best->mid->y*.25f;
            mid_change+=(int(best->mid->z)-int(a.z))/24.;
        }else complete=false;
    }
    if(depth+change<1)return false;
    for(unsigned j=0;j<3;j++){
        target[j].p[2]=float(v[j].p[2]+change*(.9985/65536.));
        target[j].p[3]=float(1./(depth+change));
        if(complete&&depth+mid_change>=1){
            mid[j].position[2]=float(v[j].p[2]+mid_change*(.9985/65536.));
            mid[j].position[3]=float(1./(depth+mid_change));
        }
    }
    return true;
}
static void prepare(){
    link_count=departing=0;
    if(previous.mode!=2||current.bones.empty()||previous.bones.empty())return;
    std::unordered_map<uint64_t,const BoneSample*> by_id;
    auto old=displayed_pose(previous),cur=displayed_pose(current);
    if(old.passes.empty()||cur.passes.empty())return;
    for(auto p:old.passes[old.shown])
        by_id.emplace((uint64_t(p->group)<<32)|p->address,p);
    // Use a substep only when precisely one unused projection falls between
    // the displayed poses. Check continuity on the body as a whole: individual
    // head/foot vertices are allowed to reverse direction during a stride.
    const unsigned trailing=unsigned(old.passes.size())-old.shown-1;
    std::unordered_map<uint64_t,const BoneSample*> half;
    if(trailing+cur.shown==1){
        const auto& samples=trailing?old.passes[old.shown+1]:cur.passes[0];
        for(auto p:samples)half.emplace((uint64_t(p->group)<<32)|p->address,p);
        double length=0,along=0,bend=0;unsigned count=0;
        for(auto p:cur.passes[cur.shown]){
            uint64_t id=(uint64_t(p->group)<<32)|p->address;auto a=by_id.find(id),m=half.find(id);
            if(a==by_id.end()||m==half.end())continue;
            double dx=p->x-a->second->x,dy=p->y-a->second->y;
            double mx=m->second->x-a->second->x,my=m->second->y-a->second->y;
            length+=dx*dx+dy*dy;along+=dx*mx+dy*my;
            mx-=dx*.5;my-=dy*.5;bend+=mx*mx+my*my;++count;
        }
        if(count<12||length<count||along<=length*.1||along>=length*.9||bend>length*.35)half.clear();
    }
    std::unordered_map<uint64_t,const BoneSample*> after;
    if(!half.empty()&&cur.shown+1<cur.passes.size())
        for(auto p:cur.passes[cur.shown+1])after.emplace((uint64_t(p->group)<<32)|p->address,p);
    current.stride_velocity.clear();
    int top=INT16_MAX,bottom=INT16_MIN;
    for(const auto& entry:by_id){int y=entry.second->y;top=std::min(top,y);bottom=std::max(bottom,y);}
    LateralStride lateral;double torso_dx=0;unsigned torso_count=0;float foot_min=0,foot_max=0;
    ByXY backward,forward;
    for(auto sample:cur.passes[cur.shown]){
        const auto& p=*sample;
        auto it=by_id.find((uint64_t(p.group)<<32)|p.address);if(it==by_id.end())continue;
        const auto& q=*it->second;
        if(!p.z||!q.z||std::abs(p.x-q.x)>640||std::abs(p.y-q.y)>640)continue;
        auto m=half.find(it->first);const auto* mid=m==half.end()?nullptr:m->second;
        Corner corner{&p,&q,mid};
        float y=bottom>top?float(q.y-top)/(bottom-top):0;
        float dx=(p.x-q.x)*.25f;
        if(y>=.20f&&y<=.48f){torso_dx+=dx;torso_count++;}
        if(y>.65f){foot_min=std::min(foot_min,dx);foot_max=std::max(foot_max,dx);}
        if(mid){
            const auto last=previous.stride_velocity.find(it->first);
            const auto next=after.find(it->first);
            const float first[]={.25f*(mid->x-q.x),.25f*(mid->y-q.y)};
            const float second[]={.25f*(p.x-mid->x),.25f*(p.y-mid->y)};
            auto& terminal=current.stride_velocity[it->first];
            for(unsigned axis=0;axis<2;axis++){
                float upcoming=second[axis];
                if(next!=after.end())upcoming=.25f*(axis?next->second->y-p.y:next->second->x-p.x);
                corner.slopes[0][axis]=last==previous.stride_velocity.end()?first[axis]:
                    StrideKnot::limit(last->second[axis],first[axis]);
                corner.slopes[1][axis]=StrideKnot::joining(first[axis],second[axis]);
                terminal[axis]=corner.slopes[2][axis]=StrideKnot::joining(second[axis],upcoming);
            }
            if(bottom>top)lateral.observe(float(q.y-top)/(bottom-top),first[0],second[0],corner);
        }
        backward[xy(p.x,p.y)].push_back(corner);
        std::swap(corner.here,corner.there);forward[xy(q.x,q.y)].push_back(corner);
    }
    if(backward.size()<12)return;
    lateral.apply(backward);lateral.apply(forward);
    std::unordered_map<GLuint,unsigned> last;
    std::unordered_map<uint64_t,bool> visible;
    auto face_key=[](std::array<uint32_t,3> ids){std::sort(ids.begin(),ids.end());return hash(1469598103934665603ULL,ids.data(),sizeof(ids));};
    for(unsigned oi=0;oi<current.ops.size();oi++){
        auto& op=current.ops[oi];if(!opaque_projected(op)||!WideView::uses_texture(op.state))continue;
        for(unsigned i=0;i+2<op.vertices.size();i+=3){
            Vertex pose[3];std::array<uint32_t,3> ids;
            StrideKnot mid[3]{};
            if(!face(op.vertices.data()+i,backward,pose,ids,mid))continue;
            if(op.actor_mid.empty())op.actor_mid.resize(op.vertices.size());
            visible[face_key(ids)]=true;last[op.target]=oi;++link_count;
            if(op.front_face.empty())op.front_face.resize(op.vertices.size()/3);
            op.front_face[i/3]=screen_area(op.vertices.data()+i)<0;
            for(unsigned j=0;j<3;j++){
                memcpy(op.prior_positions[i+j].data(),pose[j].p,sizeof(pose[j].p));
                if(!op.actor_mid.empty())op.actor_mid[i+j]=mid[j];
                op.motion_valid[i+j]=1;
            }
        }
    }
    float ground_dx=0;
    if(link_count){
        const bool tracked=track_shadow(ground_dx);
        const float body_dx=torso_count?float(torso_dx/torso_count):0;
        const bool walking=tracked&&torso_count>=12&&foot_max-foot_min>2&&std::abs(body_dx-ground_dx)<8;
        current.stride_follow.update(previous.stride_follow.end,body_dx,ground_dx,current.stride_dt,walking);
    }
    for(const auto& before:previous.ops){
        auto tail=last.find(before.target);
        if(tail==last.end()||!opaque_projected(before)||!WideView::uses_texture(before.state))continue;
        OutgoingDraw* batch=nullptr;
        for(unsigned i=0;i+2<before.vertices.size();i+=3){
            const Vertex* src=before.vertices.data()+i;
            Vertex pose[3];std::array<uint32_t,3> ids;
            StrideKnot mid[3]{};
            if(!face(src,forward,pose,ids,mid)||visible.count(face_key(ids))||screen_area(src)>=0||screen_area(pose)<0)continue;
            if(!batch){
                outgoing_draws.emplace_back();batch=&outgoing_draws.back();batch->after=tail->second;
                batch->op.target=before.target;batch->op.state=before.state;batch->op.raster=before.raster;
                batch->op.mode=GL_TRIANGLES;batch->op.match=0;
            }
            auto& draw=batch->op;draw.front_face.push_back(1);
            for(unsigned j=0;j<3;j++){
                draw.vertices.push_back(pose[j]);std::array<float,4> p;memcpy(p.data(),src[j].p,sizeof(src[j].p));
                draw.prior_positions.push_back(p);draw.motion_valid.push_back(1);draw.actor_mid.push_back(mid[j]);
            }
            ++departing;
        }
    }
}
}
