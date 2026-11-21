// Exact link between 50F900's indexed vertices (before culling) and TLVERTEX.
// A battle packet contains quarter-pixel XY and a 16-bit camera depth. The
// fixed 24-pixel Y offset is part of the remaster's battle viewport.
struct BoneSample{uint32_t group,address;int16_t x,y;uint16_t z,flags;};
static_assert(sizeof(BoneSample)==16);
struct BonePoint{int16_t x=0,y=0;uint16_t z=0;
    uint64_t key()const{return uint16_t(x)|(uint64_t(uint16_t(y))<<16)|(uint64_t(z)<<32);}
    bool operator==(const BonePoint&other)const{return key()==other.key();}
    void apply(std::array<float,4>&p)const{p[0]=x*.25f;p[1]=24.f+y*.25f;p[3]=1.f/z;}
};
static bool from_gl(const float*p,BonePoint&v){
    if(!(p[3]>0&&p[3]<.1f))return false;
    float x=p[0]*4,y=(p[1]-24.f)*4,d=1.f/p[3];
    if(!(x>=-32768&&x<=32767&&y>=-32768&&y<=32767&&d>0&&d<=65535))return false;
    int z=(int)std::round(d);
    if(x!=(int)x||y!=(int)y||p[3]!=1.f/z)return false;
    v={(int16_t)x,(int16_t)y,(uint16_t)z};return true;
}
// The battle viewport is 640 units wide and the projector stores quarter pixels.
// Distinguish a change of shot from a continuous pan using the same indexed
// stage vertices on both sides, before triangle culling or material sorting.
static bool battle_shot_changed(const std::vector<BoneSample>& before,const std::vector<BoneSample>& after){
    if(before.empty()||after.empty())return false;
    auto stage=[](const BoneSample& p){
        return p.group<4 && p.address>=0x0a080000 && p.address<0x0a100000 && !(p.flags&2);
    };
    std::unordered_map<uint64_t,BonePoint> prior;
    for(const auto& p:before)if(stage(p))
        prior.emplace((uint64_t(p.group)<<32)|p.address,BonePoint{p.x,p.y,p.z});
    unsigned visible=0,displaced=0;
    for(const auto& p:after){
        if(!stage(p)||p.x<0||p.x>2560||p.y<0||p.y>1728)continue;
        auto f=prior.find((uint64_t(p.group)<<32)|p.address);if(f==prior.end())continue;
        double dx=p.x-f->second.x,dy=p.y-f->second.y;
        ++visible;if(dx*dx+dy*dy>1280.*1280.)++displaced;
    }
    return visible>=8 && displaced*3>=visible*2;
}
using BoneLinks=std::unordered_map<uint64_t,BonePoint>;
struct BoneTracks{BoneLinks backward,forward;};
static void vote_bone(BoneLinks&links,uint64_t key,BonePoint p){
    auto r=links.emplace(key,p);if(!r.second&&!(r.first->second==p))r.first->second.z=0;
}
static BoneTracks link_bones(const std::vector<BoneSample>&a,const std::vector<BoneSample>&b){
    BoneTracks tracks;if(a.empty()||b.empty())return tracks;
    auto index=[](const auto&samples){BoneLinks out;out.reserve(samples.size());
        for(const auto&p:samples){uint64_t id=(uint64_t(p.group)<<32)|p.address;
            vote_bone(out,id,{p.x,p.y,(uint16_t)((p.flags&2)?0:p.z)});}
        return out;};
    auto old=index(a),cur=index(b);
    for(const auto&entry:cur){auto f=old.find(entry.first);if(f==old.end())continue;
        const auto&p=f->second;const auto&q=entry.second;if(!p.z||!q.z)continue;
        if(std::abs(int(p.x)-q.x)>880||std::abs(int(p.y)-q.y)>880||p.z*2.5<q.z||q.z*2.5<p.z)continue;
        vote_bone(tracks.backward,q.key(),p);vote_bone(tracks.forward,p.key(),q);}
    return tracks;
}

static const BonePoint*lookup_bone(const BoneLinks&links,const float*p){
    BonePoint here;if(!from_gl(p,here))return nullptr;
    auto f=links.find(here.key());return f!=links.end()&&f->second.z?&f->second:nullptr;
}
