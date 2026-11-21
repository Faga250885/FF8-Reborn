// Verified on the supported engine: projected z = 1/(327680*rhw) + a
// per-face ordering bias. Do not weld that bias between adjacent faces or
// hold physical z at the current pose while moving x/y/rhw to the old pose.
static bool battle_depth_enabled=true;
static constexpr double battle_depth_scale=1./327680.;
static bool triangle_bias(const Vertex*vertices,float&bias){
    double values[3];for(unsigned i=0;i<3;i++){const auto&p=vertices[i].p;if(p[3]<=0||p[3]>=.1f)return false;values[i]=p[2]-battle_depth_scale/p[3];}
    if(fabs(values[0]-values[1])>2e-6||fabs(values[0]-values[2])>2e-6)return false;
    bias=(float)((values[0]+values[1]+values[2])/3);return true;
}
static void prepare_triangle_depth(const std::vector<Primitive>&old,const std::vector<Primitive>&cur){
    for(auto&op:current.ops)if(projected_geometry(op))op.prior_depth_bias.assign(op.vertices.size()/3,NAN);
    for(const auto&tri:cur){auto&op=current.ops[tri.op];if(!projected_geometry(op))continue;float current_bias,prior_bias;
        if(!triangle_bias(&op.vertices[tri.start],current_bias))continue;prior_bias=current_bias;
        if(tri.prior>=0){const auto&before=old[tri.prior];float candidate;
            if(triangle_bias(&previous.ops[before.op].vertices[before.start],candidate))prior_bias=candidate;}
        op.prior_depth_bias[tri.start/3]=prior_bias;
    }
    for(auto&entry:outgoing_draws){auto&op=entry.op;op.prior_depth_bias.assign(op.vertices.size()/3,NAN);
        for(unsigned i=0;i+2<op.vertices.size();i+=3){Vertex before[3];for(unsigned v=0;v<3;v++)memcpy(before[v].p,op.prior_positions[i+v].data(),sizeof(before[v].p));float bias;
            if(!triangle_bias(before,bias))continue;op.prior_depth_bias[i/3]=bias;
            for(unsigned v=0;v<3;v++)op.vertices[i+v].p[2]=(float)(bias+battle_depth_scale/op.vertices[i+v].p[3]);}
    }
}
static void interpolate_depth(const Op&op,std::vector<Vertex>&vertices,float alpha){
    for(unsigned i=0;i<op.prior_depth_bias.size();i++)if(std::isfinite(op.prior_depth_bias[i])){
        float current_bias;if(!triangle_bias(&op.vertices[i*3],current_bias))continue;
        double bias=(1-alpha)*op.prior_depth_bias[i]+alpha*current_bias;
        for(unsigned v=0;v<3;v++){unsigned at=i*3+v;if(op.motion_valid[at]&&vertices[at].p[3]>0)vertices[at].p[2]=(float)(bias+battle_depth_scale/vertices[at].p[3]);}
    }
}
