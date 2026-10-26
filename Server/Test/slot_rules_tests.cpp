#include "Server/slot_rules.h"
#include <array>
#include <stdexcept>
#include <cstdint>
#include <iostream>
int main() {
    using dingosdk::server::slot_multiplier;
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    require(slot_multiplier({0,1,2})==0,"No-match payout");
    require(slot_multiplier({1,0,1})==0,"Pair must lose with no refund");
    require(slot_multiplier({4,4,4})==25,"Diamond payout");
    std::uint64_t sum=0,triples=0,pairs=0,losers=0;
    for(unsigned a=0;a<5;++a)for(unsigned b=0;b<5;++b)for(unsigned c=0;c<5;++c) {
        const auto m=slot_multiplier({a,b,c});sum+=m;
        if(a==b && b==c)++triples;else if(a==b || b==c || a==c){++pairs;require(m==0,"Pair did not lose");}else ++losers;
    }
    require(sum==56 && triples==5 && pairs==60 && losers==60,"All slot outcomes and total payout");
    bool rejected=false;try{(void)slot_multiplier({5,0,0});}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Invalid symbol rejection");
    std::cout<<"Slots payouts, 125 outcomes and 44.8% RTP verified\n";
}
