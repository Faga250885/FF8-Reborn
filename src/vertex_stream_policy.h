// Compare CPU array submission to a packed VBO under the active GL driver.
// Time the normal replay, without rendering anything twice or draining the GPU.
struct VertexStreamPolicy {
    struct Bucket {
        bool preferred=false;
        unsigned sample=0, cooldown=0;
        double times[2][3]={};
    };
    struct Choice {
        Bucket* timing=nullptr;
        bool stream=false;
    };
    Bucket buckets[3][2][2][3][3]={};
    unsigned retry_upload=0;

    Choice choose(int mode,size_t draws,unsigned vertices,bool wide,bool endpoint,bool cut){
        if(cut||!draws||!vertices||mode<1||mode>3)return {};
        if(retry_upload){--retry_upload;return {};}
        const int draw_class=draws<64?0:draws<240?1:2;
        const int vertex_class=vertices<2048?0:vertices<8192?1:2;
        auto& b=buckets[mode-1][wide][endpoint][draw_class][vertex_class];
        if(b.cooldown){--b.cooldown;return {nullptr,b.preferred};}
        // ABBAAB interleaves both paths. The median discards a cold buffer
        // allocation, a driver stall or a preemption on either path.
        const bool other=b.sample==1||b.sample==2||b.sample==5;
        return {&b,other!=b.preferred};
    }
    static void measured(Choice choice,double elapsed){
        if(!choice.timing)return;
        auto& b=*choice.timing;
        if(!(elapsed>0&&elapsed<.05)){b.sample=0;b.cooldown=60;return;}
        static constexpr unsigned slot[6]={0,0,1,1,2,2};
        b.times[choice.stream][slot[b.sample]]=elapsed;
        if(++b.sample<6)return;
        std::sort(b.times[0],b.times[0]+3);
        std::sort(b.times[1],b.times[1]+3);
        const double usual=b.times[b.preferred][1],other=b.times[!b.preferred][1];
        if(other+std::max(.000025,usual*.045)<usual)b.preferred=!b.preferred;
        b.sample=0;
        b.cooldown=360;
    }
    void upload_failed(Choice choice){
        if(choice.timing)choice.timing->sample=0;
        retry_upload=1200;
    }
};
