// Native ticker reader adapted from the supplied ReSkate tricklog.12 reference.
// See contrib/trick-terminology/README.md; project GPL-3.0 notices apply.
#include "trick_terminology.h"
#include "Extension/UI/hud_corner.h"
#include "trick_binding_probe.h"
#include "terminology_diagnostics.h"
#include "Extension/Progression/local_entitlement_trigger_runtime.h"
#include <intrin.h>
#include "native_combo_rows.h"
#include "retained_combos.h"
#include "combo_history.h"
#include "trick_editor.h"
#include "trick_terminology_rule.h"
#include "terminology_spin.h"
#include "trick_identity.h"
#include "terminology_phase_reference.h"
#include "no_bail.h"
#include "Engine/Game/Build/20260929/skater_state.h"
#include <atomic>
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Game/Build/20260929/profile.h"
#include "Engine/Game/Build/20260929/native_party.h"
#include <cstring>
#include <vector>
#include "trick_name_text.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/trick_names.h"
#include "Extension/UI/NativeMenu/native_menu_data.h"
#include "Engine/Core/Log/logging.h"
#include <windows.h>
#include <map>
#include <set>
#include <format>
#include <algorithm>
#include <cmath>

namespace dingosdk::trick_terminology {
namespace {
using namespace multiplayer::menu_data;
namespace layout = addr::trick_names;
constexpr std::array<std::uint32_t, 15> fields{
    0x53bac52d, 0xf5cd4270, 0xd813cc83, // base, direction, body rotation
    0x062499e8, 0xcf29a30d, 0x9a2a9749, 0x6cd6a294, // stance, held, late, nollie
    0xd7a6e0da, 0x680e60dd, 0xb7d7ac17, 0xe547253f, 0x2bad0ea8, 0xa2d7acc5,
    0x3c250648, 0x0205e59c}; // TrickMetaData, TrickCompletedInfo
struct Row { std::array<TextEdit,15> edits;Handle parent{};Address type{}; };
struct Association { std::uint64_t first{}; std::shared_ptr<SpinAttempt> attempt;
    std::uint64_t phase{}, segment{}, sequence{};std::string phase_kind;bool provisional{}, preexisting{}; };
struct State {
    Address manager{};
    std::uint64_t next{};
    bool localized{}, checked{};
    std::map<Handle, Row> rows;
    std::map<Handle, std::string> observations;
    unsigned recorded{}, error_records{};
    SpinTracker spins;
    trick_logger::TrickSetTracker phases{true,true,true};
    editor::ComboHistory history;
    std::map<std::uint64_t,trick_logger::TrickSetResult> sets;
    bool listener_attempted{}, listener_ready{}, measurement_valid{};
    std::array<float,3> position{};std::uintptr_t position_entity{};std::uint64_t position_at{};bool position_valid{};
    std::uint64_t phase_sequence{};std::set<std::uint64_t> invalid_sequences;
    trick_names::NativeTrickCatalog catalog;bool catalog_ready{};
    std::uint64_t wipeouts{};bool wipeouts_known{};
    std::map<Handle, Association> associations;
    std::set<Handle> known_fields;
    bool baseline{true};
    std::string status{"Off"};
};
State& state() { static State s; return s; }
editor::ConfirmedCombos confirmed_combos,confirmed_singles;
combo_rows::Presenter& row_presenter(){static combo_rows::Presenter value;return value;}
std::atomic<int> natural_goofy{-1};
std::atomic<std::uint64_t> physics_steps{}, sequence_ends{};
void rotation_physics_step(const NoBailSkater&,float,bool) noexcept {++physics_steps;}


// Native writes are observed synchronously; alias publication runs while the
// game's model write lock is still held, immediately before it releases that lock.
using Unlock = void (*)(Address);
std::atomic<Unlock> original_unlock{};
std::atomic<DWORD> client_thread{};
std::atomic<Address> hooked_manager{}, hooked_base{};
std::atomic<bool> publication_on{};
std::atomic<unsigned> boundary_events{};
bool boundary_checked{}, boundary_ready{};
thread_local bool in_tick{}, in_unlock{}, pending_native_row{};
thread_local std::uint64_t last_boundary{};
// The scoring script bypasses the public publisher observer.
struct IncomingText {Handle target{},row{};unsigned field{};std::string native,replacement;bool handled{};};
thread_local IncomingText* incoming_text{};
std::atomic<std::uint64_t> prewrite_calls{},prewrite_routes{},prewrite_fields{},prewrite_inputs{},prewrite_matches{},prewrite_cached{},prewrite_discovered{};
// A row root is encoded above the low 16-bit field selector. This was checked
// against 462 matched bindings / 31 rows in test25. Never trust the mask alone:
// verify row schema, label wrapper schema and the exact reflected Text handle.
std::optional<binding_probe::Identity> incoming_identity(Address base,Address manager,Handle target,std::uint64_t now) {
    if(const auto view=binding_probe::snapshot.load();view && now>=view->at && now-view->at<=250) {
        const auto it=std::lower_bound(view->identities.begin(),view->identities.end(),target,[](const auto& a,Handle b){return a.context<b;});
        if(it!=view->identities.end() && it->context==target && it->field<fields.size())return *it;
    }
    const Context c(base,manager);const auto row=target&~Handle{0xffff};const auto type=c.type_of(row);
    if(!row || !type || read<std::uint32_t>(read<Address>(type))!=layout::row_schema)return {};
    for(unsigned i=0;i<fields.size();++i) {
        const auto wrapper=c.field({row,type},fields[i]);
        if(read<std::uint32_t>(read<Address>(wrapper.type))!=layout::label_schema || size(wrapper.type)!=104)return {};
        if(c.field(wrapper,layout::label_text).handle==target) {++prewrite_discovered;return binding_probe::Identity{target,row,0,i};}
    }
    return {};
}
std::uint64_t internal_publish(Address manager,Handle target,Address type,const void* source,std::uint8_t flag,
                               Address caller,NativeModelWriter original) {
    const auto base=hooked_base.load();
    if(in_tick || in_unlock || !publication_on.load() || manager!=hooked_manager.load() ||
       GetCurrentThreadId()!=client_thread.load())return original(manager,target,type,source,flag);
    ++prewrite_calls;const DWORD error=GetLastError();
    Address native{};IncomingText candidate;bool prepared{},cached{};
    try {
        pending_native_row=pending_native_row || state().known_fields.contains(target);
        Address resource{};std::uint32_t key{};std::array<std::uint32_t,10> header{};
        constexpr std::array<std::uint32_t,10> expected{784,11760,14133,7607,81,0,144,5898659,25821255,266753};
        const auto vm=profile_runtime::executing_expression;const auto now=GetTickCount64();
        const bool route=caller==base+0x1924819 && !flag &&
            memory::peek(vm+0x38,resource) && memory::peek(resource+0x10,key) && key==0x5641a40b &&
            memory::peek(resource+0x20,header) && header==expected;
        if(route)++prewrite_routes;
        if(route && kind(type)==7 && size(type)==8)if(const auto identity=incoming_identity(base,manager,target,now)) {
            ++prewrite_fields;binding_probe::Text text;binding_probe::read_text(reinterpret_cast<Address>(source),text);
            if(text.valid) {
                candidate.target=target;candidate.row=identity->row;candidate.field=identity->field;candidate.native=text.bytes.data();
                const auto existing=state().rows.find(identity->row);
                if(existing!=state().rows.end()) {
                    const auto& edit=existing->second.edits[identity->field];
                    if(edit.active && edit.original==candidate.native) {
                        candidate.replacement=edit.written;candidate.handled=true;cached=true;++prewrite_cached;
                    }
                }
                const Context c(base,manager);
                if(!cached) {
                    ++prewrite_inputs;
                    struct Scope {
                        IncomingText* previous{incoming_text};bool unlocked{in_unlock};
                        Scope(IncomingText& text){incoming_text=&text;in_unlock=true;}
                        ~Scope(){incoming_text=previous;in_unlock=unlocked;}
                    } scope(candidate);
                    // Every confirmed native label can complete a rule: base,
                    // stance, held rotation, body spin or another modifier.
                    // Virtual evidence is matched before forwarding this write.
                    (void)tick(base,true,true);
                }
                if(candidate.handled) {
                    game::native_data().values.assign(&native,candidate.replacement.c_str(),static_cast<std::uint32_t>(candidate.replacement.size()));
                    prepared=true;++prewrite_matches;
                }
            }
        }
    }catch(...){}
    SetLastError(error);
    const auto result=original(manager,target,type,prepared?&native:source,flag);
    const DWORD after=GetLastError();
    if(candidate.target)try {
        const auto it=state().rows.find(candidate.row);
        if(it!=state().rows.end())it->second.edits[candidate.field].publication_finished(Context(base,manager).text({target,type},1024));
    }catch(...){}
    if(native)reinterpret_cast<void(*)(Address*)>(base+addr::engine::native_text_release)(&native);
    if(prepared && !cached)try {
        if constexpr (diagnostics::enabled) {
        auto j=Json::object();j["kind"]="terminology_prewrite";j["target"]=target;j["model_handle"]=candidate.row;j["field_index"]=candidate.field;
        j["native"]=candidate.native;j["forwarded"]=candidate.replacement;j["published"]=result;
        logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
        }
    }catch(...){}
    SetLastError(after);return result;
}

void queue_native_publication(Address manager,Handle handle,Address type) noexcept {
    // Called BEFORE the existing profile hook forwards the native write. Its
    // internal unlock can then flush this row while the model lock is still held.
    // No unprotected registry traversal or second detour on the shared publisher.
    if (in_tick || !publication_on.load() || GetCurrentThreadId()!=client_thread.load() ||
        manager!=hooked_manager.load()) return;
    const DWORD error=GetLastError();
    try {
        Address meta{};std::uint32_t hash{};
        const bool scoring_type=memory::peek(type,meta) && memory::peek(meta,hash) &&
            (hash==layout::row_schema || hash==layout::label_schema);
        if (scoring_type || state().known_fields.contains(handle)) pending_native_row=true;
    } catch (...) {}
    SetLastError(error);
}
void unlock_models(Address lock) {
    const auto now=GetTickCount64();
    bool recent_air{};
    if (GetCurrentThreadId()==client_thread.load()) {
        const auto& attempts=state().spins.attempts;
        recent_air=!attempts.empty() && (!attempts.back()->end || now<=attempts.back()->end+200);
    }
    if (!in_tick && !in_unlock && publication_on.load() && GetCurrentThreadId()==client_thread.load() &&
        lock==hooked_manager.load()+0xf20 && (pending_native_row ||
        (recent_air && !state().known_fields.empty() && now-last_boundary>=8))) {
        last_boundary=now;
        const DWORD error=GetLastError();
        pending_native_row=false;in_unlock=true;
        try {
            (void)tick(hooked_base.load(),true,true);
            if (boundary_events.fetch_add(1)<32)
                logging::write(logging::Level::info,logging::Channel::runtime,"Terminology processed native trick publication before model unlock");
        } catch (...) {}
        in_unlock=false;
        SetLastError(error);
    }
    original_unlock.load(std::memory_order_acquire)(lock);
}
template<class Function, class Contract>
bool install_checked(Address base,const Contract& contract,Function replacement,std::atomic<Function>& original) {
    std::array<unsigned char,32> actual{};
    if (!memory::peek(base+contract.rva,actual) || actual!=contract.bytes) return false;
    auto* target=reinterpret_cast<void*>(base+contract.rva);void* previous{};
    if (hook_prepare(target,reinterpret_cast<void*>(replacement),&previous)!=HookOk) return false;
    if (!previous) { (void)hook_remove(target);return false; }
    original.store(reinterpret_cast<Function>(previous),std::memory_order_release);
    if (hook_enable(target)==HookOk) return true;
    (void)hook_remove(target);return false;
}
void install_publication(Address base) {
    if (boundary_checked) return;
    boundary_checked=true;hooked_base.store(base);
    // Upstream owns this native detour for plugin HUDs and the trick feed.
    const bool publisher=hud_corner::set_model_writer(&publish_native_model);
    const bool unlocker=install_checked(base,addr::profile::model_unlock_contract,&unlock_models,original_unlock);
    boundary_ready=publisher && unlocker;
    logging::log(logging::Level::info,logging::Channel::runtime,
        "Terminology native publication hook: shared internal writer {}, unlock {}",publisher,unlocker);
}
void sample_spin(State& s, std::uint64_t now) {
    if (!s.listener_attempted) {
        s.listener_attempted=true;s.listener_ready=add_no_bail_step_listener(&rotation_physics_step);
        logging::log(logging::Level::info,logging::Channel::runtime,"Terminology native rotation step listener {}",s.listener_ready);
    }
    NoBailSkater local;
    const auto watch = watched_physics_state();
    if (watch.valid) {
        if (s.wipeouts_known && watch.wipeouts>s.wipeouts) s.spins.reset();
        s.wipeouts=watch.wipeouts;s.wipeouts_known=true;
    }
    std::array<float,16> body{};
    bool valid = no_bail_skater(local) && watch.valid && local.rig && memory::peek(local.rig + 0x4720, body);
    for (float v : body) valid = valid && std::isfinite(v);
    for (unsigned axis=0; axis<3; ++axis) {
        const auto i=axis*4; const auto length=body[i]*body[i]+body[i+1]*body[i+1]+body[i+2]*body[i+2];
        valid = valid && length>.5f && length<1.5f;
    }
    valid = valid && std::hypot(body[8],body[10])>.3f;
    const auto footing = natural_goofy.load();
    s.spins.sample(now, local.entity, watch.state, std::atan2(body[8],body[10])*57.29577951308232,
        valid, footing<0 ? std::nullopt : std::optional<bool>(footing!=0));
    // Jish's physical segments now supply matching evidence, including native
    // revert recovery. Native HUD sequence completion controls final closure.
    trick_logger::SetSample sample;sample.now=now;sample.steps=physics_steps.load();sample.sequence_ends=sequence_ends.load();sample.entity=local.entity;
    sample.position=s.position;sample.position_known=s.position_valid && s.position_entity==local.entity && now>=s.position_at && now-s.position_at<=150;
    sample.wipeouts=watch.wipeouts;sample.state=watch.state;sample.valid=valid;sample.yaw=std::atan2(body[8],body[10])*57.29577951308232;
    sample.goofy=footing<0?std::nullopt:std::optional<bool>(footing!=0);
    s.measurement_valid=sample.valid && s.listener_ready;
    if (const auto finished=s.phases.update(sample)) {
        s.sets[finished->id]=*finished;
        if constexpr (diagnostics::enabled) {
        auto event=Json::object();event["kind"]="terminology_rotation_set";event["sequence"]=finished->id;
        event["outcome"]=finished->outcome;event["rotation_complete"]=finished->rotation_complete;
        event["native_sequence_ends"]=sequence_ends.load();event["segments"]=Json::array();
        for(const auto& segment:finished->segments) {
            auto part=Json::object();part["air"]=segment.air;part["recovery"]=segment.landing;
            part["net"]=segment.air+segment.landing;part["spin"]=trick_logger::measured_spin(segment);
            event["segments"].push_back(std::move(part));
        }
        logging::write(logging::Level::info,logging::Channel::runtime,event.dump());
        }
        s.phase_sequence=finished->id;
        if (finished->landed!=true || !finished->rotation_complete) s.invalid_sequences.insert(finished->id);
    }
    if (s.phases.active_id()) {
        s.phase_sequence=s.phases.active_id();s.sets[s.phase_sequence]=s.phases.evidence();
    }
    while(s.sets.size()>32)s.sets.erase(s.sets.begin());
    while(s.invalid_sequences.size()>64)s.invalid_sequences.erase(s.invalid_sequences.begin());
    for (const auto& a : s.spins.attempts) if (a->end && !a->emitted) {
        a->emitted = true;
        if constexpr (diagnostics::enabled) {
        auto event = Json::object(); event["kind"] = "terminology_spin";
        event["attempt"] = a->id; event["entity"] = a->entity; event["start_ms"] = a->start;
        event["end_ms"] = a->end; event["signed_yaw"] = a->net; event["air_yaw"] = a->air_net; event["valid"] = a->valid;
        event["landed"] = a->landed; event["goofy"] = a->goofy ? Json(*a->goofy) : Json{};
        logging::write(logging::Level::info, logging::Channel::runtime,event.dump());
        }
    }
}
std::uint32_t schema(Address type) { return read<std::uint32_t>(read<Address>(type)); }
Value text_field(const Context& c, Value model, std::uint32_t hash,Handle* wrapper_handle=nullptr) {
    const auto wrapper = c.field(model, hash);
    if(wrapper_handle)*wrapper_handle=wrapper.handle;
    require(schema(wrapper.type) == layout::label_schema && size(wrapper.type) == 104,
            "Trick label schema differs");
    return c.field(wrapper, layout::label_text);
}
std::string readable(Address base, const std::string& raw, bool& complete) {
    return trick_names::plain_text(trick_names::resolve_text(raw, [base](std::string_view key) {
        std::uint32_t hash = 0xffffffff;
        for (unsigned char c : key) hash = hash * 33 + c;
        const auto p = reinterpret_cast<Address (*)(std::uint32_t, bool)>(base + layout::localized_string.rva)(hash, false);
        return string(p, 1024);
    }, complete));
}
}
std::uint64_t publish_native_model(Address manager,Handle target,Address type,const void* source,std::uint8_t flag,
                                  Address caller,NativeModelWriter original) {
    return internal_publish(manager,target,type,source,flag,caller,original);
}
void* begin_label_binding_probe(std::uintptr_t vm,std::uint32_t pc,bool profiled) noexcept {
    if constexpr (diagnostics::enabled)return binding_probe::begin(vm,pc,profiled);
    else return nullptr;
}
void end_label_binding_probe(void* token) noexcept {binding_probe::end(token);}
namespace {
void drain_binding_probe(std::uint64_t now) {
    if constexpr (diagnostics::enabled) {
    static std::uint64_t next{},stats{};static bool announced{};
    if(!announced && binding_probe::enabled.load()) {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        if constexpr (diagnostics::enabled) {
        auto j=Json::object();j["kind"]="terminology_binding_probe_ready";j["build"]="test26";
        j["graph"]=binding_probe::graph;j["qpc_frequency"]=frequency.QuadPart;j["observation_only"]=true;
        logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
        }announced=true;
    }
    if(now<next)return;next=now+50;
    binding_probe::Record r;
    for(unsigned n=0;n<8 && binding_probe::pop(r);++n) {
        if constexpr (diagnostics::enabled) {
        auto j=Json::object();j["kind"]="terminology_binding_probe";j["graph"]=binding_probe::graph;
        j["entry_pc"]=r.pc;j["thread"]=r.thread;j["profiled"]=r.profiled;
        j["entry_qpc"]=r.start;j["exit_qpc"]=r.finish;j["entry_ms"]=r.start_ms;j["exit_ms"]=r.finish_ms;
        j["model_tick_serial"]=r.tick;j["snapshot_generation"]=r.generation;j["snapshot_at_ms"]=r.snapshot_at;
        j["label_context"]=r.context;j["matched_known_label"]=r.matched;
        j["model_handle"]=r.row;j["field_index"]=r.field;j["rule_id"]=r.rule;
        j["vm"]=r.vm;j["frame"]=r.frame;j["output_storage"]=r.output_storage;j["storage_stable"]=r.stable;
        j["input"]=r.input.bytes.data();j["input_valid"]=r.input.valid;
        j["override"]=r.override_text.bytes.data();j["override_valid"]=r.override_text.valid;
        j["output"]=r.output.bytes.data();j["output_valid"]=r.output.valid;
        j["localized"]=r.localized.bytes.data();j["localized_valid"]=r.localized.valid;
        logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
        }
    }
    if(diagnostics::enabled && announced && now>=stats) {
        stats=now+5000;auto j=Json::object();j["kind"]="terminology_binding_probe_stats";
        j["prewrite_calls"]=prewrite_calls.load();j["prewrite_routes"]=prewrite_routes.load();
        j["prewrite_fields"]=prewrite_fields.load();j["prewrite_inputs"]=prewrite_inputs.load();j["prewrite_matches"]=prewrite_matches.load();j["prewrite_cached"]=prewrite_cached.load();j["prewrite_discovered"]=prewrite_discovered.load();
        j["shared_writer_ready"]=boundary_ready;
        j["recognized"]=binding_probe::recognized.load();j["rejected"]=binding_probe::rejected.load();
        j["queued_total"]=binding_probe::emitted.load();j["dropped"]=binding_probe::dropped.load();
        logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
    }
    }
}
}
void observe_scoring_expression(std::uintptr_t vm,std::uint32_t pc) noexcept {
    if (pc || !publication_on.load(std::memory_order_relaxed))return;
    Address resource{},instance{},check{};std::uint32_t key{};
    constexpr std::array<std::uint32_t,10> expected{80,528,203,331,6,0,6,1310735,2883607,0};
    std::array<std::uint32_t,10> actual{};
    if (memory::peek(vm+0x38,resource) && memory::peek(resource+0x10,key) && key==0xe6b5042d &&
        memory::peek(resource+0x20,actual) && actual==expected && memory::peek(vm+0x30,instance) &&
        memory::peek(instance,check) && check==resource) ++sequence_ends;
}
void observe_position(std::uintptr_t entity,const std::array<float,3>& position,bool valid) noexcept {
    auto& s=state();s.position=position;s.position_entity=entity;s.position_at=GetTickCount64();
    s.position_valid=valid && std::all_of(position.begin(),position.end(),[](float v){return std::isfinite(v);});
}
void observe_footing(bool goofy) noexcept { natural_goofy.store(goofy ? 1 : 0); }
void observe_native_publication(std::uintptr_t manager,std::uint64_t handle,std::uintptr_t type) noexcept {
    queue_native_publication(manager,handle,type);
}
std::string native_label(std::uintptr_t manager,std::uint64_t row,std::uintptr_t type,
    std::uint32_t field,const std::string& physical) {
    if (manager!=hooked_manager.load() || GetCurrentThreadId()!=client_thread.load()) return physical;
    const auto found=state().rows.find(row);
    if (found==state().rows.end() || found->second.type!=type) return physical;
    const auto slot=std::find(fields.begin(),fields.end(),field);
    if (slot==fields.end()) return physical;
    return found->second.edits[static_cast<std::size_t>(slot-fields.begin())].observe(physical);
}
bool presentation_row(std::uintptr_t manager,std::uint64_t row) noexcept {
    return manager==hooked_manager.load() && GetCurrentThreadId()==client_thread.load() && row_presenter().contains(row);
}
std::string tick(std::uintptr_t base,bool playing,bool enabled) noexcept {
    auto& s=state();
    if (in_tick) return s.status;
    struct TickGuard {TickGuard(){in_tick=true;}~TickGuard(){in_tick=false;}} tick_guard;
    const auto now=GetTickCount64();
    editor::set_enabled(enabled);
    if (!in_unlock) {client_thread.store(GetCurrentThreadId());editor::service(now);}
    publication_on.store(playing && enabled);
    binding_probe::enabled.store(diagnostics::enabled && playing && enabled,std::memory_order_relaxed);
    if(!in_unlock)drain_binding_probe(now);
    if(!playing || !enabled)binding_probe::snapshot.store(nullptr);
    try {
        if (playing && enabled) install_publication(base);
        if(!in_unlock && boundary_checked) {
            const bool shared=hud_corner::set_model_writer(playing && enabled ? &publish_native_model : nullptr);
            boundary_ready=playing && enabled && shared && original_unlock.load()!=nullptr;
        }
        if (!playing || !enabled) {confirmed_combos.clear();confirmed_singles.clear();s.spins.reset();s.phases=trick_logger::TrickSetTracker{true,true,true};s.history.clear();s.sets.clear();s.measurement_valid=false;s.phase_sequence=0;s.invalid_sequences.clear();s.wipeouts_known=false;s.associations.clear();s.baseline=true;}
        if (playing && enabled) sample_spin(s,now);
        if (!playing) {s.next=0;return s.status=enabled?"Waiting for a playable world":"Off";}
        if (!enabled && s.rows.empty() && row_presenter().empty()) return s.status="Off";
        if (now<s.next && !in_unlock) return s.status;
        s.next=now+8;
        if (!game::native_data().models.value) return s.status="Waiting for native UI";
        if (!s.checked) {
            std::array<unsigned char,32> actual{};
            s.localized=memory::peek(base+layout::localized_string.rva,actual) && actual==layout::localized_string.bytes;s.checked=true;
        }
        if (!s.localized) return s.status="Unavailable: localization signature differs";
        const auto ui=read<Address>(base+addr::engine::ui_manager);
        const auto manager=ui?read<Address>(ui+0x140):0;
        if (!manager) return s.status="Waiting for native UI";
        if (manager!=s.manager) {
            row_presenter().forget();confirmed_combos.clear();confirmed_singles.clear();
            s.rows.clear();s.observations.clear();s.history.clear();s.recorded=0;s.manager=manager;
            s.associations.clear();s.known_fields.clear();s.baseline=true;
        }
        if (!s.catalog_ready) {
            std::map<std::string,std::string> names;
            for (const auto& entry:skate::metadata) {bool complete{};const auto text=readable(base,std::string(entry.id),complete);if (complete) {s.catalog.add(entry.id,text);names.emplace(entry.id,text);}}
            editor::set_native_names(std::move(names));
            s.catalog_ready=true;
        }
        hooked_manager.store(manager);const Context c(base,manager);
        std::optional<game::ModelWriteLock> lock;if (!in_unlock) lock.emplace(manager);
        for(auto it=s.rows.begin();it!=s.rows.end();) {
            if(c.type_of(it->first)!=it->second.type) {
                s.associations.erase(it->first);s.observations.erase(it->first);
                it=s.rows.erase(it);
            }else ++it;
        }
        struct NativeRow {
            Value model;Handle parent{};std::array<Value,15> targets;std::array<Handle,15> wrappers{};
            editor::Labels raw, original, labels;
        };
        std::vector<NativeRow> native;
        std::vector<editor::Observation> frame;
        std::set<Handle> live,current_fields;unsigned failures{},matched{},conflicts{};
        const auto roots=c.roots({layout::row_schema});
        for (const auto& root:roots) {
            if (!root.value || row_presenter().contains(root.model.handle)) continue;
            try {
                NativeRow n;n.model=root.model;
                const auto parent_field=c.field(n.model,0xb99b6691);
                require(kind(parent_field.type)==18 && size(parent_field.type)==8,"Trick stack schema differs");
                n.parent=read<Handle>(c.address(parent_field));
                if((!n.parent || !c.type_of(n.parent)) && (!incoming_text || incoming_text->row!=n.model.handle))continue;
                auto it=s.rows.find(n.model.handle);
                // Moving a UI row between stacks does not change its native identity.
                if(it!=s.rows.end())it->second.parent=n.parent;
                if (it==s.rows.end()) {
                    if (!enabled || s.rows.size()>=256) continue;
                    it=s.rows.emplace(n.model.handle,Row{{},n.parent,n.model.type}).first;
                }
                live.insert(n.model.handle);auto& row=it->second;bool complete=true;
                for (unsigned i=0;i<fields.size();++i) {
                    n.targets[i]=text_field(c,n.model,fields[i],&n.wrappers[i]);current_fields.insert(n.targets[i].handle);
                    n.raw[i]=c.text(n.targets[i],1024);
                    if(incoming_text && incoming_text->target==n.targets[i].handle) {
                        row.edits[i].incoming(incoming_text->native,n.raw[i]);
                        n.raw[i]=incoming_text->native;
                    }
                    n.original[i]=row.edits[i].observe(n.raw[i]);
                    bool resolved{};n.labels[i]=readable(base,n.original[i],resolved);complete=complete && resolved;
                }
                if (!complete) continue;
                auto& association=s.associations[n.model.handle];
                if (!association.first) {association.first=now;association.preexisting=s.baseline;if (!s.baseline) association.attempt=s.spins.associate(now);}
                if (!association.preexisting && !association.phase && !s.phases.active_id()) {
                    const auto id=s.catalog.resolve(n.labels[0],true);
                    if (std::any_of(skate::metadata.begin(),skate::metadata.end(),[&](const auto& m){return m.id==id && (m.categories&24u)!=0;})) {
                        s.phases.begin_continuation();if(s.phases.active_id())s.phase_sequence=s.phases.active_id();
                    }
                }
                if (!association.phase || association.provisional) if (const auto* phase=s.phases.phase_at(association.first)) {
                    // Freeze association across ticker revisions. A delayed flip
                    // can belong to the preceding air segment, never a later one.
                    const auto id=s.catalog.resolve(n.labels[0],true);
                    const bool flip=std::any_of(skate::metadata.begin(),skate::metadata.end(),[&](const auto& m){return m.id==id && (m.categories&2u)!=0;});
                    if (flip && phase->kind!="air") for (const auto& p:s.phases.phases())
                        if (p.kind=="air" && p.segment==phase->segment) {phase=&p;break;}
                    const bool grind=std::any_of(skate::metadata.begin(),skate::metadata.end(),[&](const auto& m){return m.id==id && (m.categories&24u)!=0;});
                    if (grind && phase->kind!="grind") {
                        const trick_logger::MovementPhase* candidate{};
                        for (const auto& p:s.phases.phases()) if (p.kind=="grind") {
                            if (p.begin<=association.first) candidate=&p;else if (!candidate) {candidate=&p;break;}
                        }
                        if (candidate) phase=candidate;
                    }
                    association.provisional=grind && phase->kind!="grind";
                    association.sequence=s.phase_sequence;association.phase=phase->id;association.segment=phase->segment;association.phase_kind=phase->kind;
                }
                if(association.sequence==s.phases.capture_id() && association.phase && !association.provisional && !association.preexisting) {
                    s.phases.retain_from(association.phase);
                    if(association.phase_kind=="air")s.phases.stance(association.segment,
                        trick_logger::starting_stance(n.original[3],n.original[6],s.catalog.resolve(n.labels[0],true)),n.model.handle);
                    s.sets[association.sequence]=s.phases.evidence();
                }
                editor::Observation observation;observation.native_id=s.catalog.resolve(n.labels[0],true);observation.identity_ambiguous=s.catalog.ambiguous(n.labels[0],true);
                observation.sequence=association.sequence;observation.sequence_valid=association.sequence && !association.preexisting && !association.provisional && !s.invalid_sequences.contains(association.sequence);observation.phase=association.phase;observation.segment=association.segment;observation.phase_kind=association.phase_kind;
                observation.handle=n.model.handle;observation.first_seen=association.first;observation.labels=n.labels;
                const auto attempt=association.attempt;
                observation.attempt=attempt?attempt->id:0;observation.valid=attempt && attempt->valid;

                const auto rotation=normalized(n.labels[2]);
                const int degrees=rotation=="180"?180:rotation=="360"?360:rotation=="540"?540:rotation=="720"?720:0;
                const trick_logger::SpinSegment* segment{};bool rotation_valid{};
                if (const auto found=s.sets.find(association.sequence);found!=s.sets.end()) {
                    const auto& set=found->second;
                    rotation_valid=((set.end && set.landed==true) || s.measurement_valid) && !s.invalid_sequences.contains(set.id);
                    const auto span=trick_logger::capture_span(set.phases,set.phases.empty()?0:set.phases.front().id,true);
                    if(span.first && !span.contains(association.phase))observation.sequence_valid=false;
                    if (association.segment && association.segment<=set.segments.size()) {
                        segment=&set.segments[association.segment-1];rotation_valid=rotation_valid && segment->rotation_complete;
                    }
                }
                if (association.sequence) {
                    observation.valid=false;observation.sequence_valid=observation.sequence_valid && rotation_valid;
                }
                if (segment && rotation_valid && !association.preexisting) {
                    observation.valid=true;observation.attempt=(association.sequence<<16)|association.segment;
                    observation.measured_degrees=segment->air+segment->landing;
                    observation.air_degrees=segment->air;observation.recovery_degrees=segment->landing;
                    observation.measured_spin=trick_logger::measured_spin(*segment);
                    observation.spin_valid=degrees && observation.measured_spin==degrees;
                    // Predict only after the game acknowledges a spin. A final
                    // match still needs the exact native count plus measured count.
                    observation.early_degrees=degrees && attempt?
                        editor::early_spin_degrees(segment->air,attempt->last_delta,attempt->airborne,rotation_valid,editor::detection()):0;
                    if ((observation.spin_valid || observation.early_degrees) && segment->goofy) {
                        std::array<std::string,13> labels;std::copy_n(n.labels.begin(),13,labels.begin());
                        observation.direction=measured_body_direction(labels,observation.measured_degrees,*segment->goofy);
                    }
                }
                if (!association.sequence && !attempt && !s.baseline && degrees) {
                    observation.completed=true;observation.attempt=n.parent;observation.valid=true;observation.spin_valid=true;
                    auto dir=normalized(n.labels[13]);
                    if (dir=="bs" || dir=="idtrickbs")observation.direction="BS";
                    if (dir=="fs" || dir=="idtrickfs")observation.direction="FS";
                    if (normalized(n.labels[0])=="ollie" || normalized(n.labels[0])=="bonedollie") {
                        dir=normalized(n.labels[1]);if(dir=="bs" || dir=="fs")observation.direction=dir=="bs"?"BS":"FS";
                    }
                }
                const auto signature=diagnostics::enabled ? Json(n.labels).dump()+observation.direction+std::to_string(observation.early_degrees)+(observation.spin_valid?"valid":"invalid")+std::to_string(observation.measured_spin) : std::string{};
                if (diagnostics::enabled && enabled && s.recorded<4096 && s.observations[n.model.handle]!=signature) {
                    s.observations[n.model.handle]=signature;++s.recorded;
                    if constexpr (diagnostics::enabled) {
                    auto j=Json::object();j["kind"]="terminology_observation";j["model_handle"]=n.model.handle;
                    j["candidate_attempt"]=observation.attempt;j["spin_valid"]=observation.spin_valid;j["native_id"]=observation.native_id;j["phase_id"]=observation.phase;j["phase_kind"]=observation.phase_kind;j["early_spin"]=observation.early_degrees;j["owner_verified"]=false;
                    j["air_yaw"]=observation.air_degrees;j["recovery_yaw"]=observation.recovery_degrees;j["accepted_yaw"]=observation.measured_degrees;j["measured_spin"]=observation.measured_spin;j["native_sequence_ends"]=sequence_ends.load();j["body_direction_candidate"]=observation.direction;
                    j["labels"]=Json::object();j["raw_labels"]=Json::object();
                    constexpr const char* keys[]{"base_name","direction","body_rotation","stance","held_rotation","late","nollie",
                        "body_flip_rotation","body_flip_direction","dark_catch","dark_flip","manual_type","underflip","metadata","completed_info"};
                    for (unsigned i=0;i<fields.size();++i) {j["labels"][keys[i]]=n.labels[i];j["raw_labels"][keys[i]]=n.original[i];}
                    logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
                    }
                }
                native.push_back(std::move(n));frame.push_back(std::move(observation));
            } catch (const std::exception& e) {
                ++failures;if (s.error_records++<8) logging::write(logging::Level::warning,logging::Channel::runtime,e.what());
            }
        }
        std::set<std::uint64_t> known_sequences;
        for(const auto& [id,set]:s.sets)if(id==s.phase_sequence || std::any_of(frame.begin(),frame.end(),[&](const auto& row){return row.sequence==id;}))known_sequences.insert(id);
        frame=s.history.merge(frame,known_sequences,s.invalid_sequences);
        auto rules=editor::rules();
        for (auto& rule:rules) for (auto* part:{&rule.first,&rule.second})
            if (part->native_id.empty()) part->native_id=s.catalog.resolve(part->base,true);
        for (auto& rule:rules) for (auto& part:rule.extra) if (part.native_id.empty())part.native_id=s.catalog.resolve(part.base,true);
        std::vector<editor::Rule> singles,combos;
        for(const auto& rule:rules)(editor::combo_row(rule)?combos:singles).push_back(rule);
        auto matches=editor::resolve_frame(combos,frame);
        auto replacements=editor::resolve_frame(singles,frame);
        editor::retain_combos(singles,frame,replacements,confirmed_singles);
        editor::retain_combos(combos,frame,matches,confirmed_combos);
        std::vector<editor::Labels> wanted;for (const auto& n:native) wanted.push_back(n.original);
        std::vector<bool> aliasing(native.size(),false);
        std::vector<combo_rows::Request> combo_requests;
        if(enabled)for(unsigned i=0;i<native.size();++i)if(replacements[i].rule) {
            wanted[i]=editor::display(*replacements[i].rule,native[i].original);aliasing[i]=true;
        }
        if (enabled) for (unsigned i=0;i<matches.size();++i) {
            const auto& m=matches[i];if (m.conflict) ++conflicts;if (!m.rule) continue;
            ++matched;
            std::vector<editor::Labels> originals;for(const auto& row:native)originals.push_back(row.original);
            const auto patches=editor::render_match(*m.rule,m,i,frame,originals,rules);
            if(editor::combo_row(*m.rule)) {
                const auto parts=editor::actions(*m.rule);const auto selected=editor::handles(m,frame[i].handle);
                unsigned anchor{};while(anchor<parts.size() && editor::spin_only(parts[anchor]))++anchor;
                if(anchor<selected.size()) {
                    // Use a live row as a visual template even when the earlier
                    // anchor action has already scrolled out of native history.
                    auto source=native.end();
                    for(auto handle:selected)if(const auto found=std::find_if(native.begin(),native.end(),[&](const NativeRow& n){return n.model.handle==handle;});found!=native.end())source=found;
                    const auto shown=patches.find(selected[anchor]);
                    if(source!=native.end() && shown!=patches.end() && !shown->second[0].empty()) {
                        combo_requests.push_back({source->model,source->parent,source->model.handle,m.rule->id,shown->second[0],selected,editor::encode(*m.rule).dump()});
                    }
                }
                continue; // The performed native trick retains its own row and score.
            }
            for(unsigned j=0;j<native.size();++j)if(const auto patch=patches.find(frame[j].handle);patch!=patches.end()) {
                wanted[j]=patch->second;aliasing[j]=true;
            }
        }
        if(enabled)for(unsigned i=0;i<native.size();++i) {
            const auto observation=std::find_if(frame.begin(),frame.end(),[&](const auto& value){return value.handle==native[i].model.handle;});
            if(observation==frame.end() || aliasing[i] || wanted[i][2]!=native[i].original[2])continue;
            const auto corrected=no_comply_display_spin(observation->native_id,native[i].labels[2],observation->measured_spin,observation->valid);
            if(corrected!=native[i].labels[2]){wanted[i][2]=corrected;aliasing[i]=true;}
        }
        std::vector<binding_probe::Identity> binding_identities;
        binding_identities.reserve(native.size()*30);
        for(unsigned i=0;i<native.size();++i)for(unsigned field=0;field<15;++field) {
            const auto rule=replacements[i].rule?replacements[i].rule->id:0;
            binding_identities.push_back({native[i].wrappers[field],native[i].model.handle,rule,field});
            binding_identities.push_back({native[i].targets[field].handle,native[i].model.handle,rule,field});
        }
        if(enabled)binding_probe::publish(std::move(binding_identities),now);
        for (unsigned i=0;i<native.size();++i) {
            auto& n=native[i];auto& row=s.rows.at(n.model.handle);
            for (unsigned slot=0;slot<fields.size();++slot) if (wanted[i][slot]!=n.raw[slot]) {
                if(incoming_text && incoming_text->target==n.targets[slot].handle) {
                    incoming_text->replacement=wanted[i][slot];incoming_text->handled=true;
                } else c.text(n.targets[slot],wanted[i][slot]);
                if (aliasing[i]) row.edits[slot].applied(n.original[slot],wanted[i][slot]);else row.edits[slot].active=false;
                if (slot==0) {
                    if constexpr (diagnostics::enabled) {
                    auto j=Json::object();j["kind"]="terminology_publication";j["model_handle"]=n.model.handle;
                    j["before"]=n.raw[slot];j["requested"]=wanted[i][slot];j["readback"]=c.text(n.targets[slot],1024);
                    j["at_ui_boundary"]=in_unlock;j["rule_id"]=replacements[i].rule?replacements[i].rule->id:0;
                    logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
                    }
                }
            }
        }
        static unsigned row_events{};
        std::map<std::uint64_t,std::string> active_combo_rules;
        if(enabled)for(const auto& rule:combos)if(rule.enabled)active_combo_rules.emplace(rule.id,editor::encode(rule).dump());
        // Structural history edits only at the ordinary client tick, after the
        // native scoring writer/unlock scripts have completed. Text interception
        // remains early; it must never insert/remove stack items recursively.
        if(!in_unlock && !incoming_text && !profile_runtime::executing_expression)for(const auto& event:row_presenter().service(c,combo_requests,active_combo_rules))if(row_events++<128 || event.action=="inserted") {
            if constexpr (diagnostics::enabled) {
            auto j=Json::object();j["kind"]="terminology_combo_row";j["action"]=event.action;j["message"]=event.message;
            j["source_handle"]=event.source;j["parent_handle"]=event.parent;j["custom_handle"]=event.row;j["rule_id"]=event.rule;
            j["ui_only"]=true;j["score_value"]=0;
            logging::write(logging::Level::info,logging::Channel::runtime,j.dump());
            }
        }
        s.known_fields=std::move(current_fields);s.baseline=false;
        for (auto it=s.associations.begin();it!=s.associations.end();) if (!s.rows.contains(it->first)) it=s.associations.erase(it);else ++it;
        for (auto it=s.observations.begin();it!=s.observations.end();) if (!s.rows.contains(it->first)) it=s.observations.erase(it);else ++it;
        s.status=std::format("{}; {} rules, {} combo matches, {} conflicts, {} read failures",
            enabled?(boundary_ready?"Editor / native UI boundary":"Editor / polling fallback"):"Off",rules.size(),matched,conflicts,failures);
        if(!combo_requests.empty() && profile_runtime::executing_expression)s.status+="; structural writes deferred until native script completes";
        s.status+="; "+std::to_string(combo_requests.size())+" native row requests; "+std::to_string(frame.size())+" retained actions";
        editor::observe(frame,s.status,boundary_events.load());return s.status;
    } catch (const std::exception& e) {
        if (s.error_records++<8) logging::write(logging::Level::warning,logging::Channel::runtime,e.what());
        return s.status="Unavailable: native trick UI could not be read";
    } catch (...) {return s.status="Unavailable: native trick UI could not be read";}
}
} // namespace dingosdk::trick_terminology
