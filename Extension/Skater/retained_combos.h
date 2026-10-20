#pragma once
#include "trick_rules.h"
#include <map>
#include <cmath>
#include <set>

namespace dingosdk::trick_terminology::editor {
// Keep a confirmed result through further rotation, but never create a match
// from an unconfirmed angle. Fresh exact matches always take precedence.
struct ConfirmedCombo {
    Rule rule;
    std::vector<Observation> evidence;
};
using ConfirmedCombos=std::map<std::uint64_t,ConfirmedCombo>;
inline void retain_combos(const std::vector<Rule>& rules,const std::vector<Observation>& frame,
                          std::vector<Match>& matches,ConfirmedCombos& cache) {
    std::set<std::uint64_t> claimed;
    for(unsigned i=0;i<matches.size();++i)if(matches[i].rule) {
        for(auto handle:claimed_handles(matches[i],frame[i].handle,frame))claimed.insert(handle);
        if(matches[i].rule->combo && !matches[i].conflict) {
            ConfirmedCombo saved{*matches[i].rule,{}};
            for(auto handle:handles(matches[i],frame[i].handle)) {
                const auto row=std::find_if(frame.begin(),frame.end(),[&](const auto& o){return o.handle==handle;});
                if(row!=frame.end())saved.evidence.push_back(*row);
            }
            cache[frame[i].handle]=std::move(saved);
        }
    }
    for(auto it=cache.begin();it!=cache.end();) {
        const auto& saved=it->second;
        const auto source=std::find_if(frame.begin(),frame.end(),[&](const auto& o){return o.handle==it->first;});
        const auto rule=std::find_if(rules.begin(),rules.end(),[&](const auto& r){return r.id==saved.rule.id && r.enabled && encode(r)==encode(saved.rule);});
        if(source==frame.end() || rule==rules.end()) {it=cache.erase(it);continue;}
        const auto index=static_cast<unsigned>(source-frame.begin());
        if(matches[index].rule || matches[index].conflict) {++it;continue;}
        auto candidate=frame;bool valid=!saved.evidence.empty(),advanced{};
        for(const auto& old:saved.evidence) {
            auto row=std::find_if(candidate.begin(),candidate.end(),[&](const auto& o){return o.handle==old.handle;});
            if(row==candidate.end() || claimed.contains(old.handle) || !row->valid || row->identity_ambiguous ||
               row->attempt!=old.attempt || row->sequence!=old.sequence || row->phase!=old.phase ||
               row->native_id!=old.native_id || row->direction!=old.direction ||
               (old.sequence_valid && !row->sequence_valid)) {valid=false;break;}
            for(unsigned slot=0;slot<13;++slot)if(slot!=2 && row->labels[slot]!=old.labels[slot])valid=false;
            const auto before=rotation_number(normalized(old.labels[2])),after=rotation_number(normalized(row->labels[2]));
            if(before) {
                if(!old.spin_valid || !after || after<before || std::abs(row->measured_degrees)<std::abs(old.measured_degrees))valid=false;
                advanced=advanced || after>before || std::abs(row->measured_degrees)>std::abs(old.measured_degrees);
                row->labels[2]=old.labels[2];row->spin_valid=old.spin_valid;row->early_degrees=0;
            } else if(row->labels[2]!=old.labels[2])valid=false;
        }
        if(valid && advanced) {
            const std::vector<Rule> single{*rule};auto restored=resolve_frame(single,candidate);
            if(restored[index].rule && !restored[index].conflict) {
                auto match=restored[index];match.rule=&*rule;
                const auto selected=handles(match,source->handle);
                if(selected.size()==saved.evidence.size() && std::equal(selected.begin(),selected.end(),saved.evidence.begin(),[](auto h,const auto& o){return h==o.handle;})) {
                    matches[index]=std::move(match);
                    for(auto handle:claimed_handles(matches[index],source->handle,frame))claimed.insert(handle);
                }
            }
        }
        ++it;
    }
}
}
