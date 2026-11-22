#pragma once
#include <array>
#include <charconv>
#include <string>
#include <string_view>

namespace update_feed {
struct Release {
    unsigned build=0;
    std::string tag, sha256;
};

inline bool number(std::string_view value,unsigned& out){
    if(value.empty()||(value.size()>1&&value.front()=='0'))return false;
    auto parsed=std::from_chars(value.data(),value.data()+value.size(),out);
    return parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size();
}

// Validate the release name before using it in a GitHub path. Windows and
// Deck have independent release tracks, even when their base versions match.
inline bool tag_matches(std::string_view tag,bool deck){
    if(tag.size()<2||tag.size()>32||tag.front()!='v')return false;
    tag.remove_prefix(1);
    for(unsigned i=0;i<3;++i){
        size_t end=tag.find_first_not_of("0123456789");
        unsigned component=0;
        if(!number(tag.substr(0,end),component))return false;
        tag.remove_prefix(end==tag.npos?tag.size():end);
        if(i!=2){
            if(tag.empty()||tag.front()!='.')return false;
            tag.remove_prefix(1);
        }
    }
    if(!deck)return tag.empty();
    constexpr std::string_view suffix="-deck.";
    if(tag.substr(0,suffix.size())!=suffix)return false;
    unsigned preview=0;
    return number(tag.substr(suffix.size()),preview)&&preview>0;
}

inline bool parse(std::string_view text,std::string_view channel,Release& release){
    if(text.empty()||text.size()>512)return false;
    std::array<std::string_view,5> lines;
    for(auto& line:lines){
        size_t end=text.find('\n');
        if(end==text.npos)return false;
        line=text.substr(0,end);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        text.remove_prefix(end+1);
    }
    unsigned build=0;
    if(!text.empty()||lines[0]!="FF8-Reborn-feed-1"||(channel!="windows"&&channel!="deck")||
       lines[1]!=channel||!number(lines[2],build)||!build||!tag_matches(lines[3],channel=="deck")||
       lines[4].size()!=64||lines[4].find_first_not_of("0123456789abcdef")!=text.npos)return false;
    release={build,std::string(lines[3]),std::string(lines[4])};
    return true;
}
}
