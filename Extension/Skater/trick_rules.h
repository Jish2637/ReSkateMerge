#pragma once
#include "terminology_spin.h"
#include "Engine/Core/Json/json.h"
#include <vector>
#include <bit>
#include <charconv>
#include "trick_metadata.h"
#include <algorithm>
#include <stdexcept>

namespace dingosdk::trick_terminology::editor {
using Labels=std::array<std::string,15>;
inline constexpr std::array<unsigned,7> modifier_slots{5,7,8,9,10,11,12};
struct DetectionSettings {
    bool early_spin{};
    int lead_degrees{15};
};
inline Json encode_detection(const DetectionSettings& value) {
    auto j=Json::object();j["early_spin"]=value.early_spin;j["lead_degrees"]=value.lead_degrees;return j;
}
inline DetectionSettings decode_detection(const Json& document) {
    DetectionSettings value;
    if (!document.contains("detection")) return value;
    const auto& j=document.at("detection");
    value.early_spin=j.value("early_spin",false);value.lead_degrees=j.value("lead_degrees",15);
    if (value.lead_degrees<0 || value.lead_degrees>45) throw std::runtime_error("Spin lead must be 0 to 45 degrees.");
    return value;
}
inline int early_spin_degrees(double net,double last_delta,bool airborne,bool valid,const DetectionSettings& settings) {
    if (!settings.early_spin || !airborne || !valid || !std::isfinite(net) || !std::isfinite(last_delta) || net*last_delta<0) return 0;
    const int degrees=static_cast<int>(std::floor((std::abs(net)+settings.lead_degrees)/180))*180;
    return degrees>=180 && degrees<=720 && std::abs(std::abs(net)-degrees)<=45 ? degrees:0;
}
struct Component {
    std::string base, held, board;
    std::array<std::string,7> modifiers{};
    std::string native_id;
    int body_spin{};std::string body_direction{"Either"};
    bool next_phase{};
};
struct Rule {
    std::uint64_t id{};
    bool enabled{true}, combo{};
    bool stance_prefix{true}, rotation_multiples{};
    int stance{}; // 0..3 single, -1 Any, -2 multi-select
    unsigned stance_mask{}; // multi: Base=1, Nollie=2, Fakie=4, Switch=8
    Component first, second;
    std::vector<Component> extra;bool sequence{};
    int spin{};
    std::string direction{"Either"}, name;
};
struct Observation {
    std::uint64_t handle{}, attempt{}, first_seen{};
    Labels labels{};
    std::string direction;
    bool valid{}, spin_valid{}, identity_ambiguous{};
    std::string native_id, phase_kind;
    std::uint64_t phase{}, segment{};
    int early_degrees{};
    std::uint64_t sequence{};bool sequence_valid{}, completed{}, completed_chain{};
    double measured_degrees{}, air_degrees{}, recovery_degrees{};
    int measured_spin{};
};
struct Match {
    const Rule* rule{};
    std::uint64_t second{};
    bool conflict{};
    std::vector<std::uint64_t> following;
};
inline unsigned takeoff_mask(const Rule& r) {
    if (r.stance==-1) return 15;
    if (r.stance==-2) return r.stance_mask & 15;
    return r.stance>=0 && r.stance<4 ? 1u<<static_cast<unsigned>(r.stance) : 0;
}
inline bool accepts_stance(const Rule& r,int actual) {
    return actual>=0 && actual<4 && (takeoff_mask(r)&(1u<<static_cast<unsigned>(actual)))!=0;
}
inline std::vector<Component> following(const Rule& r) {
    std::vector<Component> parts;if (!r.second.base.empty()) parts.push_back(r.second);
    parts.insert(parts.end(),r.extra.begin(),r.extra.end());return parts;
}
inline bool spin_only(const Component& part) {return part.base=="*";}
inline std::vector<Component> actions(const Rule& rule) {auto out=following(rule);out.insert(out.begin(),rule.first);return out;}
inline bool has_spin_only(const Rule& rule) {const auto parts=actions(rule);return std::any_of(parts.begin(),parts.end(),[](const auto& p){return spin_only(p);});}
inline bool air_action(const Observation& row) {
    if(row.phase_kind=="air")return true;
    if(row.completed && row.phase_kind=="completed")for(const auto& m:skate::metadata)
        if(m.id==row.native_id)return (m.categories&6u)!=0 && (m.categories&24u)==0;
    return false;
}
inline std::vector<std::uint64_t> handles(const Match& match,std::uint64_t first) {
    std::vector<std::uint64_t> out{first};out.insert(out.end(),match.following.begin(),match.following.end());return out;
}
inline bool spin_matches(int count,const std::string& direction,const Observation& row) {
    if(!count)return true;
    const auto native=normalized(row.labels[2]);
    // Early recognition may lead native acknowledgement, never undercut a
    // larger count that the game has already reported.
    const int acknowledged=native=="180"?180:native=="360"?360:native=="540"?540:native=="720"?720:0;
    return ((native==std::to_string(count) && row.spin_valid) ||
            (row.early_degrees==count && acknowledged && acknowledged<=count)) &&
           (direction=="Either" || direction==row.direction);
}
inline bool spin_representative(const Component& part,const Observation& row,int count,const std::string& direction,const std::vector<Observation>& frame) {
    if(!spin_only(part))return true;
    if(!air_action(row) || !row.phase || !row.sequence_valid || !spin_matches(count,direction,row))return false;
    for(const auto& other:frame)if(other.handle!=row.handle && other.sequence==row.sequence && other.phase==row.phase &&
        other.sequence_valid && air_action(other) && !other.identity_ambiguous && spin_matches(count,direction,other) &&
        (other.first_seen<row.first_seen || (other.first_seen==row.first_seen && other.handle<row.handle)))return false;
    return true;
}
inline std::vector<std::uint64_t> claimed_handles(const Match& match,std::uint64_t first,const std::vector<Observation>& frame) {
    auto out=handles(match,first);if(!match.rule || !has_spin_only(*match.rule))return out;
    const auto parts=actions(*match.rule);const auto selected=out;
    for(unsigned i=0;i<parts.size();++i)if(spin_only(parts[i])) {
        const auto found=std::find_if(frame.begin(),frame.end(),[&](const auto& row){return row.handle==selected[i];});
        if(found==frame.end())continue;
        for(const auto& row:frame)if(row.sequence==found->sequence && row.phase==found->phase && air_action(row) &&
            std::find(out.begin(),out.end(),row.handle)==out.end())out.push_back(row.handle);
    }
    return out;
}
inline std::uint64_t rotation_number(std::string_view text) {
    std::uint64_t value{};if(text.empty())return 0;
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return result.ec==std::errc{} && result.ptr==text.data()+text.size()?value:0;
}
inline const char* multiple_word(unsigned factor) {
    constexpr const char* words[]{"","","Double","Triple","Quad","Quintuple","Sextuple","Septuple","Octuple","Endless"};
    return words[std::min(factor,9u)];
}
inline unsigned named_multiple(std::string_view text) {
    const auto key=normalized(text);if(key=="single")return 1;
    if(key=="quadruple")return 4;
    for(unsigned i=2;i<=9;++i)if(key==normalized(multiple_word(i)))return i;
    return 0;
}
inline unsigned held_multiple(const Component& base,std::string_view actual) {
    if(normalized(base.held)==normalized(actual))return 1;
    const auto named=named_multiple(actual),unit_named=base.held.empty()?1:named_multiple(base.held);
    if(named==9)return 9; // Native Endless is a terminal category, not an exact angle.
    const auto unit=rotation_number(base.held),value=rotation_number(actual);
    if(unit && value && value%unit==0)return static_cast<unsigned>(std::min<std::uint64_t>(value/unit,9));
    if(unit && named)return named;
    if(unit_named && named && named%unit_named==0)return named/unit_named;
    return 0;
}
inline std::vector<std::string> multiple_labels(const Component& base) {
    std::vector<std::string> values{base.held};const auto unit=rotation_number(base.held);
    if(unit)for(unsigned i=2;i<=9;++i)if(unit<=UINT64_MAX/i)values.push_back(std::to_string(unit*i));
    for(unsigned i=2;i<=9;++i)if(held_multiple(base,multiple_word(i)))values.push_back(multiple_word(i));
    for(const auto* alias:{"Single","Quadruple"})if(held_multiple(base,alias))values.push_back(alias);
    return values;
}
inline int priority(const Rule& r) {
    int detail{};
    for(const auto& part:following(r))detail+=(spin_only(part)?0:2)+(part.body_spin?10:0)+(part.body_spin && part.body_direction!="Either"?1:0)+(part.next_phase?1:0)+(!part.board.empty()?1:0);
    return detail+(r.rotation_multiples?0:8)+(spin_only(r.first)?0:2)+(!r.sequence?1:0)+(r.combo?100:0)+static_cast<int>(following(r).size())*20+(r.spin?10:0)+
        (!r.first.board.empty()?1:0)+(!r.second.board.empty()?1:0)+(r.spin && r.direction!="Either"?1:0)+(4-std::popcount(takeoff_mask(r)));
}
inline int stance_index(const Labels& labels) {
    switch (stance_of(labels[3],labels[6])) {
    case Stance::normal:return 0;case Stance::nollie:return 1;
    case Stance::fakie:return 2;case Stance::switch_:return 3;default:return -1;
    }
}
inline std::string opposite_direction(const std::string& value) {
    const auto key=normalized(value);
    if(key=="bs" || key=="idtrickbs")return "FS";
    if(key=="fs" || key=="idtrickfs")return "BS";
    return value; // Unknown direction never becomes evidence.
}
// Automatic body convention: Fakie flip tricks, with Fakie Ollie as the sole exception.
inline bool fakie_flip(const Labels& labels,std::string_view id={}) {
    const auto name=normalized(labels[0]);
    if(name=="ollie" || id=="ID_TRICK_OLLIE")return false;
    for(const auto& m:skate::metadata)if(m.id==id || (id.empty() && normalized(m.id)=="idtrick"+name))return (m.categories&2u)!=0;
    return false;
}
inline std::vector<Observation> direction_test_frame(const std::vector<Observation>& frame) {
    auto adjusted=frame;
    for(auto& row:adjusted)if(stance_index(row.labels)==2 && fakie_flip(row.labels,row.native_id))row.direction=opposite_direction(row.direction);
    return adjusted;
}
inline bool combo_row(const Rule& rule) {return !rule.second.base.empty();}
inline const char* stance_name(int value) {
    constexpr const char* names[]{"Base","Nollie","Fakie","Switch"};
    return value>=0 && value<4?names[value]:"Unknown";
}
inline std::string stance_summary(const Rule& r) {
    const auto mask=takeoff_mask(r);
    if (mask==15) return "Any (all 4)";
    std::string text;
    for (int i=0;i<4;++i) if (mask&(1u<<static_cast<unsigned>(i))) {
        if (!text.empty()) text+=" + ";text+=stance_name(i);
    }
    return text.empty()?"None selected":text;
}
inline Component component(const Labels& labels) {
    Component out{labels[0],labels[4],labels[1],{}};
    for (unsigned i=0;i<modifier_slots.size();++i) out.modifiers[i]=labels[modifier_slots[i]];
    return out;
}
inline std::string describe(const Component& c) {
    if(spin_only(c))return "Body spin (any air trick)";
    std::string out;
    if (!c.board.empty()) out+=c.board+" ";
    if (!c.held.empty()) out+=c.held+" ";
    out+=c.base;
    for (const auto& m:c.modifiers) if (!m.empty()) out+=" "+m;
    return out;
}
inline std::string describe(const Rule& r) {
    const auto action=[](const Component& part,int spin,const std::string& direction) {
        const auto rotation=direction+" "+std::to_string(spin)+" body spin";
        auto text=spin_only(part) && spin?rotation+" (any air trick)":describe(part);
        if(!spin_only(part) && spin)text+=" ["+rotation+"]";
        if(part.next_phase)text+=" [later phase]";
        return text;
    };
    auto out=stance_summary(r)+" · "+action(r.first,r.spin?r.spin:r.first.body_spin,r.spin?r.direction:r.first.body_direction);
    for(const auto& part:following(r))out+=(r.sequence?" → ":" + ")+action(part,part.body_spin,part.body_direction);
    if(r.sequence)out+=" [phase sequence]";
    if(r.rotation_multiples)out+=" [auto multiples]";
    return out;
}
inline bool component_matches(const Component& c,const Labels& labels,bool combo,std::string_view native_id={},bool multiples=false) {
    if(spin_only(c))return combo;
    if (!c.native_id.empty()) {
        if (c.native_id!=native_id) return false;
    } else if (normalized(c.base)!=normalized(labels[0])) return false;
    if (multiples?held_multiple(c,labels[4])==0:normalized(c.held)!=normalized(labels[4])) return false;
    if (!c.board.empty() && normalized(c.board)!=normalized(labels[1])) return false;
    if (combo) for (unsigned i=0;i<modifier_slots.size();++i)
        if (normalized(c.modifiers[i])!=normalized(labels[modifier_slots[i]])) return false;
    return true;
}
inline Match match(const std::vector<Rule>& rules,const Observation& observed,const std::vector<Observation>& observed_frame,bool evidence_normalized=false) {
    Match best;int rank=-1;
    const auto adjusted=evidence_normalized?std::vector<Observation>{}:direction_test_frame(observed_frame);
    const auto& frame=evidence_normalized?observed_frame:adjusted;
    const auto row=evidence_normalized?observed:direction_test_frame({observed}).front();
    for (const auto& rule:rules) {
        if(!rule.enabled || observed.identity_ambiguous || !accepts_stance(rule,stance_index(observed.labels)))continue;
        // Reject unrelated trick identities before copying phase evidence.
        if(!spin_only(rule.first)) {
            if(!rule.first.native_id.empty()) {if(rule.first.native_id!=observed.native_id)continue;}
            else if(normalized(rule.first.base)!=normalized(observed.labels[0]))continue;
        }
        if (!rule.enabled || row.identity_ambiguous || !accepts_stance(rule,stance_index(row.labels)) || !component_matches(rule.first,row.labels,rule.combo,row.native_id,rule.rotation_multiples)) continue;
        if (rule.combo && !(rule.sequence?(row.sequence_valid && row.sequence):(row.valid && row.attempt))) continue;
        const int first_spin=rule.spin?rule.spin:rule.first.body_spin;
        const auto& first_direction=rule.spin?rule.direction:rule.first.body_direction;
        if(!spin_representative(rule.first,row,first_spin,first_direction,frame))continue;
        if (!spin_matches(rule.spin,rule.direction,row) || !spin_matches(rule.first.body_spin,rule.first.body_direction,row)) continue;
        const auto parts=following(rule);
        if (row.completed && !row.completed_chain && !parts.empty())continue;
        std::vector<std::uint64_t> chosen,path;unsigned solutions{}, visited{};
        const auto visit=[&](auto&& self,std::size_t step,const Observation& previous)->void {
            if (solutions>1) return;
            if (++visited>4096) {solutions=2;return;}
            if (step==parts.size()) {chosen=path;++solutions;return;}
            const auto& part=parts[step];
            for (const auto& candidate:frame) {
                if (candidate.identity_ambiguous || candidate.handle==row.handle || (candidate.first_seen<=previous.first_seen && !(rule.sequence && candidate.phase>previous.phase))) continue;
                if (rule.sequence) {
                    if (!candidate.sequence_valid || candidate.sequence!=row.sequence || !candidate.phase || candidate.phase<previous.phase) continue;
                } else if (!candidate.valid || candidate.attempt!=row.attempt || (row.phase && candidate.phase && row.phase!=candidate.phase)) continue;
                if (part.next_phase && (!previous.phase || candidate.phase<=previous.phase)) continue;
                if (!spin_representative(part,candidate,part.body_spin,part.body_direction,frame) || !component_matches(part,candidate.labels,true,candidate.native_id) || !spin_matches(part.body_spin,part.body_direction,candidate)) continue;
                path.push_back(candidate.handle);self(self,step+1,candidate);path.pop_back();
                if (solutions>1) return;
            }
        };
        visit(visit,0,row);if (solutions!=1) continue;
        const auto second=chosen.empty()?0:chosen.front();
        const int score=priority(rule);
        if (score>rank) { best={&rule,second,false,chosen};rank=score; }
        else if (score==rank) best.conflict=true;
    }
    if (best.conflict) best.rule=nullptr;
    return best;
}
inline std::string output_stance(const Labels& labels) {
    auto stance=normalized(labels[3]),nollie=normalized(labels[6]);
    if (stance=="idstancenollie")stance="nollie";
    if (stance=="idstancefakie")stance="fakie";
    if (stance=="idstanceswitch")stance="switch";
    if (nollie=="idtricknollie" || nollie=="idstancenollie")nollie="nollie";
    switch(stance_of(stance,nollie)) {
    case Stance::nollie:return "Nollie";case Stance::fakie:return "Fakie";
    case Stance::switch_:return "Switch";default:return "";
    }
}
inline bool has_output_stance(const std::string& name,const std::string& prefix) {
    if (prefix.empty())return false;
    auto lower=name;for(auto& ch:lower)if(ch>='A' && ch<='Z')ch=static_cast<char>(ch-'A'+'a');
    auto expected=prefix;for(auto& ch:expected)if(ch>='A' && ch<='Z')ch=static_cast<char>(ch-'A'+'a');
    return lower==expected || lower.starts_with(expected+" ");
}
inline std::string multiple_output_name(const Rule& rule,const Labels& original) {
    const auto factor=rule.rotation_multiples?held_multiple(rule.first,original[4]):1;
    if(factor<=1 || rule.name.empty())return rule.name;
    const auto prefix=output_stance(original);const auto word=std::string(multiple_word(factor));
    if(has_output_stance(rule.name,prefix))return rule.name.substr(0,prefix.size())+" "+word+rule.name.substr(prefix.size());
    return word+" "+rule.name;
}
inline Labels display(const Rule& rule,const Labels& original,bool secondary=false) {
    auto out=original;
    if (secondary) {
        Rule second=rule;second.first=rule.second;second.name="";
        return display(second,original,false);
    }
    out[0]=multiple_output_name(rule,original);
    if (rule.rotation_multiples || !rule.first.held.empty()) out[4].clear();
    const auto prefix=output_stance(original);
    if (!rule.combo) {
        // Preserve native stance fields for ordinary renames unless disabled or
        // already explicitly included in the output name.
        if (!rule.stance_prefix || has_output_stance(rule.name,prefix)) {out[3].clear();out[6].clear();}
        return out;
    }
    if (rule.stance_prefix && !rule.name.empty() && !prefix.empty() && !has_output_stance(rule.name,prefix))out[0]=prefix+" "+rule.name;
    out[3].clear();out[6].clear();
    if (!rule.first.board.empty() || (rule.spin && (normalized(rule.first.base)=="ollie" || normalized(rule.first.base)=="bonedollie"))) out[1].clear();
    if (rule.spin) {
        out[2].clear();
        const auto token=normalized(out[13]);
        if (token=="bs" || token=="fs" || token=="idtrickbs" || token=="idtrickfs") out[13].clear();
    }
    for (unsigned i=0;i<modifier_slots.size();++i) if (!rule.first.modifiers[i].empty()) out[modifier_slots[i]].clear();
    return out;
}
inline std::map<std::uint64_t,Labels> render_match(const Rule& rule,const Match& match,unsigned first,const std::vector<Observation>& frame,const std::vector<Labels>& originals={},const std::vector<Rule>& rules={}) {
    std::map<std::uint64_t,Labels> patches;std::vector<std::uint64_t> spin_rows;const auto parts=actions(rule);const auto selected=handles(match,frame[first].handle);
    const auto original=[&](unsigned i)->const Labels& {return i<originals.size()?originals[i]:frame[i].labels;};
    unsigned anchor{};while(anchor<parts.size() && spin_only(parts[anchor]))++anchor;
    for(unsigned step=0;step<parts.size();++step) {
        const auto found=std::find_if(frame.begin(),frame.end(),[&](const auto& row){return row.handle==selected[step];});if(found==frame.end())continue;
        const auto index=static_cast<unsigned>(found-frame.begin());
        if(spin_only(parts[step])) {
            for(unsigned i=0;i<frame.size();++i)if(frame[i].sequence==found->sequence && frame[i].phase==found->phase && air_action(frame[i])) {
                auto shown=original(i);shown[2].clear();
                const auto base=normalized(frame[i].labels[0]);
                if(base=="ollie" || base=="nollie" || base=="bonedollie" || base=="bonednollie")shown[1].clear();
                const auto meta=normalized(shown[13]);if(meta=="bs" || meta=="fs" || meta=="idtrickbs" || meta=="idtrickfs")shown[13].clear();
                patches[frame[i].handle]=std::move(shown);spin_rows.push_back(frame[i].handle);
            }
        } else {
            auto action=rule;action.first=parts[step];action.spin=step==0?rule.spin:parts[step].body_spin;
            action.name=step==anchor?rule.name:"";
            if(has_spin_only(rule) && step==anchor) {
                const auto prefix=output_stance(frame[first].labels);
                if(rule.stance_prefix && !prefix.empty() && !has_output_stance(action.name,prefix))action.name=prefix+" "+action.name;
                action.stance_prefix=false;
            }
            patches[found->handle]=display(action,original(index));
        }
    }
    std::vector<Rule> renames;for(const auto& candidate:rules)if(!candidate.combo)renames.push_back(candidate);
    for(const auto handle:spin_rows) {
        const auto found=std::find_if(frame.begin(),frame.end(),[&](const auto& row){return row.handle==handle;});
        if(found!=frame.end())if(const auto selected_rename=dingosdk::trick_terminology::editor::match(renames,*found,frame);selected_rename.rule)
            patches[handle]=display(*selected_rename.rule,patches[handle]);
    }
    return patches;
}
inline std::vector<Match> resolve_frame(const std::vector<Rule>& rules,const std::vector<Observation>& frame) {
    std::vector<Match> out;std::vector<unsigned> order;
    if(rules.empty())return std::vector<Match>(frame.size());
    const auto adjusted=direction_test_frame(frame);
    for (unsigned i=0;i<frame.size();++i) {out.push_back(match(rules,adjusted[i],adjusted,true));order.push_back(i);}
    std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){
        const int pa=out[a].rule?priority(*out[a].rule):-1,pb=out[b].rule?priority(*out[b].rule):-1;
        return pa!=pb?pa>pb:frame[a].first_seen<frame[b].first_seen;
    });
    std::map<std::uint64_t,std::pair<unsigned,int>> claimed;
    for (const auto i:order) {
        if (!out[i].rule) continue;
        const int rank=priority(*out[i].rule);bool blocked{};
        for (const auto handle:claimed_handles(out[i],frame[i].handle,frame)) {
            if (!handle) continue;
            if (const auto found=claimed.find(handle);found!=claimed.end()) {
                blocked=true;
                if (found->second.second==rank) {out[i].conflict=true;out[found->second.first].rule=nullptr;out[found->second.first].conflict=true;}
            }
        }
        if (blocked) {out[i].rule=nullptr;continue;}
        for(const auto handle:claimed_handles(out[i],frame[i].handle,frame))claimed.emplace(handle,std::pair{i,rank});
    }
    return out;
}
inline bool safe_text(const std::string& s,std::size_t limit,bool empty=true) {
    return (empty || !s.empty()) && s.size()<=limit &&
        std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32 || c==127;});
}
inline void validate(const Rule& r) {
    const bool single=r.stance>=0 && r.stance<4;
    const bool any=r.stance==-1, multi=r.stance==-2;
    if ((!single && !any && !multi) || r.stance_mask>15 ||
        (single && r.stance_mask && r.stance_mask!=(1u<<static_cast<unsigned>(r.stance))) ||
        (any && r.stance_mask && r.stance_mask!=15) || (multi && !r.stance_mask))
        throw std::runtime_error("Choose at least one valid takeoff stance.");
    if (!safe_text(r.name,96,false) || r.name.find_first_not_of(' ')==std::string::npos) throw std::runtime_error("Choose a stance and a name of 1–96 characters.");
    if(r.rotation_multiples && r.combo)throw std::runtime_error("Auto multiples apply to simple renames; combo conditions remain exact.");
    if (r.extra.size()>4 || (!r.extra.empty() && r.second.base.empty())) throw std::runtime_error("A combo supports six ordered actions.");
    if(has_spin_only(r)) {
        if(!r.combo || !r.sequence)throw std::runtime_error("Body-spin-only actions need Phase sequence mode.");
        const auto all=actions(r);if(std::all_of(all.begin(),all.end(),[](const auto& p){return spin_only(p);}))throw std::runtime_error("Choose a specific trick or grind to receive the custom name.");
        for(unsigned i=0;i<all.size();++i)if(spin_only(all[i])) {
            const auto& p=all[i];const int amount=i==0 && r.spin?r.spin:p.body_spin;
            if(!amount)throw std::runtime_error("Choose a body spin amount for every spin-only action.");
            if(!p.held.empty() || !p.board.empty() || !p.native_id.empty() || std::any_of(p.modifiers.begin(),p.modifiers.end(),[](const auto& v){return !v.empty();}))throw std::runtime_error("Spin-only actions cannot require a flip or board modifier.");
        }
    }
    auto parts=following(r);parts.push_back(r.first);
    for (const auto& part:parts) {const auto* c=&part;
        if (c->body_spin!=0 && c->body_spin!=180 && c->body_spin!=360 && c->body_spin!=540 && c->body_spin!=720) throw std::runtime_error("Unsupported action spin.");
        if (c->body_direction!="Either" && c->body_direction!="BS" && c->body_direction!="FS") throw std::runtime_error("Invalid action spin direction.");
        if (!c->body_spin && c->body_direction!="Either") throw std::runtime_error("An action direction needs a spin.");
        if (c->next_phase && !r.sequence) throw std::runtime_error("Next phase needs phase sequence mode.");
        if (c->base.empty()) throw std::runtime_error("Choose every action in the sequence.");
        if (!safe_text(c->native_id,96) || (!c->native_id.empty() && !c->native_id.starts_with("ID_TRICK_")) || !safe_text(c->base,96) || !safe_text(c->held,32) ||
            (c->board!="" && c->board!="BS" && c->board!="FS")) throw std::runtime_error("Invalid native trick component.");
        for (const auto& m:c->modifiers) if (!safe_text(m,96)) throw std::runtime_error("Invalid modifier.");
    }
    if (r.first.base.empty()) throw std::runtime_error("Choose a native trick.");
    if (r.spin!=0 && r.spin!=180 && r.spin!=360 && r.spin!=540 && r.spin!=720) throw std::runtime_error("Unsupported body spin.");
    if (r.direction!="Either" && r.direction!="BS" && r.direction!="FS") throw std::runtime_error("Choose a body-spin direction.");
    if (!r.spin && r.direction!="Either") throw std::runtime_error("A body direction needs a spin condition.");
    if (!r.combo && (r.spin || !r.second.base.empty() || !r.extra.empty() || r.sequence || r.first.body_spin)) throw std::runtime_error("A simple rename cannot contain combo conditions.");
    if (r.combo && !r.spin && r.second.base.empty()) throw std::runtime_error("A combo needs a spin or a second trick.");
}
inline Json encode(const Component& c) {
    auto j=Json::object();j["base"]=c.base;j["native_id"]=c.native_id;j["body_spin"]=c.body_spin;j["body_direction"]=c.body_direction;j["next_phase"]=c.next_phase;j["held"]=c.held;j["board"]=c.board;j["modifiers"]=Json(c.modifiers);return j;
}
inline Component decode_component(const Json& j) {
    Component c;c.body_spin=j.value("body_spin",0);c.body_direction=j.value("body_direction","Either");c.next_phase=j.value("next_phase",false);c.native_id=j.value("native_id","");c.base=j.value("base","");c.held=j.value("held","");c.board=j.value("board","");
    if (j.contains("modifiers")) {
        const auto& values=j.at("modifiers");
        if (!values.is_array() || values.size()!=c.modifiers.size()) throw std::runtime_error("Invalid modifier list.");
        for (unsigned i=0;i<c.modifiers.size();++i) c.modifiers[i]=values.at(i).get<std::string>();
    }
    return c;
}
inline Json encode(const Rule& r) {
    auto j=Json::object();j["id"]=r.id;j["enabled"]=r.enabled;j["kind"]=combo_row(r)?"combo":r.combo?"spin_rename":"rename";
    j["stance"]=r.stance;j["stance_mask"]=takeoff_mask(r);j["first"]=encode(r.first);j["second"]=encode(r.second);
    j["sequence"]=r.sequence;j["extra"]=Json::array();for (const auto& part:r.extra) j["extra"].push_back(encode(part));
    j["spin"]=r.spin;j["direction"]=r.direction;j["name"]=r.name;j["stance_prefix"]=r.stance_prefix;j["rotation_multiples"]=r.rotation_multiples;return j;
}
inline Rule decode_rule(const Json& j) {
    Rule r;r.id=j.value("id",std::uint64_t{});r.enabled=j.value("enabled",true);
    const auto kind=j.value("kind","");if (kind!="rename" && kind!="spin_rename" && kind!="combo") throw std::runtime_error("Unknown rule kind.");
    r.combo=kind!="rename";r.rotation_multiples=j.value("rotation_multiples",false);r.stance_prefix=j.value("stance_prefix",!r.combo);r.stance=j.value("stance",-3);r.stance_mask=j.value("stance_mask",0u);r.name=j.value("name","");
    r.first=decode_component(j.at("first"));if (j.contains("second")) r.second=decode_component(j.at("second"));
    r.sequence=j.value("sequence",false);if (j.contains("extra")) {if (!j.at("extra").is_array() || j.at("extra").size()>4) throw std::runtime_error("Invalid extra actions.");for (const auto& part:j.at("extra")) r.extra.push_back(decode_component(part));}
    r.spin=j.value("spin",0);r.direction=j.value("direction","Either");validate(r);return r;
}
inline std::string condition_key(const Component& c,bool combo) {
    auto j=Json::array({normalized(c.base),normalized(c.held),normalized(c.board),std::to_string(c.body_spin),c.body_direction,c.next_phase?"next":"same"});
    if (combo) for (const auto& m:c.modifiers) j.push_back(normalized(m));
    return j.dump();
}
inline bool same_action_conditions(const Rule& a,const Rule& b) {
    if(a.rotation_multiples!=b.rotation_multiples)return false;
    if (a.sequence!=b.sequence || a.extra.size()!=b.extra.size()) return false;
    for (unsigned i=0;i<a.extra.size();++i) if (condition_key(a.extra[i],true)!=condition_key(b.extra[i],true)) return false;
    return a.combo==b.combo && a.spin==b.spin && a.direction==b.direction &&
        condition_key(a.first,a.combo)==condition_key(b.first,b.combo) && condition_key(a.second,true)==condition_key(b.second,true);
}
inline Rule native_conditions(Rule rule,int) {return rule;}
inline bool same_conditions(const Rule& a,const Rule& b) {
    const auto mask=takeoff_mask(a);if(mask!=takeoff_mask(b))return false;
    for(int stance=0;stance<4;++stance)if(mask&(1u<<stance))
        if(!same_action_conditions(native_conditions(a,stance),native_conditions(b,stance)))return false;
    return true;
}
inline bool conflicting_conditions(const Rule& a,const Rule& b) {
    const auto ma=takeoff_mask(a),mb=takeoff_mask(b),overlap=ma&mb;
    if(!overlap || std::popcount(ma)!=std::popcount(mb))return false;
    for(int stance=0;stance<4;++stance)if(overlap&(1u<<stance))
        if(same_action_conditions(native_conditions(a,stance),native_conditions(b,stance)))return true;
    return false;
}
inline void alphabetize(std::vector<Rule>& rules) {
    std::stable_sort(rules.begin(),rules.end(),[](const Rule& a,const Rule& b){
        const auto an=normalized(a.name),bn=normalized(b.name);
        if(an!=bn)return an<bn;
        return a.id<b.id;
    });
}
inline Json encode_rules(const std::vector<Rule>& rules) {
    auto j=Json::object();j["schema"]=5;j["game_base"]="ReSkate 2.1.0";j["rules"]=Json::array();
    auto ordered=rules;alphabetize(ordered);j["order"]="name";
    for (const auto& r:ordered) j["rules"].push_back(encode(r));return j;
}
inline std::vector<Rule> decode_rules(const Json& j) {
    const auto schema=j.value("schema",0);
    if ((schema!=1 && schema!=2 && schema!=3 && schema!=4 && schema!=5) || !j.at("rules").is_array() || j.at("rules").size()>256) throw std::runtime_error("Unsupported rule file.");
    std::vector<Rule> out;
    for (const auto& value:j.at("rules")) {
        auto migrated=value;
        // Legacy inversion flags are intentionally retired in favour of the
        // fixed Fakie flip convention. Old spin-only combos remain replacements.
        auto r=decode_rule(migrated);
        if (!r.id || std::any_of(out.begin(),out.end(),[&](const Rule& p){return p.id==r.id || (p.enabled && r.enabled && conflicting_conditions(p,r));}))
            throw std::runtime_error("Duplicate rule identity or ambiguous stance conditions.");
        out.push_back(std::move(r));
    }
    alphabetize(out);return out;
}
inline std::string hex(const std::string& input) {
    constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(input.size()*2);
    for (unsigned char c:input) {out+=digits[c>>4];out+=digits[c&15];}return out;
}
inline std::string unhex(const std::string& input) {
    if (input.size()%2 || input.size()>32768) throw std::runtime_error("Invalid editor command.");
    auto digit=[](char c)->unsigned{if (c>='0' && c<='9') return c-'0';if (c>='a' && c<='f') return c-'a'+10;throw std::runtime_error("Invalid editor command.");};
    std::string out;for (std::size_t i=0;i<input.size();i+=2) out+=static_cast<char>(digit(input[i])*16+digit(input[i+1]));return out;
}
inline std::vector<Rule> current_preset() {
    std::vector<Rule> rules;
    auto add=[&](int stance,std::string base,std::string held,std::string board,int spin,std::string direction,std::string name){
        Rule r;r.id=rules.size()+1;r.stance=stance;r.first={std::move(base),std::move(held),std::move(board),{}};
        r.combo=spin!=0;r.stance_prefix=!r.combo;r.spin=spin;r.direction=std::move(direction);r.name=std::move(name);rules.push_back(std::move(r));
    };
    for (int st=0;st<4;++st) {
        const std::string prefix=st?std::string(stance_name(st))+" ":"";
        const auto bs=st==2?"FS":"BS", fs=st==2?"BS":"FS";
        add(st,"Flip","360","",0,"Either","Tre Flip");
        add(st,"Hardflip","","",180,st==1 || st==2?"FS":"BS",prefix+"Ghetto Bird");
        add(st,"Pop-Shuvit","360","BS",180,bs,prefix+"Bigspin");
        add(st,"Pop-Shuvit","360","FS",180,fs,prefix+"FS Bigspin");
        add(st,"Flip","360","",180,bs,prefix+"Bigflip");
        add(st,"Hardflip","360","",180,fs,prefix+"FS Bigflip");
        add(st,"Inward Heelflip","360","",180,bs,prefix+"BS Bigheel");
        add(st,"Laserflip","","",180,fs,prefix+"Bigheel");
    }
    for (int spin:{180,360}) for (const auto* direction:{"BS","FS"})
        for (const auto* base:{"Ollie","Kickflip","Heelflip"}) {
            const auto suffix=std::string(base)=="Ollie"?"":std::string(base)=="Kickflip"?" Flip":" Heelflip";
            add(2,base,"","",spin,direction,std::string(std::string(direction)=="FS"?"FS ":"")+(spin==180?"Half-Cab":"Cab")+suffix);
        }
    return rules;
}
} // namespace dingosdk::trick_terminology::editor
