#pragma once
#include <optional>
namespace dingosdk::profile_runtime {
// Native owned-item collections cannot safely remove entries in a live session.
// Allow live enabling, but defer removal of owned items until the next process.
struct OwnershipOverrideState {
    std::optional<bool> applied;
    bool effective(bool saved) {
        if (!applied) applied = saved;
        return *applied;
    }
    bool enable_live() {
        const bool changed = !applied.value_or(false);
        applied = true;
        return changed;
    }
    bool restart_required(bool saved) const { return applied && *applied != saved; }
};
}
