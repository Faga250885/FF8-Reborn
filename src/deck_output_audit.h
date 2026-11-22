// An opt-in GPU diagnostic packs final-backbuffer samples into the next native
// parity transfer. No query, fence wait or readback occurs in SwapBuffers.
static bool deck_output_diagnostics=false;
struct DeckOutputAudit {
    static constexpr int columns=12,rows=8,capacity=12;
    enum class Pass { native, wide_native, wide_replay, endpoint, intermediate };
    struct Stamp { unsigned tick,draw_tick,sequence; int mode; float alpha; double time; Pass pass; };
    struct Observation { Stamp stamp; unsigned lit; };
    Surface strip;
    Stamp pending[capacity]{},submitted[capacity]{};
    unsigned count=0,submitted_count=0,sequence=0,inspected=0,flashes=0;
    bool disabled=false;
    unsigned consecutive_slow=0;
    Observation older{},prior{},examples[8]{};

    bool before_swap(float alpha,double time,bool scaled,bool replayed){
        ++sequence;
        if(disabled||count>=capacity||width<=0||height<=0||current.mode<1||current.mode>3||
           current.menu_edge||current.layout()!=current.mode||retro_panel::is_open())return false;
        const double begin=now();
        const bool allocated=strip.fbo!=0;
        try{
            resize(strip,capacity*columns,rows,false);
            blit(0,strip.fbo,width,height,columns,rows,GL_COLOR_BUFFER_BIT,0,0,0,count*columns);
        }catch(const std::exception&){disabled=true;return false;}
        // A driver can serialize a framebuffer transfer. Drop future probes
        // if even submitting this tiny blit takes a substantial part of a slot.
        if(allocated){
            consecutive_slow=now()-begin>.002?consecutive_slow+1:0;
            if(consecutive_slow>=3)disabled=true;
        }
        const Pass pass=alpha>=0?(alpha>0&&alpha<1?Pass::intermediate:Pass::endpoint):
            scaled?Pass::wide_native:replayed?Pass::wide_replay:Pass::native;
        pending[count++]={sources,alpha<0?sources:displayed_source,sequence,current.mode,alpha,time,pass};
        return true;
    }
    void submit(GLuint target){
        submitted_count=count;
        if(!count)return;
        std::copy_n(pending,count,submitted);count=0;
        blit(strip.fbo,target,capacity*columns,rows,capacity*columns,rows,GL_COLOR_BUFFER_BIT,0,0,180);
    }
    void collect(const BYTE* samples){
        for(unsigned index=0;index<submitted_count;++index){
            Observation seen{submitted[index],0};
            for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x){
                const BYTE* p=samples+4*(160*y+columns*index+x);
                seen.lit+=std::max({p[0],p[1],p[2]})>16;
            }
            if(older.stamp.mode==seen.stamp.mode&&prior.stamp.mode==seen.stamp.mode&&
               seen.stamp.sequence==prior.stamp.sequence+1&&prior.stamp.sequence==older.stamp.sequence+1&&
               seen.stamp.time-older.stamp.time<.25&&older.lit>=24&&prior.lit==0&&seen.lit>=24){
                if(flashes<8)examples[flashes]=prior;
                ++flashes;
            }
            older=prior;prior=seen;++inspected;
        }
        submitted_count=0;
    }
    template<class Write> void report(Write write){
        if(!inspected&&!disabled)return;
        char line[200];
        snprintf(line,sizeof(line),"Deck output probes | %u swaps sampled before display | %u isolated dark | monitor %s\n",
                 inspected,flashes,disabled?"inactive":"active");write(line);
        for(unsigned i=0;i<std::min(8u,flashes);++i){
            const auto& s=examples[i].stamp;
            const char* names[]={"native","native widescreen layout","widescreen replay","endpoint","intermediate"};
            snprintf(line,sizeof(line),"Deck dark output | mode %d | tick %u | drawn tick %u | %s | alpha %.3f\n",
                s.mode,s.tick,s.draw_tick,names[static_cast<unsigned>(s.pass)],s.alpha);write(line);
        }
        flashes=inspected=0;
    }
};
static DeckOutputAudit deck_output;
