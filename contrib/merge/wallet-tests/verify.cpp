#include "Server/player_stats.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace dingosdk::server;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    const auto root=std::filesystem::path(TEST_DATA)/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);const auto path=root/"player-stats.json";
    constexpr uint64_t a=76561198000000801ULL,b=a+1,c=a+2;
    PlayerStatsStore ledger;ledger.open(path);
    for(uint64_t id=a;id<a+10;++id)ledger.joined(id,"Player",0,2000000000);
    ledger.save();
    auto first=ledger.skate_reward("session:1",a,{a,b},2000000000);
    check(first.at("amount").get<int>()==100 && ledger.players.at(a).wallet==100,"two-player payout is 100");
    check(ledger.players.at(a).points==100 && ledger.players.at(a).weekly_points==100,"native earnings counters updated");
    auto duplicate=ledger.skate_reward("session:1",a,{b,a},2000000000);
    check(duplicate.at("duplicate").get<bool>() && ledger.players.at(a).wallet==100,"reordered duplicate roster pays once");
    ledger.skate_reward("session:2",a,{a,b,c},2000000000);
    check(ledger.players.at(a).wallet==300,"three-player payout is 200");
    std::vector<uint64_t> ten;for(uint64_t id=a;id<a+10;++id)ten.push_back(id);
    ledger.skate_reward("session:3",a,ten,2000000000);
    check(ledger.players.at(a).wallet==1200,"ten-player payout is 900");
    PlayerStatsStore reloaded;reloaded.open(path);
    reloaded.skate_reward("session:1",a,{a,b},2000000000);
    check(reloaded.players.at(a).wallet==1200,"receipt survives restart and prevents second award");
    bool rejected=false;try{reloaded.skate_reward("session:1",b,{a,b},2000000000);}catch(...){rejected=true;}
    check(rejected && reloaded.players.at(b).wallet==0,"conflicting winner cannot reuse receipt");
    rejected=false;try{reloaded.skate_reward("bad",a,{a,a},2000000000);}catch(...){rejected=true;}
    check(rejected,"duplicate participants refused");
    std::filesystem::create_directory(path.string()+".tmp");
    rejected=false;try{reloaded.skate_reward("save-failed",a,{a,b},2000000000);}catch(...){rejected=true;}
    check(rejected && reloaded.players.at(a).wallet==1200 && !reloaded.plugin_rewards.contains("save-failed"),"save failure rolls back wallet and receipt together");
    std::filesystem::remove(path.string()+".tmp");
    reloaded.skate_reward("save-failed",a,{a,b},2000000000);
    check(reloaded.players.at(a).wallet==1300,"retry after failed persistence credits exactly once");
    std::cout<<"PASS native MSC ledger: 100/200/900 rewards, earnings counters, duplicate and restart receipts, conflicts, roster bounds, persistence rollback and retry\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
