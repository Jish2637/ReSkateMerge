#pragma once
#include "server_text.h"
#include <iterator>

namespace dingosdk::server {
struct ModeratorHelpEntry { std::string_view group, command, text; };
inline constexpr std::pair<std::string_view, std::string_view> moderator_help_groups[] = {
    {"objects", "Object history, nearby objects and cleanup"},
    {"voice", "Manual voice mutes"},
    {"players", "Player lists, kicks and bans"},
    {"chat", "Staff messages and private announcements"},
    {"votes", "Polls, voting and cancellation"},
    {"teleport", "Player teleportation"},
    {"roles", "Roles and permissions"},
    {"builds", "Save and restore shared builds"},
};
inline constexpr ModeratorHelpEntry moderator_help_entries[] = {
    {"builds", "build", "/build save <name> [replace] - new saves are immediate; overwrites require confirmation."},
    {"builds", "build", "/build list [page] - current map's saves, object counts and loaded status."},
    {"builds", "build", "/build load <name> - add a server-owned copy; originals remain and may overlap."},
    {"builds", "build", "/build unload <name> - preview removal of the loaded copy; keep its save."},
    {"builds", "build", "/build remove <name> - preview permanent save deletion and unloading its active copy."},
    {"builds", "build", "/build confirm - approve your pending action within 60 seconds; changes to the target invalidate it."},
    {"builds", "build", "/build cancel - cancel your pending action. Disconnects and map changes cancel it too."},
    {"builds", "build", "Names: 1-32 letters/numbers/-/_. Restored pieces cannot be moved with a player's dropper; build around them and save again."},
    {"objects", "whoplaced", "/whoplaced [minutes] [page] - placement history; defaults to the last 10 minutes."},
    {"objects", "nearobj", "/nearobj [object name] - objects/owners within 5m; partial names work; snapshot lasts 3 minutes."},
    {"objects", "nearobj", "/nearobj page <n> - show another nearby-object results page."},
    {"objects", "delobj", "/delobj <player> - clear ALL that player's placed objects; no preview required."},
    {"objects", "delobj", "/delobj #<n> - remove that listed shared object within 5m; player owner may retain a local copy."},
    {"voice", "mute", "/mute <player> [minutes] - voice mute; omit minutes for permanent. Alias: /vmute."},
    {"voice", "unmute", "/unmute <player name or SteamID64> - restore voice, including offline players. Alias: /vunmute."},
    {"voice", "mutes", "/mutes [page] - active voice mutes and time remaining. Alias: /vmutes. Text chat stays available."},
    {"players", "players", "/players - list connected players, roles and SteamIDs."},
    {"players", "kick", "/kick <player> [minutes|restart] - use the server default when omitted; lower-rank players only."},
    {"players", "lock", "/lock <player|SteamID64> <tool[,tool...]|all> - lock tools until unlocked or restart."},
    {"players", "unlock", "/unlock <player|SteamID64> [tools|all] - restore permitted tools."},
    {"players", "locks", "/locks - list current tool locks."},
    {"players", "ban", "/ban <player or SteamID64> [name] - ban a lower-rank player."},
    {"players", "unban", "/unban <SteamID64> - remove a saved ban."},
    {"players", "bans", "/bans - list saved bans."},
    {"chat", "cleartag", "/cleartag <player> - remove and disable their active purchased name tag; ownership is retained."},
    {"chat", "msg", "/msg <player> <message> - privately message one player."},
    {"chat", "timeout", "/timeout <player> <minutes> - timed text-chat mute; duration required; survives reconnects and restarts."},
    {"chat", "untimeout", "/untimeout <player name or SteamID64> - remove a text timeout, including for an offline player."},
    {"chat", "timeouts", "/timeouts [page] - list active text-chat timeouts and remaining minutes."},
    {"chat", "msg-party", "/msg-party <player> <message> - privately message that player's party."},
    {"chat", "msg-admins", "/msg-admins <message> - privately message connected staff."},
    {"chat", "announce-to", "/announce-to <player> <text> - show one player an announcement card."},
    {"votes", "@poll", "/poll <question> | <answer> | <answer>... - start a poll when server polls are enabled."},
    {"votes", "@poll-end", "/poll end - end a poll you started; staff with poll permission can end anyone's."},
    {"votes", "vote-cancel", "/vote-cancel - stop the running vote."},
    {"votes", "@vote", "/vote <map|kick|tod|extend|custom name> [argument] - start an enabled vote."},
    {"votes", "@vote", "/vote list - list this server's custom votes and their choices."},
    {"votes", "@vote", "/yes or /no - answer the running vote. Aliases: /vote yes, /vote no."},
    {"votes", "@answer", "/1 through /6 - choose an answer in the running poll."},
    {"teleport", "tphere", "/tphere <player> - teleport a player to you."},
    {"roles", "role", "/role <player or SteamID64> <role>|none - change a lower-rank player's role within your rank limit."},
    {"roles", "roles", "/roles [reload] - list roles and permissions, or reload data/roles.json."},
};
// A category page stays within the existing twelve-private-line reply limit.
template<class Allowed>
std::string grouped_moderator_help(std::string_view argument, Allowed allowed) {
    std::vector<std::pair<std::string_view, std::string_view>> groups;
    for (const auto &group : moderator_help_groups)
        if (std::any_of(std::begin(moderator_help_entries), std::end(moderator_help_entries), [&](const auto &entry) { return entry.group == group.first && allowed(entry.command); }))
            groups.push_back(group);
    const auto [topic, page_text] = split(argument);
    if (topic.empty() || lower(topic) == "index") {
        if (!page_text.empty()) return "Usage: /modhelp [group or group number] [page].";
        std::string result = "Moderator commands - /modhelp <group> [page]";
        for (std::size_t i = 0; i < groups.size(); ++i)
            result += "\n" + std::to_string(i + 1) + ". /modhelp " + std::string(groups[i].first) + " - " + std::string(groups[i].second);
        return result;
    }
    auto selected = lower(topic);
    if (const auto n = number(topic)) {
        if (!*n || *n > groups.size()) return "Usage: /modhelp <group or group number> [page]. Type /modhelp for groups.";
        selected = groups[*n - 1].first;
    }
    const auto group = std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.first == selected; });
    if (group == groups.end()) return "Usage: /modhelp <group or group number> [page]. Type /modhelp for available groups.";
    std::vector<std::string_view> lines;
    for (const auto &entry : moderator_help_entries) if (entry.group == selected && allowed(entry.command)) lines.push_back(entry.text);
    const auto pages = (lines.size() + 7) / 8;
    const auto page = page_text.empty() ? std::optional<std::uint64_t>{1} : number(page_text);
    if (!page || !*page || *page > pages) return "Usage: /modhelp " + selected + " [page 1-" + std::to_string(pages) + "].";
    std::string result = "Moderator commands | " + std::string(group->second) + " | page " + std::to_string(*page) + "/" + std::to_string(pages);
    for (auto i = (*page - 1) * 8; i < std::min<std::size_t>(*page * 8, lines.size()); ++i) result += "\n" + std::string(lines[i]);
    if (*page < pages) result += "\nNext: /modhelp " + selected + " " + std::to_string(*page + 1);
    result += "\nGroups: /modhelp";
    return result;
}
} // namespace dingosdk::server
