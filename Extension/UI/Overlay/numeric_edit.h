#pragma once
#include <cstdint>
#include <unordered_map>
namespace dingosdk::overlay::detail {
// Only retain boxes actually being edited, until their deactivation frame. Unrelated controls
// must not overwrite that value before the numeric row commits it.
template<class Value> class NumericEdits {
    std::unordered_map<std::uint32_t, Value> values_;
public:
    Value value(std::uint32_t id, Value current, bool continuing) {
        if (!continuing) { values_.erase(id); return current; }
        const auto it = values_.find(id); return it == values_.end() ? current : it->second;
    }
    void retain(std::uint32_t id, Value value) { values_[id] = value; }
    void release(std::uint32_t id) { values_.erase(id); }
};
}
