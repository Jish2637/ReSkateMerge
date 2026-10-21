#pragma once
#include <cstdint>
namespace dingosdk::skate {
inline constexpr std::uint8_t rules_format = 1;
struct NumericalRules {
    int grind_minimum = 115;
    int spin_offset = 90;
    int recovery_limit = 450;
    bool valid() const { return grind_minimum >= 0 && grind_minimum <= 180 && spin_offset >= 0 && spin_offset <= 179 && recovery_limit >= 180 && recovery_limit <= 1080; }
    bool operator==(const NumericalRules&) const = default;
};
struct HostingPreferences {
    std::uint8_t categories = 15;
    NumericalRules numerical;
    bool valid() const { return categories <= 15 && numerical.valid(); }
    std::uint8_t effective_mask(std::uint8_t native) const { return static_cast<std::uint8_t>(native & (0xf0 | categories)); }
    bool operator==(const HostingPreferences&) const = default;
};
}
