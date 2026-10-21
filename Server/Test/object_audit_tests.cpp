#include "Server/object_audit.h"
#include <cstdio>
#include <fstream>
#include <chrono>

using namespace dingosdk;
using namespace dingosdk::server;
int main() {
    unsigned failures{}, checks{};
    const auto check = [&](bool ok, const char *why) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "%s\n", why); } };
    const auto folder = std::filesystem::temp_directory_path() / ("msc-object-audit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = folder / "object-history.jsonl";
    std::vector<std::string> logs;
    const auto log = [&](const std::string &s) { logs.push_back(s); };
    constexpr std::int64_t now = 1700000000;
    ObjectAudit audit;
    audit.open(path, now, log);
    audit.record({{now - 10, 76561198000000001ULL, 1, "Skater", "own_bk_rail_01", "San Vansterdam", true},
                  {now, 76561198000000001ULL, 2, "Skater", "own_bk_ramp_01", "San Vansterdam", false}}, now);
    {
        std::ofstream out(path, std::ios::app); out << "{interrupted\n";
    }
    ObjectAudit restarted;
    restarted.open(path, now, log);
    check(restarted.records().size() == 2, "Restart lost valid history or failed to skip malformed last record");
    check(restarted.records().front().owner == 76561198000000001ULL && restarted.records().front().imported, "Persisted ownership/import marker lost");
    check(!logs.empty(), "Corrupt-record warning missing");
    restarted.maintain(now + 86401);
    check(restarted.records().empty(), "Expired records remain in memory");
    ObjectAudit expired;
    expired.open(path, now + 86401, log);
    check(expired.records().empty(), "Expired records survived compaction/restart");

    // A path that cannot be a directory: history remains usable and emits a bounded diagnostic.
    const auto bad = folder / "not-a-directory";
    { std::ofstream out(bad); out << "x"; }
    ObjectAudit unavailable;
    unavailable.open(bad / "history", now, log);
    unavailable.record({{now, 1, 1, "A", "own_bk_rail", "map", false}}, now);
    check(unavailable.records().size() == 1, "Storage failure lost in-memory record");
    std::filesystem::remove_all(folder);
    std::printf("Object audit: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
