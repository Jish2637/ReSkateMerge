#pragma once
#include <optional>
#include <cstdint>
#include <string_view>
#include <utility>
namespace dingosdk::server {
inline bool shop_verb(std::string_view v) {return v=="joinmsg" || v=="colourbundle" || v=="shimmerbundle" || v=="shimmers" || v=="shop" || v=="tags" || v=="tag" || v=="colour" || v=="buy";}
inline constexpr std::pair<std::string_view,std::uint32_t> shop_colours[]={
    {"purple",0xA970FFFFU},{"cyan",0x00E5FFFFU},{"gold",0xFFD700FFU},{"pink",0xFF80C0FFU},
    {"green",0x70E070FFU},{"orange",0xFF9A40FFU},{"red",0xFF6060FFU},{"white",0xFFFFFFFFU}};
// Exact native gradient endpoints from custom_nametags.h; clients animate these locally.
// Keep the purchased shimmer-green key; 2.1.0's staff gradient is now blue/cyan.
inline constexpr std::pair<std::string_view,std::uint32_t> shop_shimmers[]={
    {"shimmer-purple",0xffff78b4U},{"shimmer-red",0xff6464ffU},{"shimmer-gold",0xff7ae2ffU},
    {"shimmer-blue",0xffff7b1fU},{"shimmer-green",0xfffff9b2U}};
inline bool shop_shimmer(std::string_view name) {
    for(const auto &[key,value]:shop_shimmers)if(key==name)return true;
    return false;
}
inline std::optional<std::uint32_t> shop_colour(std::string_view name) {
    for(const auto &[key,value]:shop_shimmers)if(key==name)return value;
    for(const auto &[key,value]:shop_colours)if(key==name)return 0xff000000U | ((value >> 24) & 0xff) | (((value >> 16) & 0xff) << 8) | (((value >> 8) & 0xff) << 16);
    return {};
}
}
