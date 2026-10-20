#pragma once
#include "Upstream/src/glance_emote_policy.h"
#include "Engine/Game/Input/controller_bindings.h"
#include "Engine/Game/Input/playstation_report.h"
#include <map>
#include <utility>
#include <vector>
namespace dingosdk::first_person_integration {
// Used under capture_mutex. Physical reads remain private; only game-facing
// copies are edited. Toggle latches survive camera handover and producer stalls.
class NativeButtonFilter {
    struct Source {
        jishsfirstpersonmod::ToggleEmoteBlock toggle;
        jishsfirstpersonmod::GlanceEmotePolicy glance;
        jishsfirstpersonmod::GlanceEmotePolicy::Result result;
        std::vector<std::uint8_t> edited;
    };
    std::map<std::pair<unsigned,std::uintptr_t>,Source> sources_;
    first_person_vendor::FirstPersonLookControls controls_{};
    std::uint32_t toggle_{};
    std::uintptr_t world_{};
    std::array<std::uintptr_t,4> owner_{};
    unsigned device_{};
    bool enabled_{},glances_{},sony_{};
    std::uint64_t published_{};
    std::uint64_t generation_{};
public:
    void reset() {const auto generation=generation_+1;*this={};generation_=generation;}
    std::uint64_t generation() const {return generation_;}
    bool enabled() const {return enabled_;}
    void publish(const first_person_vendor::FirstPersonLookControls& controls,std::uint32_t toggle,
                 bool allowed,bool glances,std::uintptr_t world,unsigned device,bool sony,std::uint64_t now,
                 std::array<std::uintptr_t,4> owner={}) {
        if(!allowed || !world || !device) {reset();return;}
        if(keyboard_binding(toggle)) toggle=0;
        if(world!=world_ || device!=device_ || toggle!=toggle_) {sources_.clear();++generation_;}
        bool changed=glances!=glances_ || owner!=owner_;
        for(unsigned i=0;i<4;++i) changed=changed || controls[i].combo!=controls_[i].combo;
        if(changed) for(auto& [key,state]:sources_) {state.glance.reset();state.result={};}
        controls_=controls;toggle_=keyboard_binding(toggle) ? 0:toggle;
        world_=world;owner_=owner;device_=device;sony_=sony;glances_=glances;published_=now;
        enabled_=toggle_!=0 || glances_;
    }
    bool accepts(unsigned path,std::uintptr_t source) const {
        return enabled_ && (path!=0 ? sony_:(source<4 && device_<=15 && (device_&(1u<<source))));
    }
    jishsfirstpersonmod::GlanceEmotePolicy::Result classify(unsigned path,std::uintptr_t source,std::uint32_t buttons,std::uint64_t now,std::uint64_t input_time=0) {
        if(!accepts(path,source)) return {};
        if(sources_.size()>64) sources_.clear();
        auto& state=sources_[{path,source}];
        const auto consumed=state.toggle.update(toggle_,buttons);
        if(consumed) {state.glance.reset();return state.result={consumed,0,false};}
        if(!glances_ || now<published_ || now-published_>100) {state.glance.reset();return state.result={};}
        return state.result=state.glance.update(controls_,buttons,input_time ? input_time:now);
    }
    bool forwarding(std::uint64_t now) const {
        if(!glances_ || now<published_ || now-published_>100) return false;
        for(const auto& [key,state]:sources_) if(state.result.forward) return true;
        return false;
    }
    bool filter_report(unsigned path,std::uintptr_t source,PlayStationPad kind,std::span<std::uint8_t> report,std::uint64_t now) {
        if(!accepts(path,source)) return false;
        const auto parsed=parse_playstation_report(kind,playstation_bluetooth(report.size()),report.data(),report.size());
        if(!parsed) return false;
        auto found=sources_.find({path,source});
        if(found!=sources_.end() && found->second.edited.size()==report.size() &&
            std::equal(report.begin(),report.end(),found->second.edited.begin())) return false;
        auto buttons=std::uint32_t(parsed->buttons);
        if(parsed->left_trigger>30) buttons|=0x10000;
        if(parsed->right_trigger>30) buttons|=0x20000;
        const auto result=classify(path,source,buttons,now);
        if(result.consume) {
            const auto remaining=(buttons&15)&~result.consume;
            constexpr std::uint8_t hats[]{8,0,4,8,6,7,5,8,2,1,3,8,8,8,8,8};
            const auto start=report[0]==0x31 ? 2u:report[0]==0x11 ? 3u:1u;
            const auto button=start+((kind==PlayStationPad::dualsense && (report[0]==0x31 || report.size()==64)) ? 7u:4u);
            report[button]=static_cast<std::uint8_t>((report[button]&0xf0)|hats[remaining]);
            update_playstation_report_crc(report);
        }
        // Cache only edits: identical neutral hardware reports must still release
        // the latch. Sequence bytes distinguish a fresh Sony report from replay.
        auto& state=sources_[{path,source}];state.edited.clear();
        if(result.consume) state.edited.assign(report.begin(),report.end());
        return result.consume!=0;
    }
};
}
