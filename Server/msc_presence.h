#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>
namespace dingosdk::server::msc_presence {
inline constexpr std::string_view prefix="-MSC-Midnight Skate Club-";
inline bool supported(std::string_view name) {return name.starts_with(prefix);}
inline bool parse_report(std::string_view text,bool& enabled) {
    if(!text.starts_with("/jish-fp ") || text.size()<10 || (text[9]!='0' && text[9]!='1'))return false;
    enabled=text[9]=='1';
    if(text.size()==10)return true;
    if(text[10]!=' ' || text.size()<12 || text.size()>21)return false;
    std::uint64_t sequence{};
    for(const char c:text.substr(11)) {if(c<'0'||c>'9')return false;sequence=sequence*10+static_cast<unsigned>(c-'0');}
    return sequence<=0xffffffffULL;
}
inline std::string display_name(std::string_view name,bool enabled,std::size_t maximum) {
    if(!enabled || name.starts_with("[FP] "))return std::string(name);
    const auto space=maximum>5 ? maximum-5:0;
    auto cut=std::min(space,name.size());
    while(cut<name.size() && cut && (static_cast<unsigned char>(name[cut])&0xc0)==0x80)--cut;
    return "[FP] "+std::string(name.substr(0,cut));
}
}
