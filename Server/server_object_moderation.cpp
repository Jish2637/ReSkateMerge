#include "server_host.h"
#include "server_text.h"
#include "msc_presence.h"
#include <charconv>
#include <cmath>
#include <ctime>
#include <format>

namespace dingosdk::server {
namespace {
constexpr std::size_t page_size = 8;
std::string label(std::string text, std::size_t limit = 64) {
    text = clean_chat_text(text); cut_text(text, limit); return text;
}
std::string object_label(std::string text) {
    if (text.starts_with("own_")) text.erase(0, 4);
    return label(std::move(text));
}
float distance2(const std::array<float, 3> &a, const std::array<float, 3> &b) {
    float result{};
    for (unsigned i = 0; i < 3; ++i) result += (a[i] - b[i]) * (a[i] - b[i]);
    return result;
}
}
void Host::audit_objects(Guest &guest, const std::vector<NetworkObject> &layout) {
    std::vector<ObjectPlacementRecord> records;
    std::vector<NetworkObject> added;
    const auto wall = static_cast<std::int64_t>(std::time(nullptr));
    const bool imported = !guest.objects_observed;
    guest.objects_observed = true;
    for (const auto &o : layout) {
        if (guest.shared.objects().contains(o.id)) continue;
        added.push_back(o);
        records.push_back({wall, guest.member.id, o.id, guest_name(guest), o.item, map_name(), imported});
    }
    object_audit_.record(std::move(records), wall);
    if (const auto report = guest.object_spam_window.observe(now_, added, config_.object_spam_alerts, imported)) {
        log_("[object-spam] Possible object spam: " + guest_name(guest) + " (" + std::to_string(guest.member.id) + ") - " +
             std::to_string(report->count) + " objects in " + std::to_string(config_.object_spam_alerts.window_seconds) + "s: " + report->details());
        for (const auto &line : object_spam_lines(guest_name(guest), *report, config_.object_spam_alerts.window_seconds, multiplayer_chat_max_bytes))
            tell_admins(line);
    }
}

std::string Host::object_moderation_command(std::string_view name, std::string_view argument, std::uint64_t caller) {
    const auto wall = static_cast<std::int64_t>(std::time(nullptr));
    if (name == "whoplaced") {
        const auto [minutes_text, page_text] = split(argument);
        double minutes = 10; // no argument: the last ten minutes
        if (!minutes_text.empty()) {
            const auto parsed = std::from_chars(minutes_text.data(), minutes_text.data() + minutes_text.size(), minutes);
            if (parsed.ec != std::errc{} || parsed.ptr != minutes_text.data() + minutes_text.size()) minutes = 0;
        }
        const auto page = page_text.empty() ? std::optional<std::uint64_t>{1} : number(page_text);
        if (!std::isfinite(minutes) || minutes <= 0 || minutes > 1440 || !page || !*page || *page > ObjectAudit::max_records)
            return "Usage: /whoplaced [minutes: >0 to 1440] [page]. Default: last 10 minutes.";
        std::vector<const ObjectPlacementRecord *> matches;
        for (auto it = object_audit_.records().rbegin(); it != object_audit_.records().rend(); ++it)
            if (it->time <= wall && it->time > wall - static_cast<std::int64_t>(minutes * 60)) matches.push_back(&*it);
        if (matches.empty()) return "No recorded placements in that period. History starts when this build first observes objects.";
        const auto pages = (matches.size() + page_size - 1) / page_size;
        if (*page > pages) return "That history page does not exist (" + std::to_string(pages) + " pages).";
        std::string result = "Placements: " + std::to_string(matches.size()) + " | page " + std::to_string(*page) + "/" + std::to_string(pages) + " | times UTC";
        for (auto i = (*page - 1) * page_size; i < std::min(*page * page_size, matches.size()); ++i) {
            const auto &r = *matches[i];
            const auto stamp = static_cast<std::time_t>(r.time);
            std::tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &stamp);
#else
            gmtime_r(&stamp, &utc);
#endif
            char clock[20]{}; std::strftime(clock, sizeof(clock), "%m-%d %H:%M:%S", &utc);
            result += "\n" + std::string(clock) + " " + label(r.name, 40) + " - " + object_label(r.item) + " | " + label(r.map, 40) + (r.imported ? " [brought on sync]" : "");
        }
        if (*page < pages) result += "\nNext: /whoplaced " + (minutes_text.empty() ? "10" : std::string(minutes_text)) + " " + std::to_string(*page + 1);
        return result;
    }
    auto selection = trim(argument);
    if (name == "delobj" && !selection.starts_with('#')) {
        if (selection.empty()) return "Usage: /delobj <player name> clears ALL their objects; /delobj #<number> removes one shared object from your /nearobj snapshot.";
        if (selection.size() >= 2 && ((selection.front() == '"' && selection.back() == '"') || (selection.front() == '\'' && selection.back() == '\'')))
            selection = trim(selection.substr(1, selection.size() - 2));
        if (selection.empty()) return "Usage: /delobj <player name>. Unique partial names work.";
        // MSC Hub staff forms transmit the server-validated player's Steam ID.
        // Resolve IDs before name matching so tags, role prefixes, and renames
        // do not break the object-removal action. Legacy /delobj name syntax stays.
        const auto requested_id = number(selection);
        Guest *target = requested_id ? find(*requested_id) : nullptr;
        if (target && !target->handshaken) target = nullptr;
        std::vector<Guest *> matches;
        if (!target) {
            const auto query = lower(selection);
            std::vector<Guest *> exact;
            for (const auto &[id, guest] : guests_) {
                if (!guest->handshaken) continue;
                const auto canonical = lower(guest_name(*guest));
                const auto displayed = lower(display_name(*guest));
                if (canonical == query || displayed == query) { exact.push_back(guest.get()); continue; }
                if (canonical.find(query) != std::string::npos || displayed.find(query) != std::string::npos) matches.push_back(guest.get());
            }
            if (!exact.empty()) matches = std::move(exact);
        }
        if (!target) {
            if (matches.empty()) return "No connected player matches that name. Use the player's full name or a longer partial name.";
            if (matches.size() != 1) {
                std::string result = "Multiple players match. Use a longer or exact name:";
                for (std::size_t i = 0; i < std::min<std::size_t>(matches.size(), 8); ++i) result += "\n" + label(guest_name(*matches[i]), 128);
                if (matches.size() > 8) result += "\nMore matches; narrow the name further.";
                return result;
            }
            target = matches.front();
        }

        if (target->objects.objects().empty() && target->shared.objects().empty()) return label(guest_name(*target), 64) + " has no placed objects.";
        const auto removed = target->shared.objects().size();
        for (const auto *state : {&target->objects, &target->shared})
            for (const auto &[id, value] : state->objects()) { (void)value; target->cleared.insert(id); }
        target->shared.replace({});
        target->shared_from = target->objects.revision();
        ++target->local_object_clears;
        roster_dirty_ = true;
        object_previews_.erase(caller);
        const auto *actor = find(caller);
        const auto by = actor ? guest_name(*actor) : std::string("Server console");
        log_("[object-remove] " + by + " (" + std::to_string(caller) + ") cleared " + std::to_string(removed) +
             " shared objects owned by " + guest_name(*target) + " (" + std::to_string(target->member.id) + ") on " + map_name() + "; direct player clear");
        send_chat("Server staff have removed your placed objects (all objects cleared).", target);
        return "Cleared all " + std::to_string(removed) + " shared objects owned by " + label(guest_name(*target), 48) + "; sent their local object-clear request.";
    }
    auto *actor = find(caller);
    if (!caller || !actor || !actor->handshaken || !actor->world_ready) return "Use this command in-game after your map has loaded.";
    const auto position = player_position(caller);
    if (!position || now_ < actor->pose_arrival || now_ - actor->pose_arrival > 5000000) return "Your character position is unavailable or stale. Move, then try again.";
    const auto preview_object = [&](const ObjectPreview::Entry &e) -> const NetworkObject * {
        const ObjectState *state{};
        if (e.owner == id_ && e.epoch == epoch_) state = &park_objects_;
        else if (const auto *g = find(e.owner); g && g->handshaken && g->world_ready && g->member.epoch == e.epoch) state = &g->shared;
        if (!state) return nullptr;
        const auto it = state->objects().find(e.id);
        return it == state->objects().end() ? nullptr : &it->second;
    };
    const auto owner_label = [&](const ObjectPreview::Entry &e) {
        if (e.owner == id_) {
            for (const auto &build : builds_loaded_)
                if (std::any_of(build.objects.begin(),build.objects.end(),[&](const auto &o){return o.id==e.id;}))return std::string("Saved build: ")+build.name;
            for (const auto &park : parks_spawned_)
                if (std::any_of(park.objects.begin(), park.objects.end(), [&](const auto &o) { return o.id == e.id; })) return std::string("Server park: ") + park.name;
            return std::string("Server park");
        }
        const auto *g = find(e.owner);
        return g ? guest_name(*g) : std::string("Disconnected player");
    };
    if (name == "nearobj") {
        const auto [first, rest] = split(argument);
        const bool paging = lower(first) == "page";
        const auto page = paging ? number(rest) : std::optional<std::uint64_t>{1};
        if (!page || !*page || *page > 20000) return "Usage: /nearobj [partial object name] or /nearobj page <number>";
        if (!paging) {
            auto &preview = object_previews_[caller];
            preview = {}; preview.world = world_; preview.until = now_ + 180000000;
            const auto filter = lower(trim(argument));
            for (const auto &[owner, guest] : guests_) {
                if (!guest->handshaken || !guest->world_ready) continue;
                for (const auto &[id, object] : guest->shared.objects())
                    if (distance2(*position, object.position) <= 25.f && lower(object.item).find(filter) != std::string::npos)
                        preview.entries.push_back({owner, guest->member.epoch, id, object.item});
            }
            for (const auto &[id, object] : park_objects_.objects())
                if (distance2(*position, object.position) <= 25.f && lower(object.item).find(filter) != std::string::npos)
                    preview.entries.push_back({id_, epoch_, id, object.item});
            std::sort(preview.entries.begin(), preview.entries.end(), [&](const auto &a, const auto &b) {
                const auto da = distance2(*position, preview_object(a)->position);
                const auto db = distance2(*position, preview_object(b)->position);
                if (da != db) return da < db;
                return std::tie(a.owner, a.id) < std::tie(b.owner, b.id);
            });
        }
        const auto at = object_previews_.find(caller);
        if (at == object_previews_.end() || now_ >= at->second.until || at->second.world != world_) return "Preview expired. Run /nearobj again.";
        const auto &preview = at->second;
        if (preview.entries.empty()) return "No placed objects match within 5 m.";
        const auto pages = (preview.entries.size() + page_size - 1) / page_size;
        if (*page > pages) return "That preview page does not exist.";
        std::string result = "Objects within 5 m | page " + std::to_string(*page) + "/" + std::to_string(pages) + " | snapshot lasts 3 min from scan";
        for (auto i = (*page - 1) * page_size; i < std::min(*page * page_size, preview.entries.size()); ++i) {
            const auto &e = preview.entries[i];
            const auto *object = preview_object(e);
            auto row = "\n#" + std::to_string(i + 1) + " ";
            if (!object) { result += row + "[no longer available]"; continue; }
            result += row + object_label(object->item) + " | " + label(owner_label(e), 48) + std::format(" | {:.1f} m", std::sqrt(distance2(*position, object->position)));
        }
        result += "\n/delobj #<object number>: removes that single listed shared object.";
        result += "\n/delobj <player name>: clears ALL their objects. Unique partial names work.";
        if (*page < pages) result += " | Next: /nearobj page " + std::to_string(*page + 1);
        return result;
    }
    const auto at = object_previews_.find(caller);
    if (at == object_previews_.end() || now_ >= at->second.until || at->second.world != world_) return "Preview expired. Run /nearobj again.";
    const auto index = number(selection.substr(1));
    if (!index || !*index || *index > at->second.entries.size()) return "That object number is not in your snapshot.";
    const auto e = at->second.entries[*index - 1];
    const auto *found = preview_object(e);
    if (!found) return "That object is no longer available. Run /nearobj again.";
    const auto object = *found;
    if (object.item != e.item || distance2(*position, object.position) > 25.f) return "The object changed or is now outside 5 m. Run /nearobj again.";
    const auto owned_by = owner_label(e);
    if (e.owner == id_) {
        for (auto &build : builds_loaded_) std::erase_if(build.objects,[&](const auto &o){return o.id==e.id;});
        for (auto &park : parks_spawned_) std::erase_if(park.objects, [&](const auto &o) { return o.id == e.id; });
        publish_parks();
    } else {
        auto *owner = find(e.owner); // preview_object verified this session/epoch above
        auto layout = owner->shared.layout();
        std::erase_if(layout, [&](const auto &o) { return o.id == e.id; });
        owner->cleared.insert(e.id); // later owner uploads must not resurrect this ID
        owner->shared.replace(layout);
        owner->shared_from = owner->objects.revision();
        send_chat("Server staff have removed one of your objects from the shared world.", owner);
        // Stock clients have only an owner-wide local clear instruction. Do not use
        // that here: the command must preserve this player's other objects.
    }
    // Keep the original snapshot and its indices until its three-minute deadline.
    log_("[object-remove] " + guest_name(*actor) + " (" + std::to_string(caller) + ") removed " + object.item +
         " #" + std::to_string(e.id) + " from " + owned_by + " (" + std::to_string(e.owner) + ") on " + map_name());
    return "Removed " + object_label(object.item) + " from " + label(owned_by, 48) + "." +
           (e.owner == id_ ? std::string() : "\nPlayer object removed from shared world; owner's local copy may remain until reload.");
}
} // namespace dingosdk::server
