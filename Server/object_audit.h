#pragma once
#include "Engine/Game/World/network_objects.h"
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace dingosdk::server {
struct ObjectPlacementRecord {
    std::int64_t time{};
    std::uint64_t owner{}, object{};
    std::string name, item, map;
    bool imported{};
};
// One bounded journal beside the server configuration. Never reads arbitrary server logs.
class ObjectAudit {
  public:
    using Log = std::function<void(const std::string &)>;
    static constexpr std::size_t max_records = 100000;
    static constexpr std::int64_t retention_seconds = 86400;
    void open(const std::filesystem::path &file, std::int64_t now, Log log);
    void record(std::vector<ObjectPlacementRecord> records, std::int64_t now);
    void maintain(std::int64_t now);
    const std::deque<ObjectPlacementRecord> &records() const { return records_; }
  private:
    std::deque<ObjectPlacementRecord> records_;
    std::filesystem::path file_;
    Log log_;
    std::int64_t compacted_{}, error_at_{};
    std::uintmax_t bytes_{};
    void compact(std::int64_t now);
    void failure(std::int64_t now);
};
} // namespace dingosdk::server
