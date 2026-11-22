// Battle particles can change atlas cells between ticks. Blend the two
// cells at the moving card's position only for compatible blend modes.
static bool atlas_card(const Op&o){
    const auto&s=o.state;
    if(o.clear||o.mode!=GL_TRIANGLES||o.vertices.empty()||o.vertices.size()%6||!s.program||!s.blend||
        s.depth_mask||s.alpha||s.logic_on||s.srgb||o.raster.stencil||o.raster.lighting||o.raster.fog||
        s.src_rgb!=GL_ONE||s.dst_rgb!=GL_ONE||!s.texture[0].copy)return false;
    if(s.eq_rgb!=GL_FUNC_ADD&&s.eq_rgb!=GL_FUNC_REVERSE_SUBTRACT)return false;
    for(size_t base=0;base<o.vertices.size();base+=6){
        const auto*v=&o.vertices[base];
        float dx=v[1].p[0]-v[0].p[0],dy=v[0].p[1]-v[1].p[1];
        if(!(dx>=4&&dx<=80&&dy>=4&&dy<=80&&v[0].p[3]>0&&v[0].p[3]<.1f))return false;
        if(v[0].uv[0][0]==v[1].uv[0][0]||v[0].uv[0][1]==v[1].uv[0][1])return false;
        const int xc[6]={0,1,0,1,1,0},yc[6]={0,1,1,0,1,0};
        for(int i=0;i<6;i++)if(v[i].p[0]!=v[xc[i]].p[0]||v[i].p[1]!=v[yc[i]].p[1]||v[i].p[3]!=v[0].p[3]||
               v[i].uv[0][0]!=v[xc[i]].uv[0][0]||v[i].uv[0][1]!=v[yc[i]].uv[0][1])return false;
    }return true;
}
static bool same_card(const Op&a,const Op&b,bool& changed){
    changed=false;
    const auto&s=a.state;const auto&t=b.state;
    if(a.vertices.size()!=b.vertices.size()||a.target!=b.target||memcmp(&s,&t,offsetof(State,texture))||s.uniforms.size()!=t.uniforms.size())return false;
    for(size_t i=0;i<s.uniforms.size();i++)if(!same_uniform(s.uniforms[i],t.uniforms[i]))return false;
    for(int i=0;i<4;i++)if(s.texture[i].copy!=t.texture[i].copy||memcmp(&s.texture[i],&t.texture[i],offsetof(TextureState,copy)))return false;
    for(size_t j=0;j<a.vertices.size();j+=6){const auto*v=&a.vertices[j];const auto*w=&b.vertices[j];
        float du=w[0].uv[0][0]-v[0].uv[0][0],dv=w[0].uv[0][1]-v[0].uv[0][1];
        changed|=du!=0||dv!=0;
        if(fabs(v[0].p[0]-w[0].p[0])>10||fabs(v[0].p[1]-w[0].p[1])>20||
           fabs(v[0].p[3]-w[0].p[3])>v[0].p[3]*.035||
           fabs((v[1].p[0]-v[0].p[0])-(w[1].p[0]-w[0].p[0]))>3||
           fabs((v[1].p[1]-v[0].p[1])-(w[1].p[1]-w[0].p[1]))>3)return false;
        for(int k=0;k<6;k++){
            // Replay changes only the first UV array and vertex colour.
            if(w[k].uv[0][0]-v[k].uv[0][0]!=du||w[k].uv[0][1]-v[k].uv[0][1]!=dv||
               w[k].uv[0][2]!=v[k].uv[0][2]||w[k].uv[0][3]!=v[k].uv[0][3]||
               memcmp(w[k].uv+1,v[k].uv+1,3*sizeof(v[k].uv[0]))||
               memcmp(w[k].n,v[k].n,sizeof(v[k].n)))return false;
        }
    }return true;
}
static unsigned resolve_cards(){
    std::vector<Op*> old_cards, new_cards;
    for(auto& op:previous.ops)if(atlas_card(op))old_cards.push_back(&op);
    for(auto& op:current.ops){
        op.card_before.clear();
        if(atlas_card(op))new_cards.push_back(&op);
    }
    if(old_cards.empty()||new_cards.empty())return 0;
    const auto x=[](const Op* card){return card->vertices.front().p[0];};
    const auto order=[&](const Op* a,const Op* b){return x(a)<x(b);};
    std::sort(old_cards.begin(),old_cards.end(),order);
    std::sort(new_cards.begin(),new_cards.end(),order);
    const auto unique=[&](Op* card,const std::vector<Op*>& others,bool reverse)->Op* {
        const auto begin=std::lower_bound(others.begin(),others.end(),x(card)-10,
            [&](const Op* other,float limit){return x(other)<limit;});
        const auto end=std::upper_bound(begin,others.end(),x(card)+10,
            [&](float limit,const Op* other){return limit<x(other);});
        // Limit work in a dense cloud without disabling other actors' particles.
        if(end-begin>64)return nullptr;
        Op* candidate=nullptr;
        bool advances=false;
        for(auto it=begin;it!=end;++it){
            bool changed=false;
            const bool compatible=reverse?same_card(*card,**it,changed):same_card(**it,*card,changed);
            if(!compatible)continue;
            // An unchanged neighbour is also a possible source.
            if(candidate)return nullptr;
            candidate=*it;
            advances=changed;
        }
        return advances?candidate:nullptr;
    };
    unsigned matched=0;
    for(Op* card:new_cards){
        Op* source=unique(card,old_cards,false);
        if(!source||unique(source,new_cards,true)!=card)continue;
        card->card_before=source->vertices;
        card->match=0;
        for(size_t i=0;i<card->vertices.size();i++){
            card->motion_valid[i]=1;
            card->color_valid[i]=0;
            memcpy(card->prior_positions[i].data(),source->vertices[i].p,sizeof(Vertex::p));
        }
        matched++;
    }
    return matched;
}
