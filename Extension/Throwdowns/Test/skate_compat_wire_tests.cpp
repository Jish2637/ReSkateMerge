#include "Extension/Throwdowns/throwdown_wire.h"
#include "Server/server_activity.h"
#include "Server/player_stats.h"
#include <iostream>
#include <stdexcept>

using namespace dingosdk::multiplayer;
using Kind = ThrowdownMessage::Kind;
using Format = ThrowdownMessage::Format;
namespace {
void check(bool ok, const char *reason) { if (!ok) throw std::runtime_error(reason); }
constexpr std::uint64_t host = 0x0110000100000001ULL, guest = host + 1;
// Golden bodies follow the vanilla codec in the supplied ReSkate-main.zip:
// kind:u8, leader:u64le, id:u32le; vanilla start has ONLY order.
std::vector<std::uint8_t> header(Kind kind) {
    return {static_cast<std::uint8_t>(kind),1,0,0,0,1,0,16,1,1,0,0,0};
}
ThrowdownMessage message(Kind kind) {
    ThrowdownMessage m;m.kind=kind;m.leader=host;m.id=1;m.format=Format::vanilla;return m;
}
void golden(ThrowdownMessage m, std::vector<std::uint8_t> expected) {
    check(encode_throwdown(m)==expected,"Vanilla bytes differ from reference layout");
    check(decode_throwdown(expected)==m,"Vanilla decode differs from reference fields");
    for(std::size_t n=0;n<expected.size();++n)
        check(!decode_throwdown(std::span(expected.data(),n)),"Truncated vanilla packet accepted");
    expected.push_back(0xff);
    expected.push_back(0xff);
    check(!decode_throwdown(expected),"Invalid extension accepted as vanilla");
}
}
int main() try {
    auto join=message(Kind::join);golden(join,header(Kind::join));
    auto offer=message(Kind::offer);offer.series="ThrowdownSkate";offer.placement={9};offer.settings={8};
    auto bytes=header(Kind::offer);bytes.push_back(14);
    bytes.insert(bytes.end(),offer.series.begin(),offer.series.end());
    bytes.insert(bytes.end(),{1,0,9,1,0,8,0});golden(offer,bytes);
    auto start=message(Kind::start);start.order={host,guest};bytes=header(Kind::start);
    bytes.insert(bytes.end(),{2,1,0,0,0,1,0,16,1,2,0,0,0,1,0,16,1});golden(start,bytes);
    auto attempt=message(Kind::attempt);attempt.value=1;attempt.add=true;attempt.trick[0]=0x7f;
    bytes=header(Kind::attempt);bytes.insert(bytes.end(),{1,0,0,0,1,0x7f});bytes.resize(46,0);golden(attempt,bytes);
    for(auto m:{offer,join,start,attempt}) {
        m.format=Format::msc;m.rules_revision=dingosdk::skate::rules_format;m.rules_mask=0x6f;
        if(m.kind==Kind::start)m.settings={8};
        if(m.kind==Kind::attempt)m.detected={1,2,3};
        const auto encoded=encode_throwdown(m);
        check(decode_throwdown(encoded)==m,"MSC layout changed");
        // An MSC message can have a valid vanilla prefix. All incomplete nonempty
        // extension suffixes must fail, including a truncated rules snapshot.
        for(unsigned cut=1;cut<8;++cut) {
            auto truncated=encoded;truncated.resize(truncated.size()-cut);
            const auto decoded=decode_throwdown(truncated);
            check(!decoded || decoded->rules_revision==0,"Partial rules snapshot accepted");
        }
        auto junk=encoded;junk.push_back(0);check(!decode_throwdown(junk),"MSC trailing data accepted");
        auto revision=encoded;revision[revision.size()-8]=255;check(!decode_throwdown(revision),"Unsupported revision accepted");
    }
    check(!decode_throwdown(std::vector<std::uint8_t>(4097,0)),"Oversized packet accepted");
    auto oversized=attempt;oversized.format=Format::msc;oversized.detected.resize(3501);
    check(!valid_throwdown(oversized),"Oversized detector accepted");
    // Exercise the unchanged server activity consumer with both wire layouts.
    for(auto format:{Format::vanilla,Format::msc}) {
        std::vector<std::string> lines;
        dingosdk::server::ActivityLog activity([&](const auto &line){lines.push_back(line);},
            [](auto id){return id==host?"Host":"Guest";});
        dingosdk::server::PlayerStatsStore stats;stats.open({});
        dingosdk::server::StatsSettings settings;
        stats.joined(host,"Host",1,86400);stats.joined(guest,"Guest",1,86400);
        auto send=[&](auto sender,auto m) {
            const auto encoded=encode_throwdown(m);
            activity.throwdown(sender,encoded,nullptr,1'000'000);
            const auto decoded=decode_throwdown(encoded);check(decoded.has_value(),"Stats packet did not decode");
            stats.throwdown(sender,*decoded,1'000'000,86400,settings,{});
        };
        auto advertised=offer;advertised.format=format;
        if(format==Format::msc){advertised.rules_revision=1;advertised.rules_mask=0x6f;}
        send(host,offer);send(host,advertised);send(guest,join);send(guest,join);
        auto running=start;running.format=format;
        if(format==Format::msc){running.settings={8};running.rules_revision=1;running.rules_mask=0x6f;}
        send(host,running);auto close=message(Kind::close);send(host,close);
        check(activity.playing()==std::set<std::uint64_t>{host,guest},"Cleanup close ended server activity");
        auto result=attempt;result.format=format;
        if(format==Format::msc){result.rules_revision=1;result.rules_mask=0x6f;result.detected={1};}
        send(guest,result);
        check(lines.size()==4 && lines.back().find("landed")!=std::string::npos,"Server did not log start/attempt once");
        send(guest,result);
        check(stats.players.at(guest).events[0]==1 && stats.players.at(guest).points==settings.event_points,
              "Server did not award SKATE participation exactly once");
        activity.tick(1'000'000'000);
        check(activity.playing().empty(),"Server activity did not finish");
    }
    std::cout<<"PASS: vanilla golden bytes, dual codec validation and server activity\n";
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
