#pragma once
#include "trick_terminology_rule.h"
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace dingosdk::trick_terminology {
enum class Stance { normal, nollie, fakie, switch_, unknown };
inline Stance stance_of(std::string_view stance, std::string_view nollie) {
    const auto s = normalized(stance), n = normalized(nollie);
    if (!n.empty() && n != "nollie") return Stance::unknown;
    if (!n.empty() && !s.empty() && s != "nollie") return Stance::unknown;
    if (s == "nollie" || !n.empty()) return Stance::nollie;
    if (s.empty() || s == "normal" || s == "regular") return Stance::normal;
    if (s == "fakie") return Stance::fakie;
    if (s == "switch") return Stance::switch_;
    return Stance::unknown;
}
inline std::string spin_direction(double net, bool goofy, Stance stance) {
    if (!std::isfinite(net) || std::abs(net) < 1 || stance == Stance::unknown) return {};
    // Same atan2(x,z) convention as the supplied detector. Live axis/sign
    // calibration remains required; switch and fakie reverse the direction convention.
    bool fs = net > 0;
    if (goofy) fs = !fs;
    if (stance == Stance::switch_ || stance == Stance::fakie) fs = !fs;
    return fs ? "FS" : "BS";
}

struct Alias { std::string name; bool consumes_spin{}, consumes_held{}; };
inline Alias canonical_flip_alias(const std::array<std::string,13>& labels, std::string_view direction) {
    for (std::size_t i : {4u,5u,7u,8u,9u,10u,11u,12u}) if (!labels[i].empty()) return {};
    const auto stance = stance_of(labels[3], labels[6]);
    if (stance == Stance::unknown) return {};
    const std::string prefix = stance == Stance::nollie ? "Nollie " :
        stance == Stance::fakie ? "Fakie " : stance == Stance::switch_ ? "Switch " : "";
    const auto base = normalized(labels[0]), rotation = normalized(labels[2]);
    const bool half = rotation == "180", full = rotation == "360";
    const bool bs = direction == "BS", fs = direction == "FS";
    if (half && base == "hardflip") {
        const bool front = stance == Stance::nollie || stance == Stance::fakie;
        if (front ? fs : bs) return {prefix+"Ghetto Bird",true};
    }
    if (half) {
        // Fakie reverses the body convention relative to the board-spin family.
        const bool family_bs=stance==Stance::fakie ? fs : bs;
        const bool family_fs=stance==Stance::fakie ? bs : fs;
        const auto board = normalized(labels[1]);
        if ((base == "360popshuvit" || base == "fs360popshuvit") &&
            ((family_bs && (board == "bs" || (board.empty() && base == "360popshuvit"))) ||
             (family_fs && (board == "fs" || base == "fs360popshuvit"))))
            return {prefix+(family_fs ? "FS Bigspin" : "Bigspin"),true};
        if (family_bs && base == "360flip") return {prefix+"Bigflip",true};
        if (family_fs && base == "360hardflip") return {prefix+"FS Bigflip",true};
        if (family_bs && base == "360inwardheelflip") return {prefix+"BS Bigheel",true};
        if (family_fs && base == "laserflip") return {prefix+"Bigheel",true};
    }
    if (stance == Stance::fakie && (half || full) && (bs || fs)) {
        std::string suffix;
        if (base == "kickflip") suffix = " Flip";
        else if (base == "heelflip") suffix = " Heelflip";
        if (base == "ollie" || base == "kickflip" || base == "heelflip")
            return {std::string(fs ? "FS " : "")+(half ? "Half-Cab" : "Cab")+suffix,true};
    }
    return {};
}

// Live test4: the ticker splits a native 360 trick into base and HeldRotation.
// This field is presentation data, NOT proof of a held-input gesture.
// Preserve the source labels; consume only an exact, recognized component.
inline Alias flip_alias(const std::array<std::string,13>& labels, std::string_view direction) {
    auto native=labels;
    bool component{};
    if (normalized(labels[4])=="360") {
        const auto base=normalized(labels[0]);
        if (base=="flip") native[0]="360 Flip";
        else if (base=="hardflip") native[0]="360 Hard Flip";
        else if (base=="inwardheelflip") native[0]="360 Inward Heelflip";
        else if (base=="popshuvit") native[0]="360 Pop Shuv-it";
        else return {};
        native[4].clear();component=true;
    }
    auto result=canonical_flip_alias(native,direction);
    if (!result.name.empty()) {
        result.consumes_held=component;
        return result;
    }
    // Base rename: never bake stance into this name or reject unrelated modifiers.
    // Native 360 Flip consists of Flip + HeldRotation=360. Only that component is consumed.
    if (normalized(native[0])=="360flip") return {"Tre Flip",false,component};
    return {};
}
// Kept pure so preservation and restoration behavior can be regression-tested.
inline std::string display_field(const Alias& alias,std::size_t slot,const std::string& original) {
    if (alias.name.empty()) return original;
    if (slot==0) return alias.name;
    if (slot==4 && alias.consumes_held) return {};
    if (alias.consumes_spin && (slot==1 || slot==2 || slot==3 || slot==6 ||
        (slot==13 && (normalized(original)=="bs" || normalized(original)=="fs")))) return {};
    return original;
}
inline std::string measured_body_direction(const std::array<std::string,13>& labels,double net,bool goofy) {
    // Ollie's native direction is body direction; on Shuvits it is board direction.
    const auto base=normalized(labels[0]), direction=normalized(labels[1]);
    if ((base=="ollie" || base=="bonedollie") && (direction=="bs" || direction=="fs"))
        return direction=="bs"?"BS":"FS";
    return spin_direction(net,goofy,stance_of(labels[3],labels[6]));
}
inline std::string hardflip_alias(const std::array<std::string,13>& labels, std::string_view direction) {
    return normalized(labels[0]) == "hardflip" ? flip_alias(labels,direction).name : "";
}
struct SpinAttempt {
    std::uint64_t id{}, entity{}, start{}, end{}, claimed_row{};
    double net{}, air_net{};
    bool valid{true}, landed{}, emitted{}, airborne{};
    double last_delta{};
    std::optional<bool> goofy;
};
class SpinTracker {
    std::uint64_t entity_{}, time_{}, serial_{};
    double yaw_{};
    unsigned state_{};
    bool previous_{}, air_{};
    std::shared_ptr<SpinAttempt> active_;
public:
    std::deque<std::shared_ptr<SpinAttempt>> attempts;
    void reset() {
        if (active_) active_->valid = false;
        active_.reset(); attempts.clear(); previous_ = air_ = false; entity_ = time_ = 0;
    }
    void sample(std::uint64_t now, std::uint64_t entity, unsigned state, double yaw,
                bool valid, std::optional<bool> goofy) {
        if (!valid || !std::isfinite(yaw) || !entity || (entity_ && entity_ != entity)) {
            reset(); if (!valid || !entity || !std::isfinite(yaw)) return;
        }
        const bool airborne = state==200 || state==201 || state==203 || state==701;
        const bool ground = state==100 || state==101 || state==103;
        if (previous_ && now==time_) return;
        double delta = previous_ ? std::remainder(yaw - yaw_, 360.0) : 0;
        const bool continuous = previous_ && now >= time_ && now-time_ <= 150 && std::abs(delta) < 45;
        if (airborne && !air_) {
            active_ = std::make_shared<SpinAttempt>();
            active_->id = ++serial_; active_->entity = entity; active_->start = now;
            active_->valid = continuous; active_->goofy = goofy;
            attempts.push_back(active_); while (attempts.size() > 8) attempts.pop_front();
        }
        if (active_) {
            active_->airborne=airborne;active_->last_delta=continuous?delta:0;
            if (!continuous || goofy != active_->goofy) active_->valid = false;
            if (continuous) { active_->net += delta; if (airborne || air_) active_->air_net += delta; }
            if (!airborne && state != 101) {
                active_->end = now; active_->landed = ground;
                if (!ground && !(state>=400 && state<500)) active_->valid = false;
                active_.reset();
            }
        }
        state_=state;entity_ = entity; time_ = now; yaw_ = yaw; previous_ = true; air_ = airborne;
    }
    std::shared_ptr<SpinAttempt> associate(std::uint64_t first_seen) const {
        std::shared_ptr<SpinAttempt> selected;
        for (const auto& a : attempts) {
            // New rows only: a bounded temporal association is a candidate,
            // not a native scoring-owner/sequence identity claim.
            if (first_seen + 100 < a->start || first_seen > (a->end ? a->end + 750 : a->start + 10000)) continue;
            if (selected) return {}; // overlapping attempts: no guess
            selected = a;
        }
        return selected;
    }
};
}

