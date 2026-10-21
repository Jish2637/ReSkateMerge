#include "Engine/Game/Multiplayer/msc_actions.h"
#include "skate_menu_internal.h"
#include "overlay_internal.h"
#include "nametag_gradient.h"
#include "Engine/Core/Text/word_filter.h"
#include <atomic>
#include <charconv>
#include <cstdio>

namespace dingosdk::overlay {
namespace { std::atomic<MscFeed> msc_feed_callback{}; }
void set_msc_feed(MscFeed feed) noexcept { msc_feed_callback.store(feed); }
namespace menu {
namespace {
msc::ClientState state;
bool active{};
std::optional<Json> deferred_read;
int tab{}, shop_tab{}, leader_kind{};
double refreshed{};
std::array<char,64> tag{};
std::array<char,256> joinmsg{};
std::array<char,32> amount{};
std::uint64_t recipient{},confirm_recipient{},confirm_amount{};
std::string confirm_name,preview_colour,preview_tag;
std::uint32_t preview_tint=0xffffffffU;
bool preview_selected{};
const char* boards[]{"weekly","points","time","events"};

bool request(Json body,bool mutation=false) {
    const double now=ImGui::GetTime();
    if(!state.connected || !state.data || (mutation && state.stale(now)))return false;
    if(state.pending) {if(!mutation)deferred_read=std::move(body);return false;}
    body["section"]=tab==4?"games":tab==5?"staff":"shop";
    body["v"]=msc::version;body["epoch"]=std::to_string(state.epoch);body["request"]=++state.next_request;
    if(!detail::queue_multiplayer_action("msc",body.dump())) {state.message="MSC request could not be queued.";return false;}
    state.pending=state.next_request;state.sent=now;refreshed=now;return true;
}
bool read(const char* op) {auto body=Json::object();body["op"]=op;return request(std::move(body));}
void item(std::string kind,std::string value) {
    auto body=Json::object();body["op"]="item";body["kind"]=kind;body["value"]=value;request(std::move(body),true);
}
void page(const char* op,std::uint64_t number) {
    auto body=Json::object();body["op"]=op;body["page"]=number;
    if(std::string_view(op)=="leaderboard")body["kind"]=boards[leader_kind];
    request(std::move(body));
}
std::string points(std::uint64_t n) {return std::to_string(n)+" MSC";}
std::string duration(std::uint64_t n) {return std::to_string(n/3600)+"h "+std::to_string((n/60)%60)+"m";}
std::string filtered(const Model& model,const std::string& value) {return model.multiplayer.chat_filter?text::mask_bad_words(value):value;}
void preview(SkateMenu& menu,const Model& model,const msc::Snapshot& data) {
    begin_card(menu,"msc-preview","COSMETIC PREVIEW","Menu sample only. Your character changes after the server accepts an equip or purchase.");
    const auto equipped=std::ranges::find_if(data.products,[&](const auto& p){return p.value==data.profile.colour && p.colour!=0;});
    const auto tint=preview_selected?preview_tint:equipped!=data.products.end()?equipped->colour:0xffffffffU;
    const auto colour=detail::animated_nametag_colour(tint,ImGui::GetTime());
    const auto shown_tag=preview_tag.empty()?data.profile.tag:preview_tag;
    const auto sample=(shown_tag.empty()?std::string{}:"["+shown_tag+"] ")+data.profile.name;
    auto* draw=ImGui::GetWindowDrawList();const int first_vertex=draw->VtxBuffer.Size;
    ImGui::PushStyleColor(ImGuiCol_Text,colour);ImGui::TextWrapped("%s",filtered(model,sample).c_str());ImGui::PopStyleColor();
    const auto left=ImGui::GetItemRectMin().x;const auto width=ImGui::GetItemRectSize().x;
    detail::shade_nametag_gradient(draw,first_vertex,left,width,tint,ImGui::GetTime());
    ImGui::TextWrapped("Join message: %s",filtered(model,joinmsg[0]?std::string(joinmsg.data()):data.profile.joinmsg).c_str());
    if(preview_selected)ImGui::TextWrapped("Selected: %s",preview_colour.c_str());
    if(ImGui::Button("Reset sample")){preview_selected=false;preview_tag.clear();preview_colour.clear();}
    end_card();
}
void quote(SkateMenu& menu,const msc::Snapshot& data,bool disabled) {
    if(!data.quote)return;
    const auto& q=*data.quote;
    begin_card(menu,"msc-quote","CONFIRM PURCHASE","This server quote applies only to the item shown below.");
    ImGui::TextWrapped("%s: %s",q.kind.c_str(),q.value=="shimmer-green"?"Shimmer cyan":q.value.c_str());ImGui::Text("Cost: %s",points(q.price).c_str());
    const double remaining=static_cast<double>(q.seconds)-(ImGui::GetTime()-state.received);
    ImGui::Text("Expires in %.0f seconds",std::max(0.0,remaining));
    ImGui::BeginDisabled(disabled || remaining<=0);
    if(ImGui::Button("Confirm this purchase")){auto b=Json::object();b["op"]="confirm";b["token"]=q.token;request(std::move(b),true);}
    ImGui::EndDisabled();end_card();
}
void shop(SkateMenu& menu,const Model& model,const msc::Snapshot& data,bool disabled) {
    const auto previous=shop_tab;
    category_tabs(menu,shop_tab,{"SHOP","COLLECTION"},"msc-shop-sections");
    if(shop_tab!=previous && shop_tab==1)page("collection",data.collection_page);
    preview(menu,model,data);
    if(shop_tab==0) {
        ImGui::InputText("Custom tag",tag.data(),tag.size());
        if(ImGui::Button("Preview tag sample")){preview_tag=tag.data();}
        ImGui::InputText("Join message",joinmsg.data(),joinmsg.size());
        const float available=ImGui::GetContentRegionAvail().x;
        const int columns=available>=px(600)?3:available>=px(390)?2:1;
        if(ImGui::BeginTable("msc-catalogue",columns,ImGuiTableFlags_SizingStretchSame)) {
            for(const auto& p:data.products) {
                ImGui::TableNextColumn();ImGui::PushID(p.id.c_str());
                const auto display_label=p.value=="shimmer-green"?std::string("Shimmer cyan"):p.label;
                begin_card(menu,p.id.c_str(),display_label.c_str());
                if(p.colour) {const auto c=detail::animated_nametag_colour(p.colour,ImGui::GetTime());ImGui::ColorButton("Colour sample",ImGui::ColorConvertU32ToFloat4(c),ImGuiColorEditFlags_NoTooltip,ImVec2(px(56),px(24)));}
                ImGui::TextWrapped("%s",p.equipped?"Equipped":p.owned?"Owned permanently":p.id.starts_with("colour:")?"Paid per change unless bundle owned":"Permanent purchase");
                ImGui::TextWrapped("%s",points(p.price).c_str());
                if(p.colour && ImGui::Button("Preview")){preview_selected=true;preview_tint=p.colour;preview_colour=display_label;}
                const auto label=msc::product_label(p);
                const auto action=msc::product_action(p,tag.data(),joinmsg.data());
                const bool no_text=(p.id=="tag" && !tag[0]) || (p.id=="joinmsg" && !joinmsg[0]);
                ImGui::BeginDisabled(disabled || !p.enabled || p.equipped || no_text || !action);
                if(ImGui::Button(label.c_str()) && action)request(*action,true);
                ImGui::EndDisabled();if(!p.enabled)ImGui::TextWrapped("%s",p.reason.c_str());
                end_card();ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        begin_card(menu,"msc-owned-tags","YOUR TAGS");
        for(const auto& t:data.tags) {
            ImGui::PushID(t.text.c_str());ImGui::TextWrapped("[%s] %s",filtered(model,t.text).c_str(),t.equipped?"Equipped":!t.enabled?"Unavailable":"Owned");
            if(ImGui::Button("Preview tag"))preview_tag=t.text;
            ImGui::SameLine();ImGui::BeginDisabled(disabled || !t.enabled || t.equipped);
            if(ImGui::Button("Equip"))item("tag",t.text);ImGui::EndDisabled();ImGui::PopID();
        }
        if(data.tags.empty())note("No owned tags on this page.");
        ImGui::BeginDisabled(disabled || data.collection_page<=1);if(ImGui::Button("Previous tags"))page("collection",data.collection_page-1);ImGui::EndDisabled();
        ImGui::SameLine();ImGui::Text("Page %llu / %llu",static_cast<unsigned long long>(data.collection_page),static_cast<unsigned long long>(data.collection_pages));
        ImGui::BeginDisabled(disabled || data.collection_page>=data.collection_pages);if(ImGui::Button("Next tags"))page("collection",data.collection_page+1);ImGui::EndDisabled();
        end_card();
        begin_card(menu,"msc-owned-shimmers","YOUR SHIMMERS");
        for(const auto& p:data.products)if(p.id.starts_with("shimmer:") && p.owned) {
            ImGui::PushID(p.id.c_str());ImGui::TextUnformatted(p.value=="shimmer-green"?"Shimmer cyan":p.label.c_str());
            ImGui::BeginDisabled(disabled || !p.enabled || p.equipped);if(ImGui::Button(p.equipped?"Equipped":"Equip"))item("colour",p.value);ImGui::EndDisabled();ImGui::PopID();
        }
        end_card();
        ImGui::BeginDisabled(disabled || !data.shop);
        if(ImGui::Button("Turn tag off"))item("tag","off");ImGui::SameLine();if(ImGui::Button("Default colour"))item("colour","default");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(disabled || !data.can_joinmsg || !data.profile.joinmsg_owned);
        if(ImGui::Button(data.profile.joinmsg_on?"Disable join message":"Enable join message")) {auto b=Json::object();b["op"]="joinmsg-toggle";b["enabled"]=!data.profile.joinmsg_on;request(std::move(b),true);}
        ImGui::EndDisabled();
    }
}
void profile(SkateMenu& menu,const msc::Snapshot& data) {
    const auto& p=data.profile;
    begin_card(menu,"msc-account","YOUR MSC");
    if(!data.stats)note(data.paused?"Player data is paused. Contact server staff.":"Stats unavailable while disabled or loading.");
    ImGui::Text("Balance: %s",p.unlimited?"Unlimited points":points(p.balance).c_str());
    ImGui::Text("Lifetime: %s | Weekly: %s",points(p.lifetime).c_str(),points(p.weekly).c_str());
    ImGui::Text("Active playtime: %s",duration(p.active_seconds).c_str());
    ImGui::Text("Daily streak: %llu days | Visits: %llu",static_cast<unsigned long long>(p.streak),static_cast<unsigned long long>(p.visits));
    const float progress=p.daily_target?std::min(1.0f,static_cast<float>(p.daily_seconds)/static_cast<float>(p.daily_target)):1.0f;
    ImGui::ProgressBar(progress,ImVec2(-1,0),p.daily_done?"Daily reward received":"Daily reward progress");
    note("Daily rewards are automatic. The daily window resets at 00:00 UTC.");
    ImGui::Text("Participation: SKATE %llu | Spot Battle %llu | Jam %llu",static_cast<unsigned long long>(p.events[0]),static_cast<unsigned long long>(p.events[1]),static_cast<unsigned long long>(p.events[2]));
    ImGui::TextWrapped("VIP: %s",p.vip.c_str());if(p.vip_seconds)ImGui::Text("Active time remaining: %s",duration(p.vip_seconds).c_str());
    end_card();
}
void leaderboard(SkateMenu& menu,const Model& model,const msc::Snapshot& data,bool disabled) {
    begin_card(menu,"msc-leaderboard","LEADERBOARD");
    if(ImGui::Combo("Ranking",&leader_kind,boards,4))page("leaderboard",1);
    if(ImGui::Button("Load leaderboard"))page("leaderboard",1);
    if(ImGui::BeginTable("msc-leaders",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Rank");ImGui::TableSetupColumn("Player");ImGui::TableSetupColumn("Value");ImGui::TableHeadersRow();
        for(const auto& row:data.leaders) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%llu",static_cast<unsigned long long>(row.rank));
            ImGui::TableNextColumn();ImGui::TextWrapped("%s",filtered(model,row.name).c_str());
            ImGui::TableNextColumn();ImGui::TextUnformatted(data.leader_kind=="time"?duration(row.value/1000000).c_str():std::to_string(row.value).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(disabled || data.leader_page<=1);if(ImGui::Button("Previous ranks"))page("leaderboard",data.leader_page-1);ImGui::EndDisabled();
    ImGui::SameLine();ImGui::Text("Page %llu / %llu",static_cast<unsigned long long>(data.leader_page),static_cast<unsigned long long>(data.leader_pages));
    ImGui::BeginDisabled(disabled || data.leader_page>=data.leader_pages);if(ImGui::Button("Next ranks"))page("leaderboard",data.leader_page+1);ImGui::EndDisabled();end_card();
}
void transfer(SkateMenu& menu,const Model& model,const msc::Snapshot& data,bool disabled) {
    begin_card(menu,"msc-transfer","TRANSFER MSC","Choose a connected player. Transfers are handled and saved by the server.");
    auto found=std::ranges::find_if(model.multiplayer.roster,[&](const auto& p){return p.id==recipient;});
    const auto current=found==model.multiplayer.roster.end()?std::string("Select player"):filtered(model,found->name);
    if(ImGui::BeginCombo("Recipient",current.c_str())) {
        for(const auto& p:model.multiplayer.roster)if(p.id!=data.player && p.id!=model.multiplayer.host_id) {
            const auto label=filtered(model,p.name)+"##"+std::to_string(p.id);
            if(ImGui::Selectable(label.c_str(),recipient==p.id))recipient=p.id;
        }ImGui::EndCombo();
    }
    found=std::ranges::find_if(model.multiplayer.roster,[&](const auto& p){return p.id==recipient;});
    ImGui::InputText("Whole MSC amount",amount.data(),amount.size(),ImGuiInputTextFlags_CharsDecimal);
    std::uint64_t value{};const std::string_view input(amount.data());const auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);
    const bool valid=value>0 && value<=9000000000000000ULL && parsed.ec==std::errc{} && parsed.ptr==input.data()+input.size() && found!=model.multiplayer.roster.end() && recipient!=data.player;
    ImGui::BeginDisabled(disabled || !data.can_pay || !valid);
    if(ImGui::Button("Review transfer")) {confirm_recipient=recipient;confirm_amount=value;confirm_name=filtered(model,found->name);ImGui::OpenPopup("Confirm MSC transfer");}
    ImGui::EndDisabled();
    if(ImGui::BeginPopupModal("Confirm MSC transfer",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Send %s to %s?",points(confirm_amount).c_str(),confirm_name.c_str());
        ImGui::Text("Steam ID: %s",std::to_string(confirm_recipient).c_str());
        const bool still_present=std::ranges::any_of(model.multiplayer.roster,[&](const auto& p){return p.id==confirm_recipient;});
        ImGui::BeginDisabled(disabled || !data.can_pay || !still_present);
        if(ImGui::Button("Confirm transfer")){auto b=Json::object();b["op"]="pay";b["recipient"]=std::to_string(confirm_recipient);b["amount"]=confirm_amount;request(std::move(b),true);ImGui::CloseCurrentPopup();}
        ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    end_card();
}
}
void close_msc_hub() {
    if(active){deferred_read.reset();if(state.data && !state.pending)read("close");active=false;}
}
bool draw_msc_hub(SkateMenu& menu,const Model& model) {
    const double now=ImGui::GetTime();const auto feed=msc_feed_callback.load();const auto source=feed?feed():msc::Feed{};
    if(source.epoch!=state.epoch || source.host!=state.host || !source.connected){deferred_read.reset();confirm_recipient=0;confirm_amount=0;recipient=0;reset_msc_features();}
    state.observe(source,now);state.timeout(now);
    if(!state.connected)return false;
    if(!active){active=true;refreshed=-1e9;state.received=-1e9;}
    if(!state.attempted) {
        state.opened=now;
        if(detail::queue_multiplayer_action("msc-hello",std::to_string(state.epoch)))state.attempted=true;
    }
    if(!state.data) {
        note(state.fallback(now)?"Structured MSC interface unavailable. Using the command-based hub.":"Connecting to the MSC interface...");
        return !state.fallback(now);
    }
    if(!state.pending && deferred_read){auto next=std::move(*deferred_read);deferred_read.reset();request(std::move(next));}
    if(!state.pending && now-refreshed>=15.0)read("snapshot");
    const auto& data=*state.data;const bool disabled=state.pending || state.stale(now);
    section(menu,"MSC HUB");
    ImGui::Text("Balance: %s",data.profile.unlimited?"Unlimited points":points(data.profile.balance).c_str());
    if(state.pending)note("Waiting for server...");
    else if(state.stale(now))note("Data is stale. Refresh before purchasing or transferring.");
    if(!state.message.empty())note(state.message.c_str());
    ImGui::BeginDisabled(state.pending!=0);if(ImGui::Button("Refresh MSC"))read("snapshot");ImGui::EndDisabled();
    if(data.paused)note("Server player-data storage is paused; transactions are unavailable.");
    const int previous_tab=tab;
    if(data.staff_available)category_tabs(menu,tab,{"SHOP","MY MSC","LEADERBOARD","TRANSFER","ACTIVITIES","STAFF"},"msc-main-tabs");
    else {if(tab>4)tab=0;category_tabs(menu,tab,{"SHOP","MY MSC","LEADERBOARD","TRANSFER","ACTIVITIES"},"msc-main-tabs");}
    if(previous_tab!=tab) {if(tab==4 && data.games_supported)read("games");else if(tab==5)read("staff");else if(tab==2)page("leaderboard",1);else if(previous_tab>=4)read("snapshot");else if(tab==0 && shop_tab==1)page("collection",data.collection_page);}
    // Child flattens navigation so both menu shells retain normal controller focus/scrolling.
    ImGui::BeginChild("msc-content",ImVec2(0,page_body_height(menu)),ImGuiChildFlags_NavFlattened);
    if(tab==0)shop(menu,model,data,disabled);
    else if(tab==1)profile(menu,data);
    else if(tab==2)leaderboard(menu,model,data,disabled);
    else if(tab==3)transfer(menu,model,data,disabled);
    else if(tab==4 && data.games_supported)draw_msc_games(menu,model,data,disabled,state.received,request);
    else if(tab==5)draw_msc_staff(menu,model,data,disabled,state.received,request);
    else {
        begin_card(menu,"msc-activities","SERVER ACTIVITIES","This server provides the original command interface. Install the companion game-interface server to use graphical game screens.");
        for(const auto* command:{"/coinflip","/dice","/highlow"})if(ImGui::Button(command)) {
            if(!detail::queue_multiplayer_action("chat",command))state.message="Activity command could not be queued.";
        }end_card();
    }
    if(tab==0)quote(menu,data,disabled);ImGui::EndChild();return true;
}
} // namespace menu
} // namespace dingosdk::overlay
