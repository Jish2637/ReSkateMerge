#pragma once
#include <cstdint>
#include <array>
#include <string>
namespace dingosdk::trick_terminology {
// Observation-only test15 token, scoped to one native expression invocation.
void* begin_label_binding_probe(std::uintptr_t vm,std::uint32_t pc,bool profiled) noexcept;
void end_label_binding_probe(void* token) noexcept;
void observe_scoring_expression(std::uintptr_t vm,std::uint32_t pc) noexcept;
void observe_position(std::uintptr_t entity,const std::array<float,3>& position,bool valid) noexcept;
void observe_footing(bool goofy) noexcept;
// Existing native profile publication hook calls this before forwarding the write.
void observe_native_publication(std::uintptr_t manager,std::uint64_t handle,std::uintptr_t type) noexcept;
// Existing shared UI-model writer owns the detour; Terminology forwards once
// through the supplied native relay, preserving its full return and flag ABI.
using NativeModelWriter=std::uint64_t(*)(std::uintptr_t,std::uint64_t,std::uintptr_t,const void*,std::uint8_t);
std::uint64_t publish_native_model(std::uintptr_t manager,std::uint64_t target,std::uintptr_t type,
    const void* source,std::uint8_t flag,std::uintptr_t caller,NativeModelWriter original);
// Native client thread only. The returned status is copied into Trainer's snapshot.
std::string tick(std::uintptr_t base, bool playing, bool enabled) noexcept;
// Client thread, while holding the native model lock. Detection consumes the
// producer's original labels and excludes rows created solely for presentation.
std::string native_label(std::uintptr_t manager, std::uint64_t row, std::uintptr_t type,
    std::uint32_t field, const std::string& physical);
bool presentation_row(std::uintptr_t manager, std::uint64_t row) noexcept;
}
