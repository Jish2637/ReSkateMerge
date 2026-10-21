#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dingosdk::game::build::v20260929::steezy_landings {
// Supported SHA-256 fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9 (Steam build 25414733).
// Research and live evidence: SketchyLandingMod HANDOVER.md (sections 5.1-5.3, v0.6-v0.15) and
// ManualCatch/Research/SKETCHY_*_2026-10-17.md.
//
// These are the byte prefixes the standalone SteezyLandings DLL verified before it hooked or
// relied on each site (v0.9-v0.15, live-tested). They are shorter than the 32-byte
// game::build::Fingerprint the rest of the runtime uses because only these bytes were recorded;
// extend them to full fingerprints when the image is next dumped, without changing the sites.
struct PrefixContract {
    std::uintptr_t rva;
    std::array<unsigned char, 32> bytes;
    std::size_t length;
    const char* what;
};

// void(bridge = core+0x4A0, float dt): the skater's animation/physics bridge, once per physics step
// for every skater core. Reached only through core vtable slot 0x6176EB8. The landing classifier
// (0x47C3960) and the output bridge below both run inside it.
inline constexpr PrefixContract bridge_update{0xFF6780, {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x81,0xEC,0xC0,0x00,0x00,0x00}, 18,
                                              "bridge update prologue"};
inline constexpr PrefixContract bridge_this{0xFF6795, {0x48,0x8D,0xB1,0x60,0xFB,0xFF,0xFF}, 7, "bridge this = core+0x4A0"};
// void(core, float dt): publishes the classifier's output (P+0x64 LandQuality and others) to the
// animation model. Its one caller is the direct call at 0xFF6A08 in the bridge update.
inline constexpr std::uintptr_t output_bridge = 0xFF1E00;
inline constexpr PrefixContract output_call{0xFF6A08, {0xE8,0xF3,0xB3,0xFF,0xFF,0x48,0x8B,0xCE}, 8, "bridge calls the output bridge (call 0xFF1E00; mov rcx,rsi)"};
inline constexpr PrefixContract classifier_store{0x47C3D48, {0x89,0x51,0x64}, 3, "classifier stores LandQuality at P+0x64"};
inline constexpr PrefixContract state_commit{0x47D402D, {0x89,0x81,0x18,0x14,0x00,0x00}, 6, "state commit writes ctx+0x1418"};
inline constexpr PrefixContract output_model{0xFF1E1B, {0x4C,0x8B,0xB1,0x10,0x06,0x00,0x00}, 7, "output bridge model = core+0x610"};
inline constexpr PrefixContract output_context{0xFF1E6D, {0x49,0x83,0xC6,0x48}, 4, "output bridge ant context = model+0x48"};
inline constexpr PrefixContract output_quality_ref{0xFF36F2, {0x48,0x8B,0x91,0x50,0x02,0x00,0x00}, 7, "output bridge LandQuality AntRef at config+0x250"};
inline constexpr PrefixContract enum_writer{0xFF3715, {0xBA,0x98,0xBE,0xED,0xA5,0xFF,0x10}, 7, "enum writer interface 0xA5EDBE98 via vtbl+0"};
inline constexpr PrefixContract bool_writer{0xFF3603, {0xBA,0x69,0xC4,0xC2,0xBC,0xFF,0x10}, 7, "bool writer interface 0xBCC2C469 via vtbl+0"};
// uint8(self = descriptor+0x60, context): the shared simple-Boolean getter (local/parent/default
// lookup). The landing table's own read of Bool.Anim.SketchyLanding returns to 0x11D189E.
inline constexpr PrefixContract bool_getter{0x11DD4C0, {0x40,0x53,0x48,0x83,0xEC,0x20}, 6, "simple Boolean getter prologue"};
inline constexpr std::uintptr_t landing_table_read_return = 0x11D189E;
// uintptr(self, output, evaluation): the active chooser method on the live SketchyLanding read stack
// (return 0x11D2B73). [self+0x10] is the tagged CDB, [evaluation+0x20] the model context.
inline constexpr PrefixContract active_selector{0x11D2B30, {0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x41}, 8, "active selector prologue"};
inline constexpr std::array<const PrefixContract*, 12> all{&bridge_update, &bridge_this, &output_call, &classifier_store, &state_commit, &output_model,
                                                           &output_context, &output_quality_ref, &enum_writer, &bool_writer, &bool_getter, &active_selector};

// Skater core (this of the bridge minus 0x4A0).
inline constexpr std::uintptr_t core_vtable = 0x6176D30;
inline constexpr std::uintptr_t core_trick_state = 0x3B8;  // [ +0x60] = classifier output P
inline constexpr std::uintptr_t core_context = 0x3C0;      // physics context: +0x1418 category (200 = air)
inline constexpr std::uintptr_t core_board_holder = 0x430; // [ +0x18] board physics
inline constexpr std::uintptr_t core_rig = 0x438;          // rig: +0x4630 back to core, +0x4720 world matrix
inline constexpr std::uintptr_t core_model = 0x610;        // animation model; +0x48 = its ant context
inline constexpr std::uintptr_t core_output_config = 0x630;
inline constexpr std::uintptr_t output_config_inner = 0x38, config_land_quality = 0x250;
inline constexpr std::uintptr_t ctx_category = 0x1418;
inline constexpr std::uint32_t category_air = 200;
// Classifier output P = [[core+0x3B8]+0x60].
inline constexpr std::uintptr_t trick_state_quality = 0x60;
inline constexpr std::uintptr_t p_sketchiness = 0x54, p_quality = 0x64, p_classified = 0xB0;
// Board physics, from ReSkate's TrickLogger: vtable, bodies (count 9, body 0 = root).
inline constexpr std::uintptr_t board_physics_vtable = 0x65E67A0;
inline constexpr std::uintptr_t board_physics = 0x18, physics_bodies = 0x20;
inline constexpr std::uintptr_t body_owner = 0x10, body_velocity = 0x70, body_spin = 0x90;
inline constexpr std::uint32_t board_body_count = 9;
// Native noclip uses root body +0x54 for the board's world altitude.
inline constexpr std::uintptr_t body_height = 0x54;
// Animation Boolean descriptors: vtables at +0x20 and +0x60, key at +0x28, inline name at +0x88.
inline constexpr std::uintptr_t bool_descriptor_vtable = 0x61AB030, bool_descriptor_vtable2 = 0x61AB080;
inline constexpr std::uintptr_t descriptor_key = 0x28, descriptor_name = 0x88;
inline constexpr std::uint32_t iface_bool = 0xBCC2C469, iface_enum = 0xA5EDBE98;
// The landing ContextDatabase (CDB_OnBoard_Land): fields at +0x58 (5, stride 24), database at +0x40
// (+0xA8 = 4 entries). Field 0 is SketchyLanding, field 1 TrickLandingTransition.
inline constexpr std::uintptr_t cdb_fields = 0x58, cdb_database = 0x40, cdb_entry_count = 0xA8;
} // namespace dingosdk::game::build::v20260929::steezy_landings
