#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace dingosdk::server {
// Five equally likely symbols, three independent server draws. All rewards
// are gross multipliers of the stake (a pair returns the stake, not a profit).
inline constexpr std::array<const char*,5> slot_symbols{"Cherry","Lemon","Bell","Seven","Diamond"};
inline constexpr std::array<unsigned,5> slot_triples{3,5,8,15,25};
inline unsigned slot_multiplier(const std::array<unsigned,3>& reels) {
    for(const auto r:reels)if(r>=slot_symbols.size())throw std::invalid_argument("Invalid slot symbol");
    if(reels[0]==reels[1] && reels[1]==reels[2])return slot_triples[reels[0]];
    if(reels[0]==reels[1] || reels[1]==reels[2] || reels[0]==reels[2])return 1;
    return 0;
}
// Return-to-player: (3+5+8+15+25)/125 + 60/125 = 92.8%, before integer rounding.
} // namespace dingosdk::server
