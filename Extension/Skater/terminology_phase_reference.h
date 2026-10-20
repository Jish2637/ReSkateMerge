#pragma once
// Test27 used a private copy of Jish's tracker in the same namespace. Share the
// detector's definition so both integrations obey the C++ one-definition rule.
#include "trick_set.h"

namespace dingosdk::trick_logger {
inline int measured_spin(const SpinSegment& segment) {
    const double net=segment.air+segment.landing;
    if(!std::isfinite(net) || !segment.rotation_complete ||
       (segment.grind_adjacent && std::abs(net)+1e-6<grind_spin_minimum)) return 0;
    return static_cast<int>(std::floor((std::abs(net)+90)/180))*180;
}
}
