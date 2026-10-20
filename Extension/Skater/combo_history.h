#pragma once
#include "trick_rules.h"
#include <map>
#include <set>

namespace dingosdk::trick_terminology::editor {
// Physical observations outlive their animated ticker widgets. Never join
// different sequence ids or retain evidence across an invalidated attempt.
class ComboHistory {
    std::map<std::uint64_t,Observation> rows_;
public:
    void clear(){rows_.clear();}
    std::vector<Observation> merge(const std::vector<Observation>& live,const std::set<std::uint64_t>& sequences,
                                   const std::set<std::uint64_t>& invalid) {
        for(auto it=rows_.begin();it!=rows_.end();) {
            if(!sequences.contains(it->second.sequence) || invalid.contains(it->second.sequence))it=rows_.erase(it);else ++it;
        }
        for(const auto& row:live) {
            rows_.erase(row.handle);
            if(row.sequence_valid && sequences.contains(row.sequence) && !invalid.contains(row.sequence))rows_[row.handle]=row;
        }
        while(rows_.size()>256) {
            const auto oldest=std::min_element(rows_.begin(),rows_.end(),[](const auto& a,const auto& b){return a.second.first_seen<b.second.first_seen;});
            rows_.erase(oldest);
        }
        auto out=live;
        for(const auto& [handle,row]:rows_)if(std::none_of(live.begin(),live.end(),[&](const auto& r){return r.handle==handle;}))out.push_back(row);
        return out;
    }
};
}
