#include "trick_editor_page.h"
#include "trick_editor.h"
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include <cstring>

namespace dingosdk::overlay::menu {
namespace {
namespace ed=trick_terminology::editor;
struct Page {
    bool open{};int mode{-1}, replacement{};
    std::uint64_t selected{}, delete_id{};
    ed::Rule draft;
    std::array<char,97> name{};
    std::string error;
};
Page& page() {static auto* value=new Page;return *value;}
void submit(SkateMenu& menu,const CallbacksV3& callbacks,const std::string& text) {
    send_console(menu,callbacks,"trainer terminology "+text);
}
void set_name(Page& p,const std::string& text) {
    p.name.fill(0);std::memcpy(p.name.data(),text.data(),std::min(text.size(),p.name.size()-1));
}
void draft(Page& p,ed::Rule r,bool duplicate=false) {
    if (duplicate) r.id=0;
    if(r.first.body_spin && !r.spin){r.spin=r.first.body_spin;r.direction=r.first.body_direction;r.first.body_spin=0;r.first.body_direction="Either";}
    p.draft=std::move(r);p.mode=ed::combo_row(p.draft)?1:0;set_name(p,p.draft.name);p.error.clear();
}
struct Entry {const char* label;ed::Component value;};
const std::vector<Entry>& catalogue() {
    static const std::vector<Entry> entries{
        {"Kickflip",{"Kickflip","","",{}}},{"Heelflip",{"Heelflip","","",{}}},
        {"360 Flip",{"Flip","360","",{}}},{"Hardflip",{"Hardflip","","",{}}},
        {"360 Hardflip",{"Hardflip","360","",{}}},{"Inward Heelflip",{"Inward Heelflip","","",{}}},
        {"360 Inward Heelflip",{"Inward Heelflip","360","",{}}},{"Laserflip",{"Laserflip","","",{}}},
        {"BS Pop-Shuvit",{"Pop-Shuvit","","BS",{}}},{"FS Pop-Shuvit",{"Pop-Shuvit","","FS",{}}},
        {"BS 360 Pop-Shuvit",{"Pop-Shuvit","360","BS",{}}},{"FS 360 Pop-Shuvit",{"Pop-Shuvit","360","FS",{}}},
        {"Varial Kickflip",{"Varial Kickflip","","",{}}},{"Varial Heelflip",{"Varial Heelflip","","",{}}},
        {"Ollie",{"Ollie","","",{}}},{"Boned Ollie",{"Boned Ollie","","",{}}},
        {"Impossible",{"Impossible","","",{}}},
        {"50-50",{"50-50","","",{},"ID_TRICK_50_50"}},
        {"5-0",{"5-0","","",{},"ID_TRICK_5_0_GRIND"}},
        {"Nosegrind",{"Nosegrind","","",{},"ID_TRICK_NOSEGRIND"}},
        {"Boardslide",{"Boardslide","","",{},"ID_TRICK_BOARDSLIDE"}},
        {"Lipslide",{"Lipslide","","",{},"ID_TRICK_LIPSLIDE"}},
        {"Tailslide",{"Tailslide","","",{},"ID_TRICK_TAILSLIDE"}},
        {"Noseslide",{"Noseslide","","",{},"ID_TRICK_NOSESLIDE"}},
        {"Bluntslide",{"Bluntslide","","",{},"ID_TRICK_BLUNTSLIDE"}},
        {"Nosebluntslide",{"Nosebluntslide","","",{},"ID_TRICK_NOSEBLUNTSLIDE"}},
        {"Smith",{"Smith","","",{},"ID_TRICK_SMITH"}},
        {"Feeble",{"Feeble","","",{},"ID_TRICK_FEEBLE"}},
        {"Crooked",{"Crooked","","",{},"ID_TRICK_CROOKED"}},
        {"Overcrooked",{"Overcrooked","","",{},"ID_TRICK_OVERCROOKED"}},
        {"Salad",{"Salad","","",{},"ID_TRICK_SALAD"}},
        {"Willy",{"Willy","","",{},"ID_TRICK_WILLY"}},
        {"Overwilly",{"Overwilly","","",{},"ID_TRICK_OVERWILLY"}},
        {"Darkslide",{"Darkslide","","",{},"ID_TRICK_DARKSLIDE"}},
        {"Darklip",{"Darklip","","",{},"ID_TRICK_DARKLIP"}},
        {"Axle Stall",{"Axle Stall","","",{},"ID_TRICK_AXLESTALL"}},
        {"5-0 Stall",{"5-0 Stall","","",{},"ID_TRICK_5_0_STALL"}},
        {"Blunt Stall",{"Blunt Stall","","",{},"ID_TRICK_BLUNTSTALL"}},
        {"Boardslide Stall",{"Boardslide Stall","","",{},"ID_TRICK_BOARDSLIDE_STALL"}},
        {"Lipslide Stall",{"Lipslide Stall","","",{},"ID_TRICK_LIPSLIDE_STALL"}},
        {"Tailslide Stall",{"Tailslide Stall","","",{},"ID_TRICK_TAILSLIDE_STALL"}},
        {"Noseslide Stall",{"Noseslide Stall","","",{},"ID_TRICK_NOSESLIDE_STALL"}},
        {"Nosegrind Stall",{"Nosegrind Stall","","",{},"ID_TRICK_NOSEGRIND_STALL"}},
        {"Crooked Stall",{"Crooked Stall","","",{},"ID_TRICK_CROOKED_STALL"}},
        {"Smith Stall",{"Smith Stall","","",{},"ID_TRICK_SMITH_STALL"}},
        {"Feeble Stall",{"Feeble Stall","","",{},"ID_TRICK_FEEBLE_STALL"}},
        {"Bluntslide Stall",{"Bluntslide Stall","","",{},"ID_TRICK_BLUNTSLIDESTALL"}},
        {"Nosebluntslide Stall",{"Nosebluntslide Stall","","",{},"ID_TRICK_NOSEBLUNTSLIDESTALL"}},
        {"Overcrooked Stall",{"Overcrooked Stall","","",{},"ID_TRICK_OVERCROOKED_STALL"}},
        {"Salad Stall",{"Salad Stall","","",{},"ID_TRICK_SALAD_STALL"}},
        {"Willy Stall",{"Willy Stall","","",{},"ID_TRICK_WILLY_STALL"}},
        {"Overwilly Stall",{"Overwilly Stall","","",{},"ID_TRICK_OVERWILLY_STALL"}},
        {"Darkslide Stall",{"Darkslide Stall","","",{},"ID_TRICK_DARKSLIDESTALL"}},
        {"Darklip Stall",{"Darklip Stall","","",{},"ID_TRICK_DARKLIPSTALL"}},
        {"Slappy Entry",{"Slappy Entry","","",{},"ID_TRICK_SLAPPYENTER"}},
        {"Slappy Entry Stall",{"Slappy Entry Stall","","",{},"ID_TRICK_SLAPPYENTER_STALL"}},
        {"Lean Exit",{"Lean Exit","","",{},"ID_TRICK_LEANEXIT"}},
        {"Wallride Stall",{"Wallride Stall","","",{},"ID_TRICK_WALLRIDE_STALL"}}
    };return entries;
}
void component_row(SkateMenu& menu,ed::Component& component,const char* label) {
    std::vector<std::string> options{"Captured / custom"};int selected{};
    const auto key=ed::encode(component).dump();
    for (const auto& entry:catalogue()) {
        options.push_back(entry.label);
        if (ed::encode(entry.value).dump()==key) selected=static_cast<int>(options.size()-1);
    }
    if (option_row(menu,label,"Choose an existing native trick or capture your last action.",selected,options) && selected>0)
        {
        const auto spin=component.body_spin;const auto direction=component.body_direction;const bool phase=component.next_phase;
        component=catalogue()[static_cast<std::size_t>(selected-1)].value;
        component.body_spin=spin;component.body_direction=direction;component.next_phase=phase;
    }
    info(menu,"Components",ed::describe(component));
    if (ImGui::TreeNodeEx("Native fields##component",ImGuiTreeNodeFlags_SpanAvailWidth)) {
        std::array<char,97> base{};std::memcpy(base.data(),component.base.data(),std::min(component.base.size(),base.size()-1));
        field(menu,"Native base name");
        if (ImGui::InputText("##native-base",base.data(),base.size())) {component.base=base.data();component.native_id.clear();}
        const std::vector<std::string> amounts{"None","180","360","540","720","1080","1440","1800","2160","2520","2880","3240","Double","Triple","Quad","Quintuple","Sextuple","Septuple","Octuple","Endless"};
        int held{};for (unsigned i=1;i<amounts.size();++i) if (component.held==amounts[i]) held=static_cast<int>(i);
        if (option_row(menu,"Native board rotation","This is the separate ticker component, not proof of an input gesture.",held,amounts))
            component.held=held?amounts[static_cast<std::size_t>(held)]:"";
        int board=component.board=="BS"?1:component.board=="FS"?2:0;
        if (option_row(menu,"Board direction","Keep board direction separate from body spin.",board,{"Any / absent","BS","FS"}))
            component.board=board==1?"BS":board==2?"FS":"";
        note("Captured modifiers are included in combo conditions. A simple rename preserves unrelated modifiers.");
        bool has_modifiers=std::any_of(component.modifiers.begin(),component.modifiers.end(),[](const std::string& m){return !m.empty();});
        if (button_row(menu,"clear-captured-modifiers",{{"CLEAR CAPTURED MODIFIERS",has_modifiers}})==0) component.modifiers={};
        ImGui::TreePop();
    }
}
void action_row(SkateMenu& menu,ed::Component& part,int& spin,std::string& direction,bool allow_spin_only=true) {
    const bool grind=std::any_of(catalogue().begin(),catalogue().end(),[&](const auto& entry){return !entry.value.native_id.empty() && (part.native_id==entry.value.native_id || part.base==entry.value.base);});
    int kind=ed::spin_only(part)?1:grind?2:0;
    if(allow_spin_only && option_row(menu,"Action type","Choose a native trick/grab, a body spin with any air trick, or a grind/stall.",kind,
        {"Trick / grab","Body spin","Grind / stall"})) {
        const bool phase=part.next_phase;const int old_spin=spin;const auto old_direction=direction;
        part={};part.next_phase=phase;spin=old_spin;direction=old_direction;
        if(kind==1) {part.base="*";if(!spin)spin=180;}
        else if(kind==2) {part.base="50-50";part.native_id="ID_TRICK_50_50";}
        else part.base="Ollie";
    }
    if(ed::spin_only(part))note("Matches body rotation with any airborne trick or grab. Its native trick name stays visible.");
    else component_row(menu,part,"Native trick");
    int amount=spin/180;
    if(option_row(menu,"Body spin","Body rotation for this action. Board rotation is configured under Native fields.",amount,
        {"None","180","360","540","720"})) {spin=amount*180;if(!spin)direction="Either";}
    if(spin) {
        int dir=direction=="BS"?1:direction=="FS"?2:0;
        if(option_row(menu,"Spin direction","Measured or completed native body direction for this action.",dir,{"Either","BS","FS"}))
            direction=dir==1?"BS":dir==2?"FS":"Either";
    }
}
void from_capture(Page& p,const ed::View& view) {
    if(!ed::spin_only(p.draft.first)) {p.draft.first=ed::component(view.last.labels);p.draft.first.native_id=view.last.native_id;}
    if (p.draft.stance>=0) {
        p.draft.stance=std::max(0,ed::stance_index(view.last.labels));p.draft.stance_mask=0;
    }
    if (p.mode>=0) {
        const auto& spin=view.last.labels[2];p.draft.spin=spin=="180"?180:spin=="360"?360:spin=="540"?540:spin=="720"?720:0;
        const auto evidence=ed::direction_test_frame({view.last}).front();
        p.draft.direction=evidence.direction.empty()?"Either":evidence.direction;
    }
}
}
bool terminology_editor_open() {return page().open;}
void terminology_editor_button(SkateMenu& menu,const CallbacksV3&) {
    auto& p=page();
    if (button_row(menu,"trick-editor-entry",{{p.open?"CLOSE TRICK EDITOR":"TRICK NAME CHANGER / COMBO CREATOR"}})==0) p.open=!p.open;
}
void terminology_editor_page(SkateMenu& menu,const CallbacksV3& callbacks) {
    auto& p=page();const auto current=ed::view();const auto& view=*current;
    if (button_row(menu,"trick-editor-back",{{"BACK TO MAP & HUD"}})==0) {p.open=false;return;}
    bool terminology_enabled=view.terminology_enabled;
    if (toggle_row(menu,"Skate trick terminology","Apply the active rules to the native trick ticker.",terminology_enabled))
        send_console(menu,callbacks,std::string("trainer option terminology ")+(terminology_enabled?"1":"0"));
    begin_card(menu,"terminology-detection","GLOBAL TRICK DETECTION");
    bool early=view.detection.early_spin;
    if (toggle_row(menu,"Early Spin Recognition","Predict the next spin increment after Skate acknowledges a spin. Applies to every spin combo.",early))
        submit(menu,callbacks,std::string("early-spin ")+(early?"on ":"off ")+std::to_string(view.detection.lead_degrees));
    std::vector<std::string> leads;for (int i=0;i<=45;++i) leads.push_back(std::to_string(i)+" degrees");
    int lead=view.detection.lead_degrees;
    if (option_row(menu,"Spin recognition lead","15 degrees recognizes 180 at 165 and 360 at 345. Larger leads predict earlier.",lead,leads))
        submit(menu,callbacks,std::string("early-spin ")+(view.detection.early_spin?"on ":"off ")+std::to_string(lead));
    note("Provisional names follow current airborne rotation and can change if you reverse. This changes display matching, not physics or scoring.");
    end_card();
    begin_card(menu,"trick-rule-workspace","TRICK RULE WORKSPACE");
    info(menu,"Rules",std::to_string(view.rules.size()));
    note("Starts empty. Your edits save locally. Load the current setup preset only when you want its 44 rules.");
    const int replace=button_row(menu,"workspace-replace",{{"START EMPTY"},{"LOAD CURRENT SETUP"}});
    if (replace>=0) {
        if (view.rules.empty()) {submit(menu,callbacks,replace?"preset":"new");p.mode=-1;p.selected=0;}
        else p.replacement=replace+1;
    }
    if (p.replacement) {
        warn("Replace the current rule list? Export first if you want to keep a separate copy.");
        const auto answer=button_row(menu,"confirm-workspace",{{"REPLACE LIST"},{"CANCEL"}});
        if (answer==0) {submit(menu,callbacks,p.replacement==1?"new":"preset");p.replacement=0;p.mode=-1;p.selected=0;}
        if (answer==1) p.replacement=0;
    }
    const int store=button_row(menu,"editor-storage",{{"SAVE NOW",view.ready},{"RELOAD SAVED",view.ready},{"EXPORT JSON",view.ready}});
    if (store>=0) submit(menu,callbacks,store==0?"save":store==1?"reload":"export");
    if (!view.io.empty()) note(view.io.c_str());
    if (!view.export_path.empty()) {
        info(menu,"Last export",view.export_path);
        if (button_row(menu,"copy-export-path",{{"COPY EXPORT PATH"}})==0) ImGui::SetClipboardText(view.export_path.c_str());
    }
    end_card();

    begin_card(menu,"trick-rule-list","YOUR RULES");
    const ed::Rule* selected{};int index{};std::vector<std::string> options{"Select a rule"};
    for (const auto& rule:view.rules) {
        options.push_back(std::string(rule.enabled?"":"OFF · ")+rule.name+" · "+ed::stance_summary(rule));
        if (rule.id==p.selected) {index=static_cast<int>(options.size()-1);selected=&rule;}
    }
    if (option_row(menu,"Saved rule","Choose a rule to inspect or change.",index,options)) {
        if (index>0) {p.selected=view.rules[static_cast<std::size_t>(index-1)].id;selected=&view.rules[static_cast<std::size_t>(index-1)];}
        else {p.selected=0;selected=nullptr;}
        p.delete_id=0;
    }
    if (selected) {
        info(menu,"Condition",ed::describe(*selected));
        info(menu,"Display",ed::combo_row(*selected)?"New zero-point combo row":"Replace existing trick row");
        bool enabled=selected->enabled;
        if (toggle_row(menu,"Rule enabled","Apply this rule while terminology is on.",enabled)) submit(menu,callbacks,"toggle "+std::to_string(selected->id));
        const auto action=button_row(menu,"rule-actions",{{"EDIT"},{"DUPLICATE AS DRAFT"},{"DELETE"}});
        if (action==0) draft(p,*selected);
        if (action==1) draft(p,*selected,true);
        if (action==2) p.delete_id=selected->id;
    } else if (view.rules.empty()) note("No active mappings until you create a rule or load the preset.");
    if (p.delete_id) {
        const auto answer=button_row(menu,"confirm-delete-rule",{{"CONFIRM DELETE"},{"CANCEL"}});
        if (answer==0) {submit(menu,callbacks,"delete "+std::to_string(p.delete_id));p.delete_id=0;p.selected=0;}
        if (answer==1) p.delete_id=0;
    }
    if (button_row(menu,"new-rule-draft",{{"CREATE NEW RULE"}})==0) {p.draft={};p.mode=-1;p.name.fill(0);p.error.clear();}
    end_card();

    begin_card(menu,"trick-rule-draft",p.draft.id?"EDIT RULE":"CREATE RULE");
    const auto mode=button_row(menu,"choose-rule-kind",{{"SINGLE TRICK",true,p.mode==0},{"COMBO TRICK",true,p.mode==1}});
    if (mode>=0 && p.mode!=mode) {
        p.mode=mode;
        if(mode==0) {p.draft.second={};p.draft.extra.clear();p.draft.sequence=false;if(ed::spin_only(p.draft.first))p.draft.first.base="Ollie";}
        else {p.draft.rotation_multiples=false;p.draft.sequence=true;}
    }
    if (p.mode<0) {note("First choose whether you are renaming a trick or defining a combination.");end_card();}
    else {
        p.draft.combo=p.mode==1 || p.draft.spin!=0;
        note(p.mode?"An ordered combo adds a new zero-point ticker row after the final action. Native trick rows keep their names and scores.":
            "Rename one trick, optionally with body spin. Replaces its existing ticker output; unrelated modifiers remain visible.");
        if (p.mode==0 && button_row(menu,"capture-first",{{"USE LAST PERFORMED TRICK",view.captured}})==0) from_capture(p,view);
        const auto previous_mask=ed::takeoff_mask(p.draft);
        int stance_choice=p.draft.stance==-1?4:p.draft.stance==-2?5:p.draft.stance;
        if (option_row(menu,"Takeoff stance","Choose one stance, all four, or a custom selection.",stance_choice,
            {"Base","Nollie","Fakie","Switch","Any (all 4)","Multi-select"})) {
            p.draft.stance=stance_choice==4?-1:stance_choice==5?-2:stance_choice;
            p.draft.stance_mask=stance_choice==5?previous_mask:0;
        }
        if (p.draft.stance==-2) {
            for (int st=0;st<4;++st) {
                const unsigned bit=1u<<static_cast<unsigned>(st);bool stance_selected=(p.draft.stance_mask&bit)!=0;
                ImGui::PushID(st);
                if (toggle_row(menu,ed::stance_name(st),"Include this takeoff stance in the rule.",stance_selected))
                    p.draft.stance_mask=stance_selected?p.draft.stance_mask|bit:p.draft.stance_mask&~bit;
                ImGui::PopID();
            }
            info(menu,"Selected stances",ed::stance_summary(p.draft));
        }
        if (p.draft.stance<0) note("The trick and spin conditions stay the same for each selected stance. A narrower stance rule can override this rule.");
        if(p.mode==0) {
            ImGui::PushID("first-component");action_row(menu,p.draft.first,p.draft.spin,p.draft.direction,false);ImGui::PopID();
        } else {
            section(menu,"COMBO ACTIONS");
            note("Configure each action in order, then use + Add Action. Up to six actions; each has its own spin conditions.");
            auto actions=ed::following(p.draft);
            // Keep the existing first-action storage contract; never reset it while drawing.
            if(p.draft.first.body_spin && !p.draft.spin) {p.draft.spin=p.draft.first.body_spin;p.draft.direction=p.draft.first.body_direction;}
            p.draft.first.body_spin=0;p.draft.first.body_direction="Either";
            section(menu,"ACTION 1");ImGui::PushID("first-action");
            action_row(menu,p.draft.first,p.draft.spin,p.draft.direction);
            if(button_row(menu,"capture",{{"USE LAST PERFORMED ACTION",view.captured}})==0)from_capture(p,view);
            ImGui::PopID();
            int remove=-1;
            for(unsigned i=0;i<actions.size();++i) {
                ImGui::PushID(static_cast<int>(i)+100);
                const auto title="ACTION "+std::to_string(i+2);section(menu,title.c_str());
                auto& part=actions[i];
                action_row(menu,part,part.body_spin,part.body_direction);
                const auto capture=button_row(menu,"action-buttons",{{"USE LAST PERFORMED ACTION",view.captured},{"REMOVE ACTION"}});
                if(capture==0) {
                    if(!ed::spin_only(part)) {
                        const auto spin=part.body_spin;const auto direction=part.body_direction;const bool phase=part.next_phase;
                        part=ed::component(view.last.labels);part.native_id=view.last.native_id;
                        part.body_spin=spin;part.body_direction=direction;part.next_phase=phase;
                    } else {
                        const auto& spin=view.last.labels[2];part.body_spin=spin=="180"?180:spin=="360"?360:spin=="540"?540:spin=="720"?720:0;
                        const auto evidence=ed::direction_test_frame({view.last}).front();
                        part.body_direction=part.body_spin && !evidence.direction.empty()?evidence.direction:"Either";
                    }
                }
                if(capture==1)remove=static_cast<int>(i);
                if(p.draft.sequence)toggle_row(menu,"Require a later phase","This action must follow an air/grind/air phase transition.",part.next_phase);
                ImGui::PopID();
            }
            if(remove>=0)actions.erase(actions.begin()+remove);
            ImGui::Spacing();
            if(actions.size()<5 && button_row(menu,"add-action",{{"+ ADD ACTION"}})==0) {
                ed::Component next{"Ollie","","",{}};next.next_phase=p.draft.sequence;actions.push_back(next);
            }
            section(menu,"SEQUENCE OPTIONS");
            const bool spin_only=ed::spin_only(p.draft.first) || std::any_of(actions.begin(),actions.end(),[](const auto& part){return ed::spin_only(part);});
            if(spin_only)p.draft.sequence=true;
            bool sequence=p.draft.sequence;
            if(toggle_row(menu,"Phase sequence","Allow air, grind/stall and exit actions in one physical sequence. Required for Body spin actions.",sequence,!spin_only)) {
                p.draft.sequence=sequence;
                if(!sequence)for(auto& part:actions)part.next_phase=false;
            }
            if(spin_only)note("Phase sequence is enabled because this combo contains a Body spin action. Include a specific trick or grind to receive the custom name.");
            note("Require a later phase belongs to the action it precedes. Equal or ambiguous action ordering is rejected.");
            p.draft.second=actions.empty()?ed::Component{}:actions.front();p.draft.extra.clear();
            if(actions.size()>1)p.draft.extra.assign(actions.begin()+1,actions.end());
        }
        section(menu,"RULE OPTIONS");
        if(p.mode==0 && !p.draft.spin) {
            toggle_row(menu,"Auto Double / Triple / held multiples","Extend this rename using native held rotation: 360/720/1080 Flip becomes Tre/Double Tre/Triple Tre Flip.",p.draft.rotation_multiples);
            if(p.draft.rotation_multiples)note("Names: Double, Triple, Quad, Quintuple, Sextuple, Septuple, Octuple, Endless. Uses the native held field, not body spin. Exact rename rules override this automatic rule.");
        }
        p.draft.combo=p.mode==1 || p.draft.spin!=0;
        note("Fakie flip tricks automatically invert body FS/BS for matching. Only Fakie Ollie is excluded among flip-category tricks. Grabs, grinds and board direction keep their native convention.");
        toggle_row(menu,"Prefix takeoff stance","Add Nollie, Fakie or Switch automatically. Base has no prefix. OFF uses your output name as written.",p.draft.stance_prefix);
        note("With prefix ON, enter the base name, for example Sex Change. Explicit matching stance prefixes are not doubled.");
        field(menu,"Output name");ImGui::InputText("##rule-output",p.name.data(),p.name.size());p.draft.name=p.name.data();
        info(menu,"Preview",ed::describe(p.draft)+" → "+p.draft.name+(p.draft.stance_prefix?" [stance prefix ON]":" [stance prefix OFF]"));
        if(p.draft.spin)p.draft.rotation_multiples=false;
        bool valid=true;std::string issue;
        try {if(p.mode==1 && !ed::combo_row(p.draft))throw std::runtime_error("Add a second action for a Combo Trick, or choose Single Trick for a trick + spin rename.");ed::validate(p.draft);}
        catch (const std::exception& e) {valid=false;issue=e.what();}
        for (const auto& rule:view.rules) if (rule.id!=p.draft.id && rule.enabled && p.draft.enabled && ed::conflicting_conditions(rule,p.draft)) {valid=false;issue="An enabled rule overlaps these stances with equal priority.";}
        if (!issue.empty()) note(issue.c_str());
        if (primary_button(menu,p.draft.id?"APPLY RULE CHANGES":"CREATE RULE",valid && view.ready)) {
            submit(menu,callbacks,"upsert "+ed::hex(ed::encode(p.draft).dump()));p.mode=-1;p.draft={};p.name.fill(0);
        }
        end_card();
    }
    begin_card(menu,"trick-rule-live","LAST PERFORMED ACTION");
    if (view.captured) {
        info(menu,"Native",view.native_text);
        info(menu,"Native identity",view.last.native_id.empty()?"Unresolved":view.last.native_id);
        info(menu,"Movement phase",view.last.phase_kind.empty()?"Unassociated":view.last.phase_kind+" / segment "+std::to_string(view.last.segment));
        info(menu,"Air / revert rotation",std::to_string(static_cast<int>(view.last.air_degrees))+" / "+std::to_string(static_cast<int>(view.last.recovery_degrees))+" degrees");
        info(menu,"Measured spin category",std::to_string(view.last.measured_spin));
        info(menu,"Measured body rotation",std::to_string(static_cast<int>(std::abs(view.last.measured_degrees)))+" degrees");
        info(menu,"Early spin candidate",view.last.early_degrees?std::to_string(view.last.early_degrees)+" degrees":"None");info(menu,"Matched rule",view.matched);info(menu,"Output preview",view.output);
        info(menu,"Body direction",view.last.direction.empty()?"Waiting for spin evidence":view.last.direction);
    } else note("Enable terminology and perform a trick to capture its native components.");
    info(menu,"Detection",view.status);info(menu,"UI boundary passes",std::to_string(view.boundary_scans));
    if (!view.message.empty()) note(view.message.c_str());
    end_card();
}
} // namespace dingosdk::overlay::menu
