#pragma once
#include "Engine/Core/Json/json.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
namespace dingosdk::skate {
struct TrickFeedback {
    std::string basic;
    std::vector<std::string> details;
    void clear(){basic.clear();details.clear();}
    void add(std::string text){if(details.size()<4 && std::find(details.begin(),details.end(),text)==details.end())details.push_back(std::move(text));}
    void invalid(std::string reason={}){clear();basic="Invalid trick";if(!reason.empty())basic+=": "+reason;}
    void ignored(const Json& rows) {
        std::size_t grinds{};std::uint64_t shortest=~std::uint64_t{},minimum{};
        for(const auto& r:rows) {
            const auto why=r.value("reason","");
            if(why=="grind duration") {
                ++grinds;shortest=std::min(shortest,r.value("duration_ms",std::uint64_t{}));minimum=r.value("minimum_ms",std::uint64_t{});
            } else if(why=="disabled category")add("Disabled trick category omitted.");
            else if(why=="consecutive equivalent trick")add("Repeated equivalent trick combined.");
            else if(why=="outside retained combination")add("Movement after the combination omitted.");
            else if(why=="grounded grab")add("Grounded grab omitted.");
            else if(why=="ordinary manual")add("Manual omitted.");
            else if(why=="stall observation")add("Stall omitted.");
            else if(why=="slappy entry observation")add("Slappy entry omitted.");
            else if(why=="recovery action" || why=="manual revert recovery")add("Recovery movement omitted.");
            else if(why=="trickless movement" || why=="powerslide")add("Non-trick movement omitted.");
        }
        if(grinds){char text[128];
            if(grinds==1)std::snprintf(text,sizeof(text),"Grind omitted: %.2fs / %.2fs.",shortest/1000.0,minimum/1000.0);
            else std::snprintf(text,sizeof(text),"%zu grinds omitted: shortest %.2fs / %.2fs.",grinds,shortest/1000.0,minimum/1000.0);
            add(text);
        }
    }
    void counted(const Json& holds) {
        std::size_t count{};std::uint64_t shortest=~std::uint64_t{},minimum{};
        for(const auto& h:holds)if(h.value("qualified",false) && h.value("minimum_ms",0)>0){++count;shortest=std::min(shortest,h.value("duration_ms",std::uint64_t{}));minimum=h.value("minimum_ms",std::uint64_t{});}
        if(count){char text[128];
            if(count==1)std::snprintf(text,sizeof(text),"Grind counted: %.2fs / %.2fs.",shortest/1000.0,minimum/1000.0);
            else std::snprintf(text,sizeof(text),"%zu grinds counted: shortest %.2fs / %.2fs.",count,shortest/1000.0,minimum/1000.0);
            add(text);
        }
    }
    std::vector<std::string> lines(bool detailed) const {
        std::vector<std::string> out;if(!basic.empty())out.push_back(basic);
        if(detailed)out.insert(out.end(),details.begin(),details.end());return out;
    }
};
inline std::string invalid_reason(const std::string& raw) {
    if(raw.find("rotation")!=std::string::npos)return "Incomplete rotation evidence.";
    if(raw.find("phase")!=std::string::npos)return "Movement phase could not be confirmed.";
    if(raw.find("rules")!=std::string::npos)return "Match rules could not be confirmed.";
    return "No valid trick could be identified.";
}
}
