#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace dingosdk::server {
// Callers provide unbiased draws in [0,count). The fractional remainder is an
// independent Bernoulli draw, so integer wallets do not create a house edge.
template<class Draw>
std::uint64_t highlow_payout(std::uint64_t stake,unsigned initial,unsigned second,bool higher,Draw draw) {
    if(!stake || stake>1000000 || initial<1 || initial>13 || second<1 || second>13)throw std::invalid_argument("Invalid High / Low inputs");
    const auto wins=higher?13-initial:initial-1;
    if(!wins)throw std::invalid_argument("Impossible High / Low direction");
    if(second==initial)return stake;
    if(higher?second<initial:second>initial)return 0;
    const auto numerator=12*stake;
    return numerator/wins+(draw(wins)<numerator%wins?1:0);
}
template<class Roll,class Notice>
std::uint64_t dice_winner(std::vector<std::uint64_t> contenders,Roll roll,Notice notice) {
    if(contenders.size()<2)throw std::invalid_argument("Dice requires two players");
    unsigned rounds=0;
    while(contenders.size()>1) {
        if(++rounds>128)throw std::runtime_error("Dice reroll limit reached");
        unsigned highest=0;std::vector<std::uint64_t> tied;
        for(auto id:contenders) {
            const auto result=roll();if(result<1 || result>6)throw std::runtime_error("Invalid die");
            notice(rounds,id,result);
            if(result>highest){highest=result;tied.clear();}if(result==highest)tied.push_back(id);
        }
        contenders=std::move(tied);
    }
    return contenders.front();
}
}
