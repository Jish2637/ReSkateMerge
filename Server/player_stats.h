#pragma once
#include "Engine/Core/Json/json.h"
#include "Engine/Game/Multiplayer/session_limits.h"
#include "Extension/Throwdowns/throwdown_wire.h"
#include "serial_tasks.h"
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace dingosdk::server {
inline constexpr std::uint64_t max_stats_counter = 9000000000000000ULL;
struct StatsSettings {
    bool enabled = true;
    unsigned idle_seconds = 300, hourly_points = 10, daily_minutes = 5, daily_points = 25;
    unsigned streak_step = 5, streak_cap = 50, event_points = 10, rewarded_events_per_day = 6;
    unsigned vip_hours = 25, leaderboard_minutes = 60;
    std::string vip_role = "vip";
};
struct ShopSettings {
    bool enabled = true, joinmsg_enabled = true, colourbundle_enabled = true, shimmerbundle_enabled = true;
    unsigned tag_price = 250, colour_price = 50, shimmer_price = 2500;
    unsigned joinmsg_price = 2500, colourbundle_price = 1500, shimmerbundle_price = 8000;
    unsigned joinmsg_max_length = 60, joinmsg_cooldown_seconds = 300;
};
struct WagerSettings {
    bool enabled = false, coinflip_enabled = true, dice_enabled = true, highlow_enabled = true, slots_enabled = true;
    unsigned min_stake = 10, coinflip_max_stake = 1000, dice_max_stake = 1000, highlow_max_stake = 500, slots_max_stake = 500;
    unsigned lobby_timeout_seconds = 300, highlow_timeout_seconds = 120;
};
struct Wager {
    std::uint64_t id{}, creator{}, stake{}, expires{}, capacity{}, initial{};
    std::string game, state = "pending", result;
    Json detail=Json::object(); // Optional presentation data; never used to settle balances.
    std::set<std::uint64_t> members;
    std::map<std::uint64_t,std::uint64_t> payouts;
    std::uint64_t exposure() const { return stake * (game=="highlow" ? 12 : capacity); }
};
struct PlayerStats {
    std::string name;
    bool joinmsg_owned{}, joinmsg_enabled{}, colourbundle_owned{};
    std::string joinmsg;
    std::uint64_t joinmsg_last{}, wager_headroom{}; // headroom is derived from pending ledger records
    std::uint64_t wallet_limit() const { return max_stats_counter-wager_headroom; }
    std::uint64_t wallet{};
    std::vector<std::string> tags, disabled_tags, shimmers;
    std::string equipped_tag, colour;
    std::uint64_t active_us{}, points{}, weekly_points{}, point_remainder{};
    std::int64_t day = -1, week = -1, last_claim_day = -1;
    std::uint64_t daily_us{}, streak{}, daily_events{}, qualifying_days{};
    std::array<std::uint64_t, 3> events{}; // SKATE, Spot Battle, Jam
    bool vip_awarded{}, vip_pending{};
};
// Structured counters only: no text-log parsing, game results, or client protocol changes.
class PlayerStatsStore {
public:
    explicit PlayerStatsStore(std::shared_ptr<SerialTasks> writer={}) : writer_(std::move(writer)) {}
    using Notice = std::function<void(std::uint64_t, const std::string &)>;
    std::map<std::uint64_t, PlayerStats> players;
    std::vector<Wager> wagers;
    std::uint64_t next_wager_id = 1;
    void rebuild_headroom();
    void refund_pending();
    void open(const std::filesystem::path &path);
    void save() const;
    bool save_async() const;
    bool poll_save() const;
    static std::int64_t day_at(std::int64_t utc) { return utc / 86400; }
    static std::int64_t week_at(std::int64_t utc) { return (day_at(utc) + 3) / 7; }
    static void calendar(PlayerStats &, std::int64_t utc);
    void joined(std::uint64_t id, std::string name, std::uint64_t now, std::int64_t utc);
    void active(std::uint64_t id, std::uint64_t now);
    void advance(std::uint64_t id, std::uint64_t now, std::int64_t utc, const StatsSettings &, const Notice &, bool persist_rewards=true);
    void left(std::uint64_t id);
    void pause(std::uint64_t id) { sessions_.erase(id); }
    void world_changed();
    void throwdown(std::uint64_t sender, const multiplayer::ThrowdownMessage &, std::uint64_t now,
                   std::int64_t utc, const StatsSettings &, const Notice &);
    std::vector<std::uint64_t> board(std::string_view kind, std::int64_t utc) const;
    std::uint64_t value(const PlayerStats &, std::string_view kind, std::int64_t utc) const;
private:
    struct Session { std::uint64_t at{}, active{}; std::int64_t utc{}; };
    struct Event { int mode = -1; bool started{}; std::set<std::uint64_t> participants, credited; };
    std::map<std::uint64_t, Session> sessions_;
    // Tombstones remain until the host leaves/world changes: a duplicate start cannot award twice.
    std::map<std::pair<std::uint64_t, std::uint32_t>, Event> events_;
    std::filesystem::path path_;
    mutable std::shared_ptr<SerialTasks> writer_;
    mutable std::future<void> pending_save_;
    mutable bool save_completed_{};
    std::future<void> submit_save() const;
    static void write_snapshot(const std::filesystem::path &,const std::map<std::uint64_t,PlayerStats> &, const std::vector<Wager> &, std::uint64_t);
    static void credit(PlayerStats &, std::uint64_t points);
};
} // namespace dingosdk::server
