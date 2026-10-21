// Steezy Landings without the game (steezy_landings_model.h): everything starts off, the order of
// conditions, their chances and Global Chance, and steezy-landings.json.
#include "Extension/SteezyLandings/steezy_landings_model.h"
#include "Extension/UI/Overlay/numeric_edit.h"

#include <cstdio>
#include <deque>
#include <iostream>
#include <string>

namespace {
using namespace dingosdk::steezy_landings;
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
// Hands out the given numbers in turn; counts what was taken.
struct Rolls {
    std::deque<double> next;
    int taken{};
    double operator()() {
        ++taken;
        if (next.empty()) return 99.999;
        const double value = next.front();
        next.pop_front();
        return value;
    }
};
Decision run(const Tuning& t, const Measurements& m, std::initializer_list<double> numbers, int* taken = nullptr) {
    Rolls rolls{std::deque<double>(numbers)};
    const auto d = decide(t, m, [&] { return rolls(); });
    if (taken) *taken = rolls.taken;
    return d;
}
Measurements landing(double air, double drop, double speed, double spin_angle = 0, std::uint32_t quality = quality_straight) {
    Measurements m;
    m.air = air;
    m.drop = drop;
    m.speed = speed;
    m.spin_angle = spin_angle;
    m.game_quality = quality;
    m.air_known = m.drop_known = m.motion_known = m.angle_known = true;
    return m;
}
void set(Tuning& t, Condition c, double value, bool and_group = false, double chance = 100) {
    t.threshold[index(c)] = value;
    t.and_group[index(c)] = and_group;
    t.chance[index(c)] = chance;
}
} // namespace

int main() {
    // ---- defaults: nothing happens until the player sets something --------------------------
    {
        const Settings s;
        check(!s.enabled && !s.diagnostics, "the switch and diagnostics start off");
        check(s.tuning.use_global_chance && s.tuning.global_chance == 0, "Global Chance starts on, at 0 %");
        check(active_count(s.tuning) == 0, "every condition starts off");
        for (double c : s.tuning.chance) check(c == 100, "a condition's own chance starts at 100 % (neutral until the condition is on)");
        check(s.tuning.grade == quality_spin && s.tuning.selector_override && s.tuning.hold_grade && s.tuning.window_start == 0 && s.tuning.window_length == 3,
              "advanced settings start at the accepted smooth setup (Spin, selector, hold, 0 + 3)");
        int taken = 0;
        const auto d = run(s.tuning, landing(1, 2, 6), {0.0}, &taken);
        check(d.qualified && !d.chosen && taken == 0, "the defaults pick nothing and draw nothing");
        check(builtin_presets().size() == 1 && builtin_presets().front().first == "Jjs Recommended", "recommended preset ships");
        const auto& recommended = builtin_presets().front().second;
        check(!recommended.use_global_chance && recommended.global_chance == 0, "recommended Global Chance is off");
        check(active_count(recommended) == 3 && recommended.threshold[index(Condition::min_air)] == 2.0 &&
              recommended.threshold[index(Condition::min_drop)] == 1.7 && recommended.threshold[index(Condition::body_spin)] == 200.0,
              "recommended thresholds match the supplied settings");
        for (std::size_t i = 0; i < condition_count; ++i)
            check(!recommended.and_group[i] && recommended.chance[i] == 100, "recommended conditions are OR at 100 percent");
        check(!run(recommended, landing(.5, .5, 2), {}).chosen, "recommended rejects a small straight landing");
        check(run(recommended, landing(2.0, .5, 2), {}).chosen, "recommended air condition qualifies alone");
        check(run(recommended, landing(.5, 1.7, 2), {}).chosen, "recommended drop condition qualifies alone");
        auto spinning = landing(.5, .5, 2);
        spinning.spin_angle = 200;
        check(run(recommended, spinning, {}).chosen, "recommended body spin qualifies alone");
        check(tuning_from_json(to_json(recommended)) == recommended, "recommended settings round trip exactly");
    }
    // ---- Global Chance alone ----------------------------------------------------------------
    {
        Tuning t;
        t.global_chance = 100;
        int taken = 0;
        check(run(t, landing(.3, .1, 2), {}, &taken).chosen && taken == 0, "100 % picks every landing without a draw");
        t.global_chance = 30;
        check(run(t, landing(.3, .1, 2), {29.9}).chosen, "a roll under 30 picks");
        check(!run(t, landing(.3, .1, 2), {30.0}).chosen, "a roll of 30 does not");
        t.use_global_chance = false;
        const auto d = run(t, landing(.3, .1, 2), {0.0}, &taken);
        check(d.qualified && !d.chosen && taken == 0, "Global Chance off and no condition on: nothing is picked");
    }
    // ---- one OR condition with its own chance ---------------------------------------------------
    {
        Tuning t;
        t.use_global_chance = false;
        set(t, Condition::body_spin, 170, false, 50);
        int taken = 0;
        auto d = run(t, landing(.8, 1, 5, 120), {0.0}, &taken);
        check(!d.qualified && !d.chosen && taken == 0, "a failed condition draws nothing and does not qualify (the game's own pulse is cancelled)");
        d = run(t, landing(.8, 1, 5, 175), {49.0}, &taken);
        check(d.qualified && d.chosen && taken == 1, "a held condition rolls its own chance once");
        d = run(t, landing(.8, 1, 5, 175), {51.0});
        check(d.qualified && !d.chosen, "a missed condition roll qualifies but does not pick");
        auto m = landing(.8, 1, 5, 175);
        m.angle_known = false;
        check(!run(t, m, {0.0}).qualified, "an unknown body angle never meets the spin condition");
    }
    // ---- condition, then Global Chance -------------------------------------------------------------
    {
        Tuning t;
        t.global_chance = 50;
        set(t, Condition::min_drop, 1.5, false, 100);
        int taken = 0;
        check(run(t, landing(.8, 2, 5), {10.0}, &taken).chosen && taken == 1, "condition at 100 % needs no draw, then Global Chance rolls");
        check(!run(t, landing(.8, 2, 5), {60.0}).chosen, "Global Chance can still refuse a qualified landing");
        set(t, Condition::min_drop, 1.5, false, 40);
        check(!run(t, landing(.8, 2, 5), {50.0, 0.0}).chosen, "a missed condition roll stops before Global Chance");
        check(run(t, landing(.8, 2, 5), {30.0, 10.0}, &taken).chosen && taken == 2, "condition roll then global roll");
    }
    // ---- several OR conditions: any one success is enough --------------------------------------------
    {
        Tuning t;
        t.use_global_chance = false;
        set(t, Condition::min_air, .6, false, 20);
        set(t, Condition::min_speed, 4, false, 70);
        check(run(t, landing(.8, 1, 5), {50.0, 60.0}).chosen, "the first OR misses, the second picks");
        check(!run(t, landing(.8, 1, 5), {50.0, 80.0}).chosen, "both OR rolls miss");
        int taken = 0;
        check(run(t, landing(.8, 1, 5), {10.0, 99.0}, &taken).chosen && taken == 1, "the first success ends the OR rolls");
        check(run(t, landing(.2, 1, 5), {60.0}).chosen, "only the held OR condition rolls");
    }
    // ---- the AND group: all of it, then one roll at its lowest chance ----------------------------------
    {
        Tuning t;
        t.use_global_chance = false;
        set(t, Condition::min_drop, 1.0, true, 80);
        set(t, Condition::min_speed, 4.0, true, 30);
        int taken = 0;
        auto d = run(t, landing(.8, 2, 3), {0.0}, &taken);
        check(!d.qualified && taken == 0, "one AND member failing fails the group, without a draw");
        d = run(t, landing(.8, 2, 5), {29.0}, &taken);
        check(d.chosen && taken == 1, "the whole AND group holds: one roll");
        check(!run(t, landing(.8, 2, 5), {31.0}).chosen, "the group's roll uses its lowest chance (30)");
        // Mixed: body spin (OR) or (drop AND speed).
        set(t, Condition::body_spin, 170, false, 100);
        check(run(t, landing(.8, .2, 1, 180), {}).chosen, "the OR route picks on its own");
        check(run(t, landing(.8, 2, 5, 10), {10.0}).chosen, "the AND route picks on its own");
        check(!run(t, landing(.8, .2, 5, 10), {0.0}).qualified, "neither route holds");
        // The OR route missing its roll still leaves the AND route its own roll.
        set(t, Condition::body_spin, 170, false, 10);
        check(run(t, landing(.8, 2, 5, 180), {50.0, 20.0}).chosen, "OR roll misses, AND roll picks");
    }
    // ---- Max conditions and the game's grade ----------------------------------------------------------
    {
        Tuning t;
        t.global_chance = 100;
        set(t, Condition::max_air, .5);
        check(run(t, landing(.4, 0, 3), {}).chosen && !run(t, landing(.6, 0, 3), {}).qualified, "Max air time keeps short airs only");
        Tuning g;
        g.global_chance = 100;
        set(g, Condition::game_grade, 1);
        check(run(g, landing(.5, 1, 4, 0, quality_spin), {}).chosen, "Sketchy or Spin: a Spin landing");
        check(!run(g, landing(.5, 1, 4, 0, quality_good), {}).qualified, "Sketchy or Spin: not a Good landing");
        set(g, Condition::game_grade, 2);
        check(run(g, landing(.5, 1, 4, 0, quality_straight), {}).chosen, "Straight or Good: a Straight landing");
        Tuning y;
        y.global_chance = 100;
        set(y, Condition::board_yaw, 180);
        auto m = landing(.5, 1, 4);
        m.board_yaw = -200;
        check(run(y, m, {}).chosen, "board yaw speed counts either direction");
    }
    // ---- sanitizing -------------------------------------------------------------------------------
    {
        Tuning t;
        t.global_chance = 250;
        t.threshold[index(Condition::min_air)] = -1;
        t.chance[index(Condition::min_air)] = std::nan("");
        t.threshold[index(Condition::game_grade)] = 1.6;
        t.grade = 7;
        t.window_length = 0;
        t.window_start = 99;
        t = sanitize(t);
        check(t.global_chance == 100 && t.threshold[index(Condition::min_air)] == 0 && t.chance[index(Condition::min_air)] == 100, "numbers are clamped");
        check(t.threshold[index(Condition::game_grade)] == 2 && t.grade == quality_spin, "grades are whole");
        check(t.window_length == 1 && t.window_start == 30, "the window stays in range");
    }
    // ---- names --------------------------------------------------------------------------------------
    check(valid_name("JJ's Recommended") && valid_name("Big drops_2") && !valid_name("") && !valid_name(" lead") && !valid_name("a/b"),
          "preset names");
    check(descriptor_name("Animation/Dingo/Bool.Anim.SketchyLanding", "Bool.Anim.SketchyLanding") &&
              descriptor_name("animation/dingo/bool.anim.sketchylanding", "Bool.Anim.SketchyLanding") &&
              !descriptor_name("Animation/Dingo/Bool.Anim.SketchyLandingX", "Bool.Anim.SketchyLanding"),
          "descriptor names match exactly, in any case");
    // ---- steezy-landings.json -------------------------------------------------------------------------
    {
        Store s;
        s.settings.enabled = true;
        s.settings.preset = "Mine";
        auto& t = s.settings.tuning;
        t.use_global_chance = false;
        t.global_chance = 35;
        set(t, Condition::body_spin, 170, false, 60);
        set(t, Condition::min_drop, 1.2, true, 45);
        set(t, Condition::game_grade, 1, true, 100);
        t.grade = quality_sketchy;
        t.hold_grade = false;
        t.window_start = 1;
        t.window_length = 5;
        s.presets["Mine"] = t;
        s.presets["Other"] = Tuning{};
        const auto back = store_from_json(dingosdk::Json::parse(to_json(s).dump(2)));
        check(back.settings == s.settings, "settings come back exactly");
        check(back.presets == s.presets, "presets come back exactly");
        const auto empty = store_from_json(dingosdk::Json::parse("{}"));
        check(empty.settings == Settings{} && empty.presets.empty(), "an empty file gives the defaults");
        const auto odd = store_from_json(dingosdk::Json::parse(
            R"({"enabled":"yes","current":{"global":{"chance":"x"},"conditions":{"min_air":{"value":0.7},"nonsense":{}},"grade":"sketchy","window_length":500},)"
            R"("presets":{"bad/name":{},"Fine":{"global":{"on":false}}},"future":1})"));
        check(!odd.settings.enabled, "a wrong kind of value keeps the default");
        check(odd.settings.tuning.threshold[index(Condition::min_air)] == 0.7 && odd.settings.tuning.chance[index(Condition::min_air)] == 100,
              "a condition without its other fields keeps their defaults");
        check(odd.settings.tuning.grade == quality_sketchy && odd.settings.tuning.window_length == 60, "grade read, window clamped");
        check(odd.presets.size() == 1 && odd.presets.contains("Fine") && !odd.presets.at("Fine").use_global_chance, "invalid preset names are dropped");
        Store find;
        find.presets["Big Drops"] = Tuning{};
        check(find.find("big drops") != find.presets.end() && find.find("nope") == find.presets.end(), "presets are found in any letter case");
    }
    // Flight/classifier timing: a grounded frame must not erase the completed jump.
    {
        FlightMeasurements flight;
        flight.sample(false, .1, 10, true);
        flight.sample(true, .1, 10.5, true);
        flight.sample(true, .1, 12, true);
        flight.sample(true, .1, 11, true);
        flight.sample(false, .1, 9, true);
        flight.sample(false, .1, 8, true); // classifier delayed; ground motion must not change touchdown
        check(flight.pending && flight.time_known && std::abs(flight.seconds - .3) < 1e-6,
              "airtime survives grounded frames until the classifier consumes it");
        check(flight.height_known && flight.drop == 3, "drop is measured from peak to first touchdown, not a gravity/speed estimate");
        flight.consume(8, true);
        check(!flight.pending && flight.drop == 3, "a consumed flight cannot be reused by another classified event");
        flight.sample(true, .1, 8.5, true);
        flight.sample(false, .1, 8, true);
        check(flight.pending && std::abs(flight.seconds - .1) < 1e-6 && flight.drop == .5,
              "the next flight gets fresh measurements without reloading the instance");
        FlightMeasurements early;
        early.sample(true, .1, 7, true); early.sample(true, .1, 8, true);
        early.consume(6, true); // classified inside output bridge before post-step sees Ground
        check(early.landed && !early.pending && early.drop == 2, "classification before post-step still freezes the correct touchdown height");
        FlightMeasurements missing;
        missing.sample(true, .1, 0, false); missing.sample(false, .1, 4, true);
        check(!missing.height_known && missing.time_known, "missing position invalidates drop without invalidating airtime");
        missing.sample(true, .5, 5, true);
        check(!missing.time_known, "invalid time step cannot qualify an airtime condition");
    }
    // Conditions fail closed on absent measurements and see changed values immediately.
    {
        Tuning t; t.global_chance = 100;
        set(t, Condition::min_air, .6);
        auto m = landing(.8, 2, 5);
        check(run(t, m, {}).chosen, "initial airtime threshold passes");
        set(t, Condition::min_air, 1.2);
        check(!run(t, m, {}).qualified, "edited airtime threshold applies on the very next decision");
        set(t, Condition::min_air, 0); set(t, Condition::min_drop, 1.5);
        check(run(t, m, {}).chosen, "measured drop can qualify");
        set(t, Condition::min_drop, 3);
        check(!run(t, m, {}).qualified, "edited drop threshold applies without reloading settings");
        set(t, Condition::min_drop, 0); set(t, Condition::max_air, 1);
        m.air = 0; m.air_known = false;
        check(!run(t, m, {}).qualified, "unknown air is not treated as a zero-second qualifying jump");
        set(t, Condition::max_air, 0); set(t, Condition::max_drop, 1);
        m.drop = 0; m.drop_known = false;
        check(!run(t, m, {}).qualified, "unknown drop is not treated as a qualifying flat landing");
    }
    // Exact value entry keeps its edited number through blur, even if other rows draw first.
    {
        dingosdk::overlay::detail::NumericEdits<double> edits;
        check(edits.value(1, .5, false) == .5, "new numeric edit starts from its live value");
        edits.retain(1, 1.25);
        check(edits.value(2, 3, false) == 3, "other widgets do not overwrite the pending numeric edit");
        edits.release(2);
        check(edits.value(1, .5, true) == 1.25, "deactivation frame commits the edited value, not the old model snapshot");
        edits.retain(2, 4);
        check(edits.value(1, .5, true) == 1.25, "switching to an earlier row preserves both edit/deactivation values");
        edits.release(1);
        check(edits.value(1, 2, false) == 2, "next edit follows current live settings");
    }
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "steezy landings: all checks passed\n";
    return 0;
}
