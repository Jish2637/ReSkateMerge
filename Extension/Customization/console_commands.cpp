#include "Extension/Console/commands.h"
#include "Extension/Customization/item_browser.h"
#include "Extension/Customization/local_customization_runtime.h"

namespace dingosdk::console {
void register_item_commands(Commands &registry) {
    auto arguments = argument("verb_and_arguments", Type::text, true);
    arguments.rest = true;
    arguments.complete = [](const Model &, auto) {
        return std::vector<std::string>{"fav", "search", "filter", "status", "peek", "poke"};
    };
    auto items = action("items", "Skater item grids: fav (favorite the highlighted item), search <text>, "
                        "filter all|favorites|mods|game, status; peek/poke are research aids",
                        Group::console, {std::move(arguments)});
    items.run = [](const Model &, const Values &values, const Output &out) {
        const auto text = values.empty() ? std::string{} : std::get<std::string>(values[0]);
        const auto space = text.find(' ');
        const auto verb = text.substr(0, space);
        const auto rest = space == std::string::npos ? std::string{} : text.substr(space + 1);
        if (verb == "ownership_override") {
            // Intentionally omitted from public help, menus and completions.
            if (rest == "on" || rest == "1") profile_runtime::set_ownership_override(true);
            else if (rest == "off" || rest == "0") profile_runtime::set_ownership_override(false);
            else if (!rest.empty() && rest != "status") {
                out("Usage: items ownership_override on|off|status");
                return;
            }
            out(std::string("Ownership override: ") + (profile_runtime::ownership_override_enabled() ? "on" : "off") +
                (profile_runtime::ownership_override_restart_required() ? ". Saved. Restart the game to apply; current-session ownership is unchanged." : ". Saved to your local profile; active for this session."));
        } else if (verb == "fav") {
            item_browser::toggle_focused_favorite();
            out("Favorite toggled for the highlighted item.");
        } else if (verb == "search") {
            item_browser::set_search(rest);
            out(rest.empty() ? "Search cleared." : "Searching for \"" + rest + "\".");
        } else if (verb == "filter") {
            for (std::size_t i = 0; i < std::size(item_browser::filter_names); ++i)
                if (item_browser::folded(item_browser::filter_names[i]).starts_with(item_browser::folded(rest)) && !rest.empty()) {
                    item_browser::set_filter(static_cast<item_browser::Filter>(i));
                    out("Filter: " + std::string(item_browser::filter_names[i]) + ".");
                    return;
                }
            item_browser::cycle_filter();
            out("Next filter.");
        } else if (verb == "peek" || verb == "poke") {
            item_browser::request_poke(text);
            out("Queued; the answer is in the log.");
        } else {
            item_browser::request_status();
            out("Status written to the log.");
        }
    };
    registry.add(std::move(items));
}
} // namespace dingosdk::console
