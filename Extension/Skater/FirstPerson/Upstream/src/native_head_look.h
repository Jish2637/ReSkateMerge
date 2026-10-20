#pragma once
namespace jishsfirstpersonmod {
// Upstream fe80fa7: only after existing owner/focus/menu/device/camera checks.
inline bool native_head_look_passthrough(bool enabled,bool active_aim,bool stick_enabled,bool compatibility,
                                         bool turn_selected,bool turning,bool recovery) {
    return enabled && active_aim && stick_enabled && (compatibility || !turn_selected) && !turning && !recovery;
}
}
