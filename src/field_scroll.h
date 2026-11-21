// A field tile's atlas cell selects its artwork. Its screen rectangle belongs
// to the scrolling scenery even when that cell changes on a native tick.
namespace field_scroll {
using XY=std::array<int,2>; // quarter native pixels
struct Tile {
    size_t op,first;
    XY xy;
    uint64_t art,plane,grid;
};
template<class T> static uint64_t key(uint64_t seed,const T& value){return hash(seed,&value,sizeof(value));}
static std::vector<Tile> tiles(const Frame& frame){
    std::vector<Tile> out;
    for(size_t i=0;i<frame.ops.size();i++){
        const auto& op=frame.ops[i];const auto& s=op.state;
        if(!op.target||op.clear||op.mode!=GL_TRIANGLES||op.vertices.size()%6||
           !s.program||!s.texture[0].id||s.viewport[2]!=320||s.viewport[3]!=240||
           std::abs(s.projection[0]-2.f/640)>.00001f||std::abs(s.projection[5]+2.f/480)>.00001f)continue;
        // The colour and occlusion passes draw the same tiles to separate
        // targets. Share camera evidence between them.
        const uint64_t pass=key(1469598103934665603ULL,s.program);
        for(size_t j=0;j<op.vertices.size();j+=6){
            const auto* q=op.vertices.data()+j;WideView::Bounds b;
            if(!(q->p[3]>0&&q->p[3]<.1f)||!WideView::flat_rect(q,b)||
               b.r-b.l!=32||b.b-b.t!=32||std::abs(b.l)>4096||std::abs(b.t)>4096)continue;
            const XY xy={int(std::lround(4*b.l)),int(std::lround(4*b.t))};
            if(xy[0]!=4*b.l||xy[1]!=4*b.t)continue;
            const XY phase={((xy[0]%128)+128)%128,((xy[1]%128)+128)%128};
            const auto plane=key(key(pass,q->p[3]),q->p[2]);
            auto art=key(plane,s.texture[0].id);
            for(int v:{0,1,2})art=key(art,q[v].uv[0]);
            out.push_back({i,j,xy,art,plane,key(pass,phase)});
        }
    }
    return out;
}
struct Position {XY value;bool unique=true;};
static auto positions(const std::vector<Tile>& tiles){
    std::unordered_map<uint64_t,Position> result;
    for(const auto& t:tiles){
        auto entry=result.emplace(t.art,Position{t.xy});
        if(entry.first->second.value!=t.xy)entry.first->second.unique=false;
    }
    return result;
}
struct Scroll {
    struct Vote {XY delta,first;unsigned count;bool spans;};
    std::vector<Vote> votes;
    void observe(XY movement,XY at){
        for(auto& v:votes)if(v.delta==movement){v.count++;v.spans|=at!=v.first;return;}
        votes.push_back({movement,at,1,false});
    }
    const Vote* consensus()const{
        const Vote* best=nullptr;unsigned total=0;
        for(const auto& v:votes){
            total+=v.count;
            if(!best||v.count>best->count)best=&v;
        }
        // An animation can switch into the atlas cell used by the adjacent
        // tile last tick. That isolated vote must not cancel the whole pan.
        return best&&best->spans&&best->count>2*(total-best->count)?best:nullptr;
    }
};
// Different passes can render the water and the paving below it. Keep only
// unambiguous landmark trajectories at each screen position.
using Landmarks=std::unordered_map<uint64_t,Position>;
static uint64_t screen_cell(XY xy){
    return (uint64_t(uint32_t(xy[0]))<<32)|uint32_t(xy[1]);
}
static bool ground_scroll(const Landmarks& marks,XY at,XY& delta){
    int agreeing=0;
    for(int y=-128;y<=128;y+=128)for(int x=-128;x<=128;x+=128){
        auto m=marks.find(screen_cell({at[0]+x,at[1]+y}));
        if(m==marks.end())continue;
        if(!m->second.unique||(agreeing&&delta!=m->second.value))return false;
        delta=m->second.value;++agreeing;
    }
    return agreeing>=2;
}
struct TileIndex {
    std::vector<Tile> list;
    std::unordered_map<uint64_t,Position> art;
    std::unordered_set<uint64_t> occupied;
    explicit TileIndex(const Frame& frame):list(tiles(frame)),art(positions(list)){
        occupied.reserve(list.size());
        for(const auto& t:list)occupied.insert(key(t.plane,t.xy));
    }
};
static std::shared_ptr<TileIndex> index(const Frame& frame){
    if(!frame.match_input)return std::make_shared<TileIndex>(frame);
    auto& cache=frame.match_input->field_tiles;
    if(!cache)cache=std::make_shared<TileIndex>(frame);
    return cache;
}
static size_t align(const Frame& before,Frame& after){
#ifdef GEO_TEST
    if(std::getenv("FF8_REPLAY_OLD_FIELD_SCROLL"))return 0;
#endif
    const auto dest=index(after);
    const auto& cur=dest->list;if(cur.empty())return 0;
    const auto source_index=index(before);
    const auto& old_art=source_index->art;const auto& new_art=dest->art;
    std::unordered_map<uint64_t,Scroll> layer_scroll,grid_scroll;
    Landmarks marks;
    for(const auto& t:cur){
        auto a=old_art.find(t.art);
        if(a==old_art.end()||!a->second.unique||!new_art.at(t.art).unique)continue;
        const auto& p=a->second.value;
        XY delta={p[0]-t.xy[0],p[1]-t.xy[1]};
        // A large jump can be a new scene with reused atlas artwork.
        if(std::max(std::abs(delta[0]),std::abs(delta[1]))>128)continue;
        layer_scroll[key(t.plane,t.grid)].observe(delta,t.xy);
        grid_scroll[t.grid].observe(delta,t.xy);
        auto mark=marks.emplace(screen_cell(t.xy),Position{delta});
        if(mark.first->second.value!=delta)mark.first->second.unique=false;
    }
    size_t aligned=0;
    for(const auto& t:cur){
        const auto group=grid_scroll.find(t.grid);
        const auto layer=layer_scroll.find(key(t.plane,t.grid));
        const auto* local=layer==layer_scroll.end()?nullptr:layer->second.consensus();
        const auto* scroll=local?local:(group==grid_scroll.end()?nullptr:group->second.consensus());
        XY delta{};
        if(scroll)delta=scroll->delta;
        const XY prior={t.xy[0]+delta[0],t.xy[1]+delta[1]};
        if(!local&&(!scroll||!source_index->occupied.count(key(t.plane,prior)))){
            // The Deling fountain changes both depth and coverage as its
            // animation advances. Carry new spray with the surrounding field
            // if nearby landmarks agree; preserve the new water depth.
            if(!ground_scroll(marks,t.xy,delta))continue;
        }
        auto& op=after.ops[t.op];
        const auto source=old_art.find(t.art);
        for(size_t v=t.first;v<t.first+6;v++){
            const auto* p=op.vertices[v].p;auto& history=op.prior_positions[v];
            std::copy_n(p,4,history.begin());
            history[0]+=delta[0]*.25f;history[1]+=delta[1]*.25f;
            op.motion_valid[v]=1;
            // Colour and atlas artwork belong to the current animation cell.
            if(source==old_art.end())op.color_valid[v]=0;
        }
        ++aligned;
    }
    return aligned;
}
}
