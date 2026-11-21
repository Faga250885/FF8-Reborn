// Track whole UI rectangles by the texture region at each corner. A glyph,
// highlighted item or panel keeps four straight edges throughout its motion.
struct MenuRect {
    unsigned op,start;
    std::array<unsigned,6> corner{};
    std::array<unsigned,4> vertex{};
    std::array<float,4> bounds{};
    uint64_t key=0;
};
static std::vector<MenuRect> menu_rectangles(const Frame&frame){
    std::vector<MenuRect> result;
    for(unsigned oi=0;oi<frame.ops.size();oi++){
        const auto&op=frame.ops[oi];if(!projected_geometry(op)||op.vertices.size()%6)continue;
        uint64_t state=surface_state_key(op);
        state=hash(state,&op.state.program,sizeof(op.state.program));
        state=hash(state,&op.state.src_rgb,sizeof(op.state.src_rgb));state=hash(state,&op.state.dst_rgb,sizeof(op.state.dst_rgb));
        state=hash(state,&op.state.scissor_on,sizeof(op.state.scissor_on));
        if(op.state.scissor_on)state=hash(state,op.state.scissor,sizeof(op.state.scissor));
        state=hash(state,&op.state.texture[0].id,sizeof(op.state.texture[0].id));
        if(!op.state.texture[0].id)continue;
        for(unsigned at=0;at<op.vertices.size();at+=6){
            const Vertex*quad=&op.vertices[at];MenuRect rect{oi,at};
            float left=quad[0].p[0],right=left,top=quad[0].p[1],bottom=top;
            bool valid=true;
            for(unsigned v=0;v<6;v++){
                left=std::min(left,quad[v].p[0]);right=std::max(right,quad[v].p[0]);
                top=std::min(top,quad[v].p[1]);bottom=std::max(bottom,quad[v].p[1]);
                valid&=quad[v].p[3]>=.1f&&quad[v].p[3]==quad[0].p[3];
            }
            if(!valid||right<=left||bottom<=top)continue;
            unsigned seen=0;
            for(unsigned v=0;v<6;v++){
                auto&p=quad[v].p;if((p[0]!=left&&p[0]!=right)||(p[1]!=top&&p[1]!=bottom)){valid=false;break;}
                unsigned corner=(p[0]==right)+2u*(p[1]==bottom);
                if((seen&(1u<<corner))&&memcmp(quad[v].uv[0],quad[rect.vertex[corner]].uv[0],sizeof(quad[v].uv[0]))){valid=false;break;}
                seen|=1u<<corner;rect.corner[v]=corner;rect.vertex[corner]=v;
            }
            if(!valid||seen!=15)continue;
            rect.key=state;rect.bounds={left,top,right,bottom};
            for(unsigned v:rect.vertex)rect.key=hash(rect.key,quad[v].uv[0],sizeof(quad[v].uv[0]));
            result.push_back(rect);
        }
    }
    return result;
}
static unsigned menu_pairs=0;
static void match_menu_frames(){
    menu_pairs=moving_draws=matched_candidates=0;outgoing_draws.clear();outgoing_triangles=0;
    auto from=menu_rectangles(previous),to=menu_rectangles(current);
    std::unordered_map<uint64_t,std::vector<unsigned>> by_key;
    for(unsigned i=0;i<from.size();i++)by_key[from[i].key].push_back(i);
    struct Choice {int index=-1;double best=1e20,next=1e20;};
    auto consider=[](Choice&choice,unsigned i,double cost){
        if(cost<choice.best){choice.next=choice.best;choice.best=cost;choice.index=(int)i;}else choice.next=std::min(choice.next,cost);
    };
    std::vector<Choice> old_choice(from.size()),new_choice(to.size());
    for(unsigned i=0;i<to.size();i++){
        auto group=by_key.find(to[i].key);if(group==by_key.end()||group->second.size()>128)continue;
        for(unsigned j:group->second){
            double cost=0;for(int k=0;k<4;k++){double d=to[i].bounds[k]-from[j].bounds[k];cost+=d*d;}
            if(cost>4*96*96)continue;
            consider(new_choice[i],j,cost);consider(old_choice[j],i,cost);
        }
    }
    for(auto&op:current.ops){
        op.match=-1;op.motion_valid.assign(op.vertices.size(),0);op.color_valid.assign(op.vertices.size(),0);
        op.prior_positions.resize(op.vertices.size());op.prior_colors.resize(op.vertices.size());op.prior_depth_bias.clear();op.front_face.clear();
    }
    for(unsigned i=0;i<to.size();i++){
        const auto&choice=new_choice[i];if(choice.index<0||choice.best+1e-7>=choice.next*.8)continue;
        const auto&back=old_choice[choice.index];if(back.index!=(int)i||back.best+1e-7>=back.next*.8)continue;
        const auto&a=from[choice.index];const auto&b=to[i];auto&op=current.ops[b.op];const auto&before=previous.ops[a.op];
        bool moved=false;menu_pairs++;op.match=0;
        for(unsigned v=0;v<6;v++){
            const auto&old=before.vertices[a.start+a.vertex[b.corner[v]]];const auto&now=op.vertices[b.start+v];
            auto&position=op.prior_positions[b.start+v];memcpy(position.data(),now.p,sizeof(now.p));
            position[0]=old.p[0];position[1]=old.p[1];op.motion_valid[b.start+v]=1;
            memcpy(op.prior_colors[b.start+v].data(),old.c,sizeof(old.c));op.color_valid[b.start+v]=1;
            moved|=position[0]!=now.p[0]||position[1]!=now.p[1]||memcmp(old.c,now.c,sizeof(old.c))!=0;
        }
        moving_draws+=moved;
    }
}
