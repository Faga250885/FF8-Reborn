// Coordinates below are relative to the live drawable, whose size can change.
// Identify the geometry pass by the native 640 x 480 projection. Postprocess
// passes also use 4:3 targets but receive a full-screen NDC rectangle.
// Measured in native scene pixels. Only the rejection tests use this band;
// the visible projection and the game's camera keep the same dimensions.
static float turn_guard(int width,int height,int active_lines){
    if(height<=0||width*3<=height*4)return 0;
    return std::min(160.f,active_lines*.125f*width/height);
}
static bool gameplay_layout(int mode){return mode>=1&&mode<=3;}
static constexpr int after_battle_mode=4; // FFVIII's EXP, items and AP pages
static bool scenery_draw(const Op& op){
    if(op.clear||!op.target||op.mode!=GL_TRIANGLES||op.vertices.empty()||
       std::abs(op.state.projection[0]-2.f/640)>.00001f)return false;
    const auto* p=op.vertices.front().p;
    return (p[3]>0&&p[3]<.1f)||(std::abs(p[3]-1.f/3)<.00001f&&std::abs(p[2]-.00155f)<.00001f);
}
// Opening screens use the field mode word as well. Recognize their native
// composition, rather than applying a field crop to movies or real scenery.
static bool centered_menu_composite(const Op& op,int width,int height){
    const int* vp=op.state.viewport;
    return !op.clear&&!op.target&&op.vertices.size()==6&&vp[2]>0&&vp[3]>0&&
           std::abs(3*vp[2]-4*vp[3])<=4&&std::abs(2*vp[0]+vp[2]-width)<=2&&
           std::abs(2*vp[1]+vp[3]-height)<=2;
}
static Frame::Opening opening_screen(const Frame& f){
    using Opening=Frame::Opening;
    if(!f.valid||f.ops.empty()||(f.mode!=0&&f.mode!=1))return Opening::none;
    if(!centered_menu_composite(f.ops.back(),f.w,f.h))return Opening::none;
    bool title_image=false,press_prompt=false,prompt_rule=false;
    bool title_a=false,title_b=false,header=false,info=false;
    for(const auto& op:f.ops){
        if(scenery_draw(op))return Opening::none;
        if(op.clear||!op.target||op.mode!=GL_TRIANGLES||op.vertices.empty()||
           std::abs(op.state.projection[0]-2.f/640)>.00001f||op.vertices.size()>2048)continue;
        const auto* p=op.vertices.front().p;
        if(std::abs(p[3]-1.f)>.002f)continue;
        if(f.mode==0){
            // These startup sprites use normalized coordinates. The blinking
            // prompt and its underline distinguish the logo from an intro movie.
            if(op.vertices.size()!=6||p[2]!=1.f)continue;
        }else if(p[2]<0||p[2]>=.004f)continue;
        float l=1e6f,r=-1e6f,t=1e6f,b=-1e6f;
        for(const auto& v:op.vertices){l=std::min(l,v.p[0]);r=std::max(r,v.p[0]);t=std::min(t,v.p[1]);b=std::max(b,v.p[1]);}
        auto is=[&](float x,float y,float z,float w){return std::abs(l-x)<1&&std::abs(r-y)<1&&std::abs(t-z)<1&&std::abs(b-w)<1;};
        if(f.mode==0){
            const bool centered=std::abs(l+r)<.015f;
            title_image|=centered&&std::abs(l+1)<.001f&&std::abs(t+1)<.001f&&std::abs(b-1)<.001f;
            press_prompt|=centered&&r>.1f&&r<.94f&&t>-.9f&&b<-.5f&&b-t>.025f&&b-t<.16f;
            prompt_rule|=centered&&r>.2f&&r<.97f&&t>-.9f&&b<-.5f&&b>t&&b-t<.015f;
        }
        if(f.mode==1){
            title_a|=is(0,426.667f,16,306);title_b|=is(426.667f,640,16,306);
            header|=is(450,600,28,72);info|=is(50,590,74,126);
        }
    }
    if(title_image&&press_prompt&&prompt_rule)return Opening::white_title;
    if(title_a&&title_b)return Opening::black_title;
    if(header&&info)return Opening::save_menu;
    return Opening::none;
}
static void resolve_menu_layout(Frame& f,const Frame& prior){
    f.layout_override=-1;f.menu_edge=0;
    f.menu_top=28;f.menu_bottom=448;f.menu_half_width=282;
    f.opening=opening_screen(f);
    if(f.opening==Frame::Opening::white_title){f.menu_top=40;f.menu_bottom=440;}
    // The Remaster also composites a small footer below the native menu rows.
    else if(f.opening==Frame::Opening::black_title){f.menu_top=16;f.menu_bottom=458;}
    else if(f.opening==Frame::Opening::save_menu){f.menu_top=24;f.menu_bottom=458;}
    if(f.mode==after_battle_mode){
        // The header and confirmation panels on the EXP and loot pages run
        // from x=26 2/3 to 613 1/3, y=32 to 436 in FFVIII's 640x480 canvas.
        f.menu_top=32;f.menu_bottom=436;f.menu_half_width=294;
    }
    if(prior.mode==after_battle_mode&&f.mode!=after_battle_mode&&f.mode!=6)return;
    if(f.opening!=Frame::Opening::none||f.mode==after_battle_mode||f.mode==6||prior.mode==6)f.menu_edge=5;
    else if(prior.menu_edge>0)f.menu_edge=prior.menu_edge-1;
    else if(f.mode==0&&gameplay_layout(prior.mode))f.menu_edge=5;
    else if(gameplay_layout(f.mode)&&f.mode==prior.mode&&
            std::none_of(f.ops.begin(),f.ops.end(),scenery_draw)&&
            (prior.layout()==6||std::any_of(prior.ops.begin(),prior.ops.end(),scenery_draw)))f.menu_edge=5;
    if(!f.menu_edge||(!gameplay_layout(f.mode)&&f.mode!=0&&f.mode!=after_battle_mode&&f.mode!=6))return;
    bool scenery=false,ui=false;
    for(const auto& op:f.ops){
        if(op.clear||!op.target||op.mode!=GL_TRIANGLES||op.vertices.empty()||
           std::abs(op.state.projection[0]-2.f/640)>.00001f)continue;
        const auto* p=op.vertices.front().p;
        if(scenery_draw(op)){scenery=true;break;}
        if(std::abs(p[3]-1.f)>=.002f||p[2]<=0||p[2]>=.004f)continue;
        if(p[0]>2&&p[1]>2)ui=true;
        // Keep visible text and selectors in atypical submenus inside the
        // fit. Respect list scissors so off-screen rows cannot change scale.
        const auto& s=op.state;
        if(op.vertices.size()%6||s.viewport[2]<=0||s.viewport[3]<=0)continue;
        for(size_t i=0;i<op.vertices.size();i+=6){
            float l=640,r=0,t=480,b=0;
            for(size_t j=i;j<i+6;j++){const auto* v=op.vertices[j].p;
                l=std::min(l,v[0]);r=std::max(r,v[0]);t=std::min(t,v[1]);b=std::max(b,v[1]);}
            if(r-l>128||b-t>80)continue;
            if(s.scissor_on){
                l=std::max(l,640.f*(s.scissor[0]-s.viewport[0])/s.viewport[2]);
                r=std::min(r,640.f*(s.scissor[0]+s.scissor[2]-s.viewport[0])/s.viewport[2]);
                t=std::max(t,480.f*(1.f-float(s.scissor[1]+s.scissor[3]-s.viewport[1])/s.viewport[3]));
                b=std::min(b,480.f*(1.f-float(s.scissor[1]-s.viewport[1])/s.viewport[3]));
            }
            if(l>=r||t>=b||r<=0||l>=640||b<=0||t>=480)continue;
            f.menu_top=std::min(f.menu_top,std::max(0,int(std::floor(t))));
            f.menu_bottom=std::max(f.menu_bottom,std::min(480,int(std::ceil(b))));
            f.menu_half_width=std::max(f.menu_half_width,std::min(320,int(std::ceil(std::max(320-l,r-320)))));
        }
    }
    // The mode word can switch before the last field fade or the first
    // actual menu draw. Pure composite frames inherit the preceding layout.
    if(scenery&&gameplay_layout(f.mode))return;
    if(f.opening!=Frame::Opening::none||f.mode==after_battle_mode)f.layout_override=6;
    else if(scenery&&gameplay_layout(prior.layout()))f.layout_override=prior.layout();
    else if(ui&&!scenery)f.layout_override=6;
    else if(!scenery&&(gameplay_layout(prior.layout())||prior.layout()==6))f.layout_override=prior.layout();
    if(f.layout()==6&&prior.layout()==6&&f.opening==prior.opening&&
       (f.mode==after_battle_mode)==(prior.mode==after_battle_mode)){
        f.menu_top=std::min(f.menu_top,prior.menu_top);
        f.menu_bottom=std::max(f.menu_bottom,prior.menu_bottom);
        f.menu_half_width=std::max(f.menu_half_width,prior.menu_half_width);
    }
}
struct WideView {
    int drawable_width,drawable_height,inner_width=0,outer_width=0,outer_height=0;
    int scene_width=0,scene_height=0;
    int menu_y=0;
    bool menu=false;
    std::array<int,4> result_viewport{};
    bool menu_composite(const Op& op)const{
        return centered_menu_composite(op,drawable_width,drawable_height);
    }
    float horizontal=1;
    bool battle=false;
    GLuint map_target=0;
    int map_right=0;
    const Op* full_map=nullptr;
    int atlas_width=0;
    int command_right=244;
    bool corner_hud=false;
    bool summon_hud=false;
    float menu_cursor_depth=.001f;
    GLuint battle_backdrop_target=0;
    std::vector<std::pair<const Op*,const Op*>> battle_screen_meshes;
    explicit WideView(const Frame& frame,bool requested):drawable_width(frame.w),drawable_height(frame.h),battle(frame.layout()==3){
        const int mode=frame.layout();
        if(!requested||(!gameplay_layout(mode)&&mode!=6)||frame.w<1||frame.h<1)return;
        if(mode==6){
            int top=frame.menu_top,bottom=frame.menu_bottom,half_width=frame.menu_half_width;
            if(frame.mode==after_battle_mode){
                // Results are composed natively even at 60/120 FPS. Only
                // fit the centered 4:3 image submitted on this actual swap.
                auto last=std::find_if(frame.ops.rbegin(),frame.ops.rend(),
                    [](const Op& op){return !op.target;});
                if(last==frame.ops.rend()||!frame.valid||!menu_composite(*last))return;
                const auto* vp=last->state.viewport;
                if(vp[0]<0||vp[1]<0||vp[0]+vp[2]>frame.w||vp[1]+vp[3]>frame.h)return;
                std::copy_n(vp,4,result_viewport.begin());
                include_result_icons(frame,last,top,bottom,half_width);
            }
            const double scale=std::min(double(frame.h)/(bottom-top),double(frame.w)/(2*half_width));
            menu=true;outer_width=int(std::lround(640*scale));outer_height=int(std::lround(480*scale));
            menu_y=int(std::lround(frame.h*.5-(480-(top+bottom)*.5)*scale));
            return;
        }
        if(frame.ops.empty())return;
        const auto& end=frame.ops.back();
        const int* vp=end.state.viewport;
        if(end.target||end.clear||end.vertices.size()!=6||vp[1]!=0||vp[3]!=frame.h||
           std::abs(vp[2]*3-vp[3]*4)>4||std::abs(2*vp[0]+vp[2]-frame.w)>2)return;
        // Verify a gameplay scissor, not the title, a movie or a save screen.
        const int active_lines=mode==3?432:448;
        inner_width=vp[2];
        bool scene=false;
        for(const auto&op:frame.ops){
            const auto&s=op.state;
            // Low output resolutions can still render the native scene at
            // 1280x960. Identify that canvas independently of the final blit.
            auto target=frame.targets.find(op.target);
            if(op.target&&s.scissor_on&&s.viewport[2]>0&&s.viewport[3]>0&&
               std::abs(s.viewport[2]*3-s.viewport[3]*4)<=4&&
               target!=frame.targets.end()&&target->second.w==s.viewport[2]&&target->second.h==s.viewport[3]&&
               std::abs(s.scissor[3]-s.viewport[3]*active_lines/480)<=2&&s.scissor[2]>=s.viewport[2]-2&&
               std::abs(s.projection[0]-2.f/640)<.00001f){scene=true;scene_width=s.viewport[2];scene_height=s.viewport[3];break;}
        }
        if(!scene)return;
        outer_height=int(std::lround(frame.h*480./active_lines));
        int natural_width=int(std::lround(inner_width*480./active_lines));
        outer_width=mode==1?std::min(natural_width,frame.w):frame.w;
        horizontal=std::min(1.f,float(natural_width)/outer_width);
        if(mode==2){
            for(const auto& op:frame.ops){
                if(!op.target||op.mode!=GL_TRIANGLES||op.vertices.empty()||
                   op.state.viewport[2]!=scene_width||!map_layer(op.vertices.front()))continue;
                float l=1000,r=-1000,t=1000,b=-1000;
                for(const auto& v:op.vertices){
                    l=std::min(l,v.p[0]);r=std::max(r,v.p[0]);
                    t=std::min(t,v.p[1]);b=std::max(b,v.p[1]);
                }
                // Native atlas rectangle (full or inset), or a mesh batch of the globe.
                if(op.vertices.size()==6&&l==64&&r==576&&t==48&&b==432){
                    full_map=&op;map_target=0;map_right=0;
                    // Fit the 512 x 384 atlas, centered inside the native 640 x 480
                    // composition. Scale the final viewport so the game's points,
                    // cursor, label and scissor rectangles share its transform.
                    const double scale=std::min(double(frame.w)/(r-l),double(frame.h)/(b-t));
                    atlas_width=int(std::lround((r-l)*scale));
                    outer_width=int(std::lround(640.*scale));
                    outer_height=int(std::lround(480.*scale));
                    horizontal=1;
                    break;
                }
                else if(op.vertices.size()==6&&l==372&&r==628&&t==264&&b==456){map_target=op.target;map_right=628;}
                else if(op.vertices.size()>6&&l>=436&&r<=604&&r-l>=80&&t>=290&&b<=464){map_target=op.target;map_right=600;}
            }
        }
        if(battle&&horizontal<1){
            find_battle_backdrop(frame.ops);
            for(const auto& op:frame.ops){
                if(!op.target||op.clear||op.state.viewport[2]!=scene_width||
                   std::abs(op.state.projection[0]-2.f/640)>.00001f)continue;
                // The trigger gauge is one window with two long horizontal
                // rails. Its frame tiles and moving sprites share native X.
                if(has_gauge_rail(op)){corner_hud=false;break;}
                inspect_battle_hud(op);
                if(hud_clip(op.state)!=1)continue;
                corner_hud=true;
                const int x=int(std::lround(640.*op.state.scissor[0]/scene_width));
                if(x<436)command_right=std::max(command_right,x);
            }
        }
    }
    static bool map_layer(const Vertex& v){
        return std::abs(v.p[3]-1.f/3)<.00001f&&std::abs(v.p[2]-.00155f)<.00001f;
    }
    bool world_map_draw(const Op& op) const {
        if(!map_target||op.target!=map_target||op.mode!=GL_TRIANGLES||op.vertices.empty())return false;
        if(!map_layer(op.vertices.front()))return false;
        for(const auto& v:op.vertices)
            if(!map_layer(v)||v.p[0]<344||v.p[0]>652||v.p[1]<238||v.p[1]>482)return false;
        return true;
    }
    int hud_clip(const State& s) const {
        const int* clip=s.scissor;
        if(!battle||!s.scissor_on||scene_width<=0||scene_height<=0||clip[2]>=scene_width)return 0;
        int x=int(std::lround(640.*clip[0]/scene_width));
        int end=int(std::lround(640.*(clip[0]+clip[2])/scene_width));
        int h=int(std::lround(480.*clip[3]/scene_height));
        // The left edge of a name row changes when the item list opens.
        // Its shared boundary with the HP column is stable.
        if(h==30&&(end==436||x==436))return 1;
        if(x<240&&clip[1]+clip[3]<=scene_height/2)return -1;
        return 0;
    }
    // The GF nameplate and HP backing use their own native HUD clip
    // (x=0..624, y=16..448 in top-left coordinates).
    bool summon_clip(const State& s) const {
        const int* clip=s.scissor;
        return s.scissor_on&&clip[0]==0&&
               std::lround(640.*clip[2]/scene_width)==624&&
               std::lround(480.*clip[1]/scene_height)==32&&
               std::lround(480.*clip[3]/scene_height)==432;
    }
    void inspect_battle_hud(const Op& op){
        if(op.mode!=GL_TRIANGLES||op.vertices.empty()||op.vertices.size()%6||
           std::abs(op.vertices.front().p[3]-1.f)>.002f)return;
        const bool gf_clip=summon_clip(op.state);
        for(size_t i=0;i<op.vertices.size();i+=6){
            const auto* q=op.vertices.data()+i;
            Bounds b;
            if(gf_clip&&!summon_hud&&q->p[1]>=358&&q->p[1]<=448&&
               q[0].uv[0][1]==.375f&&flat_rect(q,b)&&b.l==244&&b.r==388&&
               (b.t==358||b.t==388||b.t==418)&&b.b==b.t+30)summon_hud=true;
            // The countdown is drawn before the command hand. Its last
            // digit advances the hand's depth, even as the colon blinks.
            if(q->p[0]>=520&&q->p[0]<=606&&q->p[2]>=.001f&&q->p[2]<.00125f&&
               q[0].uv[0][1]==.625f&&flat_rect(q,b)&&b.t==36&&b.b==68&&b.r==606&&b.l==574){
                constexpr float depth_step=.0005f/31;
                menu_cursor_depth=q->p[2]+depth_step;
            }
        }
    }
    int clipped_hud_side(const State& s) const {
        if(!corner_hud)return 0;
        return summon_hud&&summon_clip(s)?1:hud_clip(s);
    }

    int sprite_side(const Vertex* q,int clip_side) const {
        if(!corner_hud)return 0;
        float l=q->p[0],r=l,t=q->p[1],b=t;
        for(int i=0;i<6;i++){
            const float* p=q[i].p;
            // Alternate font atlas glyphs use 0.9997 instead of exactly 1.
            if(std::abs(p[3]-1.f)>.002f||p[2]<0||p[2]>.004f)return 0;
            l=std::min(l,p[0]);r=std::max(r,p[0]);
            t=std::min(t,p[1]);b=std::max(b,p[1]);
        }
        if(l<0||r>640||t<208||b>468)return 0;
        for(int i=0;i<6;i++){
            const float* p=q[i].p;
            if((p[0]!=l&&p[0]!=r)||(p[1]!=t&&p[1]!=b))return 0;
        }
        if(clip_side)return clip_side;
        // The floating target hand uses the same artwork as the command
        // hand, at a different depth. Keep it on the battler.
        if(r-l==48&&b-t==32&&q->p[2]>menu_cursor_depth+.00001f&&q[0].uv[0][1]==0.f)return 0;
        // The two tiles of the floating target label ("Blanco") can be
        // batched with UI artwork even when the battler is behind a window.
        if(r-l==32&&b-t==32&&q->p[2]>=.00199f&&q->p[2]<.0024f&&
           std::abs(q[0].uv[0][1]-.5f)<.001f&&
           (q[0].uv[0][0]>.999f||std::abs(q[0].uv[0][0]-.125f)<.001f))return 0;
        if(t>=344&&l>=436)return 1;
        if(t>=296&&r<=command_right)return -1;
        return 0;
    }

    const Vertex* position_hud(const Op&op,const Vertex* vertices,std::vector<Vertex>&buffer) const {
        if(!corner_hud||horizontal>=1||!outer_width||!op.target||op.mode!=GL_TRIANGLES||
           op.vertices.empty()||op.vertices.size()%6||std::abs(op.vertices[0].p[3]-1.f)>.002f||
           op.state.viewport[2]!=scene_width||std::abs(op.state.projection[0]-2.f/640)>.00001f)return vertices;
        const float distance=320*(1/horizontal-1);
        const int clip_side=clipped_hud_side(op.state);
        // Some submissions batch several windows or text styles in one draw.
        // Classify complete original sprites before translating the live pose.
        for(size_t i=0;i<op.vertices.size();i+=6){
            const int side=sprite_side(op.vertices.data()+i,clip_side);
            if(!side)continue;
            if(vertices!=buffer.data()){buffer=op.vertices;vertices=buffer.data();}
            for(size_t j=i;j<i+6;j++)buffer[j].p[0]+=side*distance;
        }
        return vertices;
    }
    struct Bounds { float l,r,t,b; };
    static bool flat_rect(const Vertex* q,Bounds& box,int n=6){
        box={q[0].p[0],q[0].p[0],q[0].p[1],q[0].p[1]};
        for(int i=1;i<n;i++){
            if(q[i].p[2]!=q[0].p[2]||q[i].p[3]!=q[0].p[3])return false;
            box.l=std::min(box.l,q[i].p[0]);box.r=std::max(box.r,q[i].p[0]);
            box.t=std::min(box.t,q[i].p[1]);box.b=std::max(box.b,q[i].p[1]);
        }
        unsigned corners=0;
        for(int i=0;i<n;i++){
            if((q[i].p[0]!=box.l&&q[i].p[0]!=box.r)||(q[i].p[1]!=box.t&&q[i].p[1]!=box.b))return false;
            corners|=1u<<((q[i].p[0]==box.r?1:0)+(q[i].p[1]==box.b?2:0));
        }
        return corners==15;
    }
    static bool has_gauge_rail(const Op& op){
        if(op.mode!=GL_TRIANGLES||op.vertices.size()%6||op.vertices.empty()||
           std::abs(op.vertices.front().p[3]-1)>.002f)return false;
        for(size_t i=0;i<op.vertices.size();i+=6){
            const auto* q=op.vertices.data()+i;
            if(q->p[1]<350||q->p[1]>444||q->p[2]<0||q->p[2]>.004f||
               std::abs(q->p[3]-1)>.002f)continue;
            Bounds b;
            if(flat_rect(q,b)&&b.l>=8&&b.l<=56&&b.r>=584&&b.r<=632&&
               b.b>b.t&&b.b-b.t<=6)return true;
        }
        return false;
    }
    static bool uses_texture(const State& s){
        if(s.program){
            for(const auto& u:s.uniforms)if(u.type==GL_SAMPLER_2D)return true;
        }else for(const auto& t:s.texture)if(t.enabled)return true;
        return false;
    }
    bool battle_canvas_draw(const Op& op) const {
        const auto& s=op.state;
        return !op.clear&&op.mode==GL_TRIANGLES&&!op.vertices.empty()&&op.vertices.size()%3==0&&
               s.viewport[2]==scene_width&&s.viewport[3]==scene_height&&
               s.scissor_on&&s.scissor[0]==0&&s.scissor[2]>=scene_width-2&&
               std::abs(s.scissor[3]-scene_height*432/480)<=2&&
               std::abs(s.projection[0]-2.f/640)<.00001f;
    }
    bool backdrop_draw(const Op& op) const {
        const auto& s=op.state;
        if(!s.blend||s.src_rgb!=GL_ONE||s.dst_rgb!=GL_ONE||s.eq_rgb!=GL_FUNC_ADD||
           !s.depth||s.depth_mask||s.alpha||s.cull||!battle_canvas_draw(op))return false;
        // Retain the focal scale of foreground effects, actors and UI.
        return std::all_of(op.vertices.begin(),op.vertices.end(),[](const Vertex& v){
            return v.p[2]>.008f&&v.p[3]>0&&v.p[3]<.01f;
        });
    }
    bool screen_mesh_draw(const Op& op) const {
        const auto& s=op.state;
        if(!s.depth||!s.depth_mask||s.scissor[1]!=scene_height/20||op.vertices.size()<12||
           !battle_canvas_draw(op))return false;
        const auto& first=op.vertices.front();
        if(first.p[3]!=1.f||first.p[2]<=.004f)return false;
        return std::all_of(op.vertices.begin(),op.vertices.end(),[&](const Vertex& v){
            return v.p[3]==1.f&&v.p[2]==first.p[2]&&v.p[0]>=-2.f&&v.p[0]<=642.f;
        });
    }
    struct BattleCoverage {
        static constexpr int rows=7;
        std::array<unsigned,3> covered{};
        size_t vertices=0;
        void add(const Op& op){
            vertices+=op.vertices.size();
            if(std::all_of(covered.begin(),covered.end(),[](unsigned mask){return mask==(1u<<rows)-1;}))return;
            for(size_t j=0;j<op.vertices.size();j+=3){
                const float* a=op.vertices[j].p;
                const float* b=op.vertices[j+1].p;
                const float* c=op.vertices[j+2].p;
                const float l=std::min({a[0],b[0],c[0]}),r=std::max({a[0],b[0],c[0]});
                const float t=std::min({a[1],b[1],c[1]}),bottom=std::max({a[1],b[1],c[1]});
                const auto edge=[](const float* u,const float* v,float x,float y){
                    return (v[0]-u[0])*(y-u[1])-(v[1]-u[1])*(x-u[0]);
                };
                if(std::abs(edge(a,b,c[0],c[1]))<.001f)continue;
                for(size_t column=0;column<covered.size();++column){
                    constexpr float samples[]={8.f,320.f,632.f};
                    const float x=samples[column];
                    if(x<l||x>r)continue;
                    for(int row=0;row<rows;++row){
                        const unsigned bit=1u<<row;
                        const float y=36.f+68.f*row;
                        if((covered[column]&bit)||y<t||y>bottom)continue;
                        const float p=edge(a,b,x,y),q=edge(b,c,x,y),r=edge(c,a,x,y);
                        if((p>=0&&q>=0&&r>=0)||(p<=0&&q<=0&&r<=0))covered[column]|=bit;
                    }
                }
            }
        }
        bool fills_picture() const {
            return vertices>=192&&std::all_of(covered.begin(),covered.end(),[](unsigned mask){
                return std::bitset<rows>(mask).count()>=5;
            });
        }
    };
    void find_battle_backdrop(const std::vector<Op>& ops){
        auto clear=std::find_if(ops.begin(),ops.end(),[&](const Op& op){
            return op.target&&(op.clear&GL_COLOR_BUFFER_BIT)&&op.state.viewport[2]==scene_width&&
                   op.state.viewport[3]==scene_height;
        });
        if(clear==ops.end())return;
        auto first=clear;
        auto last=std::find_if(clear+1,ops.end(),[&](const Op& op){return op.target!=clear->target;});
        for(auto i=clear+1;i!=last;++i)if(i->clear&GL_COLOR_BUFFER_BIT)first=i;

        // Replacement scenery may be interleaved with actors. Coverage must
        // reach across the picture before widening the additive backdrop.
        BattleCoverage backdrop;
        for(auto i=first+1;i!=last;){
            if(backdrop_draw(*i))backdrop.add(*i);
            if(!screen_mesh_draw(*i)){++i;continue;}

            // One screen distortion can submit many adjacent texture pages.
            // Keep those pages together, without moving individual sprites.
            const auto begin=i++;
            BattleCoverage mesh;
            mesh.add(*begin);
            while(i!=last&&screen_mesh_draw(*i)){
                const auto& a=begin->state;
                const auto& b=i->state;
                if(i->vertices.front().p[2]!=begin->vertices.front().p[2]||
                   a.program!=b.program||a.blend!=b.blend||a.alpha!=b.alpha||a.cull!=b.cull||
                   a.src_rgb!=b.src_rgb||a.dst_rgb!=b.dst_rgb||a.eq_rgb!=b.eq_rgb)break;
                mesh.add(*i++);
            }
            if(mesh.fills_picture())battle_screen_meshes.emplace_back(&*begin,&*(i-1));
        }
        if(backdrop.fills_picture())battle_backdrop_target=clear->target;
    }
    bool fitted_battle_layer(const Op& op) const {
        if(battle_backdrop_target&&op.target==battle_backdrop_target&&backdrop_draw(op))return true;
        const std::less<const Op*> less;
        return std::any_of(battle_screen_meshes.begin(),battle_screen_meshes.end(),[&](const auto& range){
            return !less(&op,range.first)&&!less(range.second,&op);
        });
    }
    static void include_result_icons(const Frame& frame,std::vector<Op>::const_reverse_iterator copy,
                                     int& top,int& bottom,int& half_width){
        const GLuint source=copy->state.texture[0].id;
        const auto surface=frame.targets.find(source);
        if(!source||surface==frame.targets.end()||surface->second.w<=0||surface->second.h<=0)return;
        const double sx=640./surface->second.w,sy=480./surface->second.h;
        // Battle assists are small textured quads added to the final surface
        // after the game copies the result page. Their projection differs
        // from the panel projection. Measure only the visible part this swap.
        for(auto it=std::next(copy);it!=frame.ops.rend();++it){
            const auto& op=*it;
            if(op.target!=source)continue;
            if(op.clear&GL_COLOR_BUFFER_BIT)break;
            const auto& s=op.state;
            if(op.mode!=GL_TRIANGLES||op.vertices.size()!=6||!uses_texture(s))continue;
            Bounds quad;
            if(!flat_rect(op.vertices.data(),quad))continue;
            if(quad.l==-1&&quad.r==1&&quad.t==-1&&quad.b==1){
                const int w=surface->second.w,h=surface->second.h;
                const bool full_clip=!s.scissor_on||(s.scissor[0]==0&&s.scissor[1]==0&&
                                                     s.scissor[2]>=w&&s.scissor[3]>=h);
                if(!s.blend&&full_clip&&frame.targets.count(s.texture[0].id)&&s.viewport[0]==0&&
                   s.viewport[1]==0&&s.viewport[2]==w&&s.viewport[3]==h)break;
                continue; // full-page copies and effects use clip coordinates directly
            }
            if(op.vertices[0].p[2]!=0||op.vertices[0].p[3]!=1)continue;
            double left=640,right=0,low=480,high=0;
            bool projected=true;
            for(const auto& v:op.vertices){
                // FFVIII's transformed sprite shader uses gl_ProjectionMatrix
                // directly. Fixed-function sprites also apply the modelview.
                float point[4];
                std::copy_n(v.p,4,point);
                if(!s.program){
                    double eye[4];transform(s.model,point,eye);std::copy_n(eye,4,point);
                }
                double clip[4];transform(s.projection,point,clip);
                if(!(clip[3]>0)||!std::isfinite(clip[3])){projected=false;break;}
                const double x=(s.viewport[0]+(1+clip[0]/clip[3])*.5*s.viewport[2])*sx;
                const double y=(s.viewport[1]+(1+clip[1]/clip[3])*.5*s.viewport[3])*sy;
                if(!std::isfinite(x)||!std::isfinite(y)){projected=false;break;}
                left=std::min(left,x);right=std::max(right,x);
                low=std::min(low,y);high=std::max(high,y);
            }
            // Reject large layers before clipping: a scissored fullscreen
            // overlay still represents a screen effect, not an icon.
            if(!projected||right-left>80||high-low>80)continue;
            if(s.scissor_on){
                left=std::max(left,s.scissor[0]*sx);right=std::min(right,(s.scissor[0]+s.scissor[2])*sx);
                low=std::max(low,s.scissor[1]*sy);high=std::min(high,(s.scissor[1]+s.scissor[3])*sy);
            }
            left=std::max(left,0.);right=std::min(right,640.);
            low=std::max(low,0.);high=std::min(high,480.);
            if(left>=right||low>=high)continue;
            top=std::min(top,int(std::floor(480-high)));bottom=std::max(bottom,int(std::ceil(480-low)));
            half_width=std::max(half_width,int(std::ceil(std::max(320-left,right-320))));
        }
    }
    // The native battle canvas is 640 x 480, with rows 24..456 visible.
    // Flashes may be opaque, additive or textured. Use the screen footprint
    // and constant 2D depth so a projected spell or a local HUD sprite keeps
    // its size even when it shares a draw with a flash.
    const Vertex* battle_layers(const Op& op,const Vertex* vertices,std::vector<Vertex>& result,int count) const {
        int step=0;
        if(op.mode==GL_TRIANGLES&&count%6==0)step=6;
        else if(op.mode==GL_QUADS&&count%4==0)step=4;
        else if((op.mode==GL_TRIANGLE_STRIP||op.mode==GL_TRIANGLE_FAN)&&count==4)step=4;
        if(!step)return vertices;
        auto bounds=[&](int i,Bounds& b){
            const auto* q=op.vertices.data()+i;
            if(std::abs(q[0].p[3]-1.f)>.002f||!flat_rect(q,b,step)||
               b.l< -4||b.r>644||b.r<=b.l||b.b<=b.t)return false;
            // Effects can use a 512-line source rectangle. Measure from the
            // visible canvas instead of rejecting its padded bottom.
            if(!std::isfinite(b.t)||!std::isfinite(b.b)||b.b<24||b.t>456)return false;
            auto corner=[&](int j){return (q[j].p[0]==b.r?1:0)+(q[j].p[1]==b.b?2:0);};
            // Two adjoining triangles must share a diagonal. Four corners
            // alone also match overlapping triangles that leave a hole.
            if(step==6){
                unsigned a=0,c=0;
                for(int j=0;j<3;j++){a|=1u<<corner(j);c|=1u<<corner(j+3);}
                return (a&c)==6||(a&c)==9;
            }
            return (corner(op.mode==GL_TRIANGLE_STRIP?1:0)^corner(2))==3;
        };
        auto full=[](const Bounds& b){return b.l<=2&&b.r>=638&&b.t<=26&&b.b>=454;};
        bool edited=false;
        for(int i=0;i<count;i+=step){
            Bounds b;
            if(!bounds(i,b))continue;
            int end=i+step;
            bool cinema_bar=b.l<=2&&b.r>=638&&(b.t<=26||b.b>=454)&&b.b-b.t<=128&&
                            op.state.blend&&!uses_texture(op.state);
            if(cinema_bar)for(int j=i;j<end;j++){const auto* c=op.vertices[j].c;
                if(c[0]!=0||c[1]!=0||c[2]!=0){cinema_bar=false;break;}}
            if(!full(b)&&!cinema_bar){
                // Angelo's impact submits four 320 x 240 tiles in reverse
                // order and clips the last 48 rows. Accept a tiled overlay
                // only if its non-overlapping rectangles cover the picture.
                if((b.l>2&&b.r<638)||(b.t>26&&b.b<454))continue;
                auto visible=[](Bounds r){
                    r.l=std::max(r.l,0.f);r.r=std::min(r.r,640.f);
                    r.t=std::max(r.t,24.f);r.b=std::min(r.b,456.f);
                    return r;
                };
                auto area=[](Bounds r){return double(r.r-r.l)*(r.b-r.t);};
                std::array<Bounds,16> tiles;
                Bounds extent=visible(b);
                if(extent.b<=extent.t||extent.r<=extent.l)continue;
                tiles[0]=extent;int tile_count=1;double covered=area(extent);
                bool complete=false;
                while(end<count&&tile_count<int(tiles.size())){
                    Bounds next;
                    if(op.vertices[i].p[2]!=op.vertices[end].p[2]||!bounds(end,next))break;
                    const Bounds tile=visible(next);
                    if(tile.b<=tile.t||tile.r<=tile.l)break;
                    bool overlap=false;
                    for(int k=0;k<tile_count;k++){
                        const auto& a=tiles[k];
                        if(a.l<tile.r&&tile.l<a.r&&a.t<tile.b&&tile.t<a.b){overlap=true;break;}
                    }
                    if(overlap)break;
                    tiles[tile_count++]=tile;covered+=area(tile);
                    extent={std::min(extent.l,tile.l),std::max(extent.r,tile.r),
                            std::min(extent.t,tile.t),std::max(extent.b,tile.b)};
                    b.l=std::min(b.l,next.l);b.r=std::max(b.r,next.r);
                    end+=step;
                    if(full(extent)&&std::abs(area(extent)-covered)<.001){complete=true;break;}
                }
                if(!complete)continue;
            }
            if(!edited){result.assign(vertices,vertices+count);edited=true;}
            for(int j=i;j<end;j++){
                float x=vertices[j].p[0];
                // A 0..639 inclusive packet must reach the last pixel just
                // like its 0..640 counterpart after fractional scaling.
                if(op.vertices[j].p[0]==b.l)x=std::min(0.f,x);
                if(op.vertices[j].p[0]==b.r)x=std::max(640.f,x);
                result[j].p[0]=320+(x-320)/horizontal;
            }
            i=end-step;
        }
        return edited?result.data():vertices;
    }
    // Quarter-pixel world packet 555070 submits contiguous 512 x 128 cloud
    // tiles with a fixed atlas window and z=4095. Repeat only a missing neighbour.
    const Vertex* screen_layers(const Op& op,const Vertex* vertices,std::vector<Vertex>& result,GLsizei& count) const {
        if(horizontal>=1||!op.target||
           op.state.viewport[2]!=scene_width||std::abs(op.state.projection[0]-2.f/640)>.00001f)return vertices;
        if(fitted_battle_layer(op)){
            result.assign(vertices,vertices+count);
            for(auto& v:result)v.p[0]=320+(v.p[0]-320)/horizontal;
            return result.data();
        }
        if(count<4)return vertices;
        if(battle)return battle_layers(op,vertices,result,count);
        if(op.mode!=GL_TRIANGLES||count<6||count%6)return vertices;
        const bool cloud_layer=std::abs(op.vertices[0].p[3]-1.f/4095)<1e-7f;
        const bool overlay=op.state.blend&&!uses_texture(op.state);
        if(!overlay&&!cloud_layer)return vertices;
        const float margin=320*(1/horizontal-1);
        bool edited=false;
        auto edit=[&](){if(!edited){result.assign(vertices,vertices+count);edited=true;}};
        for(int i=0;i<count;i+=6){
            const auto* q=op.vertices.data()+i;
            Bounds b;
            if(!flat_rect(q,b))continue;
            // Full-width colour overlays can be batched with unrelated quads,
            // split into horizontal strips, or use the game's vertex shader.
            bool covers_width=b.l>=-2&&b.l<=1&&b.r>=639&&b.r<=642;
            if(overlay&&!covers_width&&count==12){
                Bounds other;
                if(flat_rect(op.vertices.data()+(i==0?6:0),other)&&other.t==b.t&&other.b==b.b&&
                   b.t<=24&&b.b>=456&&(other.r==b.l||other.l==b.r))
                    covers_width=std::abs(std::min(b.l,other.l))<=2&&std::abs(std::max(b.r,other.r)-640)<=2;
            }
            if(overlay&&covers_width){
                edit();
                for(int j=i;j<i+6;j++)result[j].p[0]=320+(vertices[j].p[0]-320)/horizontal;
            }else if(cloud_layer&&std::abs(b.r-b.l-512)<.1f&&std::abs(b.b-b.t-128)<.1f){
                float u0=1,u1=0,v0=1,v1=0;
                for(int j=0;j<6;j++){
                    u0=std::min(u0,q[j].uv[0][0]);u1=std::max(u1,q[j].uv[0][0]);
                    v0=std::min(v0,q[j].uv[0][1]);v1=std::max(v1,q[j].uv[0][1]);
                }
                if(u0>.016f||u1<.98f||std::abs(v0-.254f)>.01f||std::abs(v1-.491f)>.01f)continue;
                auto append=[&](float offset){
                    edit();
                    for(int j=i;j<i+6;j++){Vertex v=vertices[j];v.p[0]+=offset;result.push_back(v);}
                };
                float left=vertices[i].p[0],right=left;
                for(int j=i+1;j<i+6;j++){
                    left=std::min(left,vertices[j].p[0]);right=std::max(right,vertices[j].p[0]);
                }
                const float extent=margin+turn_guard(drawable_width,drawable_height,448);
                if(b.l<=0&&b.r>0)for(float offset=0;left+offset>-extent;offset-=512)append(offset-512);
                if(b.l<640&&b.r>=640)for(float offset=0;right+offset<640+extent;offset+=512)append(offset+512);
            }
        }
        if(edited){count=(GLsizei)result.size();return result.data();}
        return vertices;
    }
    void apply_output(const State& s,int y) const {
        const int x=(drawable_width-outer_width)/2;
        glViewport(x,y,outer_width,outer_height);
        if(!s.scissor_on)return;
        const int* vp=s.viewport;
        const double sx=double(outer_width)/vp[2],sy=double(outer_height)/vp[3];
        int l=int(std::floor(x+(s.scissor[0]-vp[0])*sx)),b=int(std::floor(y+(s.scissor[1]-vp[1])*sy));
        int r=int(std::ceil(x+(s.scissor[0]+s.scissor[2]-vp[0])*sx)),t=int(std::ceil(y+(s.scissor[1]+s.scissor[3]-vp[1])*sy));
        glScissor(l,b,r-l,t-b);
    }
    void apply(const Op&op) const {
        if(!outer_width||op.clear)return;
        const State&s=op.state;
        if(menu){
            if(menu_composite(op))apply_output(s,menu_y);
            return;
        }
        if(!op.target&&s.viewport[2]==inner_width&&s.viewport[3]==drawable_height){
            apply_output(s,(drawable_height-outer_height)/2);
            return;
        }
        if(!op.target||horizontal>=1||s.viewport[2]!=scene_width||s.viewport[3]!=scene_height||
           std::abs(s.projection[0]-2.f/640)>.00001f)return;

        float projection[16];memcpy(projection,s.projection,sizeof(projection));
        for(int k=0;k<16;k+=4)projection[k]*=horizontal;
        float shift=clipped_hud_side(s)*(1-horizontal);
        if(world_map_draw(op)){
            // Keep the same 12-unit right inset as the native rectangular minimap.
            const float extra=320*(1/horizontal-1);
            shift=(extra+std::min(extra,float(628-map_right)))*horizontal/320;
            projection[12]+=shift;
        }

        glMatrixMode(GL_PROJECTION);glLoadMatrixf(projection);
        if(s.scissor_on&&(s.scissor[0]!=0||s.scissor[2]!=scene_width)){
            // Some native clips use an inclusive last pixel. Compressing a
            // nearly full-width clip would cut every wide flash back to 4:3.
            if(battle&&std::abs(s.scissor[0])<=1&&std::abs(s.scissor[0]+s.scissor[2]-scene_width)<=2){
                glScissor(0,s.scissor[1],scene_width,s.scissor[3]);return;
            }
            const float center=scene_width*.5f;
            float native_left=float(s.scissor[0]);
            if(battle&&shift>0&&std::lround(640.*(s.scissor[0]+s.scissor[2])/scene_width)==436){
                // Stock clips the start of long names behind the open list.
                // Release only as much of this occlusion as the two panels
                // have moved apart, retaining the original name column.
                native_left=std::max(244.f*scene_width/640,native_left-scene_width*(1/horizontal-1));
            }
            int left=int(std::floor(center*(1+shift)+(native_left-center)*horizontal));
            int right=int(std::ceil(center*(1+shift)+(s.scissor[0]+s.scissor[2]-center)*horizontal));
            glScissor(left,s.scissor[1],right-left,s.scissor[3]);
        }
    }
};

// A field exit can keep presenting the native 4:3 composite throughout a
// load, including swaps without a captured draw. Keep the last confirmed
// scene's fit until the next scene supplies its own projection.
static bool resolve_travel_layout(Frame& f,const Frame& prior,const WideView& view){
    const int layout=f.layout();
    if((layout==1||layout==2)&&view.scene_width&&view.outer_width&&!view.full_map){
        f.travel=Frame::Travel::scene;
        return false;
    }
    if(prior.travel==Frame::Travel::none||view.full_map||layout==6||
       (f.mode!=0&&f.mode!=-1&&f.mode!=1&&f.mode!=2))return false;
    // A different native presentation (e.g. a movie) ends the transition.
    // An empty swap or a centred 4:3 final copy is still part of the fade.
    if(f.valid&&!f.ops.empty()){
        const auto& last=f.ops.back();
        if(!last.clear&&!last.target&&!std::any_of(f.ops.rbegin(),f.ops.rend(),
                [&](const Op& op){return view.menu_composite(op);}))return false;
    }
    f.travel=Frame::Travel::fade;
    f.layout_override=prior.layout();
    return layout!=f.layout();
}
