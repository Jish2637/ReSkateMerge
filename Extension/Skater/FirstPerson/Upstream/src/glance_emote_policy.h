#pragma once
#include "Extension/Skater/FirstPerson/Upstream/vendor/Engine/Game/Input/controller_bindings.h"
#include <bit>
namespace jishsfirstpersonmod {
// Adapted from JishsFirstPersonMod fe80fa7 (same policy, native type namespace).
class ToggleEmoteBlock {
    std::uint32_t held_{};
public:
    std::uint32_t update(std::uint32_t combo,std::uint32_t buttons,std::uint32_t native=0) {
        held_&=buttons&15;
        for(const auto binding:{combo,native})
            if((binding&0x100) && (binding&15) && first_person_vendor::valid_controller_combo(binding) && (buttons&binding)==binding)
                held_|=binding&15;
        return held_;
    }
};
class GlanceEmotePolicy {
    std::uint32_t previous_{},consumed_{},forwarded_{};
    int candidate_=-1;
    std::uint64_t first_{};
public:
    struct Result {std::uint32_t consume{},forward{};bool emote{};};
    void reset() {*this={};}
    Result update(const first_person_vendor::FirstPersonLookControls& controls,std::uint32_t buttons,std::uint64_t now) {
        consumed_&=buttons;forwarded_&=buttons;
        if(!(buttons&0x100)) candidate_=-1;
        const auto rising=(buttons&15)&~previous_;
        unsigned count{};int direction=-1;std::uint32_t matched{};
        for(unsigned i=0;i<4;++i) {
            const auto combo=controls[i].combo,dpad=combo&15;
            if((combo&~15u)!=0x100 || std::popcount(dpad)!=1 || !first_person_vendor::valid_controller_combo(combo)) continue;
            if((buttons&combo)==combo) {matched|=dpad;if(rising&dpad) {++count;direction=static_cast<int>(dpad);}}
        }
        bool emote{};
        if(rising && (std::popcount(buttons&15)!=1 || count!=1)) candidate_=-1;
        else if(count==1) {
            if(candidate_==direction && now>=first_ && now-first_<=250) {
                forwarded_|=static_cast<unsigned>(direction);consumed_&=~forwarded_;candidate_=-1;emote=true;
            } else {candidate_=direction;first_=now;}
        }
        if(candidate_>=0 && now-first_>250) candidate_=-1;
        consumed_|=matched&~forwarded_;previous_=buttons&15;
        return {consumed_,forwarded_,emote};
    }
};
}
