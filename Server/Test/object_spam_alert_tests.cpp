#include "Server/object_spam_alerts.h"
#include <cstdio>

using namespace dingosdk;
using namespace dingosdk::server;
int main() {
    unsigned checks{}, failures{};
    const auto check = [&](bool ok, const char *message) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } };
    const std::string ramp = "own_bkramps_generic_kickercurvedlarge_00001";
    const std::vector<NetworkObject> one{{1, ramp}};
    const std::vector<NetworkObject> six(6, NetworkObject{1, ramp});
    const std::vector<NetworkObject> five(5, NetworkObject{1, ramp});
    ObjectSpamSettings settings;
    ObjectSpamWindow replay;
    const unsigned sample[]{0, 3, 6, 7, 9, 10, 12, 14, 15, 16, 18, 19, 21, 22, 23};
    unsigned alerts{}, first{};
    for (const auto t : sample) {
        if (const auto r = replay.observe(std::uint64_t{t} * 1000000, one, settings, t == 0)) {
            if (!alerts) first = t;
            ++alerts;
            check(r->count == 6 && r->items.at(ramp) == 6, "Replay warning count/type wrong");
        }
    }
    check(alerts == 1 && first == 12, "User's exact Duplicate timings did not trigger once at 12:53:44");
    check(replay.observe(72000000, six, settings).has_value(), "Alert did not resume at exact 60s cooldown boundary");
    ObjectSpamWindow slow;
    for (unsigned t = 0; t < 180; t += 3) check(!slow.observe(std::uint64_t{t} * 1000000, one, settings), "Slow placement caused warning");
    ObjectSpamWindow threshold;
    check(!threshold.observe(0, five, settings), "Fewer than six caused warning");
    check(threshold.observe(9999999, one, settings).has_value(), "Six within ten seconds did not warn");
    ObjectSpamWindow expired;
    check(!expired.observe(0, five, settings) && !expired.observe(10000000, one, settings), "Exact ten-second-old events did not expire");
    ObjectSpamWindow imports;
    check(!imports.observe(0, six, settings, true), "Initial layout caused warning");
    check(!imports.observe(1000000, five, settings), "Initial layout counted toward later activity");
    check(imports.observe(2000000, one, settings).has_value(), "New placements after baseline did not count");
    ObjectSpamWindow empty;
    check(!empty.observe(0, five, settings) && !empty.observe(1000000, {}, settings), "No new IDs caused warning");
    check(empty.observe(2000000, one, settings).has_value(), "Empty update lost valid prior placements");
    ObjectSpamWindow mixed;
    std::vector<NetworkObject> objects = five;
    objects.push_back({2, "own_bkrails_generic_flatrailround_00001"});
    const auto r = mixed.observe(0, objects, settings);
    check(r && r->count == 6 && r->items.size() == 2 && r->items.at(ramp) == 5, "Mixed object types not grouped correctly");
    check(r && r->details().find(ramp + " x5") != std::string::npos, "Full log breakdown lost raw asset names");
    check(compact_object_name(ramp) == "kickercurvedlarge", "Compact object label wrong");
    if (r) {
        const auto lines = object_spam_lines("Jish", *r, 10);
        check(lines.size() <= 2 && lines.front().starts_with("Possible object spam: Jish"), "Warning format wrong");
        check(lines.front().find("6 objects in 10s") != std::string::npos, "Warning omitted count/window");
    }
    ObjectSpamReport many;
    for (unsigned i = 0; i < 100; ++i) { many.items["own_bkramps_generic_verylongobjectname_" + std::to_string(i) + "_00001"] = i + 1; many.count += i + 1; }
    const auto lines = object_spam_lines(std::string(40, 'x') + "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC", many, 10);
    check(lines.size() == 2 && lines[0].size() <= 200 && lines[1].size() <= 200, "Large warning exceeded two bounded chat lines");
    check(lines.back().find("more types") != std::string::npos, "Large warning omitted remaining-type summary");
    ObjectSpamWindow disabled;
    settings.enabled = false;
    check(!disabled.observe(0, six, settings), "Disabled feature caused warning");
    settings.enabled = true;
    check(!disabled.observe(1000000, one, settings), "Disabled observations survived re-enable");
    ObjectSpamWindow changed;
    check(!changed.observe(0, five, settings), "Threshold setup failed");
    settings.count = 7;
    check(!changed.observe(1000000, one, settings), "Settings change did not reset window");
    settings.count = 6;
    ObjectSpamWindow rollback;
    check(!rollback.observe(5000000, five, settings) && !rollback.observe(1000000, one, settings), "Clock rollback retained future observations");
    rollback = {};
    check(!rollback.observe(2000000, five, settings), "Session reset retained observations");
    std::printf("Object spam alerts: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
