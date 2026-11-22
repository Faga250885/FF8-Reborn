// Recycle CPU arrays only after their source frame has left the presentation
// history. Shaders, textures, material state and pose data have frame lifetimes.
class DeckDrawStorage {
    struct Buffers {
        std::vector<Vertex> vertices;
        std::vector<std::array<float,4>> positions,colors;
        std::vector<BYTE> motion,color_motion;
        void exchange(Op& op){
            vertices.swap(op.vertices);
            positions.swap(op.prior_positions);
            colors.swap(op.prior_colors);
            motion.swap(op.motion_valid);
            color_motion.swap(op.color_valid);
        }
    };
    std::array<std::vector<Buffers>,20> free_by_size;
    static unsigned bucket(size_t size){
        unsigned order=0;
        while(size>1){size>>=1;++order;}
        return order;
    }
public:
    void retire(Frame& frame,bool enabled){
        for(auto& list:free_by_size)list.clear();
        size_t retained=0;
        constexpr size_t limit=16*1024*1024;
        if(enabled)for(auto& op:frame.ops){
            const auto count=op.vertices.capacity();
            if(!count||count>MAX_VERTICES)continue;
            const size_t bytes=count*sizeof(Vertex)+
                (op.prior_positions.capacity()+op.prior_colors.capacity())*sizeof(std::array<float,4>)+
                op.motion_valid.capacity()+op.color_valid.capacity();
            if(bytes>limit-retained)continue;
            auto& list=free_by_size[bucket(count)];
            try{list.emplace_back();}
            catch(const std::bad_alloc&){return;} // Retire the rest normally if the pool cannot grow.
            list.back().exchange(op);
            retained+=bytes;
        }
    }
    void take(Op& op,size_t count){
        auto& list=free_by_size[bucket(count)];
        if(list.empty())return;
        auto& item=list.back();
        if(item.vertices.capacity()>=count){
            item.exchange(op);
            // read_draw_vertices writes every Vertex component, including
            // the defaults for short client arrays. Only pose data is sparse.
            op.prior_positions.clear();op.prior_colors.clear();
            op.motion_valid.clear();op.color_valid.clear();
        }
        list.pop_back();
    }
};
