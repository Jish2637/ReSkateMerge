#pragma once
#include "server_text.h"
#include <string>
#include <string_view>
#include <utility>

namespace dingosdk::server {
inline constexpr std::pair<std::string_view,std::string_view> msc_command_aliases[] = {
    {"b","bal"}, {"balance","bal"}, {"points","bal"},
    {"give","pay"}, {"send","pay"}, {"st","stats"}, {"mystats","stats"},
    {"d","daily"}, {"bonus","daily"}, {"lb","top"}, {"leaderboard","top"},
    {"s","shop"}, {"store","shop"}, {"t","tag"}, {"nametag","tag"},
    {"tl","tags"}, {"mytags","tags"}, {"c","colour"}, {"color","colour"},
    {"sh","shimmers"}, {"confirm","buy"}, {"purchase","buy"},
    {"jm","joinmsg"}, {"joinmessage","joinmsg"}, {"cb","colourbundle"}, {"colorbundle","colourbundle"},
    {"sb","shimmerbundle"}, {"effectbundle","shimmerbundle"},
    {"cf","coinflip"}, {"flip","coinflip"}, {"dd","dice"}, {"diceduel","dice"},
    {"hl","highlow"}, {"hilo","highlow"}
};
// Resolve only the command token. Arguments (including names and custom text)
// never pass through this function. Unknown commands keep their existing route.
inline std::string resolve_msc_command(std::string_view token) {
    const auto command=lower(token);
    for(const auto &[alias,canonical]:msc_command_aliases)if(command==alias)return std::string(canonical);
    return command;
}
inline std::string resolve_msc_action(std::string_view game,std::string_view token) {
    const auto action=lower(token);
    if(game=="coinflip" || game=="dice") {
        for(const auto &[alias,canonical]:{std::pair<std::string_view,std::string_view>{"c","create"},
            {"j","join"},{"l","leave"},{"go","start"},{"s","status"},{"x","cancel"}})
            if(action==alias)return std::string(canonical);
    } else if(game=="highlow") {
        if(action=="h")return "higher";
        if(action=="l")return "lower";
        if(action=="x")return "cancel";
    }
    return action;
}
} // namespace dingosdk::server
