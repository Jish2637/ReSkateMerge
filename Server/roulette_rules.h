#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace dingosdk::server {
inline std::string_view roulette_colour(unsigned number) {
    if(number>36)throw std::invalid_argument("Invalid roulette pocket");
    static constexpr std::array<unsigned,18> red{1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36};
    return number==0?"green":std::ranges::find(red,number)!=red.end()?"red":"black";
}
inline std::uint64_t roulette_payout(std::uint64_t stake,unsigned number,std::string_view choice) {
    if(!stake || stake>std::numeric_limits<std::uint64_t>::max()/20 ||
       (choice!="red" && choice!="black" && choice!="green"))throw std::invalid_argument("Invalid roulette bet");
    return roulette_colour(number)==choice?stake*(choice=="green"?20:2):0;
}
}
