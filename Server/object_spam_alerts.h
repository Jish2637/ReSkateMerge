#pragma once
#include "server_text.h"
#include "Engine/Game/World/network_objects.h"
#include "Extension/Multiplayer/Net/protocol.h"
#include <deque>
#include <map>
#include <span>

namespace dingosdk::server {
struct ObjectSpamSettings {
    bool enabled = true;
    unsigned count = 6, window_seconds = 10, cooldown_seconds = 60;
    bool operator==(const ObjectSpamSettings &) const = default;
};
struct ObjectSpamReport {
    unsigned count{};
    std::map<std::string, unsigned> items;
    std::string details() const {
        std::string result;
        for (const auto &[item, n] : items) result += (result.empty() ? "" : ", ") + item + " x" + std::to_string(n);
        return result;
    }
};
// Lives with a Guest, so disconnect/session replacement/map travel discard its state.
// Batches follow the existing 100 ms object-sync cadence and validated layout limit.
class ObjectSpamWindow {
    struct Batch { std::uint64_t at{}; ObjectSpamReport placed; };
    std::deque<Batch> batches_;
    ObjectSpamSettings settings_;
    std::uint64_t last_time_{}, last_alert_{};
    bool alerted_{};
  public:
    std::optional<ObjectSpamReport> observe(std::uint64_t now, std::span<const NetworkObject> added,
                                          const ObjectSpamSettings &settings, bool initial_layout = false) {
        if (settings != settings_ || now < last_time_ || initial_layout || !settings.enabled) {
            batches_.clear(); alerted_ = false; last_alert_ = 0; settings_ = settings;
        }
        last_time_ = now;
        if (initial_layout || !settings.enabled) return {};
        const auto window = std::uint64_t{settings.window_seconds} * 1000000;
        while (!batches_.empty() && now - batches_.front().at >= window) batches_.pop_front();
        if (added.empty()) return {};
        Batch batch; batch.at = now; batch.placed.count = static_cast<unsigned>(added.size());
        for (const auto &o : added) ++batch.placed.items[o.item];
        batches_.push_back(std::move(batch));
        if (alerted_ && now - last_alert_ < std::uint64_t{settings.cooldown_seconds} * 1000000) return {};
        ObjectSpamReport report;
        for (const auto &b : batches_) {
            report.count += b.placed.count;
            for (const auto &[item, n] : b.placed.items) report.items[item] += n;
        }
        if (report.count < settings.count) return {};
        alerted_ = true; last_alert_ = now;
        return report;
    }
};
inline std::string compact_object_name(std::string item) {
    if (const auto generic = item.find("_generic_"); generic != std::string::npos) item.erase(0, generic + 9);
    else if (item.starts_with("own_")) item.erase(0, 4);
    if (const auto suffix = item.rfind('_'); suffix != std::string::npos && suffix + 1 < item.size() &&
        std::all_of(item.begin() + suffix + 1, item.end(), [](char c) { return c >= '0' && c <= '9'; })) item.resize(suffix);
    item = multiplayer::clean_chat_text(item);
    cut_text(item, 32);
    return item.empty() ? "object" : item;
}
inline std::vector<std::string> object_spam_lines(std::string name, const ObjectSpamReport &report,
                                                unsigned seconds, std::size_t limit = 200) {
    name = multiplayer::clean_chat_text(name); cut_text(name, 48);
    std::vector<std::string> lines;
    std::string line = "Possible object spam: " + name + " \xE2\x80\x94 " + std::to_string(report.count) +
                       " objects in " + std::to_string(seconds) + "s:";
    std::map<std::string, unsigned> compact;
    for (const auto &[item, n] : report.items) compact[compact_object_name(item)] += n;
    std::vector<std::pair<std::string, unsigned>> ordered(compact.begin(), compact.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const auto &entry = ordered[i];
        const auto text = entry.first + " x" + std::to_string(entry.second);
        // Leave room for the remaining-type summary in the last line.
        const auto reserve = i + 1 < ordered.size() ? 48U : 0U;
        if (line.size() + 1 + text.size() + reserve > limit) {
            if (lines.empty()) { lines.push_back(line); line = "Object types:"; }
            else {
                line += " + " + std::to_string(ordered.size() - i) + " more types (full details in server log).";
                cut_text(line, limit); break;
            }
        }
        line += " " + text + (i + 1 < ordered.size() ? "," : ".");
    }
    cut_text(line, limit); lines.push_back(std::move(line));
    return lines;
}
} // namespace dingosdk::server
