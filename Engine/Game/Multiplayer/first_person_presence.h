#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace dingosdk::multiplayer {
// Mode 1 owns its active camera independently of the normal view's enabled
// flag. Optional addon initialization is not a prerequisite for core FP.
inline bool first_person_camera_owned(bool first_person,bool lease,bool ambiguous,
    bool controllable,bool phase,bool editor,bool noclip,bool drone,
    bool identity_matches,unsigned mode,bool active_camera) noexcept {
    return first_person && lease && !ambiguous && controllable && phase &&
        !editor && !noclip && !drone && identity_matches && mode==1 && active_camera;
}
inline bool first_person_discovery_needed(bool eligible,bool listed_capable,bool opted) noexcept {
    return eligible && !listed_capable && !opted;
}
inline std::string first_person_report(bool enabled,std::uint32_t sequence) {
    // The server runs chat duplicate suppression before parsing presence.
    // Identical five-second heartbeats would be rejected for fifteen seconds.
    return std::string(enabled ? "/jish-fp 1 ":"/jish-fp 0 ")+std::to_string(sequence);
}
struct FirstPersonReports {
    std::uint64_t host{},epoch{},due{};bool enabled{};
    std::optional<bool> poll(std::uint64_t now,std::uint64_t server,std::uint64_t session,bool supported,bool active) {
        if(!supported || !server || !session){*this={};return {};}
        if(host!=server || epoch!=session){host=server;epoch=session;due=0;enabled=false;}
        if(active!=enabled || (active && now>=due)){enabled=active;due=now+5000000;return active;}
        return {};
    }
};
}
