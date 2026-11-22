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
// Each input sample contributes at most one key. The four temporary bone
// indices can therefore use bounded, contiguous storage with no node allocation.
class BoneLinks {
    struct Entry {uint64_t key=0; BonePoint point{}; bool used=false;};
    std::vector<Entry> slots;
    size_t mask=0,occupied=0;
    size_t locate(uint64_t key) const {
        uint32_t mixed=uint32_t(key)^uint32_t(key>>32)*0x9e3779b9u;
        mixed^=mixed>>16;mixed*=0x85ebca6bu;mixed^=mixed>>13;
        size_t i=mixed&mask;
        while(slots[i].used && slots[i].key!=key)i=(i+1)&mask;
        return i;
    }
public:
    bool empty() const{return occupied==0;}
    size_t size() const{return occupied;}
    void reserve(size_t upper){
        size_t capacity=2;while(capacity<upper+upper/2+1)capacity*=2;
        slots.resize(capacity);mask=capacity-1;
    }
    void vote(uint64_t key,BonePoint point){
        auto& slot=slots[locate(key)];
        if(!slot.used){slot={key,point,true};++occupied;}
        else if(!(slot.point==point))slot.point.z=0;
    }
    const BonePoint* find(uint64_t key)const{
        if(empty())return nullptr;
        const auto& slot=slots[locate(key)];
        return slot.used?&slot.point:nullptr;
    }
    template<class F> void each(F visit)const{
        for(const auto& e:slots)if(e.used)visit(e.key,e.point);
    }
};
struct BoneTracks{BoneLinks backward,forward;};
static BoneTracks link_bones(const std::vector<BoneSample>&a,const std::vector<BoneSample>&b){
    BoneTracks tracks;if(a.empty()||b.empty())return tracks;
    auto index=[](const auto&samples){BoneLinks out;out.reserve(samples.size());
        for(const auto&p:samples){uint64_t id=(uint64_t(p.group)<<32)|p.address;
            out.vote(id,{p.x,p.y,(uint16_t)((p.flags&2)?0:p.z)});}
        return out;};
    auto old=index(a),cur=index(b);
    tracks.backward.reserve(cur.size());tracks.forward.reserve(cur.size());
    cur.each([&](uint64_t key,const BonePoint& q){auto f=old.find(key);if(!f)return;
        const auto&p=*f;if(!p.z||!q.z)return;
        if(std::abs(int(p.x)-q.x)>880||std::abs(int(p.y)-q.y)>880||p.z*2.5<q.z||q.z*2.5<p.z)return;
        tracks.backward.vote(q.key(),p);tracks.forward.vote(p.key(),q);});
    return tracks;
}

static const BonePoint*lookup_bone(const BoneLinks&links,const float*p){
    BonePoint here;if(!from_gl(p,here))return nullptr;
    const auto* f=links.find(here.key());return f&&f->z?f:nullptr;
}
