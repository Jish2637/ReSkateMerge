#include "steezy_landings.h"

#include "Engine/Core/Hooks/hooks.h"
#include "Extension/Skater/no_bail.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/20260929/steezy_landings.h"

#include <Windows.h>
#include <ShlObj.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>

namespace dingosdk::steezy_landings {
namespace {
namespace sl = game::build::v20260929::steezy_landings;
using std::uintptr_t;

uintptr_t base{}, image_end{};
// The live settings: replaced whole on every change; the physics thread takes a reference per step.
std::atomic<std::shared_ptr<const Settings>> live{std::make_shared<const Settings>()};
std::atomic<uintptr_t> local_core{};
std::atomic<std::uint64_t> owner_seen{};
bool owns_core(uintptr_t core) noexcept { return core && local_core.load() == core && GetTickCount64() - owner_seen.load() < 1000; }
std::atomic<bool> feature_on{}; // the switch, for the hooks' quick exit
std::atomic<bool> started{}, output_wrapped{}, selector_active{};
std::string start_problem;      // written once by start(), before `started`

template<class... Args> void say(logging::Level level, std::format_string<Args...> text, Args&&... args) {
    logging::log(level, logging::Channel::skater, "Steezy Landings: {}", std::format(text, std::forward<Args>(args)...));
}

// ---- memory ------------------------------------------------------------------------------------
template<class T> bool read(uintptr_t address, T& value) noexcept { return memory::peek(address, value); }
bool write_bytes(uintptr_t address, const void* source, std::size_t size) noexcept {
    if (address < 0x10000 || address > memory::highest_user_address - size) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template<class T> bool write(uintptr_t address, const T& value) noexcept { return write_bytes(address, &value, sizeof(T)); }
uintptr_t pointer(uintptr_t address) noexcept {
    uintptr_t value{};
    return read(address, value) ? value : 0;
}
bool in_image(uintptr_t address) noexcept { return address >= base && address < image_end; }
bool contract_holds(const sl::PrefixContract& c) noexcept {
    std::array<unsigned char, 32> actual{};
    return memory::read_bytes(base + c.rva, actual.data(), c.length) && std::memcmp(actual.data(), c.bytes.data(), c.length) == 0;
}

// ---- Bool.Anim.SketchyLanding --------------------------------------------------------------------
constexpr std::string_view sketchy_suffix = "Bool.Anim.SketchyLanding", transition_suffix = "Bool.Anim.TrickLandingTransition";
// Found through the landing table the active selector evaluates; every use checks it again.
std::atomic<uintptr_t> sketchy_node{};
std::atomic<std::uint32_t> sketchy_key{};
bool valid_descriptor(uintptr_t node, std::uint32_t& key) noexcept {
    uintptr_t vt1{}, vt2{};
    return node >= 0x10000 && !(node & 7) && read(node + 0x20, vt1) && vt1 == base + sl::bool_descriptor_vtable && read(node + 0x60, vt2) &&
           vt2 == base + sl::bool_descriptor_vtable2 && read(node + sl::descriptor_key, key) && (key >> 24);
}
bool named_descriptor(uintptr_t node, std::string_view suffix, std::uint32_t& key) noexcept {
    if (!valid_descriptor(node, key)) return false;
    std::array<char, 96> name{};
    const auto length = memory::peek_cstring(node + sl::descriptor_name, name.data(), name.size());
    return length >= 0 && descriptor_name(std::string_view(name.data(), static_cast<std::size_t>(length)), suffix);
}
void consider_descriptor(uintptr_t node) noexcept {
    std::uint32_t key{};
    if (sketchy_node.load(std::memory_order_acquire) || !named_descriptor(node, sketchy_suffix, key)) return;
    sketchy_key.store(key, std::memory_order_release);
    sketchy_node.store(node, std::memory_order_release);
}
// The local Boolean entry of an existing page; never the asset default.
uintptr_t bool_slot(uintptr_t context, std::uint32_t key) noexcept {
    if (!key || context < 0x10000) return 0;
    uintptr_t page{};
    if (!read(context + 0x18 + static_cast<uintptr_t>(key & 0xFFF) * 8, page) || page < 0x10000) return 0;
    const std::uint32_t slot = (key >> 12) & 0x7FF;
    std::uint32_t bits{};
    if (!read(page + 0x71C + (slot >> 5) * 4, bits) || !(bits & (1u << (slot & 31)))) return 0;
    return page + slot;
}
uintptr_t ant_context(uintptr_t core) noexcept {
    const auto model = pointer(core + sl::core_model);
    return model ? model + 0x48 : 0;
}
// The game-state writer the output bridge uses: AntRef+0x20 -> QueryInterface(vtbl+0) -> writer vtbl+0x20(iface, context, value).
// 0 written, 1 bad AntRef, 2 no interface, 3 bad writer, 4 exception.
int call_writer(uintptr_t ant_ref, std::uint32_t interface_id, uintptr_t context, std::uint32_t value) noexcept {
    const auto subobject = ant_ref + 0x20;
    uintptr_t vt{}, query{};
    if (ant_ref < 0x10000 || !read(subobject, vt) || !in_image(vt) || !read(vt, query) || !in_image(query)) return 1;
    __try {
        using Query = void*(__fastcall*)(void*, std::uint32_t);
        using Write = void(__fastcall*)(void*, uintptr_t, std::uint32_t);
        void* const iface = reinterpret_cast<Query>(query)(reinterpret_cast<void*>(subobject), interface_id);
        if (!iface) return 2;
        const auto writer = *reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(iface) + 0x20);
        if (!in_image(writer)) return 3;
        reinterpret_cast<Write>(writer)(iface, context, value);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 4;
    }
}
// Our own reads through the game's typed getter (they include the parent/default fallback). They are
// never answered by the selector override below.
thread_local unsigned own_reads{};
int call_reader_native(uintptr_t ref, std::uint32_t type, uintptr_t context) noexcept {
    if (ref < 0x10000 || context < 0x10000) return -1;
    __try {
        const auto subobject = ref + 0x20;
        const auto vt = *reinterpret_cast<const uintptr_t*>(subobject);
        if (!in_image(vt)) return -1;
        const auto query = *reinterpret_cast<const uintptr_t*>(vt);
        if (!in_image(query)) return -1;
        using Query = uintptr_t(__fastcall*)(uintptr_t, std::uint32_t);
        const auto object = reinterpret_cast<Query>(query)(subobject, type);
        if (!object) return -1;
        const auto object_vt = *reinterpret_cast<const uintptr_t*>(object);
        if (!in_image(object_vt)) return -1;
        const auto getter = *reinterpret_cast<const uintptr_t*>(object_vt + 0x18);
        if (!in_image(getter)) return -1;
        using Get = std::uint64_t(__fastcall*)(uintptr_t, uintptr_t);
        const auto value = reinterpret_cast<Get>(getter)(object, context);
        return type == sl::iface_bool ? static_cast<int>(value & 0xff) : static_cast<int>(static_cast<std::uint32_t>(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
int call_reader(uintptr_t ref, std::uint32_t type, uintptr_t context) noexcept {
    ++own_reads;
    const auto value = call_reader_native(ref, type, context);
    --own_reads;
    return value;
}
// EnumGS.LandQuality, written as the output bridge writes it (5: no output config or AntRef).
int publish_quality(uintptr_t core, std::uint32_t quality) noexcept {
    const auto config = pointer(core + sl::core_output_config);
    const auto inner = config ? pointer(config + sl::output_config_inner) & ~uintptr_t{4} : 0;
    const auto ref = inner ? pointer(inner + sl::config_land_quality) & ~uintptr_t{4} : 0;
    const auto context = ant_context(core);
    if (!ref || !context) return 5;
    return call_writer(ref, sl::iface_enum, context, quality);
}
// 0 written by the game's writer, 6 flag not found yet, 7 written to the page directly, 8 failed.
int set_sketchy(uintptr_t core, bool value) noexcept {
    const auto node = sketchy_node.load(std::memory_order_acquire);
    std::uint32_t key{};
    if (!node || !named_descriptor(node, sketchy_suffix, key) || key != sketchy_key.load(std::memory_order_acquire)) return 6;
    const auto context = ant_context(core);
    if (!context) return 8;
    if (call_writer(node, sl::iface_bool, context, value ? 1u : 0u) == 0) return 0;
    const auto slot = bool_slot(context, key);
    return slot && write(slot, static_cast<std::uint8_t>(value ? 1 : 0)) ? 7 : 8;
}
int read_sketchy(uintptr_t core) noexcept {
    std::uint32_t key{};
    if (!named_descriptor(sketchy_node.load(std::memory_order_acquire), sketchy_suffix, key) || key != sketchy_key.load(std::memory_order_acquire)) return -1;
    const auto slot = bool_slot(ant_context(core), key);
    std::uint8_t value{};
    return slot && read(slot, value) ? (value ? 1 : 0) : -1;
}

// ---- measurements ---------------------------------------------------------------------------------
struct Motion {
    bool known{};
    float vx{}, vy{}, vz{}, yaw_rate{}, height{};
    bool height_known{};
};
Motion board_motion(uintptr_t core) noexcept {
    Motion m;
    const auto physics = pointer(pointer(core + sl::core_board_holder) + sl::board_physics);
    const auto bodies = physics ? pointer(physics + sl::physics_bodies) : 0;
    uintptr_t vt{}, owner{};
    std::uint32_t count{};
    std::array<float, 3> v{}, w{};
    const bool body_known = read(physics, vt) && vt == base + sl::board_physics_vtable && read(bodies, count) && count == sl::board_body_count &&
              read(bodies + sl::body_owner, owner) && owner == physics;
    m.known = body_known && read(bodies + sl::body_velocity, v) && read(bodies + sl::body_spin, w);
    for (float value : v) m.known = m.known && std::isfinite(value);
    for (float value : w) m.known = m.known && std::isfinite(value);
    m.height_known = body_known && read(bodies + sl::body_height, m.height) && std::isfinite(m.height) && std::fabs(m.height) < 1e6f;
    if (m.known) {
        m.vx = v[0];
        m.vy = v[1];
        m.vz = v[2];
        m.yaw_rate = w[1]; // +Y up
    }
    return m;
}
// The rider's heading: the rig's forward axis (validated core -> rig -> core link), atan2(x, z).
bool body_yaw(uintptr_t core, double& yaw) noexcept {
    const auto rig = pointer(core + sl::core_rig);
    if (!rig || pointer(rig + 0x4630) != core || pointer(rig) != pointer(core + sl::core_context)) return false;
    std::array<float, 16> m{};
    if (!read(rig + 0x4720, m)) return false;
    for (const float x : m)
        if (!std::isfinite(x)) return false;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto i = axis * 4;
        const auto n = m[i] * m[i] + m[i + 1] * m[i + 1] + m[i + 2] * m[i + 2];
        if (n < .5f || n > 1.5f) return false;
    }
    if (std::hypot(m[8], m[10]) <= .3f) return false;
    yaw = std::atan2(m[8], m[10]);
    return true;
}

// ---- per skater ------------------------------------------------------------------------------------
struct CoreState {
    // Fixed from the landing's decision until its window closes.
    Tuning tuning;
    bool enabled{}, diagnostics{};
    uintptr_t model{};
    std::uint32_t step{}, last_category{};
    FlightMeasurements flight;
    Motion last_air{};
    double air_yaw{}, last_body_yaw{};
    bool air_yaw_known{}, body_yaw_known{};
    bool handled{}; // the current classification is decided
    bool qualified{}, chosen{};
    std::uint32_t decided_step{}, game_quality{}, written_quality{};
    bool wrote_pulse{};
};
std::mutex cores_mutex; // physics thread: bridge and output hooks
std::unordered_map<uintptr_t, CoreState> cores;
std::mt19937_64 rng{static_cast<std::uint64_t>(GetTickCount64()) ^ 0x5ce7c4u};
std::atomic<std::uint64_t> decided_landings{}, chosen_landings{};
std::mutex last_mutex;
std::string last_line;
std::mutex log_mutex;
std::vector<std::string> log_lines; // diagnostics, written from the client tick

void sample_body_spin(CoreState& c, double yaw, bool valid, bool airborne, bool new_jump) noexcept {
    if (new_jump) {
        c.air_yaw = 0;
        c.air_yaw_known = c.body_yaw_known;
    }
    if (airborne) {
        c.air_yaw_known = c.air_yaw_known && valid && c.body_yaw_known;
        if (valid && c.body_yaw_known) c.air_yaw += std::remainder(yaw - c.last_body_yaw, 6.283185307179586);
    }
    c.body_yaw_known = valid;
    if (valid) c.last_body_yaw = yaw;
}

// The selector's view of each skater's window, without the physics thread's lock: `context | policy`
// while that core's window is open (1 chosen: answer true, 4 not qualified: answer false), `context`
// while closed, 0 when free. Contexts are 8-aligned (model+0x48). Owners change only under cores_mutex.
constexpr std::size_t window_slots = 16;
std::array<std::atomic<uintptr_t>, window_slots> windows{};
std::array<uintptr_t, window_slots> window_owners{};
bool window_open(const CoreState& c) noexcept {
    if (!c.model || !c.decided_step || !c.enabled || !c.tuning.selector_override || c.step < c.decided_step) return false;
    const auto since = static_cast<int>(c.step - c.decided_step);
    return since >= c.tuning.window_start && since < c.tuning.window_start + c.tuning.window_length;
}
void publish_window(uintptr_t core, const CoreState& c) noexcept {
    std::size_t slot = window_slots, free_slot = window_slots;
    for (std::size_t i = 0; i < window_slots; ++i) {
        if (window_owners[i] == core) {
            slot = i;
            break;
        }
        if (!window_owners[i] && free_slot == window_slots) free_slot = i;
    }
    const uintptr_t context = c.model ? c.model + 0x48 : 0;
    if (slot == window_slots) {
        if (!context || free_slot == window_slots) return; // nothing to publish, or the table is full (left native)
        slot = free_slot;
        window_owners[slot] = core;
    }
    const uintptr_t policy = window_open(c) ? (c.chosen ? 1u : !c.qualified ? 4u : 0u) : 0u;
    windows[slot].store(context ? context | policy : 0, std::memory_order_release);
}
void forget_core(uintptr_t core) noexcept {
    for (std::size_t i = 0; i < window_slots; ++i)
        if (window_owners[i] == core) {
            windows[i].store(0, std::memory_order_release);
            window_owners[i] = 0;
        }
}
uintptr_t window_policy(uintptr_t context) noexcept {
    if (!context || (context & 7)) return 0;
    for (const auto& window : windows) {
        const auto value = window.load(std::memory_order_acquire);
        if ((value & ~uintptr_t{7}) == context) return value & 7;
    }
    return 0;
}

const char* grade_name(std::uint32_t q) noexcept {
    static constexpr const char* names[]{"Straight", "Sketchy", "Spin", "Good"};
    return q < 4 ? names[q] : "?";
}
// Once per classified landing, under cores_mutex.
void decide_landing(uintptr_t core, uintptr_t p, CoreState& c, const Settings& s) noexcept {
    if (c.wrote_pulse) set_sketchy(core, false); // a quick second landing never keeps the first one's flag
    c.wrote_pulse = false;
    c.tuning = s.tuning;
    c.enabled = s.enabled;
    c.diagnostics = s.diagnostics;
    c.handled = true;
    c.decided_step = c.step;
    std::uint32_t quality{};
    float sketchiness{};
    read(p + sl::p_quality, quality);
    read(p + sl::p_sketchiness, sketchiness);
    if (active(s.tuning, Condition::body_spin) && c.flight.pending && !c.flight.landed) {
        double yaw{};
        const bool valid = body_yaw(core, yaw);
        sample_body_spin(c, yaw, valid, true, false);
    }
    const auto current_motion = board_motion(core);
    const bool fresh_flight = c.flight.pending;
    c.flight.finish(current_motion.height, current_motion.height_known);
    Measurements m;
    m.air_known = fresh_flight && c.flight.time_known && c.flight.seconds > 0;
    m.air = m.air_known ? c.flight.seconds : 0;
    m.drop_known = fresh_flight && c.flight.height_known && c.flight.landed;
    m.drop = m.drop_known ? c.flight.drop : 0;
    m.game_quality = quality;
    m.sketchiness = sketchiness;
    m.angle_known = fresh_flight && c.air_yaw_known && c.flight.seconds > 0;
    m.spin_angle = std::fabs(c.air_yaw) * 57.29577951308232;
    const auto& motion = fresh_flight ? c.last_air : current_motion;
    m.motion_known = motion.known;
    if (m.motion_known) {
        m.speed = std::sqrt(static_cast<double>(motion.vx) * motion.vx + static_cast<double>(motion.vz) * motion.vz);
        m.board_yaw = motion.yaw_rate * 57.29577951308232;
    }
    c.flight.consume(current_motion.height, current_motion.height_known);
    const auto d = decide(s.tuning, m, [] { return std::uniform_real_distribution<double>(0, 100)(rng); });
    c.qualified = d.qualified;
    c.chosen = d.chosen;
    c.game_quality = quality;
    c.written_quality = d.chosen && quality <= quality_good ? s.tuning.grade : quality;
    ++decided_landings;
    if (d.chosen) ++chosen_landings;
    auto line = std::format("{}, {:.2f} s air, {:.1f} m drop, {:.1f} m/s, {:.0f} deg; game grade {}{}", d.chosen ? "chosen" : d.qualified ? "not chosen" : "no condition met",
                            m.air, m.drop, m.speed, m.spin_angle, grade_name(quality),
                            c.written_quality != quality ? std::string(" -> ") + grade_name(c.written_quality) : std::string{});
    if (s.diagnostics) {
        std::lock_guard lock(log_mutex);
        if (log_lines.size() < 256) log_lines.push_back(std::format("landing {}: {} (rolls {}, motion {}, angle {}, yaw {:.0f} deg/s, sketchiness {:.2f})", decided_landings.load(),
                                                                  line, d.draws, m.motion_known, m.angle_known, m.board_yaw, m.sketchiness));
    }
    std::lock_guard lock(last_mutex);
    last_line = std::move(line);
}

// The flag window, once per step.
void pulse_step(uintptr_t core, CoreState& c) noexcept {
    if (!c.decided_step || c.step < c.decided_step) return;
    const auto since = static_cast<int>(c.step - c.decided_step);
    if (since < c.tuning.window_start || since >= c.tuning.window_start + c.tuning.window_length) return;
    if (c.chosen) {
        const int result = set_sketchy(core, true);
        c.wrote_pulse = c.wrote_pulse || result == 0 || result == 7;
    } else if (c.enabled && !c.qualified && read_sketchy(core) == 1) {
        set_sketchy(core, false); // no condition met: the game's own pulse is cancelled too
    }
}

// ---- hooks -------------------------------------------------------------------------------------------
using StepFunction = void(__fastcall*)(void*, float);
std::atomic<StepFunction> original_bridge{}, original_output{};

// In place of the output bridge (this = core): the chosen grade stands in P+0x64 while the game
// publishes it, and the game's own grade is put back right after.
void __fastcall output_hook(void* self, float dt) {
    const auto original = original_output.load(std::memory_order_acquire);
    if (!feature_on.load(std::memory_order_relaxed)) return original(self, dt);
    const auto core = reinterpret_cast<uintptr_t>(self);
    if (!owns_core(core)) return original(self, dt);
    uintptr_t p{}, vt{};
    std::uint32_t saved{};
    bool forced = false;
    if (read(core, vt) && vt == base + sl::core_vtable) {
        const auto settings = live.load(std::memory_order_acquire);
        std::lock_guard lock(cores_mutex);
        auto& c = cores[core];
        const auto container = pointer(core + sl::core_trick_state);
        p = container ? pointer(container + sl::trick_state_quality) : 0;
        std::uint8_t classified{};
        if (p) read(p + sl::p_classified, classified);
        if (classified && !c.handled) decide_landing(core, p, c, *settings);
        if (p) read(p + sl::p_quality, saved);
        if (c.decided_step && c.step >= c.decided_step && c.chosen && p) {
            const auto since = static_cast<int>(c.step - c.decided_step);
            if (since < c.tuning.window_start + c.tuning.window_length && (since == 0 || c.tuning.hold_grade) && write(p + sl::p_quality, c.written_quality))
                forced = true;
        }
        pulse_step(core, c);
        publish_window(core, c);
    }
    original(self, dt);
    if (forced) write(p + sl::p_quality, saved); // every other reader of P keeps the game's own grade
}

void step(uintptr_t core, float dt, bool before) noexcept {
    uintptr_t vt{};
    if (!read(core, vt) || vt != base + sl::core_vtable) return;
    const auto settings = live.load(std::memory_order_acquire);
    const Settings& s = *settings;
    std::lock_guard lock(cores_mutex);
    auto& c = cores[core];
    const auto model = pointer(core + sl::core_model);
    if (c.model != model) {
        c = CoreState{};
        c.model = model;
    }
    const auto container = pointer(core + sl::core_trick_state);
    const auto p = container ? pointer(container + sl::trick_state_quality) : 0;
    std::uint8_t classified{};
    if (p) read(p + sl::p_classified, classified);
    if (before) {
        ++c.step;
        if (classified && !c.handled) decide_landing(core, p, c, s);
        publish_window(core, c);
        return;
    }
    std::uint32_t category{};
    read(pointer(core + sl::core_context) + sl::ctx_category, category);
    const bool spin_on = active(s.tuning, Condition::body_spin);
    const bool in_air = category >= sl::category_air && category < sl::category_air + 100;
    const bool new_jump = in_air && !c.flight.airborne;
    const bool landed_now = !in_air && c.flight.airborne;
    const auto motion = board_motion(core);
    // Freeze the final body angle at touchdown, not a later classifier tick or ground turn.
    if (landed_now && !c.flight.landed && spin_on) {
        double yaw{}; const bool valid = body_yaw(core, yaw);
        sample_body_spin(c, yaw, valid, true, false);
    }
    c.flight.sample(in_air, dt, motion.height, motion.height_known);
    if (in_air) {
        c.last_air = motion;
        if (spin_on) {
            double yaw{}; const bool valid = body_yaw(core, yaw);
            sample_body_spin(c, yaw, valid, true, new_jump);
        } else c.body_yaw_known = c.air_yaw_known = false;
    }
    c.last_category = category;
    const bool wrapped = output_wrapped.load(std::memory_order_relaxed);
    const bool decided_now = classified && !c.handled;
    if (decided_now) decide_landing(core, p, c, s);
    if (!in_air) {
        if (spin_on) {
            double yaw{}; const bool valid = body_yaw(core, yaw);
            sample_body_spin(c, yaw, valid, false, false);
        } else c.body_yaw_known = false;
    }
    if (!classified && c.handled && c.step > c.decided_step) c.handled = false;
    if (c.decided_step && c.step >= c.decided_step) {
        const auto since = static_cast<int>(c.step - c.decided_step);
        const int window_end = c.tuning.window_start + c.tuning.window_length;
        if (since < window_end) {
            // Without the output wrapper, or for a landing classified after this step's publish: write over
            // the grade the game has just published, through its own writer.
            if (c.chosen && (!wrapped || decided_now) && (since == 0 || c.tuning.hold_grade)) publish_quality(core, c.written_quality);
            if (!wrapped || decided_now) pulse_step(core, c);
        } else {
            if (c.wrote_pulse && read_sketchy(core) == 1) set_sketchy(core, false); // ours never outlasts the window
            c.wrote_pulse = false;
            c.decided_step = 0;
        }
    }
    publish_window(core, c);
}

// The bridge update (this = core+0x4A0), every physics step of every skater.
void __fastcall bridge_hook(void* self, float dt) {
    const auto original = original_bridge.load(std::memory_order_acquire);
    const auto core = reinterpret_cast<uintptr_t>(self) - 0x4A0;
    if (!owns_core(core)) return original(self, dt);
    if (!feature_on.load(std::memory_order_relaxed)) {
        {
            std::lock_guard lock(cores_mutex);
            if (const auto found = cores.find(core); found != cores.end()) {
                if (found->second.wrote_pulse) set_sketchy(core, false);
                cores.erase(found);
            }
            forget_core(core);
        }
        return original(self, dt);
    }
    step(core, dt, true);
    original(self, dt);
    step(core, dt, false);
}

// While the active selector evaluates the landing table for a skater whose window is open, the
// table's own read of SketchyLanding (and only that read, from 0x11D189E, in that context) is answered.
struct SelectorRead {
    uintptr_t context{};
    bool answer{}, suppress{};
};
thread_local SelectorRead selector_read{};
using BoolGetter = std::uint8_t(__fastcall*)(uintptr_t, uintptr_t);
std::atomic<BoolGetter> original_getter{};
std::uint8_t __fastcall bool_getter_hook(uintptr_t self, uintptr_t context) {
    const auto native = original_getter.load(std::memory_order_acquire)(self, context);
    if (!selector_read.answer && !selector_read.suppress) return native; // every other Boolean read of the game
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (own_reads || caller != base + sl::landing_table_read_return || context != selector_read.context || !feature_on.load(std::memory_order_relaxed))
        return native;
    const auto node = self >= 0x10060 ? self - 0x60 : 0;
    std::uint32_t key{};
    if (!node || node != sketchy_node.load(std::memory_order_acquire) || !named_descriptor(node, sketchy_suffix, key)) return native;
    return selector_read.suppress ? 0 : 1;
}
// The landing table: five fields (SketchyLanding, TrickLandingTransition, GenericLanding, LandQuality, ...), four entries.
bool landing_table(uintptr_t asset, std::array<uintptr_t, 4>& refs) noexcept {
    const auto fields = pointer(asset + sl::cdb_fields);
    const auto database = pointer(asset + sl::cdb_database) & ~uintptr_t{4};
    std::uint32_t count{}, entries{}, key{};
    if (!fields || !database || !read(fields - 4, count) || (count & 0x7fffffff) != 5 || !read(database + sl::cdb_entry_count, entries) || entries != 4) return false;
    for (std::size_t i = 0; i < refs.size(); ++i) refs[i] = pointer(fields + i * 24) & ~uintptr_t{4};
    return named_descriptor(refs[0], sketchy_suffix, key) && named_descriptor(refs[1], transition_suffix, key);
}
using Selector = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
std::atomic<Selector> original_selector{};
uintptr_t __fastcall selector_hook(uintptr_t self, uintptr_t output, uintptr_t evaluation) {
    const auto original = original_selector.load(std::memory_order_acquire);
    static thread_local bool inside{};
    if (inside || !feature_on.load(std::memory_order_relaxed)) return original(self, output, evaluation);
    std::array<uintptr_t, 4> refs{};
    if (!landing_table(pointer(self + 0x10) & ~uintptr_t{4}, refs)) return original(self, output, evaluation);
    consider_descriptor(refs[0]);
    const auto context = pointer(evaluation + 0x20);
    const auto policy = window_policy(context);
    if (!policy) return original(self, output, evaluation);
    struct Scope {
        bool& flag;
        SelectorRead previous;
        ~Scope() {
            selector_read = previous;
            flag = false;
        }
    } scope{inside, selector_read};
    inside = true;
    const int transition = call_reader(refs[1], sl::iface_bool, context);
    const int quality = call_reader(refs[3], sl::iface_enum, context);
    // Generic landings only (TrickLandingTransition 0): short trick landings keep the game's own path.
    selector_read = {context, (policy & 1) && transition == 0 && (quality == static_cast<int>(quality_sketchy) || quality == static_cast<int>(quality_spin)),
                     (policy & 4) != 0};
    return original(self, output, evaluation);
}

// The relay to the original is published before the hook goes live: a game thread can enter the
// replacement the moment hook_enable attaches it.
template<class Function, class Hook>
bool install(const sl::PrefixContract* contract, std::uintptr_t rva, Hook replacement, std::atomic<Function>& original, const char* what) {
    if (contract && !contract_holds(*contract)) {
        say(logging::Level::warning, "{} differs from the supported build; not hooked.", what);
        return false;
    }
    auto* target = reinterpret_cast<void*>(base + rva);
    void* relay{};
    auto status = hook_prepare(target, reinterpret_cast<void*>(replacement), &relay);
    if (status == HookOk) {
        original.store(reinterpret_cast<Function>(relay), std::memory_order_release);
        status = hook_enable(target);
        if (status != HookOk) hook_remove(target);
    }
    if (status != HookOk) say(logging::Level::warning, "{} could not be hooked ({}).", what, hook_status_string(status));
    return status == HookOk;
}

// ---- settings and steezy-landings.json ---------------------------------------------------------------
struct Shared {
    std::mutex mutex;
    Store store;
    std::uint64_t revision{1};
    bool save_due{};
    std::uint64_t save_at{};
};
Shared& shared() {
    static auto* value = new Shared;
    return *value;
}
std::filesystem::path data_directory() {
    PWSTR raw{};
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) result = std::filesystem::path(raw) / L"ReSkate" / L"trainer";
    if (raw) CoTaskMemFree(raw);
    return result;
}
// Under shared().mutex: the live copy follows the store, and a save is due.
void changed_locked(Shared& s) {
    s.store.settings.tuning = sanitize(s.store.settings.tuning);
    feature_on.store(s.store.settings.enabled, std::memory_order_release);
    live.store(std::make_shared<const Settings>(s.store.settings), std::memory_order_release);
    ++s.revision;
    s.save_due = true;
    s.save_at = GetTickCount64() + 1500;
}
void load_file() {
    auto& s = shared();
    Store loaded;
    try {
        const auto path = data_directory() / L"steezy-landings.json";
        std::ifstream file(path, std::ios::binary);
        if (file) {
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (!text.empty() && text.size() <= 1024 * 1024) loaded = store_from_json(Json::parse(text));
        }
    } catch (...) {
        say(logging::Level::warning, "steezy-landings.json could not be read; starting from the defaults (everything off).");
    }
    std::lock_guard lock(s.mutex);
    s.store = std::move(loaded);
    s.store.settings.tuning = sanitize(s.store.settings.tuning);
    feature_on.store(s.store.settings.enabled, std::memory_order_release);
    live.store(std::make_shared<const Settings>(s.store.settings), std::memory_order_release);
    ++s.revision;
}
// Written on a thread of its own, in order, as the trainer writes trainer.json.
void save_file(std::string text) {
    static std::atomic<std::uint64_t> latest{};
    const auto serial = ++latest;
    std::thread([text = std::move(text), serial] {
        static std::mutex writing;
        std::lock_guard lock(writing);
        if (serial != latest.load()) return; // a delayed older worker never replaces a newer save
        try {
            const auto directory = data_directory();
            if (directory.empty()) return;
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            const auto target = directory / L"steezy-landings.json", temporary = directory / L"steezy-landings.json.tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file << text;
                if (!file) return;
            }
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                say(logging::Level::warning, "could not replace steezy-landings.json (previous save retained).");
        } catch (...) {}
    }).detach();
}
} // namespace

bool start(std::uintptr_t image_base) noexcept {
    try {
        base = image_base;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<uintptr_t>(dos->e_lfanew));
        image_end = base + nt->OptionalHeader.SizeOfImage;
        load_file();
        for (const auto* contract : sl::all)
            if (!contract_holds(*contract)) {
                start_problem = std::format("The game differs from the supported build ({}).", contract->what);
                say(logging::Level::warning, "{} Inactive.", start_problem);
                return false;
            }
        if (!install(&sl::bridge_update, sl::bridge_update.rva, &bridge_hook, original_bridge, "The skater bridge update")) {
            start_problem = "The skater bridge update could not be hooked.";
            return false;
        }
        output_wrapped.store(install(nullptr, sl::output_bridge, &output_hook, original_output, "The landing output bridge"), std::memory_order_release);
        if (install(&sl::bool_getter, sl::bool_getter.rva, &bool_getter_hook, original_getter, "The Boolean getter"))
            selector_active.store(install(&sl::active_selector, sl::active_selector.rva, &selector_hook, original_selector, "The landing selector"),
                                  std::memory_order_release);
        started.store(true, std::memory_order_release);
        say(logging::Level::info, "ready (output wrapper {}, landing selector {}); {}.", output_wrapped.load() ? "on" : "off", selector_active.load() ? "on" : "off",
            feature_on.load() ? "switched on" : "switched off");
        return true;
    } catch (...) {
        start_problem = "Steezy Landings could not start.";
        return false;
    }
}

void on_client_tick() noexcept {
    try {
        NoBailSkater owner;
        const auto core = no_bail_skater(owner) ? owner.core : 0;
        if (local_core.exchange(core) != core) {
            std::lock_guard lock(cores_mutex);
            cores.clear();
            for (std::size_t i = 0; i < window_slots; ++i) { windows[i].store(0); window_owners[i] = 0; }
        }
        owner_seen.store(GetTickCount64());
        {
            std::vector<std::string> lines;
            {
                std::lock_guard lock(log_mutex);
                lines.swap(log_lines);
            }
            for (const auto& line : lines) say(logging::Level::info, "{}", line);
        }
        auto& s = shared();
        std::string text;
        {
            std::lock_guard lock(s.mutex);
            if (!s.save_due || GetTickCount64() < s.save_at) return;
            s.save_due = false;
            text = to_json(s.store).dump(2);
        }
        save_file(std::move(text));
    } catch (...) {}
}

View view() {
    View v;
    auto& s = shared();
    {
        std::lock_guard lock(s.mutex);
        v.settings = s.store.settings;
        for (const auto& [name, tuning] : s.store.presets) v.own.push_back(name);
        v.revision = s.revision;
    }
    std::ranges::sort(v.own, [](const auto& a, const auto& b) { return lower(a) < lower(b); });
    for (const auto& [name, tuning] : builtin_presets()) v.builtin.push_back(name);
    v.status.started = started.load(std::memory_order_acquire);
    v.status.output_wrapped = output_wrapped.load(std::memory_order_acquire);
    v.status.selector_active = selector_active.load(std::memory_order_acquire);
    v.status.flag_found = sketchy_node.load(std::memory_order_acquire) != 0;
    v.status.landings = decided_landings.load();
    v.status.chosen = chosen_landings.load();
    if (!v.status.started) v.status.problem = start_problem.empty() ? "Not started yet." : start_problem;
    std::lock_guard lock(last_mutex);
    v.status.last_landing = last_line;
    return v;
}

void apply(const Settings& next) {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    Settings checked = next;
    checked.tuning = sanitize(checked.tuning);
    if (checked == s.store.settings) return;
    s.store.settings = std::move(checked);
    changed_locked(s);
}

std::string load_preset(const std::string& name) {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    for (const auto& [builtin, tuning] : builtin_presets())
        if (lower(builtin) == lower(name)) {
            s.store.settings.tuning = tuning;
            s.store.settings.preset = builtin;
            changed_locked(s);
            return std::format("Loaded {}.", builtin);
        }
    const auto found = s.store.find(name);
    if (found == s.store.presets.end()) return std::format("There is no preset called {}.", name);
    s.store.settings.tuning = found->second;
    s.store.settings.preset = found->first;
    changed_locked(s);
    return std::format("Loaded {}.", found->first);
}
std::string save_preset(const std::string& name) {
    if (!valid_name(name)) return "A preset name is 1 to 48 letters, digits, spaces, - _ or '.";
    if (builtin_name(name)) return std::format("{} comes with ReSkate; pick another name.", name);
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    const auto found = s.store.find(name);
    const bool replaced = found != s.store.presets.end();
    const auto key = replaced ? found->first : name;
    s.store.presets[key] = s.store.settings.tuning;
    s.store.settings.preset = key;
    changed_locked(s);
    return std::format("{} {}.", replaced ? "Replaced" : "Saved", key);
}
std::string delete_preset(const std::string& name) {
    if (builtin_name(name)) return std::format("{} comes with ReSkate and cannot be deleted.", name);
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    const auto found = s.store.find(name);
    if (found == s.store.presets.end()) return std::format("There is no preset called {}.", name);
    const auto key = found->first;
    s.store.presets.erase(found);
    if (lower(s.store.settings.preset) == lower(key)) s.store.settings.preset.clear();
    changed_locked(s);
    return std::format("Deleted {}. The values it set stay until you change them.", key);
}
std::string restore_defaults() {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    s.store.settings.tuning = Tuning{};
    s.store.settings.preset.clear();
    changed_locked(s);
    return "Every Steezy Landings value is back to its default: Global Chance 0 %, every condition off.";
}
} // namespace dingosdk::steezy_landings
