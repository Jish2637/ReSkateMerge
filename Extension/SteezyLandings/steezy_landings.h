#pragma once
#include "steezy_landings_model.h"

#include <cstdint>
#include <string>
#include <vector>

// Steezy Landings: skate.'s heavy "Experimental Sketchy Landings" animations on the landings the
// player picks (by chance, or by air time, drop, speed, spin...), instead of only the game's own
// rare ones. Needs the game's Experimental > "Experimental Sketchy Landings" setting on: that is
// what loads the heavy clips.
//
// How (live-tested in the standalone DLL, v0.9-v0.15): once per landing, on the physics step the
// game classifies it (P+0xB0), the landing is measured and decided. For a chosen landing, during a
// short window of steps the output bridge publishes the chosen grade (Spin by default) instead of
// the game's own, Bool.Anim.SketchyLanding is set through the game's own Boolean writer, and the
// landing table's own read of that flag (CDB_OnBoard_Land, caller 0x11D189E) is answered true for
// that skater's context while the active selector evaluates it. When conditions are on and a
// landing meets none of them, the game's own occasional sketchy pulse is cancelled the same way,
// so the conditions hold exactly. Switched off, nothing is changed: every hook forwards untouched.
//
// Ownership: this module owns its settings (one live copy, published to the physics and animation
// threads as an immutable snapshot) and steezy-landings.json in ReSkate's trainer folder; the menu
// (SANDBOX > TUNE > STEEZY LANDINGS) edits them through the functions below. Only the validated local skater
// published by ReSkate's No Bail owner is handled; remote riders and NPCs stay native.
namespace dingosdk::steezy_landings {
// Runtime initialization, after the supported build is verified. Reads the saved settings and
// installs the hooks (Detours, through Engine/Core/Hooks). False leaves the game untouched.
bool start(std::uintptr_t image_base) noexcept;
// Client thread, every tick: writes steezy-landings.json a moment after the last change.
void on_client_tick() noexcept;

struct Status {
    bool started{};          // hooks in place
    bool output_wrapped{};   // the grade is supplied while the game publishes it (else after it)
    bool selector_active{};  // the landing table's own read is answered
    bool flag_found{};       // Bool.Anim.SketchyLanding resolved from the landing table
    std::uint64_t landings{}, chosen{};
    std::string last_landing; // "chosen, 0.52 s air, ~1.5 m drop, 6.5 m/s, 172 deg; game grade Spin"
    std::string problem;      // why it did not start, when it did not
};
struct View {
    Settings settings;
    std::vector<std::string> builtin; // presets that come with ReSkate
    std::vector<std::string> own;     // the player's, sorted
    Status status;
    std::uint64_t revision{};         // changes whenever settings or presets change
};
View view();

// Menu thread. A changed setting applies from the next landing and is saved shortly after.
void apply(const Settings& next);
// Preset actions; each answers a line for the menu's feedback.
std::string load_preset(const std::string& name);  // sets every value the preset holds
std::string save_preset(const std::string& name);  // the current values under this name (replaces one of the player's)
std::string delete_preset(const std::string& name);
std::string restore_defaults();                   // everything off and 0 again (the switch keeps its state)
} // namespace dingosdk::steezy_landings
