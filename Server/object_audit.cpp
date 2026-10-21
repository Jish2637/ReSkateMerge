#include "object_audit.h"
#include "server_text.h"
#include "Engine/Core/Json/json.h"
#include <fstream>
#include <sstream>

namespace dingosdk::server {
namespace {
constexpr std::uintmax_t max_journal_bytes = 128 * 1024 * 1024;
Json encode(const ObjectPlacementRecord &r) {
    return Json{{"time", r.time}, {"owner", std::to_string(r.owner)}, {"object", std::to_string(r.object)},
                {"name", r.name}, {"item", r.item}, {"map", r.map}, {"imported", r.imported}};
}
bool recent(std::int64_t time, std::int64_t now) { return time > now - ObjectAudit::retention_seconds && time <= now + 60; }
}
void ObjectAudit::failure(std::int64_t now) {
    if (!error_at_ || now < error_at_ || now - error_at_ >= 60) {
        error_at_ = now;
        if (log_) log_("[object-audit] History journal could not be read/written; in-memory history remains available.");
    }
}
void ObjectAudit::open(const std::filesystem::path &file, std::int64_t now, Log log) {
    file_ = file; log_ = std::move(log); records_.clear();
    if (file_.empty()) return;
    try {
        if (std::filesystem::exists(file_)) {
            if (std::filesystem::file_size(file_) > max_journal_bytes) throw std::runtime_error("journal too large");
            std::ifstream in(file_, std::ios::binary);
            if (!in) throw std::runtime_error("journal unreadable");
            std::string line;
            bool malformed{};
            while (std::getline(in, line)) {
                try {
                    if (line.size() > 4096) throw std::runtime_error("long record");
                    const auto row = Json::parse(line);
                    ObjectPlacementRecord r;
                    r.time = row.at("time").get<std::int64_t>();
                    r.owner = number(row.at("owner").string()).value_or(0);
                    r.object = number(row.at("object").string()).value_or(0);
                    r.name = row.at("name").string(); r.item = row.at("item").string(); r.map = row.at("map").string();
                    r.imported = row.value("imported", false);
                    if (!r.owner || !r.object || r.name.size() > 128 || r.item.size() > 256 || r.map.size() > 256)
                        throw std::runtime_error("invalid record");
                    if (recent(r.time, now)) records_.push_back(std::move(r));
                    if (records_.size() > max_records) records_.pop_front();
                } catch (...) { malformed = true; }
            }
            if (in.bad()) throw std::runtime_error("journal read failed");
            if (malformed && log_) log_("[object-audit] Skipped malformed history records (possibly an interrupted final write).");
        }
        compact(now);
    } catch (...) { failure(now); }
}
void ObjectAudit::compact(std::int64_t now) {
    std::erase_if(records_, [&](const auto &r) { return !recent(r.time, now); });
    if (file_.empty()) { compacted_ = now; return; }
    std::filesystem::create_directories(file_.parent_path());
    auto temp = file_; temp += ".tmp";
    std::uintmax_t written{};
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto &r : records_) {
            const auto line = encode(r).dump();
            out << line << '\n'; written += line.size() + 1;
        }
        out.flush();
        if (!out) throw std::runtime_error("journal write failed");
    }
    std::filesystem::rename(temp, file_);
    bytes_ = written; compacted_ = now;
}
void ObjectAudit::maintain(std::int64_t now) {
    if (now >= compacted_ && now - compacted_ < 300) return;
    try { compact(now); } catch (...) { compacted_ = now; failure(now); }
}
void ObjectAudit::record(std::vector<ObjectPlacementRecord> rows, std::int64_t now) {
    if (rows.empty()) return;
    std::string text;
    for (auto &r : rows) {
        cut_text(r.name, 128); cut_text(r.item, 256); cut_text(r.map, 256);
        text += encode(r).dump() + '\n'; records_.push_back(std::move(r));
        if (records_.size() > max_records) records_.pop_front();
    }
    if (file_.empty()) return;
    try {
        if (bytes_ + text.size() > max_journal_bytes) { compact(now); return; }
        std::filesystem::create_directories(file_.parent_path());
        std::ofstream out(file_, std::ios::binary | std::ios::app);
        out << text; out.flush();
        if (!out) throw std::runtime_error("journal append failed");
        bytes_ += text.size();
    } catch (...) { failure(now); }
}
} // namespace dingosdk::server
