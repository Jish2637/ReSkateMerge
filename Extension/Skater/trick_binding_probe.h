#pragma once
// Test15 observation only. No engine calls, model writes or disk IO in callbacks.
#include "Engine/Core/Platform/memory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace dingosdk::trick_terminology::binding_probe {
using Address=std::uintptr_t;
inline constexpr std::uint32_t graph=0x4e911fe8;
inline constexpr std::array<std::uint32_t,10> layout{192,584,248,717,15,0,5,983073,3211264,0};
struct Identity { std::uint64_t context{},row{},rule{};unsigned field{}; };
struct Snapshot {std::uint64_t generation{},at{};std::vector<Identity> identities;};
inline std::atomic<std::shared_ptr<const Snapshot>> snapshot;
inline std::atomic<bool> enabled{};
inline std::atomic<std::uint64_t> dropped{},recognized{},rejected{},emitted{},tick_serial{};
struct Text { std::array<char,192> bytes{};bool valid{}; };
struct Record {
    std::uint64_t start{},finish{},start_ms{},finish_ms{},tick{},generation{},snapshot_at{},context{},row{},rule{};
    Address vm{},frame{},output_storage{};
    std::uint32_t pc{},thread{};unsigned field{};
    bool matched{},stable{},profiled{};
    Text input,override_text,output,localized;
};
struct Capture {Record record;Address instance{},resource{},context_storage{};};
inline thread_local std::array<Capture,8> captures;
inline thread_local unsigned depth{};
inline std::mutex queue_mutex;
inline std::array<Record,256> queue;
inline unsigned head{},count{};
inline std::uint64_t clock_ticks() noexcept {LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;}
inline void read_text(Address storage,Text& out) noexcept {
    out={};Address chars{};
    if (!memory::peek(storage,chars))return;
    if (!chars) {out.valid=true;return;}
    out.valid=memory::peek_cstring(chars,out.bytes.data(),out.bytes.size())>=0;
    if (!out.valid)out.bytes={};
}
inline bool parameter(Address frame,unsigned offset,Address& storage) noexcept {
    return frame<=memory::highest_user_address-offset && memory::peek(frame+offset,storage) && storage>=0x10000;
}
inline bool frame_for(Address vm,Address& resource,Address& instance,Address& frame) noexcept {
    if(vm<0x10000 || vm>memory::highest_user_address-0x40)return false;
    Address check{},registers{};std::uint32_t key{};std::array<std::uint32_t,10> actual{};
    if(!memory::peek(vm+0x38,resource) || resource<0x10000 || resource>memory::highest_user_address-0x48 ||
       !memory::peek(resource+0x10,key) || key!=graph || !memory::peek(resource+0x20,actual) || actual!=layout ||
       !memory::peek(vm+0x30,instance) || !memory::peek(instance,check) || check!=resource ||
       instance>memory::highest_user_address-192-16)return false;
    registers=instance+192;
    return memory::peek(registers+8,frame) && frame>=0x10000 && frame<=memory::highest_user_address-0x90;
}
inline void* begin(Address vm,std::uint32_t pc,bool profiled) noexcept {
    if(!enabled.load(std::memory_order_relaxed) || emitted.load(std::memory_order_relaxed)>=4096)return nullptr;
    const DWORD error=GetLastError();
    Address resource{},instance{},frame{};
    if(!frame_for(vm,resource,instance,frame)) {SetLastError(error);return nullptr;}
    ++recognized;
    if(depth==captures.size()) {++dropped;SetLastError(error);return nullptr;}
    auto& c=captures[depth];c={};auto& r=c.record;
    r.vm=vm;r.pc=pc;r.frame=frame;r.profiled=profiled;r.thread=GetCurrentThreadId();
    c.resource=resource;c.instance=instance;
    Address input{},over{};
    if(!parameter(frame,0x58,c.context_storage) || !memory::peek(c.context_storage,r.context) ||
       !parameter(frame,0x38,input) || !parameter(frame,0x30,over) || !parameter(frame,0x28,r.output_storage)) {
        ++rejected;SetLastError(error);return nullptr;
    }
    r.start_ms=GetTickCount64();r.start=clock_ticks();r.tick=tick_serial.load();
    read_text(input,r.input);read_text(over,r.override_text);
    if(const auto view=snapshot.load(std::memory_order_acquire)) {
        r.generation=view->generation;r.snapshot_at=view->at;
        const auto it=std::lower_bound(view->identities.begin(),view->identities.end(),r.context,
            [](const Identity& item,std::uint64_t key){return item.context<key;});
        if(it!=view->identities.end() && it->context==r.context && r.start_ms>=view->at && r.start_ms-view->at<=250) {
            r.matched=true;r.row=it->row;r.field=it->field;r.rule=it->rule;
        }
    }
    if(!r.matched){SetLastError(error);return nullptr;}
    ++depth;SetLastError(error);return &c;
}
inline void end(void* token) noexcept {
    if(!token)return;
    const DWORD error=GetLastError();
    if(!depth || token!=&captures[depth-1]) {++rejected;SetLastError(error);return;}
    auto& c=captures[--depth];auto& r=c.record;
    r.finish=clock_ticks();r.finish_ms=GetTickCount64();
    Address resource{},instance{},frame{},output{},context{},context_storage{},localized{};
    r.stable=frame_for(r.vm,resource,instance,frame) && resource==c.resource && instance==c.instance && frame==r.frame &&
        parameter(frame,0x28,output) && output==r.output_storage && parameter(frame,0x58,context_storage) &&
        context_storage==c.context_storage && memory::peek(context_storage,context) && context==r.context;
    if(r.stable) {
        read_text(output,r.output);
        if(parameter(frame,0x40,localized))read_text(localized,r.localized);
    }
    // Retain changed labels and a 250 ms heartbeat for unchanged labels. No allocation.
    struct Seen {std::uint64_t context{},signature{},at{};};
    static thread_local std::array<Seen,128> seen{};
    static thread_local unsigned next{};
    std::uint64_t hash=1469598103934665603ULL;
    for(const auto* text:{&r.input,&r.override_text,&r.output,&r.localized}) {
        hash=(hash^static_cast<std::uint64_t>(text->valid))*1099511628211ULL;
        for(unsigned char ch:text->bytes)hash=(hash^ch)*1099511628211ULL;
    }
    for(const auto value:{static_cast<std::uint64_t>(r.stable),static_cast<std::uint64_t>(r.matched),r.rule,r.row,static_cast<std::uint64_t>(r.field)})
        hash=(hash^value)*1099511628211ULL;
    for(auto& item:seen)if(item.context==r.context && item.signature==hash && r.finish_ms-item.at<250) {
        SetLastError(error);return;
    }
    seen[next++%seen.size()]={r.context,hash,r.finish_ms};
    // Unmatched samples expose mapped context IDs; reserve most of the budget for known rows.
    static std::atomic<unsigned> unknown{};
    if(!r.matched && unknown.fetch_add(1)>=1024) {SetLastError(error);return;}
    std::unique_lock lock(queue_mutex,std::try_to_lock);
    if(!lock || count==queue.size())++dropped;
    else {queue[(head+count)%queue.size()]=r;++count;++emitted;}
    SetLastError(error);
}
inline bool pop(Record& out) noexcept {
    std::unique_lock lock(queue_mutex,std::try_to_lock);
    if(!lock || !count)return false;
    out=queue[head];head=(head+1)%queue.size();--count;return true;
}
inline void publish(std::vector<Identity> identities,std::uint64_t now) {
    std::sort(identities.begin(),identities.end(),[](const Identity& a,const Identity& b){return a.context<b.context;});
    auto view=std::make_shared<Snapshot>();view->generation=++tick_serial;view->at=now;view->identities=std::move(identities);
    snapshot.store(std::move(view),std::memory_order_release);
}
}
