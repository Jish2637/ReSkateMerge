#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace dingosdk::msc {
struct GameMember {std::uint64_t id{},payout{};std::string name;};
struct DiceRoll {std::uint64_t round{},player{},value{};};
struct SlotWin {std::string name;std::uint64_t stake{},payout{};};
struct GameRound {
    std::uint64_t id{},creator{},stake{},pot{},capacity{},seconds{},initial{},second{},winner{},my_payout{};
    std::string state,result,direction;
    std::vector<GameMember> members;std::vector<DiceRoll> rolls;
    std::array<unsigned,3> reels{};bool has_reels{};std::uint64_t multiplier{};
};
struct GameView {
    std::string id,label,reason;bool enabled{};std::uint64_t minimum{},maximum{},capacity{},cooldown{};
    std::vector<SlotWin> wins;
    std::vector<std::string> actions;std::optional<GameRound> round,previous;
};
inline bool game_allows(const GameView& game,const std::string& action) {
    for(const auto& name:game.actions)if(name==action)return true;
    return false;
}
struct StaffPlayer {std::uint64_t id{},epoch{},rank{};std::string name,role;bool voice_muted{},text_muted{};std::uint64_t locks{};};
struct StaffQuote {std::string token,label,description;std::uint64_t seconds{};};
struct StaffView {
    std::string role,output;std::vector<std::string> actions,roles;
    std::vector<StaffPlayer> players;std::uint64_t page{1},pages{1};
    std::map<std::string,std::string> settings;std::optional<StaffQuote> quote;
};
} // namespace dingosdk::msc
