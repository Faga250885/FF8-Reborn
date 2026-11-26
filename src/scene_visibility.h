// A fast tilt can expose small wedges above or below the reconstructed
// scene. Track only the parts of the edges that the native scene covers.
static SceneEdgeRepair scene_edge_repair(const Frame& frame,const std::vector<OutgoingDraw>& departing){
    SceneEdgeRepair repair;
    if(frame.ops.empty()||(frame.mode!=2&&frame.mode!=3))return repair;
    const bool battle=frame.mode==3;
    const float inset=battle?24.f:16.f;
    const auto& end=frame.ops.back();
    const float width=std::round(end.state.viewport[2]*480.f/(480-2*inset));
    if(width<=0||width>=frame.w||!centered_menu_composite(end,frame.w,frame.h))return repair;
    constexpr unsigned columns=64;
    constexpr uint64_t all=~uint64_t{0};
    constexpr auto phases=SceneEdgeRepair::phases;
    const std::array<float,6> rows={inset+.5f,inset+2.f,inset+7.f,
                                    473.f-inset,478.f-inset,479.5f-inset};
    std::array<std::array<uint64_t,6>,phases.size()> coverage{};
    const float step=640.f*frame.w/(width*columns);
    const float left=320.f-step*(columns-1)/2;

    auto collect=[&](const Op& op,bool leaving){
        if(!op.target||!opaque_projected(op)||op.vertices.empty()||
           std::abs(op.state.projection[0]-2.f/640)>.00001f||
           std::abs(op.state.projection[5]+2.f/480)>.00001f)return;
        const bool clouds=!battle&&std::abs(op.vertices.front().p[3]-1.f/4095)<1e-7f;
        if(!op.state.depth_mask&&!clouds)return;
        for(size_t i=0;i+2<op.vertices.size();i+=3){
            float low=INFINITY,high=-INFINITY;
            for(size_t corner=0;corner<3;++corner){
                const size_t v=i+corner;
                const auto* p=op.vertices[v].p;
                if(!(p[3]>0&&p[3]<.1f)){low=rows.back()+1;break;}
                low=std::min(low,p[1]);
                high=std::max(high,p[1]);
                if(op.match>=0&&op.motion_valid[v]){
                    low=std::min(low,op.prior_positions[v][1]);
                    high=std::max(high,op.prior_positions[v][1]);
                }
            }
            if(std::none_of(rows.begin(),rows.end(),[&](float y){return y>=low&&y<=high;}))continue;
            for(size_t phase=leaving?1:0;phase<phases.size();++phase){
                auto& mask=coverage[phase];
                if(std::all_of(mask.begin(),mask.end(),[](uint64_t row){return row==all;}))continue;
                float x[3],y[3];
                for(size_t corner=0;corner<3;++corner){
                    const size_t v=i+corner;
                    const auto* p=op.vertices[v].p;
                    float point[4];
                    if(phase&&op.match>=0&&op.motion_valid[v]){
                        const auto& prior=op.prior_positions[v];
                        if(battle)interpolate_battle_point(prior.data(),p,phases[phase],point);
                        else for(unsigned axis=0;axis<2;++axis)
                            point[axis]=prior[axis]+phases[phase]*(p[axis]-prior[axis]);
                        p=point;
                    }
                    x[corner]=p[0];y[corner]=p[1];
                }
                const float area=(x[1]-x[0])*(y[2]-y[0])-(y[1]-y[0])*(x[2]-x[0]);
                if(!std::isfinite(area)||std::abs(area)<.001f||
                   (phase&&i/3<op.front_face.size()&&op.front_face[i/3]&&area>0))continue;
                for(size_t row=0;row<rows.size();++row){
                    if(mask[row]==all||rows[row]<low||rows[row]>high)continue;
                    float first=INFINITY,last=-INFINITY;
                    for(size_t a=0;a<3;++a){
                        const size_t b=(a+1)%3;
                        if(rows[row]<std::min(y[a],y[b])||rows[row]>std::max(y[a],y[b]))continue;
                        if(y[a]==y[b]){
                            first=std::min({first,x[a],x[b]});last=std::max({last,x[a],x[b]});
                        }else{
                            const float crossing=x[a]+(x[b]-x[a])*((rows[row]-y[a])/(y[b]-y[a]));
                            first=std::min(first,crossing);last=std::max(last,crossing);
                        }
                    }
                    const float begin=std::max(0.f,std::ceil((first-left)/step));
                    const float finish=std::min(float(columns-1),std::floor((last-left)/step));
                    if(begin<=finish)mask[row]|=(all<<unsigned(begin))&(all>>(columns-1-unsigned(finish)));
                }
            }
        }
    };
    for(const auto& op:frame.ops)collect(op,false);
    for(const auto& draw:departing)collect(draw.op,true);
    for(size_t row=0;row<rows.size();++row){
        const uint64_t endpoint=coverage[0][row];
        if(std::bitset<columns>(endpoint).count()<columns-3)continue;
        for(size_t i=1;i<phases.size();++i){
            const uint64_t lost=endpoint&~coverage[i][row];
            if(lost&(lost>>1))(row<rows.size()/2?repair.top:repair.bottom)[i]|=lost;
        }
    }
    return repair;
}
#ifdef GEO_TEST
static bool scene_loses_vertical_edge(const Frame& frame,const std::vector<OutgoingDraw>& departing){
    return scene_edge_repair(frame,departing).needed();
}
#endif
