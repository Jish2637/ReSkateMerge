// SANDBOX > TUNE > SKETCHY LANDINGS: the switch, presets and Global Chance, the conditions with their own
// OR/AND and chance, and the advanced settings (a closed dropdown) with the feature's status. Every control edits a
// copy of the module's settings and hands it back (steezy_landings::apply); the module owns the values,
// applies them from the next landing and saves them.
#include "steezy_landings.h"
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include "Extension/UI/skate_theme.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace dingosdk::overlay::menu {
namespace {
namespace steezy = dingosdk::steezy_landings;

struct Ui {
    std::string pick;           // the preset the box shows, by name (kept across list changes)
    std::array<char, 49> name{}; // Save as
    bool seeded{};
};
Ui& ui() {
    static auto* value = new Ui;
    return *value;
}
void hint(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || !ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
float button_width(const char* label) { return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2; }

// Preset box, Load and Delete; then a name, Save; then back to the defaults.
void preset_rows(SkateMenu& menu, const steezy::View& v) {
    auto& u = ui();
    if (!u.seeded) {
        u.seeded = true;
        u.pick = v.settings.preset;
        std::snprintf(u.name.data(), u.name.size(), "%s", v.settings.preset.c_str());
    }
    std::vector<std::string> all = v.builtin;
    all.insert(all.end(), v.own.begin(), v.own.end());
    const auto is_builtin = [&](const std::string& name) { return std::ranges::find(v.builtin, name) != v.builtin.end(); };
    if (std::ranges::find(all, u.pick) == all.end()) u.pick = std::ranges::find(all, v.settings.preset) != all.end() ? v.settings.preset : std::string{};
    const auto& style = ImGui::GetStyle();
    const float load = button_width("Load"), remove = button_width("Delete"), save = button_width("Save");

    field(menu, "Preset", "A preset holds every value below: Global Chance, the conditions and the advanced settings. Not the on/off switch.");
    ImGui::SetNextItemWidth(std::max(px(80), ImGui::GetContentRegionAvail().x - load - remove - style.ItemSpacing.x * 2));
    const std::string shown = u.pick.empty() ? (all.empty() ? std::string("none saved yet") : std::string("Choose a preset")) : u.pick;
    if (ImGui::BeginCombo("##steezy-preset", shown.c_str())) {
        for (const auto& name : all) {
            const auto label = std::format("{}{}{}###{}", name, is_builtin(name) ? "   (ReSkate)" : "", name == v.settings.preset ? "   (loaded)" : "", name);
            if (ImGui::Selectable(label.c_str(), name == u.pick)) u.pick = name;
            if (name == u.pick) ImGui::SetItemDefaultFocus();
        }
        if (all.empty()) ImGui::TextDisabled("Set the values up, type a name below and press Save.");
        ImGui::EndCombo();
    }
    hint("Presets that come with ReSkate are marked; the rest are yours. Pick one, then Load.");
    ImGui::SameLine();
    ImGui::BeginDisabled(u.pick.empty());
    if (ImGui::Button("Load", ImVec2(load, 0))) feedback(menu, steezy::load_preset(u.pick).c_str());
    hint("Sets every value saved in this preset.");
    ImGui::SameLine();
    ImGui::BeginDisabled(is_builtin(u.pick));
    if (ImGui::Button("Delete", ImVec2(remove, 0))) {
        feedback(menu, steezy::delete_preset(u.pick).c_str());
        u.pick.clear();
    }
    ImGui::EndDisabled();
    hint(is_builtin(u.pick) ? "Presets that come with ReSkate cannot be deleted." : "Removes this preset from your list. The values it set stay.");
    ImGui::EndDisabled();

    field(menu, "Save as", "Saves every value below under this name. An existing preset of yours with this name is replaced.");
    ImGui::SetNextItemWidth(std::max(px(80), ImGui::GetContentRegionAvail().x - save - style.ItemSpacing.x));
    ImGui::InputTextWithHint("##steezy-name", "preset name", u.name.data(), u.name.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(!u.name[0]);
    if (ImGui::Button("Save", ImVec2(save, 0))) {
        const std::string name = u.name.data();
        feedback(menu, steezy::save_preset(name).c_str());
        u.pick = name;
    }
    ImGui::EndDisabled();
    hint(u.name[0] ? "Saves the values as they are now under this name." : "Type a name first.");
    if (ImGui::SmallButton("Back to defaults")) feedback(menu, steezy::restore_defaults().c_str());
    hint("Global Chance back to 0 % and every condition off, as Sketchy Landings starts. Your presets are kept; the switch stays as it is.");
}

// One condition: its threshold (0 is off), then its OR/AND switch and its own chance, together.
bool condition_rows(SkateMenu& menu, steezy::Tuning& t, std::size_t i) {
    const auto& info = steezy::conditions[i];
    const auto c = static_cast<steezy::Condition>(i);
    const std::string label(info.label);
    bool changed = false;
    ImGui::PushID(static_cast<int>(i));
    if (c == steezy::Condition::game_grade) {
        int grade = static_cast<int>(t.threshold[i]);
        static const std::vector<std::string> names(steezy::game_grade_names.begin(), steezy::game_grade_names.end());
        if (option_row(menu, label.c_str(), std::string(info.about).c_str(), grade, names)) {
            t.threshold[i] = grade;
            changed = true;
        }
    } else {
        field(menu, label.c_str(), std::string(info.about).c_str());
        double value = t.threshold[i];
        if (slider_double("##value", &value, 0.0, info.high, value > 0 ? info.format : "off")) {
            t.threshold[i] = std::clamp(value, 0.0, info.limit);
            changed = true;
        }
    }
    const bool on = t.threshold[i] > 0;
    field(menu, "", nullptr);
    ImGui::BeginDisabled(!on);
    const float mode = std::max(button_width("AND"), button_width("OR")) + px(8);
    if (t.and_group[i]) ImGui::PushStyleColor(ImGuiCol_Button, skate_theme::blue);
    const bool pressed = ImGui::Button(t.and_group[i] ? "AND###logic" : "OR###logic", ImVec2(mode, 0));
    if (t.and_group[i]) ImGui::PopStyleColor();
    if (pressed) {
        t.and_group[i] = !t.and_group[i];
        changed = true;
    }
    hint(on ? "OR: this condition qualifies a landing by itself. AND: it joins the AND group, which qualifies a landing only when every AND condition holds."
            : "Set the condition above first.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    double chance = t.chance[i];
    if (slider_double("##chance", &chance, 0.0, 100.0, "%.0f%% chance", ImGuiSliderFlags_AlwaysClamp)) {
        t.chance[i] = chance;
        changed = true;
    }
    hint(t.and_group[i] ? "The AND group rolls once when all of its conditions hold, at the lowest chance among them."
                        : "When this condition holds, the landing is picked this often (then Global Chance, if it is on).");
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, px(4)));
    return changed;
}
} // namespace

void steezy_landings_section(SkateMenu& menu) {
    const auto v = steezy::view();
    auto s = v.settings;
    bool changed = false;
    if (!v.status.started) warn(v.status.problem.c_str());

    begin_card(menu, "steezy-main", "GENERAL", "The heavy sketchy landing animations, on the landings you pick");
    {
        if (toggle_row(menu, "Sketchy Landings",
                       "Plays skate.'s heavy sketchy landing animations on the landings Global Chance and the conditions pick. Off, the game lands as it shipped.",
                       s.enabled, v.status.started))
            changed = true;
        note("Needs the game's own setting Experimental > Experimental Sketchy Landings on: it loads these animations.");
        // Preset actions replace settings immediately. Refresh this frame's edit copy so a
        // later widget cannot write the pre-load values back over the loaded setup.
        if (changed) { steezy::apply(s); changed = false; }
        const auto before_presets = steezy::view();
        preset_rows(menu, before_presets);
        const auto after_presets = steezy::view();
        if (after_presets.revision != before_presets.revision) { end_card(); return; }
        s = after_presets.settings;
        ImGui::Dummy(ImVec2(0, px(4)));
        if (toggle_row(menu, "Global Chance",
                       "On: after the conditions, every landing they let through is picked this often (with no condition on, every landing is). "
                       "Off: only the conditions' own chances decide, and with no condition on nothing is picked.",
                       s.tuning.use_global_chance))
            changed = true;
        ImGui::BeginDisabled(!s.tuning.use_global_chance);
        field(menu, "Chance", "Percent of the landings that get the heavy animation. 0 % is none.");
        double chance = s.tuning.global_chance;
        if (slider_double("##global-chance", &chance, 0.0, 100.0, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            s.tuning.global_chance = chance;
            changed = true;
        }
        ImGui::EndDisabled();
        if (s.enabled && !s.tuning.use_global_chance && !steezy::active_count(s.tuning)) warn("Global Chance is off and no condition is on: no landing is picked.");
        else if (s.enabled && s.tuning.use_global_chance && s.tuning.global_chance <= 0) warn("Global Chance is 0 %: no landing is picked. Raise it, or turn it off to use the conditions' own chances.");
    }
    end_card();

    begin_card(menu, "steezy-conditions", "CONDITIONS", "Which landings can be picked. A condition at off does nothing.");
    {
        note("A landing qualifies through any OR condition that holds (each then rolls its own chance), or through the AND group when every AND "
             "condition holds (one roll, at the lowest of their chances). Global Chance is rolled after that, if it is on. With no condition on, "
             "every landing qualifies. A landing that meets no condition also loses the game's own occasional sketchy landing.");
        for (std::size_t i = 0; i < steezy::condition_count; ++i) changed |= condition_rows(menu, s.tuning, i);
    }
    end_card();

    // Closed until opened: for power users, with the status underneath.
    begin_card(menu, "steezy-advanced");
    if (ImGui::TreeNodeEx("ADVANCED##steezy-advanced", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        int grade = s.tuning.grade == steezy::quality_sketchy ? 0 : 1;
        field(menu, "Landing grade", "The grade a picked landing is given. Spin blends in the most smoothly; Sketchy is the grade the game uses for sideways landings.");
        if (choice(menu, "steezy-grade", grade, {"Sketchy", "Spin"})) {
            s.tuning.grade = grade == 0 ? steezy::quality_sketchy : steezy::quality_spin;
            changed = true;
        }
        if (toggle_row(menu, "Supply the flag at the landing selector",
                       "Answers the landing animation table's own read of the sketchy flag for a picked landing. This is what makes the heavy animation play.",
                       s.tuning.selector_override, v.status.selector_active, "N/A"))
            changed = true;
        if (toggle_row(menu, "Hold the grade through the flag window", "Keeps giving the picked grade for every step of the window, not only the first.",
                       s.tuning.hold_grade))
            changed = true;
        field(menu, "Window start (steps)", "Physics steps after touchdown before the flag is set.");
        if (slider_int("##window-start", &s.tuning.window_start, 0, 30, "%d", ImGuiSliderFlags_AlwaysClamp)) changed = true;
        field(menu, "Window length (steps)", "Physics steps the flag stays set. The game's own lasts 1.");
        if (slider_int("##window-length", &s.tuning.window_length, 1, 60, "%d", ImGuiSliderFlags_AlwaysClamp)) changed = true;
        if (toggle_row(menu, "Diagnostics", "One line per landing in logs\\ReSkate.log (lines starting \"Steezy Landings:\"). Off by default.", s.diagnostics))
            changed = true;
        section(menu, "STATUS");
        info(menu, "Hooks", !v.status.started ? v.status.problem : v.status.output_wrapped ? "active" : "active (grade written after the game's)");
        info(menu, "Landing selector", v.status.selector_active ? "active" : "not hooked: the heavy animation cannot be asked for");
        info(menu, "Sketchy flag", v.status.flag_found ? "found" : "not seen yet (found at the first landing with Sketchy Landings on)");
        info(menu, "Landings", std::format("{} decided, {} picked", v.status.landings, v.status.chosen));
        if (!v.status.last_landing.empty()) info(menu, "Last", v.status.last_landing);
        ImGui::TreePop();
    }
    end_card();

    if (changed) steezy::apply(s);
}
} // namespace dingosdk::overlay::menu
