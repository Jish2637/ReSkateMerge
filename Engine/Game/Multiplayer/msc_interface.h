#pragma once
#include "Engine/Core/Json/json.h"
#include <charconv>
#include <array>
#include <optional>
#include "msc_features.h"

namespace dingosdk::msc {
inline constexpr std::size_t max_payload = 16384, page_size = 10;
inline constexpr unsigned version = 1;
struct Feed { bool connected{}; std::uint64_t host{}, epoch{}; std::string payload; };
inline std::uint64_t integer(const Json& j, std::string_view key) {
    const auto& v=j.at(key);
    if(!v.is_number_unsigned() || v.get<std::uint64_t>()>9000000000000000ULL) throw std::runtime_error("Invalid MSC integer");
    return v.get<std::uint64_t>();
}
inline std::uint64_t identity(const Json& j, std::string_view key) {
    const auto value=j.at(key).get<std::string>(); std::uint64_t n{};
    const auto r=std::from_chars(value.data(),value.data()+value.size(),n);
    if(r.ec!=std::errc{} || r.ptr!=value.data()+value.size() || !n) throw std::runtime_error("Invalid MSC identity");
    return n;
}
inline std::string text(const Json& j,std::string_view key,std::size_t max=256) {
    const auto v=j.at(key).get<std::string>();
    if(v.size()>max || v.find('\0')!=std::string::npos) throw std::runtime_error("Invalid MSC text");
    return v;
}
inline Json parse(std::string_view payload) {
    auto j=Json::parse(payload,{max_payload,12,4096});
    if(!j.is_object() || integer(j,"v")!=version)throw std::runtime_error("Unsupported MSC interface");
    return j;
}
struct Product {
    std::string id,label,value,action,reason; std::uint64_t price{};
    bool owned{},equipped{},enabled{}; std::uint32_t colour{};
};
struct Tag { std::string text; bool enabled{},equipped{}; };
struct Leader { std::uint64_t id{},rank{},value{}; std::string name; };
struct Quote { std::string token,kind,value; std::uint64_t price{},seconds{}; };
struct Profile {
    std::string name,tag,colour,joinmsg,vip;
    std::uint64_t balance{},lifetime{},weekly{},active_seconds{},daily_seconds{},daily_target{},streak{},visits{},vip_seconds{};
    std::array<std::uint64_t,3> events{};
    bool unlimited{},daily_done{},joinmsg_owned{},joinmsg_on{};
};
struct Snapshot {
    std::uint64_t revision{},request{},epoch{},player{},collection_page{1},collection_pages{1},leader_page{1},leader_pages{1};
    bool shop{},stats{},paused{},can_pay{},can_tag{},can_joinmsg{};
    bool games_supported{},staff_available{};std::vector<GameView> games;std::optional<StaffView> staff;
    std::string message,leader_kind{"weekly"};
    Profile profile; std::vector<Product> products; std::vector<Tag> tags; std::vector<Leader> leaders; std::optional<Quote> quote;
};
inline GameRound game_round(const Json& j) {
    GameRound r;r.id=integer(j,"id");r.creator=identity(j,"creator");r.stake=integer(j,"stake");r.pot=integer(j,"pot");r.capacity=integer(j,"capacity");r.seconds=integer(j,"seconds");
    r.initial=integer(j,"initial");r.second=integer(j,"second");r.my_payout=integer(j,"my_payout");r.state=text(j,"state",16);r.result=text(j,"result",512);r.direction=text(j,"direction",16);
    if(!j.at("winner").string().empty())r.winner=identity(j,"winner");
    if(!j.at("members").is_array() || j.at("members").size()>64 || !j.at("rolls").is_array() || j.at("rolls").size()>24)throw std::runtime_error("Invalid MSC game size");
    for(const auto& p:j.at("members"))r.members.push_back({identity(p,"id"),integer(p,"payout"),text(p,"name",128)});
    for(const auto& p:j.at("rolls")){const auto value=integer(p,"value");if(value<1 || value>6)throw std::runtime_error("Invalid MSC die");r.rolls.push_back({integer(p,"round"),identity(p,"player"),value});}
    if(r.initial>13 || r.second>13)throw std::runtime_error("Invalid MSC card");
    return r;
}
inline void features(Snapshot& s,const Json& j) {
    s.games_supported=j.value("games_supported",false);s.staff_available=j.value("staff_available",false);
    if(j.contains("games")) {
        if(!j.at("games").is_array() || j.at("games").size()>8)throw std::runtime_error("Invalid MSC game catalogue");
        for(const auto& v:j.at("games")) {
            GameView g;g.id=text(v,"id",32);g.label=text(v,"label",64);g.reason=text(v,"reason",256);g.enabled=v.at("enabled").get<bool>();
            g.minimum=integer(v,"minimum");g.maximum=integer(v,"maximum");g.capacity=integer(v,"capacity");
            if(!v.at("actions").is_array() || v.at("actions").size()>8)throw std::runtime_error("Invalid MSC game actions");
            for(const auto& a:v.at("actions")){if(!a.is_string() || a.string().size()>16)throw std::runtime_error("Invalid game action");g.actions.push_back(a.string());}
            if(!v.at("round").is_null())g.round=game_round(v.at("round"));if(!v.at("previous").is_null())g.previous=game_round(v.at("previous"));if(g.id=="highlow" && ((g.round && !g.round->initial) || (g.previous && !g.previous->initial)))throw std::runtime_error("Invalid High / Low card");s.games.push_back(std::move(g));
        }
    }
    if(j.contains("staff") && !j.at("staff").is_null()) {
        const auto& v=j.at("staff");StaffView t;t.role=text(v,"role",24);t.output=text(v,"output",2304);t.page=integer(v,"page");t.pages=integer(v,"pages");
        if(!v.at("actions").is_array() || v.at("actions").size()>160 || !v.at("roles").is_array() || v.at("roles").size()>64 || !v.at("players").is_array() || v.at("players").size()>10 || !v.at("settings").is_object() || v.at("settings").size()>64)throw std::runtime_error("Invalid staff snapshot size");
        for(const auto& a:v.at("actions")){if(!a.is_string() || a.string().size()>48)throw std::runtime_error("Invalid staff action");t.actions.push_back(a.string());}
        for(const auto& a:v.at("roles")){if(!a.is_string() || a.string().size()>24)throw std::runtime_error("Invalid staff role");t.roles.push_back(a.string());}
        for(const auto& a:v.at("players"))t.players.push_back({identity(a,"id"),identity(a,"epoch"),integer(a,"rank"),text(a,"name",128),text(a,"role",24),a.at("voice_muted").get<bool>(),a.at("text_muted").get<bool>(),integer(a,"locks")});
        for(const auto& [key,val]:v.at("settings").items()){if(key.size()>48 || !val.is_string() || val.string().size()>128)throw std::runtime_error("Invalid staff setting");t.settings[key]=val.string();}
        if(!v.at("quote").is_null()){const auto& q=v.at("quote");t.quote=StaffQuote{text(q,"token",96),text(q,"label",96),text(q,"description",1024),integer(q,"seconds")};}
        s.staff=std::move(t);
    }
}
inline Snapshot snapshot(const Json& j) {
    Snapshot s; s.revision=integer(j,"revision"); s.request=integer(j,"request"); s.epoch=identity(j,"epoch");s.player=identity(j,"player");
    s.shop=j.at("shop").get<bool>();s.stats=j.at("stats").get<bool>();s.paused=j.at("paused").get<bool>();s.can_pay=j.at("can_pay").get<bool>();
    s.can_tag=j.at("can_tag").get<bool>();s.can_joinmsg=j.at("can_joinmsg").get<bool>();s.message=text(j,"message",1024);
    const auto& p=j.at("profile"); auto& a=s.profile;
    a.name=text(p,"name",128);a.tag=text(p,"tag",64);a.colour=text(p,"colour",32);a.joinmsg=text(p,"joinmsg",256);a.vip=text(p,"vip",256);
    a.balance=integer(p,"balance");a.lifetime=integer(p,"lifetime");a.weekly=integer(p,"weekly");a.active_seconds=integer(p,"active_seconds");
    a.daily_seconds=integer(p,"daily_seconds");a.daily_target=integer(p,"daily_target");a.streak=integer(p,"streak");a.visits=integer(p,"visits");a.vip_seconds=integer(p,"vip_seconds");
    a.unlimited=p.at("unlimited").get<bool>();a.daily_done=p.at("daily_done").get<bool>();a.joinmsg_owned=p.at("joinmsg_owned").get<bool>();a.joinmsg_on=p.at("joinmsg_on").get<bool>();
    for(std::size_t i=0;i<3;++i){const auto& v=p.at("events").at(i);if(!v.is_number_unsigned())throw std::runtime_error("Invalid event count");a.events[i]=v.get<std::uint64_t>();}
    if(!j.at("products").is_array() || j.at("products").size()>20)throw std::runtime_error("MSC catalogue too large");
    for(const auto& v:j.at("products")) {
        Product c;c.id=text(v,"id",48);c.label=text(v,"label",80);c.value=text(v,"value",128);c.action=text(v,"action",24);c.reason=text(v,"reason",256);
        c.price=integer(v,"price");c.owned=v.at("owned").get<bool>();c.equipped=v.at("equipped").get<bool>();c.enabled=v.at("enabled").get<bool>();
        c.colour=static_cast<std::uint32_t>(integer(v,"colour"));s.products.push_back(std::move(c));
    }
    s.collection_page=integer(j,"collection_page");s.collection_pages=integer(j,"collection_pages");
    if(!j.at("tags").is_array() || j.at("tags").size()>page_size)throw std::runtime_error("MSC collection too large");
    for(const auto& t:j.at("tags"))s.tags.push_back({text(t,"text",64),t.at("enabled").get<bool>(),t.at("equipped").get<bool>()});
    s.leader_kind=text(j,"leader_kind",16);s.leader_page=integer(j,"leader_page");s.leader_pages=integer(j,"leader_pages");
    if(!j.at("leaders").is_array() || j.at("leaders").size()>page_size)throw std::runtime_error("MSC leaderboard too large");
    for(const auto& v:j.at("leaders"))s.leaders.push_back({identity(v,"id"),integer(v,"rank"),integer(v,"value"),text(v,"name",128)});
    if(!j.at("quote").is_null()) {const auto& q=j.at("quote");s.quote=Quote{text(q,"token",96),text(q,"kind",32),text(q,"value",256),integer(q,"price"),integer(q,"seconds")};}
    features(s,j);
    return s;
}
// A reply is usable only for this connection and if newer than the current snapshot.
inline bool accepts(const Snapshot& s,std::uint64_t epoch,std::uint64_t revision) { return s.epoch==epoch && s.revision>revision; }

struct ClientState {
    std::uint64_t host{},epoch{},next_request{},pending{};
    double opened{},received{},sent{};
    bool connected{},attempted{};
    std::string last_payload,message;
    std::optional<Snapshot> data;
    void observe(const Feed& feed,double now) {
        if(!feed.connected || feed.host!=host || feed.epoch!=epoch) {
            host=feed.host;epoch=feed.epoch;connected=feed.connected;next_request=0;pending=0;
            attempted=false;data.reset();last_payload.clear();message.clear();opened=now;received=0;sent=0;
        }
        connected=feed.connected;
        if(!connected || feed.payload.empty() || feed.payload==last_payload)return;
        last_payload=feed.payload;
        try {
            auto next=snapshot(parse(feed.payload));
            if(!accepts(next,epoch,data?data->revision:0))return;
            if(next.request==pending)pending=0;
            if(!next.message.empty())message=next.message;
            data=std::move(next);received=now;
        } catch(const std::exception&) { message="Invalid MSC server response. Refresh to retry."; }
    }
    bool stale(double now) const {return !data || now-received>20.0;}
    bool fallback(double now) const {return attempted && !data && now-opened>=5.0;}
    void timeout(double now) {
        if(pending && now-sent>8.0){pending=0;received=-1e9;message="No response to the last request. Refresh and check its result before retrying.";}
    }
};
} // namespace dingosdk::msc
