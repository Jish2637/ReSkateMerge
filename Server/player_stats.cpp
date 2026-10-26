#include "player_stats.h"
#include "shop.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace dingosdk::server {
namespace {
std::uint64_t natural(const Json &j, std::string_view key) {
    const auto &v = j.at(key);
    if (!v.is_number_integer() || v.get<double>() < 0 || v.get<double>() > max_stats_counter)
        throw std::runtime_error("Invalid player stats counter: " + std::string(key));
    return v.get<std::uint64_t>();
}
std::int64_t date(const Json &j, std::string_view key) {
    const auto &v=j.at(key);
    if(!v.is_number_integer() || v.get<double>() < -1 || v.get<double>() > 10000000)
        throw std::runtime_error("Invalid player stats calendar: "+std::string(key));
    return v.get<std::int64_t>();
}
}
void PlayerStatsStore::open(const std::filesystem::path &path) {
    if(pending_save_.valid())pending_save_.get();save_completed_=false;
    path_ = path;
    if (path.empty() || !std::filesystem::exists(path)) return;
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read player-stats.json");
    const auto root = Json::parse(std::string(std::istreambuf_iterator<char>(in), {}), {64 * 1024 * 1024, 32, 8000000});
    if (!root.at("version").is_number_integer() || (root.at("version").get<int>() != 1 && root.at("version").get<int>() != 2 && root.at("version").get<int>() != 3 && root.at("version").get<int>() != 4) || !root.at("players").is_array())
        throw std::runtime_error("Unsupported player stats file");
    std::map<std::uint64_t, PlayerStats> loaded;
    for (const auto &j : root.at("players")) {
        const auto idtext = j.at("id").get<std::string>();
        if(idtext.empty() || !std::ranges::all_of(idtext,[](char c){return c>='0' && c<='9';}))
            throw std::runtime_error("Invalid player stats identity");
        std::size_t end{};
        auto id = std::stoull(idtext, &end);
        if (!id || end != idtext.size() || idtext.front() == '-' || loaded.contains(id))
            throw std::runtime_error("Invalid/duplicate player stats identity");
        PlayerStats p;
        p.name = j.at("name").get<std::string>();
        p.joinmsg_owned=j.value("joinmsg_owned",false);p.joinmsg_enabled=j.value("joinmsg_enabled",false);
        p.colourbundle_owned=j.value("colourbundle_owned",false);p.joinmsg=j.value("joinmsg",std::string{});
        p.joinmsg_last=j.contains("joinmsg_last")?natural(j,"joinmsg_last"):0;
        if(p.joinmsg.size()>4096 || (p.joinmsg_enabled && !p.joinmsg_owned))throw std::runtime_error("Invalid join message");
        p.active_us = natural(j,"active_us"); p.points = natural(j,"points");
        p.wallet = root.at("version").get<int>() == 1 ? p.points : natural(j,"wallet");
        if(root.at("version").get<int>() >= 2) {
            if(!j.at("tags").is_array() || !j.at("disabled_tags").is_array())throw std::runtime_error("Invalid tag collection");
            for(const auto &tag:j.at("tags"))p.tags.push_back(tag.get<std::string>());
            for(const auto &tag:j.at("disabled_tags"))p.disabled_tags.push_back(tag.get<std::string>());
            if(j.contains("shimmers")) {
                if(!j.at("shimmers").is_array())throw std::runtime_error("Invalid shimmer collection");
                std::set<std::string> owned;
                for(const auto &item:j.at("shimmers")) {
                    const auto key=item.get<std::string>();
                    if(!shop_shimmer(key) || !owned.insert(key).second)throw std::runtime_error("Invalid/duplicate owned shimmer");
                    p.shimmers.push_back(key);
                }
            }
            p.equipped_tag=j.at("equipped_tag").get<std::string>(); p.colour=j.at("colour").get<std::string>();
            std::set<std::string> unique;
            for(const auto &tag:p.tags) {
                auto key=tag;for(auto &c:key)if(c>='A' && c<='Z')c+=32;
                if(tag.empty() || tag.size()>12 || !unique.insert(key).second ||
                   !std::ranges::all_of(tag,[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='-'||c=='_';}))
                    throw std::runtime_error("Invalid purchased tag");
            }
            for(const auto &tag:p.disabled_tags)if(std::ranges::find(p.tags,tag)==p.tags.end())throw std::runtime_error("Invalid disabled tag");
            if(!p.equipped_tag.empty() && std::ranges::find(p.tags,p.equipped_tag)==p.tags.end())throw std::runtime_error("Invalid equipped tag");
            if(!p.colour.empty() && (!shop_colour(p.colour) || (shop_shimmer(p.colour) && std::ranges::find(p.shimmers,p.colour)==p.shimmers.end())))throw std::runtime_error("Invalid purchased colour");
        }
        p.weekly_points = natural(j,"weekly_points"); p.point_remainder = natural(j,"point_remainder");
        p.day = date(j,"day"); p.week = date(j,"week");
        p.last_claim_day = date(j,"last_claim_day");
        p.daily_us = natural(j,"daily_us"); p.streak = natural(j,"streak"); p.daily_events = natural(j,"daily_events");
        for (unsigned i=0;i<3;++i) p.events[i] = natural(j, "events" + std::to_string(i));
        p.vip_awarded = j.at("vip_awarded").get<bool>();
        p.vip_pending = j.value("vip_pending", false);
        p.qualifying_days = j.contains("qualifying_days") ? natural(j,"qualifying_days") : 0;
        if (p.point_remainder >= 3600000000ULL || p.last_claim_day>p.day || p.weekly_points>p.points || p.daily_us>p.active_us)
            throw std::runtime_error("Invalid player stats calendar or remainder");
        loaded.emplace(id,std::move(p));
    }
    if(root.at("version").get<int>()>=3 && (!root.contains("wagers") || !root.contains("next_wager_id")))throw std::runtime_error("Missing wager ledger");
    std::vector<Wager> ledger;
    const auto next=root.contains("next_wager_id")?natural(root,"next_wager_id"):1;
    if(!next)throw std::runtime_error("Invalid wager sequence");
    std::set<std::uint64_t> ids;
    std::set<std::pair<std::string,std::uint64_t>> private_games;
    std::set<std::string> pending_games;
    const auto identity=[](const Json &value) {
        const auto text=value.get<std::string>();
        if(text.empty() || !std::ranges::all_of(text,[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("Invalid wager identity");
        std::size_t end=0;const auto id=std::stoull(text,&end);
        if(!id || end!=text.size())throw std::runtime_error("Invalid wager identity");return id;
    };
    if(root.contains("wagers")) {
        if(!root.at("wagers").is_array())throw std::runtime_error("Invalid wager ledger");
        for(const auto &j:root.at("wagers")) {
            Wager w;w.id=natural(j,"id");w.creator=0;
            // Steam IDs exceed the stats counter ceiling; identities are strings.
            w.creator=identity(j.at("creator_id"));
            w.stake=natural(j,"stake");w.expires=natural(j,"expires");w.capacity=natural(j,"capacity");w.initial=natural(j,"initial");
            w.game=j.at("game").get<std::string>();w.state=j.at("state").get<std::string>();w.result=j.at("result").get<std::string>();
            if(j.contains("detail") && j.at("detail").is_object() && j.at("detail").dump().size()<=8192)w.detail=j.at("detail");
            if(!w.id || w.id>=next || !ids.insert(w.id).second || !w.stake || w.stake>max_stats_counter || !w.capacity || w.capacity>multiplayer_player_limit-1 ||
               (w.game!="coinflip" && w.game!="dice" && w.game!="highlow" && w.game!="slots" && w.game!="roulette" && w.game!="blackjack") ||
               (w.state!="pending" && w.state!="settled" && w.state!="refunded"))throw std::runtime_error("Invalid wager record");
            for(const auto &m:j.at("members")) {
                const auto id=identity(m);
                if(!loaded.contains(id) || !w.members.insert(id).second)throw std::runtime_error("Invalid wager participant");
            }
            for(const auto &p:j.at("payouts")) {
                const auto id=identity(p.at("id"));
                if(!w.members.contains(id) || !w.payouts.emplace(id,natural(p,"amount")).second)throw std::runtime_error("Invalid payout");
            }
            if(w.members.empty() || w.members.size()>w.capacity || !w.members.contains(w.creator) ||
               (w.game=="highlow" && (w.members.size()!=1 || w.initial<1 || w.initial>13)) ||
               (w.game=="slots" && (w.capacity!=1 || w.members.size()!=1 || w.state!="settled")) ||
               (w.game=="roulette" && (w.capacity!=1 || w.members.size()!=1 || w.state!="settled")) ||
               (w.game=="blackjack" && (w.capacity!=3 || w.members.size()!=1)) ||
               (w.state=="pending" && !w.payouts.empty()))throw std::runtime_error("Invalid wager membership");
            if(w.state=="pending") {
                if(w.game=="highlow" || w.game=="blackjack"){
                    if(!private_games.emplace(w.game,w.creator).second)throw std::runtime_error("Duplicate private game");
                } else if(!pending_games.insert(w.game).second)throw std::runtime_error("Duplicate pending game");
            } else if(w.state=="refunded") {
                if(w.payouts.size()!=w.members.size())throw std::runtime_error("Incomplete refund");
                for(auto id:w.members)if(w.payouts.at(id)!=w.stake)throw std::runtime_error("Invalid refund amount");
            } else if(w.game=="slots") {
                if(w.payouts.size()!=1 || w.payouts.begin()->second>25*w.stake)throw std::runtime_error("Invalid slots settlement");
            } else if(w.game=="roulette" || w.game=="blackjack") {
                if(w.payouts.size()!=1 || w.payouts.begin()->second>(w.game=="blackjack"?3:20)*w.stake)
                    throw std::runtime_error("Invalid casino settlement");
            } else if(w.game!="highlow") {
                if(w.payouts.size()!=1 || w.payouts.begin()->second!=w.stake*w.members.size())throw std::runtime_error("Invalid multiplayer settlement");
            } else if(w.payouts.size()!=1 || w.payouts.begin()->second>12*w.stake)throw std::runtime_error("Invalid High Low settlement");
            ledger.push_back(std::move(w));
        }
    }
    Json rewards=Json::object();
    if(root.at("version").get<int>()>=4 && !root.contains("plugin_rewards"))throw std::runtime_error("Missing reward receipts");
    if(root.contains("plugin_rewards")) {
        rewards=root.at("plugin_rewards");
        if(!rewards.is_object() || rewards.size()>10000)throw std::runtime_error("Invalid reward receipts");
        for(const auto& [id,row]:rewards.items()) {
            if(id.empty() || id.size()>200 || !row.is_object() || row.at("status").get<std::string>()!="paid" || !row.at("amount").is_number_integer() ||
               row.at("amount").get<unsigned>()<100 || row.at("amount").get<unsigned>()>900 || row.at("amount").get<unsigned>()%100 || !row.at("players").is_array())throw std::runtime_error("Invalid reward receipt");
        }
    }
    plugin_rewards=std::move(rewards);
    players = std::move(loaded);wagers=std::move(ledger);next_wager_id=next;
    rebuild_headroom();
}
std::future<void> PlayerStatsStore::submit_save() const {
    if(!writer_)writer_=std::make_shared<SerialTasks>();
    return writer_->submit([path=path_,snapshot=players,ledger=wagers,next=next_wager_id,rewards=plugin_rewards]{write_snapshot(path,snapshot,ledger,next,rewards);});
}
void PlayerStatsStore::save() const {
    // Durable economy barrier: never overtake a pending autosave or acknowledge failed writes.
    if(pending_save_.valid()){pending_save_.get();save_completed_=true;}
    submit_save().get();
}
bool PlayerStatsStore::save_async() const {
    if(pending_save_.valid())return false; // at most one outstanding autosave snapshot
    pending_save_=submit_save();return true;
}
bool PlayerStatsStore::poll_save() const {
    if(pending_save_.valid() && pending_save_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        pending_save_.get();save_completed_=true;
    }
    return std::exchange(save_completed_,false);
}
void PlayerStatsStore::write_snapshot(const std::filesystem::path &path,const std::map<std::uint64_t,PlayerStats> &snapshot,const std::vector<Wager> &ledger,std::uint64_t next,const Json& rewards) {
    if (path.empty()) return;
    auto root = Json::object(), list = Json::array();
    root["version"] = 4;
    root["plugin_rewards"]=rewards;
    root["next_wager_id"]=next;auto records=Json::array();
    for(const auto &w:ledger) {
        auto j=Json::object();j["id"]=w.id;j["creator_id"]=std::to_string(w.creator);j["stake"]=w.stake;
        j["expires"]=w.expires;j["capacity"]=w.capacity;j["initial"]=w.initial;j["game"]=w.game;j["state"]=w.state;j["result"]=w.result;j["detail"]=w.detail;
        auto members=Json::array(),payouts=Json::array();
        for(auto id:w.members)members.push_back(std::to_string(id));
        for(const auto &[id,amount]:w.payouts){auto p=Json::object();p["id"]=std::to_string(id);p["amount"]=amount;payouts.push_back(std::move(p));}
        j["members"]=std::move(members);j["payouts"]=std::move(payouts);records.push_back(std::move(j));
    }
    root["wagers"]=std::move(records);
    for (const auto &[id,p] : snapshot) {
        auto j = Json::object(); j["id"] = std::to_string(id); j["name"] = p.name;
        j["joinmsg_owned"]=p.joinmsg_owned;j["joinmsg_enabled"]=p.joinmsg_enabled;j["joinmsg"]=p.joinmsg;
        j["joinmsg_last"]=p.joinmsg_last;j["colourbundle_owned"]=p.colourbundle_owned;
        j["shimmers"]=p.shimmers; j["wallet"]=p.wallet; j["tags"]=p.tags; j["disabled_tags"]=p.disabled_tags; j["equipped_tag"]=p.equipped_tag; j["colour"]=p.colour;
        j["active_us"]=p.active_us; j["points"]=p.points; j["weekly_points"]=p.weekly_points;
        j["point_remainder"]=p.point_remainder; j["day"]=p.day; j["week"]=p.week;
        j["last_claim_day"]=p.last_claim_day; j["daily_us"]=p.daily_us; j["streak"]=p.streak;
        j["daily_events"]=p.daily_events; j["vip_awarded"]=p.vip_awarded;
        j["vip_pending"]=p.vip_pending;
        j["qualifying_days"]=p.qualifying_days;
        for(unsigned i=0;i<3;++i) j["events"+std::to_string(i)]=p.events[i];
        list.push_back(std::move(j));
    }
    root["players"]=std::move(list);
    std::filesystem::create_directories(path.parent_path());
    auto temp=path; temp += ".tmp";
    { std::ofstream out(temp,std::ios::binary|std::ios::trunc); const auto text=root.dump(2);
      if (!out || !out.write(text.data(),text.size()) || !(out.flush())) throw std::runtime_error("Cannot write player stats"); }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace player stats file");
#else
    std::filesystem::rename(temp,path);
#endif
}
void PlayerStatsStore::rebuild_headroom() {
    for(auto &[id,p]:players)p.wager_headroom=0;
    for(const auto &w:wagers)if(w.state=="pending")for(auto id:w.members) {
        auto &p=players.at(id);const auto amount=w.exposure();
        if(amount>max_stats_counter-p.wager_headroom)throw std::runtime_error("Wager exposure overflow");
        p.wager_headroom+=amount;
    }
    for(const auto &[id,p]:players)if(p.wallet>p.wallet_limit())throw std::runtime_error("Wallet exceeds escrow-safe limit");
}
void PlayerStatsStore::refund_pending() {
    for(auto &w:wagers)if(w.state=="pending") {
        for(auto id:w.members){players.at(id).wallet+=w.stake;w.payouts[id]=w.stake;}
        w.state="refunded";w.result="restart recovery";
    }
    rebuild_headroom();
}
void PlayerStatsStore::calendar(PlayerStats &p, std::int64_t utc) {
    const auto day=day_at(utc),week=week_at(utc);
    // Never reopen an already-claimed day/week when the wall clock moves backwards.
    if (day>p.day) { p.day=day; p.daily_us=0; p.daily_events=0; }
    if (week>p.week) { p.week=week; p.weekly_points=0; }
}
void PlayerStatsStore::credit(PlayerStats &p,std::uint64_t points) {
    p.points+=points; p.weekly_points+=points;
    p.wallet+=std::min(points,p.wallet_limit()-p.wallet);
}

Json PlayerStatsStore::skate_reward(std::string id,std::uint64_t winner,std::vector<std::uint64_t> roster,std::int64_t utc) {
    if(path_.empty())throw std::runtime_error("MSC wallet persistence is not configured");
    if(id.empty() || id.size()>200 || !std::ranges::all_of(id,[](unsigned char c){return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c==':' || c=='-' || c=='_' || c=='.';}))throw std::runtime_error("Invalid reward ID");
    std::ranges::sort(roster);
    if(roster.size()<2 || roster.size()>10 || std::adjacent_find(roster.begin(),roster.end())!=roster.end() || !std::ranges::binary_search(roster,winner))throw std::runtime_error("Invalid reward roster");
    auto members=Json::array();for(auto player:roster) {
        if(!players.contains(player))throw std::runtime_error("Reward participant is not in the MSC ledger");
        members.push_back(std::to_string(player));
    }
    const auto amount=100U*static_cast<unsigned>(roster.size()-1);
    if(plugin_rewards.contains(id)) {
        const auto& prior=plugin_rewards.at(id);
        if(prior.at("winner").get<std::string>()!=std::to_string(winner) || prior.at("amount").get<unsigned>()!=amount || prior.at("players").dump()!=members.dump())throw std::runtime_error("Reward ID conflict");
        auto duplicate=prior;duplicate["duplicate"]=true;return duplicate;
    }
    if(plugin_rewards.size()>=10000)throw std::runtime_error("Reward receipt limit reached; operator review required");
    auto& player=players.at(winner);const auto before=player;
    if(amount>player.wallet_limit()-player.wallet || amount>max_stats_counter-player.points || amount>max_stats_counter-player.weekly_points)throw std::runtime_error("Reward exceeds ledger counter limit");
    auto receipt=Json::object();receipt["status"]="paid";receipt["winner"]=std::to_string(winner);receipt["amount"]=amount;receipt["players"]=members;receipt["utc"]=utc;
    calendar(player,utc);credit(player,amount);plugin_rewards[id]=receipt;
    try { save(); } catch(const std::exception& e) { player=before;plugin_rewards.erase(id);throw std::runtime_error(std::string("MSC reward save failed: ")+e.what()); }
    return receipt;
}
void PlayerStatsStore::joined(std::uint64_t id,std::string name,std::uint64_t now,std::int64_t utc) {
    auto &p=players[id]; p.name=std::move(name); calendar(p,utc);
    sessions_[id]={now,now,utc};
}
void PlayerStatsStore::active(std::uint64_t id,std::uint64_t now) {
    if(auto it=sessions_.find(id);it!=sessions_.end()) it->second.active=now;
}
void PlayerStatsStore::advance(std::uint64_t id,std::uint64_t now,std::int64_t utc,const StatsSettings &s,const Notice &notice,bool persist_rewards) {
    auto it=sessions_.find(id); if(it==sessions_.end())return;
    auto &session=it->second; auto &p=players.at(id);
    const auto end=std::min(now,session.active+std::uint64_t{s.idle_seconds}*1000000);
    const auto earned=end>session.at ? end-session.at : 0;
    // Split intervals at UTC midnight (including the weekly boundary).
    auto cursor=session.at;
    while(cursor<session.at+earned) {
        const auto wall=session.utc+static_cast<std::int64_t>((cursor-session.at)/1000000);
        const auto next=(day_at(wall)+1)*86400;
        const auto part=std::min(session.at+earned-cursor,static_cast<std::uint64_t>(next-wall)*1000000);
        calendar(p,wall);
        p.active_us+=part; p.daily_us+=part;
        p.point_remainder+=part*s.hourly_points;
        credit(p,p.point_remainder/3600000000ULL); p.point_remainder%=3600000000ULL;
        cursor+=part;
        if(p.daily_us>=std::uint64_t{s.daily_minutes}*60000000 && p.last_claim_day<p.day) {
            const auto before=p;
            p.streak=p.last_claim_day==p.day-1 ? p.streak+1 : 1;
            p.last_claim_day=p.day;
            ++p.qualifying_days;
            const auto bonus=s.daily_points+std::min<std::uint64_t>(p.streak*s.streak_step,s.streak_cap);
            credit(p,bonus);
            if(persist_rewards)try { save(); } catch(...) { p=before; session.at=now; session.utc=utc; throw; }
            if(notice)notice(id,"Daily bonus: +"+std::to_string(bonus)+" MSC points! Streak: "+std::to_string(p.streak)+" days.");
        }
    }
    session.at=now; session.utc=utc; calendar(p,utc);
}
void PlayerStatsStore::left(std::uint64_t id) {
    sessions_.erase(id);
    std::erase_if(events_,[&](const auto &e){return e.first.first==id;});
    for(auto &[key,e]:events_) e.participants.erase(id);
}
void PlayerStatsStore::world_changed(){sessions_.clear(); events_.clear();}
void PlayerStatsStore::throwdown(std::uint64_t sender,const multiplayer::ThrowdownMessage &m,std::uint64_t now,
                                std::int64_t utc,const StatsSettings &s,const Notice &notice) {
    using K=multiplayer::ThrowdownMessage::Kind;
    if(!sessions_.contains(sender))return;
    const auto key=std::pair{m.leader,m.id};
    if(m.kind==K::offer && sender==m.leader) {
        if(events_.contains(key) || events_.size()>=65536)return;
        Event e; e.mode=m.series=="ThrowdownSkate"?0:m.series=="SpotBattle"?1:m.series=="JamSession"?2:-1;
        if(e.mode>=0)events_.emplace(key,std::move(e)); return;
    }
    auto found=events_.find(key); if(found==events_.end())return; auto &e=found->second;
    if(m.kind==K::start && sender==m.leader && !e.started) {
        e.started=true;
        for(auto id:m.order)if(sessions_.contains(id))e.participants.insert(id);
        if(e.participants.size()<2)e.participants.clear();
        return;
    }
    const bool activity=(e.mode==0 && m.kind==K::attempt) ||
        (e.mode==1 && (m.kind==K::turn_end || (m.kind==K::row && m.value>0))) || (e.mode==2 && m.kind==K::score && m.value>0);
    if(!e.started || !activity || !e.participants.contains(sender))return;
    active(sender,now);
    if(e.credited.contains(sender))return;
    auto &p=players.at(sender); calendar(p,utc); const auto before=p;
    ++p.events[e.mode];
    const bool reward=p.daily_events<s.rewarded_events_per_day;
    if(reward){++p.daily_events;credit(p,s.event_points);}
    try { save(); } catch(...) {p=before;throw;}
    e.credited.insert(sender);
    if(reward && notice)notice(sender,"Event participation: +"+std::to_string(s.event_points)+" MSC points.");
}
std::uint64_t PlayerStatsStore::value(const PlayerStats &p,std::string_view kind,std::int64_t utc)const {
    if(kind=="weekly")return p.week==week_at(utc)?p.weekly_points:0;
    if(kind=="time")return p.active_us;
    if(kind=="events")return p.events[0]+p.events[1]+p.events[2];
    return p.points;
}
std::vector<std::uint64_t> PlayerStatsStore::board(std::string_view kind,std::int64_t utc)const {
    std::vector<std::uint64_t> result;
    for(const auto &[id,p]:players)if(value(p,kind,utc))result.push_back(id);
    std::ranges::sort(result,[&](auto a,auto b){const auto x=value(players.at(a),kind,utc),y=value(players.at(b),kind,utc);return x!=y?x>y:a<b;});
    return result;
}
} // namespace dingosdk::server
