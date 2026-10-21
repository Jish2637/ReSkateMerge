#pragma once
#include "msc_interface.h"
namespace dingosdk::msc {
inline std::optional<Json> product_action(const Product& p,std::string_view tag,std::string_view message) {
    auto b=Json::object();b["op"]="item";
    if(p.id=="tag"){b["kind"]="tag";b["value"]=tag;}
    else if(p.id=="joinmsg"){b["kind"]="joinmsg";b["value"]=message;}
    else if(p.id.starts_with("colour:") || p.id.starts_with("shimmer:")){b["kind"]="colour";b["value"]=p.value;}
    else if(p.id=="colourbundle" || p.id=="shimmerbundle"){b["kind"]=p.id;b["value"]="";}
    else return {};
    return b;
}
inline std::string product_label(const Product& p) {
    if(p.equipped)return "Equipped";
    if(!p.enabled)return "Unavailable";
    if(p.id=="joinmsg" && p.owned)return "Save message";
    if(p.owned)return "Equip";
    return std::string(p.action=="change"?"Change colour - ":"Buy - ")+std::to_string(p.price)+" MSC";
}
}
