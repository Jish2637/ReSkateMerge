#pragma once
// Steezy Landings, the parts with no game in them: what a setup holds, how a landing is chosen
// from its measurements, and how setups are kept in steezy-landings.json. The runtime
// (steezy_landings.cpp) measures each landing and acts on the decision; the menu
// (steezy_landings_page.cpp) edits a Settings; the tests (Test/steezy_landings_tests.cpp) run
// everything here on its own.
//
// Port of the standalone SteezyLandings/AlwaysSketchy DLL (v0.15.0). Changes from that DLL, as
// asked for the native version:
//   - everything starts off: the feature is disabled, Global Chance is 0 % and every condition
//     is off until the player sets one or loads a preset;
//   - "Keep the game's Sketchy/Spin grade", "Suppress the game's own sketchy pulse" and
//     "Include short trick landings" are gone (always off);
//   - every condition has its own chance next to its OR/AND switch, and Global Chance can be
//     switched off so only the conditions' chances decide;
//   - a "Max" condition is off at 0, like every other condition (the DLL used "no limit").
#include "Engine/Core/Json/json.h"
#include "landing_measurements.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dingosdk::steezy_landings {
// The game's landing grades (classifier output P+0x64, EnumGS.LandQuality).
inline constexpr std::uint32_t quality_straight = 0, quality_sketchy = 1, quality_spin = 2, quality_good = 3;

enum class Condition : std::size_t { min_air, max_air, min_drop, max_drop, min_speed, max_speed, body_spin, board_yaw, sketchiness, game_grade, count };
inline constexpr std::size_t condition_count = static_cast<std::size_t>(Condition::count);
constexpr std::size_t index(Condition c) noexcept { return static_cast<std::size_t>(c); }

// One condition as the menu shows it and the file stores it. A threshold of 0 is "off".
struct ConditionInfo {
    std::string_view key;   // in steezy-landings.json
    std::string_view label; // in the menu
    std::string_view unit;
    double high;            // the slider's top; a typed value may go up to `limit`
    double limit;
    const char* format;     // the slider's text for a value that is on
    std::string_view about; // the tooltip
};
inline constexpr std::array<ConditionInfo, condition_count> conditions{{
    {"min_air", "Min air time", "s", 3.0, 60.0, "%.2f s", "Only landings after at least this long in the air."},
    {"max_air", "Max air time", "s", 3.0, 60.0, "%.2f s", "Only landings after at most this long in the air."},
    {"min_drop", "Min drop", "m", 10.0, 1000.0, "%.1f m",
     "Only landings that fell at least this far. Measured vertically from the highest observed board position in this flight to touchdown. Missing position samples do not qualify."},
    {"max_drop", "Max drop", "m", 10.0, 1000.0, "%.1f m", "Only landings that descended at most this far from the flight's highest board position to touchdown."},
    {"min_speed", "Min speed", "m/s", 20.0, 200.0, "%.1f m/s", "Only landings rolling at least this fast across the ground. 1 m/s is 3.6 km/h."},
    {"max_speed", "Max speed", "m/s", 20.0, 200.0, "%.1f m/s", "Only landings rolling at most this fast across the ground."},
    {"body_spin", "Min body spin angle", "deg", 720.0, 3600.0, "%.0f deg",
     "Only landings after the rider's body turned at least this far in the air (net, so turning back cancels). 170 catches body 180s; a shuvit alone does not count."},
    {"board_yaw", "Min board yaw speed", "deg/s", 1080.0, 10000.0, "%.0f deg/s",
     "Only landings where the board is still spinning at least this fast as it touches down. Shuvits count; turn it off to gate on body spins only."},
    {"sketchiness", "Min sketchiness", "", 1.0, 1.0, "%.2f", "Only landings the game measured at least this sideways (0 to 1)."},
    {"game_grade", "Game grade", "", 2.0, 2.0, "%.0f", "Only landings the game itself graded this way, before Steezy Landings changes anything."},
}};
inline constexpr std::array<const char*, 3> game_grade_names{"Any", "Sketchy or Spin", "Straight or Good"};

// Everything a preset holds. The live switches beside it (on/off, diagnostics) are not part of a preset.
struct Tuning {
    bool use_global_chance{true};
    double global_chance{};                          // percent; 0 = never until it is raised
    std::array<double, condition_count> threshold{}; // 0 = off; game_grade: 0 any, 1 sketchy/spin, 2 straight/good
    std::array<bool, condition_count> and_group{};   // false = OR (default), true = member of the AND group
    std::array<double, condition_count> chance{};    // percent, rolled when the condition qualifies a landing
    // Advanced: the accepted smooth setup of the DLL (v0.12-v0.15 live tests).
    std::uint32_t grade{quality_spin};               // grade a chosen landing is given: Sketchy or Spin
    bool selector_override{true};                    // supply SketchyLanding at the landing table's own read
    bool hold_grade{true};                           // keep publishing the grade through the flag window
    int window_start{0}, window_length{3};           // physics steps after touchdown
    Tuning() { chance.fill(100.0); }
    bool operator==(const Tuning&) const = default;
};
struct Settings {
    bool enabled{};     // the feature's switch; off by default
    bool diagnostics{}; // one line per landing in ReSkate.log
    Tuning tuning;
    std::string preset; // the preset last loaded or saved (shown, not authoritative)
    bool operator==(const Settings&) const = default;
};

inline double finite_or(double value, double fallback) { return std::isfinite(value) ? value : fallback; }
inline Tuning sanitize(Tuning t) {
    t.global_chance = std::clamp(finite_or(t.global_chance, 0.0), 0.0, 100.0);
    for (std::size_t i = 0; i < condition_count; ++i) {
        t.threshold[i] = std::clamp(finite_or(t.threshold[i], 0.0), 0.0, conditions[i].limit);
        t.chance[i] = std::clamp(finite_or(t.chance[i], 100.0), 0.0, 100.0);
    }
    t.threshold[index(Condition::game_grade)] = std::round(t.threshold[index(Condition::game_grade)]);
    if (t.grade != quality_sketchy) t.grade = quality_spin;
    t.window_start = std::clamp(t.window_start, 0, 30);
    t.window_length = std::clamp(t.window_length, 1, 60);
    return t;
}
inline bool active(const Tuning& t, Condition c) noexcept { return t.threshold[index(c)] > 0; }
inline std::size_t active_count(const Tuning& t) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < condition_count; ++i) n += t.threshold[i] > 0;
    return n;
}

// ---- one landing -------------------------------------------------------------------------
struct Measurements {
    double air{};          // seconds airborne
    double drop{};         // metres, measured peak board height minus touchdown height
    double speed{};        // m/s across the ground at touchdown
    double board_yaw{};    // board yaw rate at touchdown, deg/s (signed)
    double spin_angle{};   // net airborne body heading change, degrees (absolute)
    double sketchiness{};  // classifier's signed sketchiness
    std::uint32_t game_quality{};
    bool air_known{}, drop_known{}; // valid measurements from a fresh, unconsumed flight
    bool motion_known{};   // board velocity / spin read
    bool angle_known{};    // body heading tracked through the whole air time
};
// Whether a condition that is on holds for this landing. Missing measurements never qualify.
inline bool met(Condition c, double t, const Measurements& m) noexcept {
    const auto ok = [](double v) { return std::isfinite(v); };
    switch (c) {
    case Condition::min_air: return m.air_known && ok(m.air) && m.air >= t;
    case Condition::max_air: return m.air_known && ok(m.air) && m.air <= t;
    case Condition::min_drop: return m.drop_known && ok(m.drop) && m.drop >= t;
    case Condition::max_drop: return m.drop_known && ok(m.drop) && m.drop <= t;
    case Condition::min_speed: return m.motion_known && ok(m.speed) && m.speed >= t;
    case Condition::max_speed: return m.motion_known && ok(m.speed) && m.speed <= t;
    case Condition::body_spin: return m.angle_known && ok(m.spin_angle) && m.spin_angle >= t;
    case Condition::board_yaw: return m.motion_known && ok(m.board_yaw) && std::fabs(m.board_yaw) >= t;
    case Condition::sketchiness: return ok(m.sketchiness) && std::fabs(m.sketchiness) >= t;
    case Condition::game_grade: {
        const bool sketchy = m.game_quality == quality_sketchy || m.game_quality == quality_spin;
        const bool clean = m.game_quality == quality_straight || m.game_quality == quality_good;
        return t < 1.5 ? sketchy : clean;
    }
    default: return false;
    }
}

struct Decision {
    bool qualified{}; // some route's conditions held (or none are on): the game's own sketchy landings are left alone
    bool chosen{};    // this landing gets the heavy animation
    int draws{};      // random numbers used (none when nothing could qualify)
};
// The order is fixed: conditions first, then their chances, then Global Chance.
//   - Each OR condition that holds rolls its own chance; one success is enough.
//   - The AND group (every condition switched to AND) holds when all of its members hold, and then
//     rolls once, with the lowest chance among its members.
//   - No condition on: every landing qualifies and only Global Chance decides.
//   - Global Chance, when on, is rolled last. Switched off, the conditions' own chances decide alone,
//     and with no condition on nothing is chosen.
// `draw` returns a number in [0, 100). A chance of 0 or 100 needs no draw.
template<class Draw> Decision decide(const Tuning& t, const Measurements& m, Draw&& draw) {
    Decision d;
    const auto hit = [&](double chance) {
        if (chance >= 100.0) return true;
        if (chance <= 0.0) return false;
        ++d.draws;
        return draw() < chance;
    };
    bool any = false, and_on = false, and_all = true;
    double and_chance = 100.0;
    std::array<std::size_t, condition_count> passing{};
    std::size_t passing_count = 0;
    for (std::size_t i = 0; i < condition_count; ++i) {
        if (!(t.threshold[i] > 0)) continue;
        any = true;
        const bool holds = met(static_cast<Condition>(i), t.threshold[i], m);
        if (t.and_group[i]) {
            and_on = true;
            and_all = and_all && holds;
            and_chance = std::min(and_chance, t.chance[i]);
        } else if (holds) {
            passing[passing_count++] = i;
        }
    }
    const bool and_holds = and_on && and_all;
    d.qualified = !any || passing_count > 0 || and_holds;
    if (!d.qualified) return d;
    bool route = !any;
    for (std::size_t k = 0; k < passing_count && !route; ++k) route = hit(t.chance[passing[k]]);
    if (!route && and_holds) route = hit(and_chance);
    if (!route) return d;
    if (t.use_global_chance) d.chosen = hit(t.global_chance);
    else d.chosen = any;
    return d;
}

// An animation descriptor's inline name ("Animation/Dingo/Bool.Anim.SketchyLanding") is exactly the
// Dingo prefix and `suffix`, in any letter case.
inline bool descriptor_name(std::string_view name, std::string_view suffix) noexcept {
    constexpr std::string_view prefix = "animation/dingo/";
    if (name.size() != prefix.size() + suffix.size()) return false;
    const auto folded = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
    for (std::size_t i = 0; i < name.size(); ++i)
        if (folded(name[i]) != folded(i < prefix.size() ? prefix[i] : suffix[i - prefix.size()])) return false;
    return true;
}

// ---- presets -------------------------------------------------------------------------------
// Names usable as a preset: 1-48 letters, digits, spaces, '-', '_' or '\'', not starting or ending with a space.
inline std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
inline bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 48 || name.front() == ' ' || name.back() == ' ') return false;
    return std::ranges::all_of(name, [](char ch) {
        const auto c = static_cast<unsigned char>(ch);
        return std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '\'';
    });
}
// Presets that come with ReSkate, listed above the player's own.
inline const std::vector<std::pair<std::string, Tuning>>& builtin_presets() {
    static const std::vector<std::pair<std::string, Tuning>> list = [] {
        Tuning recommended;
        recommended.use_global_chance = false;
        recommended.threshold[index(Condition::min_air)] = 2.0;
        recommended.threshold[index(Condition::min_drop)] = 1.7;
        recommended.threshold[index(Condition::body_spin)] = 200.0;
        return std::vector<std::pair<std::string, Tuning>>{{"Jjs Recommended", recommended}};
    }();
    return list;
}
inline bool builtin_name(std::string_view name) {
    return std::ranges::any_of(builtin_presets(), [&](const auto& p) { return lower(p.first) == lower(name); });
}

// The whole of steezy-landings.json.
struct Store {
    Settings settings;
    std::map<std::string, Tuning> presets; // the player's own, by name as typed
    // The player's preset called `name` in any letter case.
    std::map<std::string, Tuning>::iterator find(std::string_view name) {
        return std::ranges::find_if(presets, [&](const auto& p) { return lower(p.first) == lower(name); });
    }
};

// ---- steezy-landings.json -------------------------------------------------------------------
inline Json to_json(const Tuning& t) {
    Json out = Json::object();
    Json global = Json::object();
    global["on"] = t.use_global_chance;
    global["chance"] = t.global_chance;
    out["global"] = std::move(global);
    Json list = Json::object();
    for (std::size_t i = 0; i < condition_count; ++i) {
        Json one = Json::object();
        one["value"] = t.threshold[i];
        one["and"] = t.and_group[i];
        one["chance"] = t.chance[i];
        list[conditions[i].key] = std::move(one);
    }
    out["conditions"] = std::move(list);
    out["grade"] = t.grade == quality_sketchy ? "sketchy" : "spin";
    out["selector_override"] = t.selector_override;
    out["hold_grade"] = t.hold_grade;
    out["window_start"] = t.window_start;
    out["window_length"] = t.window_length;
    return out;
}
namespace detail {
inline void number(const Json& from, std::string_view key, double& out) {
    if (from.is_object() && from.contains(key) && from.at(key).is_number()) out = from.at(key).get<double>();
}
inline void number(const Json& from, std::string_view key, int& out) {
    double value = out;
    number(from, key, value);
    if (std::isfinite(value) && value > -1e6 && value < 1e6) out = static_cast<int>(value);
}
inline void flag(const Json& from, std::string_view key, bool& out) {
    if (from.is_object() && from.contains(key) && from.at(key).is_boolean()) out = from.at(key).get<bool>();
}
} // namespace detail
// Fields that are missing or of the wrong kind keep their defaults; unknown fields are ignored.
inline Tuning tuning_from_json(const Json& in) {
    Tuning t;
    if (!in.is_object()) return t;
    if (in.contains("global")) {
        detail::flag(in.at("global"), "on", t.use_global_chance);
        detail::number(in.at("global"), "chance", t.global_chance);
    }
    if (in.contains("conditions") && in.at("conditions").is_object()) {
        const auto& list = in.at("conditions");
        for (std::size_t i = 0; i < condition_count; ++i) {
            if (!list.contains(conditions[i].key)) continue;
            const auto& one = list.at(conditions[i].key);
            detail::number(one, "value", t.threshold[i]);
            detail::flag(one, "and", t.and_group[i]);
            detail::number(one, "chance", t.chance[i]);
        }
    }
    if (in.contains("grade") && in.at("grade").is_string()) t.grade = lower(in.at("grade").string()) == "sketchy" ? quality_sketchy : quality_spin;
    detail::flag(in, "selector_override", t.selector_override);
    detail::flag(in, "hold_grade", t.hold_grade);
    detail::number(in, "window_start", t.window_start);
    detail::number(in, "window_length", t.window_length);
    return sanitize(t);
}
inline Json to_json(const Store& s) {
    Json out = Json::object();
    out["version"] = 1;
    out["enabled"] = s.settings.enabled;
    out["diagnostics"] = s.settings.diagnostics;
    out["preset"] = s.settings.preset;
    out["current"] = to_json(s.settings.tuning);
    Json presets = Json::object();
    for (const auto& [name, tuning] : s.presets) presets[name] = to_json(tuning);
    out["presets"] = std::move(presets);
    return out;
}
inline Store store_from_json(const Json& in) {
    Store s;
    if (!in.is_object()) return s;
    detail::flag(in, "enabled", s.settings.enabled);
    detail::flag(in, "diagnostics", s.settings.diagnostics);
    if (in.contains("preset") && in.at("preset").is_string()) s.settings.preset = in.at("preset").string();
    if (in.contains("current")) s.settings.tuning = tuning_from_json(in.at("current"));
    if (in.contains("presets") && in.at("presets").is_object())
        for (const auto& [name, value] : in.at("presets").items())
            if (valid_name(name) && !builtin_name(name) && value.is_object()) s.presets[name] = tuning_from_json(value);
    return s;
}
} // namespace dingosdk::steezy_landings
