#pragma once
#include "trick_rules.h"
#include "trick_metadata.h"
#include "trick_editor.h"
#include "trick_name_text.h"
#include <set>

namespace dingosdk::trick_terminology::editor {
// Presentation-only adapter for completed native S.K.A.T.E. text. It consumes
// whole recognized phrases; never substring-replaces player names or prompts.
inline std::string joined(const std::vector<std::string>& tokens) {
    std::string text;for (const auto& token:tokens) if (!token.empty()) {if (!text.empty())text+=' ';text+=token;}return text;
}
inline std::string completed_output(const Labels& labels,const std::string& body_direction={}) {
    return joined({labels[3],labels[6],body_direction,labels[2],labels[1],labels[4],labels[5],labels[0],labels[12],labels[9],labels[10],labels[8],labels[7],labels[11]});
}
inline std::vector<Observation> completed_candidates(const std::string& text,const std::vector<Rule>& rules,const std::vector<Component>& catalogue={}) {
    std::vector<Component> components=catalogue;
    for (const auto& rule:rules) {
        if(rule.rotation_multiples)for(const auto& held:multiple_labels(rule.first)) {auto part=rule.first;part.held=held;components.push_back(std::move(part));}
        components.push_back(rule.first);const auto parts=following(rule);components.insert(components.end(),parts.begin(),parts.end());}
    // Read candidate native degree tokens rather than imposing an enumeration
    // limit. Final matching still requires an exact whole native phrase.
    for(std::size_t at=0;at<text.size();) {
        if(text[at]<'0' || text[at]>'9') {++at;continue;}
        const auto begin=at;while(at<text.size() && text[at]>='0' && text[at]<='9')++at;
        const auto held=text.substr(begin,at-begin);
        for(const auto& rule:rules)if(rule.rotation_multiples && held_multiple(rule.first,held)) {
            auto part=rule.first;part.held=held;components.push_back(std::move(part));
        }
    }
    std::vector<Observation> out;std::set<std::string> seen;
    const auto wanted=normalized(trick_names::plain_text(text));
    // Enumerate only fully specified native phrases. In particular, do not infer
    // a body direction from a board/grind direction when only one is supplied.
    for (const auto& component:components) for (int stance=0;stance<4;++stance)
    for (int body:{0,180,360,540,720}) for (const auto* body_direction:{"","BS","FS"}) {
        if(spin_only(component) || wanted.find(normalized(component.base))==std::string::npos)continue;
        if (!body && *body_direction)continue;
        for (const auto* board:{"","BS","FS"}) {
            const auto base=normalized(component.base);
            bool directional=base=="popshuvit" || !component.board.empty();
            for (const auto& m:dingosdk::skate::metadata)if(m.id==component.native_id && (m.categories&24u)) directional=true;
            if (*board && !directional)continue;
            Observation row;row.completed=true;row.completed_chain=true;row.valid=row.sequence_valid=true;row.attempt=row.sequence=1;
            row.labels[0]=component.base;row.labels[4]=component.held;row.labels[1]=board;
            row.labels[3]=stance?stance_name(stance):"";row.labels[2]=body?std::to_string(body):"";
            row.direction=body_direction;row.spin_valid=body!=0;row.native_id=component.native_id;
            for (unsigned i=0;i<modifier_slots.size();++i)row.labels[modifier_slots[i]]=component.modifiers[i];
            const auto prefix=row.labels[3],rotation=row.labels[2];
            const auto suffix=joined({row.labels[12],row.labels[9],row.labels[10],row.labels[8],row.labels[7],row.labels[11]});
            const auto phrase=joined({board,component.held,row.labels[5],component.base,suffix});
            // Native UI variants: body first or after the native trick phrase.
            const std::array<std::string,3> variants{
                joined({prefix,body_direction,rotation,phrase}),
                joined({prefix,phrase,body_direction,rotation}),
                joined({prefix,board,rotation,component.held,row.labels[5],component.base,suffix})};
            bool matches=false;
            for (unsigned v=0;v<variants.size();++v) {
                // Third form has no independent body direction; preserve unknown.
                if (v==2 && *body_direction)continue;
                if (normalized(variants[v])==wanted)matches=true;
            }
            if (!matches)continue;
            const auto key=Json(row.labels).dump()+row.direction;
            if (seen.insert(key).second)out.push_back(std::move(row));
        }
    }
    return out;
}
inline std::string simple_completed_output(const Rule& rule,const std::string& text,const Labels& labels,const std::string& direction) {
    auto source=text;const auto prefix=output_stance(labels);
    if ((!rule.stance_prefix || has_output_stance(rule.name,prefix)) && !prefix.empty()) {
        if (has_output_stance(source,prefix)) {
            source.erase(0,prefix.size());const auto first=source.find_first_not_of(' ');
            source=first==std::string::npos?"":source.substr(first);
        } else return completed_output(display(rule,labels),direction);
    }
    const auto phrase=joined({rule.rotation_multiples?labels[4]:rule.first.held,rule.first.base});
    const auto lower=[](std::string value) {for(auto& ch:value)if(ch>='A' && ch<='Z')ch=static_cast<char>(ch-'A'+'a');return value;};
    const auto haystack=lower(source),needle=lower(phrase);const auto at=haystack.find(needle);
    if (!needle.empty() && at!=std::string::npos && (at==0 || haystack[at-1]==' ') &&
        (at+needle.size()==source.size() || haystack[at+needle.size()]==' ') && haystack.find(needle,at+1)==std::string::npos) {
        auto result=source;result.replace(at,phrase.size(),multiple_output_name(rule,labels));return result;
    }
    return completed_output(display(rule,labels),direction);
}
inline std::string replace_completed_text(const std::string& text,const std::vector<Rule>& rules,const std::vector<Component>& catalogue={}) {
    if (text.empty() || text.size()>2048 || rules.empty())return text;
    std::vector<std::string> pieces,separators;std::size_t start{};
    while (start<text.size()) {
        const auto plus=text.find(" + ",start),bar=text.find(" | ",start);
        const auto end=std::min(plus,bar);
        pieces.push_back(text.substr(start,end==std::string::npos?end:end-start));
        if (end==std::string::npos)break;
        separators.push_back(text.substr(end,3));start=end+3;
        if (pieces.size()>6)return text;
    }
    if (pieces.empty())return text;
    std::vector<std::vector<Observation>> choices;
    for (const auto& piece:pieces) {auto options=completed_candidates(piece,rules,catalogue);if (options.empty()) {Observation unknown;unknown.labels[0]=piece;options.push_back(unknown);}choices.push_back(std::move(options));}
    // A bounded Cartesian search handles native 360/base ambiguity. Every
    // interpretation must agree before text is substituted.
    unsigned combinations=1;for (const auto& c:choices) {if(c.size()>64 || combinations>128/c.size())return text;combinations*=static_cast<unsigned>(c.size());}
    std::set<std::string> outputs;
    const auto walk=[&](auto&& self,std::size_t index,std::vector<Observation>& frame)->void {
        if (outputs.size()>1)return;
        if (index<choices.size()) {
            for (auto row:choices[index]) {
                row.handle=index+1;row.first_seen=index+1;row.phase=1;
                for (unsigned i=0;i<index;++i)if(separators[i]==" | ")++row.phase;
                row.segment=row.phase;row.phase_kind="completed";
                frame.push_back(std::move(row));self(self,index+1,frame);frame.pop_back();
            }
            return;
        }
        const auto resolved=resolve_frame(rules,frame);auto shown=pieces;std::vector<bool> consumed(frame.size());
        for (unsigned i=0;i<resolved.size();++i) if (resolved[i].rule) {
            if(!resolved[i].rule->combo) {shown[i]=simple_completed_output(*resolved[i].rule,pieces[i],frame[i].labels,frame[i].direction);continue;}
            const auto patches=render_match(*resolved[i].rule,resolved[i],i,frame,{},rules);
            for(unsigned j=0;j<frame.size();++j)if(const auto patch=patches.find(frame[j].handle);patch!=patches.end()) {
                if(patch->second[0].empty())consumed[j]=true;
                else shown[j]=completed_output(patch->second,patch->second[2].empty()?"":frame[j].direction);
            }
        }
        std::string output;for(unsigned i=0;i<shown.size();++i)if(!consumed[i]) {if(!output.empty())output+=i?separators[i-1]:" + ";output+=shown[i];}
        outputs.insert(output);
    };
    std::vector<Observation> frame;walk(walk,0,frame);
    return outputs.size()==1?*outputs.begin():text;
}
} // namespace dingosdk::trick_terminology::editor

namespace dingosdk::trick_terminology::editor {
inline std::string local_completed_name(const std::string& text) {
    const auto snapshot=view();if (!snapshot->terminology_enabled || snapshot->rules.empty())return text;
    struct Cache {std::uint64_t revision{~std::uint64_t{}};std::map<std::string,std::string> values;std::vector<Rule> rules;std::vector<Component> catalogue;};
    thread_local Cache cache;
    if (cache.revision!=snapshot->revision || cache.rules.empty()) {
        cache.revision=snapshot->revision;cache.values.clear();cache.rules=snapshot->rules;cache.catalogue.clear();
        for(const auto& [id,label]:snapshot->native_names)for(const auto& meta:skate::metadata)if(meta.id==id && (meta.categories&6u) && !(meta.categories&24u)) {
            Component part;part.base=label;part.native_id=id;if(id=="ID_TRICK_FLIP")part.held="360";cache.catalogue.push_back(part);
            if(id=="ID_TRICK_HARDFLIP" || id=="ID_TRICK_INWARDHEELFIP" || id=="ID_TRICK_POPSHUVIT") {part.held="360";cache.catalogue.push_back(part);}
        }
        for (auto& rule:cache.rules) {
            const auto resolve=[&](Component& part) {
                if (part.native_id.empty() && !part.base.empty()) {
                    std::string found;bool ambiguous{};
                    for(const auto& [id,label]:snapshot->native_names)if(normalized(label)==normalized(part.base)) {
                        if(!found.empty())ambiguous=true;found=id;
                    }
                    if(!ambiguous)part.native_id=found;
                }
                if (const auto it=snapshot->native_names.find(part.native_id);it!=snapshot->native_names.end())part.base=it->second;};
            resolve(rule.first);resolve(rule.second);for(auto& part:rule.extra)resolve(part);
        }
    }
    if (const auto it=cache.values.find(text);it!=cache.values.end())return it->second;
    const auto result=replace_completed_text(text,cache.rules,cache.catalogue);
    if (cache.values.size()>=128)cache.values.clear();cache.values.emplace(text,result);return result;
}
}
