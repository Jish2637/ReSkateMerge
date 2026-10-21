#include "skate_menu_internal.h"
#include "Engine/Game/Multiplayer/msc_staff.h"
#include "Engine/Core/Text/word_filter.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>

namespace dingosdk::overlay::menu {
namespace {
std::string selected_game="coinflip",staff_group,staff_action_id;
std::map<std::string,std::array<char,32>> stakes;
std::map<std::string,std::array<std::array<char,257>,6>> staff_fields;
std::uint64_t selected_player{};
std::optional<Json> game_review;
bool game_review_open{};
std::string game_review_text;
std::uint64_t game_review_epoch{};
struct Reveal {std::uint64_t pending{},result{};double started{-1000};};
std::map<std::string,Reveal> reveals;
std::string safe(const Model& model,const std::string& s){return model.multiplayer.chat_filter?text::mask_bad_words(s):s;}
std::string money(std::uint64_t n){return std::to_string(n)+" MSC";}
bool whole(std::string_view s,std::uint64_t& value) {const auto r=std::from_chars(s.data(),s.data()+s.size(),value);return !s.empty() && r.ec==std::errc{} && r.ptr==s.data()+s.size();}
void game_request(const msc::GameView& g,const char* action,std::uint64_t stake,const MscRequest& send) {
    auto b=Json::object();b["op"]="game";b["game"]=g.id;b["action"]=action;b["round"]=g.round?g.round->id:0;b["stake"]=stake;send(std::move(b),true);
}
void reserve(const msc::GameView& g,const char* action,std::uint64_t amount,std::uint64_t epoch) {
    auto b=Json::object();b["op"]="game";b["game"]=g.id;b["action"]=action;b["round"]=g.round?g.round->id:0;b["stake"]=amount;
    game_review=std::move(b);game_review_epoch=epoch;game_review_text="Reserve "+money(amount)+" for "+g.label+"?";
    game_review_open=true;
}
void die(unsigned value,float size) {
    const auto at=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(at,ImVec2(at.x+size,at.y+size),IM_COL32(235,237,242,255),px(6));
    const auto dot=[&](float x,float y){draw->AddCircleFilled(ImVec2(at.x+x*size,at.y+y*size),size*.065f,IM_COL32(18,22,30,255));};
    if(value%2)dot(.5f,.5f);if(value>=2){dot(.25f,.25f);dot(.75f,.75f);}if(value>=4){dot(.75f,.25f);dot(.25f,.75f);}if(value==6){dot(.25f,.5f);dot(.75f,.5f);}
    ImGui::Dummy(ImVec2(size,size));
}
void result(SkateMenu& menu,const Model& model,const msc::GameView& game,const msc::GameRound& round,std::uint64_t viewer) {
    auto& reveal=reveals[game.id];
    if(reveal.result!=round.id){reveal.result=round.id;reveal.started=reveal.pending==round.id && round.state=="settled"?ImGui::GetTime():-1000;}
    const double elapsed=ImGui::GetTime()-reveal.started;
    const double step=.65, duration=game.id=="dice" && !round.rolls.empty()?step*round.rolls.size():game.id=="coinflip"?3.0:2.4;
    const bool showing=elapsed<duration;
    begin_card(menu,"msc-game-result",showing?"REVEALING RESULT":"ACTIVITY RESULT");
    ImGui::Text("%s | Round #%llu",game.label.c_str(),static_cast<unsigned long long>(round.id));
    if(game.id=="dice" && !round.rolls.empty()) {
        const auto visible=showing?std::min(round.rolls.size(),static_cast<std::size_t>(elapsed/step)+1):round.rolls.size();
        if(showing)note("Rolling each die in order... Tied highest rolls reroll.");
        if(ImGui::BeginTable("msc-dice-rolls",3,ImGuiTableFlags_SizingStretchSame)) {
            for(std::size_t i=0;i<visible;++i) {
                const auto& r=round.rolls[i];const bool rolling=showing && i+1==visible;
                ImGui::TableNextColumn();
                const auto who=std::ranges::find_if(round.members,[&](const auto& p){return p.id==r.player;});
                ImGui::TextUnformatted(who==round.members.end()?std::to_string(r.player).c_str():safe(model,who->name).c_str());
                die(rolling?1+static_cast<unsigned>(elapsed*18)%6:static_cast<unsigned>(r.value),px(52));
                ImGui::Text("Round %llu%s",static_cast<unsigned long long>(r.round),rolling?" - rolling":"");
            }ImGui::EndTable();
        }
    } else if(showing) {
        if(game.id=="coinflip") {
            const auto at=ImGui::GetCursorScreenPos();const float width=px(56)*(.12f+.88f*std::abs(std::cos(static_cast<float>(elapsed)*12)));
            ImGui::GetWindowDrawList()->AddEllipseFilled(ImVec2(at.x+px(64),at.y+px(64)),ImVec2(width,px(56)),IM_COL32(246,195,64,255));
            ImGui::Dummy(ImVec2(px(128),px(128)));note("Coin flipping...");
        } else {ImGui::PushFont(menu.title);ImGui::Text("%llu  ->  ?",static_cast<unsigned long long>(round.initial));ImGui::PopFont();note("Turning the next card...");}
    }
    if(showing) {
        ImGui::ProgressBar(static_cast<float>(elapsed/duration),ImVec2(-1,px(8)),"");
        note("Revealing the server result. Settlement has already been recorded.");
    } else {
        const bool member=std::ranges::any_of(round.members,[&](const auto& p){return p.id==viewer;});
        const bool returned=round.state=="refunded" || round.my_payout==round.stake;
        const bool won=round.my_payout>round.stake;
        const char* title=!member?"ROUND COMPLETE":returned?"STAKE RETURNED":won?"YOU WON!":"YOU LOST";
        const auto tint=!member?IM_COL32(235,237,242,255):returned?IM_COL32(246,195,64,255):won?IM_COL32(100,235,145,255):IM_COL32(255,115,115,255);
        ImGui::PushStyleColor(ImGuiCol_Text,tint);ImGui::PushFont(menu.title);ImGui::TextUnformatted(title);ImGui::PopFont();ImGui::PopStyleColor();
        if(member) {
            const auto net=static_cast<std::int64_t>(round.my_payout)-static_cast<std::int64_t>(round.stake);
            ImGui::Text("Net change: %s%lld MSC",net>0?"+":"",static_cast<long long>(net));
            ImGui::Text("Payout: %s | Stake: %s",money(round.my_payout).c_str(),money(round.stake).c_str());
        }
        if(round.winner){const auto who=std::ranges::find_if(round.members,[&](const auto& p){return p.id==round.winner;});ImGui::TextWrapped("Winner: %s | Pot: %s",who==round.members.end()?std::to_string(round.winner).c_str():safe(model,who->name).c_str(),money(round.pot).c_str());}
        if(game.id=="highlow" && round.second)ImGui::Text("Cards: %llu -> %llu | Choice: %s",static_cast<unsigned long long>(round.initial),static_cast<unsigned long long>(round.second),round.direction.c_str());
        ImGui::TextWrapped("%s",safe(model,round.result).c_str());
    }
    end_card();
}
bool staff_allowed(const msc::StaffView& data,const std::string& id){return std::ranges::find(data.actions,id)!=data.actions.end();}
void copy(std::array<char,257>& buffer,const std::string& value){std::snprintf(buffer.data(),buffer.size(),"%s",value.c_str());}
}
void reset_msc_features() {
    reveals.clear();game_review.reset();game_review_open=false;game_review_epoch=0;selected_player=0;staff_fields.clear();staff_action_id.clear();staff_group.clear();
}
void draw_msc_games(SkateMenu& menu,const Model& model,const msc::Snapshot& data,bool disabled,double received,const MscRequest& send) {
    if(data.games.empty()){note("Loading server game states...");return;}
    section(menu,"MSC GAMES");
    for(const auto& game:data.games){ImGui::PushID(game.id.c_str());if(ImGui::Button(game.label.c_str()))selected_game=game.id;ImGui::PopID();ImGui::SameLine();}ImGui::NewLine();
    auto it=std::ranges::find_if(data.games,[](const auto& g){return g.id==selected_game;});if(it==data.games.end()){it=data.games.begin();selected_game=it->id;}
    const auto& game=*it;
    begin_card(menu,"msc-game-current",game.label.c_str(),"Game results and wallet changes come from the server.");
    if(!game.enabled)note(game.reason.c_str());
    ImGui::Text("Stake limits: %s - %s",money(game.minimum).c_str(),money(game.maximum).c_str());
    ImGui::Text("Spendable wallet: %s",money(data.profile.balance).c_str());
    if(data.profile.unlimited)note("Wagers use real spendable wallet funds, including for administrators.");
    if(!game.round) {
        note(game.id=="highlow"?"Reserve a stake, then predict higher or lower. Numbers are 1-13; a tie refunds the stake.":"Open or join an equal-stake lobby. Its creator starts the game; the winner receives the full pot.");
        auto& input=stakes[game.id];if(!input[0])std::snprintf(input.data(),input.size(),"%llu",static_cast<unsigned long long>(game.minimum));
        ImGui::InputText("Stake",input.data(),input.size(),ImGuiInputTextFlags_CharsDecimal);
        std::uint64_t amount{};const bool valid=whole(input.data(),amount) && amount>=game.minimum && amount<=game.maximum && amount<=data.profile.balance;
        ImGui::BeginDisabled(disabled || !game.enabled || !valid || !msc::game_allows(game,"create"));
        if(ImGui::Button(game.id=="highlow"?"Reserve stake and draw first card":"Create lobby and reserve stake"))reserve(game,"create",amount,data.epoch);
        ImGui::EndDisabled();
    } else {
        const auto& round=*game.round;reveals[game.id].pending=round.id;
        const double seconds=std::max(0.0,static_cast<double>(round.seconds)-(ImGui::GetTime()-received));
        ImGui::Text("Round #%llu | Stake %s | Pot %s",static_cast<unsigned long long>(round.id),money(round.stake).c_str(),money(round.pot).c_str());
        ImGui::Text("Refund timeout: %.0f seconds",seconds);
        if(game.id=="highlow") {
            ImGui::PushFont(menu.title);ImGui::Text("%llu",static_cast<unsigned long long>(round.initial));ImGui::PopFont();
            ImGui::Text("Higher: %llu winning numbers | Lower: %llu",static_cast<unsigned long long>(13-round.initial),static_cast<unsigned long long>(round.initial-1));
            note("Ties refund. Winning gross payout is 12 x stake / winning numbers, with fair rounding.");
            for(const auto* action:{"higher","lower"}) {
                ImGui::BeginDisabled(disabled || seconds<=0 || !msc::game_allows(game,action));
                if(ImGui::Button(action))game_request(game,action,0,send);ImGui::EndDisabled();ImGui::SameLine();
            }ImGui::NewLine();
        } else {
            ImGui::Text("Players: %llu / %llu",static_cast<unsigned long long>(round.stake?round.pot/round.stake:round.members.size()),static_cast<unsigned long long>(round.capacity));
            if(round.stake && round.pot/round.stake>round.members.size())note("Showing up to ten members, including you, the creator and winner. The count and pot include everyone.");
            if(ImGui::BeginTable("msc-game-members",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Player",ImGuiTableColumnFlags_WidthStretch);ImGui::TableSetupColumn("Status",ImGuiTableColumnFlags_WidthFixed,px(90));
                for(const auto& p:round.members){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(safe(model,p.name).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(p.id==round.creator?"Creator":"Joined");}ImGui::EndTable();
            }
            ImGui::BeginDisabled(disabled || seconds<=0 || !msc::game_allows(game,"join"));if(ImGui::Button("Join and reserve equal stake"))reserve(game,"join",round.stake,data.epoch);ImGui::EndDisabled();
            ImGui::BeginDisabled(disabled || seconds<=0 || !msc::game_allows(game,"start"));if(ImGui::Button("Start game"))game_request(game,"start",0,send);ImGui::EndDisabled();
            ImGui::BeginDisabled(disabled || !msc::game_allows(game,"leave"));if(ImGui::Button("Leave and refund stake"))game_request(game,"leave",0,send);ImGui::EndDisabled();
        }
        ImGui::BeginDisabled(disabled || !msc::game_allows(game,"cancel"));if(ImGui::Button(game.id=="highlow"?"Cancel and refund stake":"Cancel lobby and refund everyone"))game_request(game,"cancel",0,send);ImGui::EndDisabled();
    }
    end_card();
    if(game.previous)result(menu,model,game,*game.previous,data.player);
    // Open and draw in the same parent window/ID scope, after either card style ends.
    if(game_review_open){ImGui::OpenPopup("Reserve MSC stake");game_review_open=false;}
    if(ImGui::BeginPopupModal("Reserve MSC stake",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s",game_review_text.c_str());
        bool valid=game_review && game_review_epoch==data.epoch;
        if(valid){const auto match=std::ranges::find_if(data.games,[&](const auto& g){return g.id==game_review->at("game").string();});valid=match!=data.games.end() && msc::game_allows(*match,game_review->at("action").string()) && (match->round?match->round->id:0)==game_review->at("round").get<std::uint64_t>();}
        ImGui::BeginDisabled(disabled || !valid);if(ImGui::Button("Confirm stake") && !disabled && valid && send(*game_review,true)){game_review.reset();ImGui::CloseCurrentPopup();}ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("Cancel")){game_review.reset();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
    }
}
void draw_msc_staff(SkateMenu& menu,const Model& model,const msc::Snapshot& data,bool disabled,double received,const MscRequest& send) {
    if(!data.staff){note("Loading authorized staff tools...");return;}
    const auto& staff=*data.staff;
    section(menu,"STAFF TOOLS");ImGui::Text("Your server role: %s",staff.role.c_str());
    note("Only actions granted by your current server role are listed. Changes require review and confirmation.");
    if(ImGui::CollapsingHeader("Connected players",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::BeginTable("msc-staff-players",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Player");ImGui::TableSetupColumn("Role / rank");ImGui::TableSetupColumn("Voice / chat");ImGui::TableSetupColumn("Tool locks");ImGui::TableHeadersRow();
            for(const auto& p:staff.players) {
                ImGui::TableNextRow();ImGui::TableNextColumn();const auto name=safe(model,p.name)+"##staff-"+std::to_string(p.id);
                if(ImGui::Selectable(name.c_str(),selected_player==p.id))selected_player=p.id;
                ImGui::TableNextColumn();ImGui::Text("%s / %llu",p.role.c_str(),static_cast<unsigned long long>(p.rank));
                ImGui::TableNextColumn();ImGui::Text("%s / %s",p.voice_muted?"muted":"on",p.text_muted?"timeout":"on");
                ImGui::TableNextColumn();ImGui::Text("%llu",static_cast<unsigned long long>(p.locks));
            }ImGui::EndTable();
        }
        ImGui::BeginDisabled(disabled || staff.page<=1);if(ImGui::Button("Previous players")){auto b=Json::object();b["op"]="staff";b["page"]=staff.page-1;send(std::move(b),false);}ImGui::EndDisabled();
        ImGui::SameLine();ImGui::Text("Page %llu / %llu",static_cast<unsigned long long>(staff.page),static_cast<unsigned long long>(staff.pages));
        ImGui::BeginDisabled(disabled || staff.page>=staff.pages);if(ImGui::Button("Next players")){auto b=Json::object();b["op"]="staff";b["page"]=staff.page+1;send(std::move(b),false);}ImGui::EndDisabled();
    }
    std::vector<std::string> groups;for(const auto& a:msc::staff_actions())if(staff_allowed(staff,a.id) && std::ranges::find(groups,a.group)==groups.end())groups.push_back(a.group);
    if(groups.empty()){note("No staff forms are available for this role.");return;}
    if(std::ranges::find(groups,staff_group)==groups.end())staff_group=groups.front();
    if(ImGui::BeginCombo("Tool category",staff_group.c_str())){for(const auto& g:groups)if(ImGui::Selectable(g.c_str(),g==staff_group)){staff_group=g;staff_action_id.clear();}ImGui::EndCombo();}
    std::vector<const msc::StaffAction*> actions;for(const auto& a:msc::staff_actions())if(a.group==staff_group && staff_allowed(staff,a.id))actions.push_back(&a);
    auto selected=std::ranges::find_if(actions,[](const auto* a){return a->id==staff_action_id;});if(selected==actions.end()){selected=actions.begin();staff_action_id=(*selected)->id;}
    const auto* spec=*selected;
    if(ImGui::BeginCombo("Action",spec->label.c_str())){for(const auto* a:actions)if(ImGui::Selectable(a->label.c_str(),a->id==staff_action_id))staff_action_id=a->id;ImGui::EndCombo();}
    spec=msc::staff_action(staff_action_id);if(!spec)return;
    begin_card(menu,"msc-staff-form",spec->label.c_str());
    auto setting=staff.settings.find(spec->id);if(setting==staff.settings.end())setting=staff.settings.find(msc::staff_permission(*spec));if(setting!=staff.settings.end())ImGui::TextWrapped("Current: %s",setting->second.c_str());
    auto& fields=staff_fields[spec->id];bool complete=true;
    for(std::size_t i=0;i<spec->fields.size() && i<fields.size();++i) {
        const auto& f=spec->fields[i];auto& value=fields[i];ImGui::PushID(static_cast<int>(i));
        const auto label=f.label+(f.optional?" (optional)":"");
        if(f.type=="choice" || f.type=="role") {
            const auto& choices=f.type=="role"?staff.roles:f.choices;
            if(!value[0] && !f.optional && !choices.empty())copy(value,choices.front());
            if(ImGui::BeginCombo(label.c_str(),value[0]?value.data():"Default")){if(f.optional && ImGui::Selectable("Default",!value[0]))value[0]=0;for(const auto& c:choices)if(ImGui::Selectable(c.c_str(),c==value.data()))copy(value,c);ImGui::EndCombo();}
        } else if(f.type=="player") {
            const auto who=std::ranges::find_if(staff.players,[&](const auto& p){return std::to_string(p.id)==value.data();});
            const auto shown=who==staff.players.end()?std::string(value[0]?value.data():"Select player"):safe(model,who->name);
            if(ImGui::BeginCombo(label.c_str(),shown.c_str())){if(f.optional && ImGui::Selectable("Default",!value[0]))value[0]=0;for(const auto& p:staff.players){const auto name=safe(model,p.name)+"##"+std::to_string(p.id);if(ImGui::Selectable(name.c_str(),std::to_string(p.id)==value.data()))copy(value,std::to_string(p.id));}ImGui::EndCombo();}
            if(selected_player && ImGui::Button("Use selected player"))copy(value,std::to_string(selected_player));
        } else {
            const auto flags=f.type=="secret"?ImGuiInputTextFlags_Password:f.type=="number" || f.type=="object" || f.type=="identity"?ImGuiInputTextFlags_CharsDecimal:ImGuiInputTextFlags_None;
            ImGui::InputText(label.c_str(),value.data(),value.size(),flags);
            if(f.type=="identity" && selected_player && ImGui::Button("Use selected Steam ID"))copy(value,std::to_string(selected_player));
            if(f.type=="map" && !model.multiplayer.server_maps.empty() && ImGui::BeginCombo("Known maps",value[0]?value.data():"Choose a map")){for(const auto& map:model.multiplayer.server_maps)if(ImGui::Selectable(map.c_str()))copy(value,map);ImGui::EndCombo();}
        }
        if(!f.optional && !value[0])complete=false;ImGui::PopID();
    }
    ImGui::BeginDisabled(disabled || !complete);
    if(ImGui::Button(spec->read_only?"Run query":"Review action")) {
        auto b=Json::object();b["op"]=spec->read_only?"staff-read":"staff-preview";b["action"]=spec->id;auto args=Json::array();for(std::size_t i=0;i<spec->fields.size();++i)args.push_back(fields[i].data());b["args"]=std::move(args);
        if(send(std::move(b),!spec->read_only))for(std::size_t i=0;i<spec->fields.size();++i)if(spec->fields[i].type=="secret")fields[i].fill(0);
    }ImGui::EndDisabled();end_card();
    if(staff.quote) {
        const auto& q=*staff.quote;begin_card(menu,"msc-staff-review","CONFIRM STAFF ACTION");ImGui::TextWrapped("%s",q.description.c_str());
        const auto seconds=std::max(0.0,static_cast<double>(q.seconds)-(ImGui::GetTime()-received));ImGui::Text("Review expires in %.0f seconds",seconds);
        note("This confirms only the reviewed action. Build overwrites, unloads and deletions retain their separate server confirmation.");
        ImGui::BeginDisabled(disabled || seconds<=0);if(ImGui::Button("Confirm reviewed staff action")){auto b=Json::object();b["op"]="staff-confirm";b["token"]=q.token;send(std::move(b),true);}ImGui::EndDisabled();
        ImGui::BeginDisabled(disabled);if(ImGui::Button("Discard staff review")){auto b=Json::object();b["op"]="staff-cancel";send(std::move(b),true);}ImGui::EndDisabled();end_card();
    }
    begin_card(menu,"msc-staff-result","SERVER RESULT");
    if(staff.output.empty())note("Queries and completed actions appear here.");else ImGui::TextWrapped("%s",safe(model,staff.output).c_str());
    end_card();
}
} // namespace dingosdk::overlay::menu
