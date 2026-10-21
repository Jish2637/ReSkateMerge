#include "server_host.h"
#include "server_text.h"
#include <algorithm>
#include <ctime>

namespace dingosdk::server {
namespace {
std::string duration(std::uint64_t us) {
    const auto minutes=us/60000000;
    return std::to_string(minutes/60)+"h "+std::to_string(minutes%60)+"m";
}
std::string label(std::string_view kind) {
    return kind=="weekly"?"Weekly MSC points":kind=="time"?"Lifetime active playtime":kind=="events"?"Lifetime event participation":"Lifetime MSC points";
}
std::vector<std::uint64_t> eligible_board(const PlayerStatsStore &stats,const Roles &roles,std::string_view kind,std::int64_t utc) {
    auto rows=stats.board(kind,utc);
    // Resolve offline players too, before pagination, ranks, ties and leader congratulations.
    std::erase_if(rows,[&](auto id){const auto *role=roles.of(id);return role && role->granted_all;});
    return rows;
}
}
void Host::stats_joined(Guest &g) {
    if(!config_.player_stats.enabled || stats_failed_)return;
    stats_.joined(g.member.id,guest_name(g),now_,static_cast<std::int64_t>(std::time(nullptr)));
    if(!g.shop_login_done) {
        g.shop_login_done=true;
        shop_quotes_.erase(g.member.id);
        auto &p=stats_.players.at(g.member.id);
        if(config_.shop.enabled && config_.name=="-MSC-Midnight Skate Club-Jishs Test Server" && p.wallet<10000) {
            const auto before=p;p.wallet=std::min<std::uint64_t>(10000,p.wallet_limit());
            if(shop_commit(g,before,"test-server top-up"))reply(g,"MSC shop testing: your balance has been topped up to 10,000 points. Have fun!");
        }
        if(!p.equipped_tag.empty() && (!tag_error(p.equipped_tag).empty() || std::ranges::find(p.disabled_tags,p.equipped_tag)!=p.disabled_tags.end()))
            reply(g,"Your custom tag is currently unavailable. It is still in /tags.");
    }
}
void Host::stats_activity(Guest &g) {
    if(!config_.player_stats.enabled || stats_failed_ || !g.world_ready)return;
    stats_.active(g.member.id,now_);
}
void Host::account_stats() {
    if(!config_.player_stats.enabled || stats_failed_)return;
    const auto utc=static_cast<std::int64_t>(std::time(nullptr));
    for(const auto &[id,g]:guests_)if(g->handshaken && g->world_ready && stats_.players.contains(id))
        stats_.advance(id,now_,utc,config_.player_stats,[&](std::uint64_t who,const std::string &text){
            stats_notices_.emplace_back(who,text);stats_save_due_=true;
        },false); // no serialization, disk access, waits or player notifications
}
void Host::poll_stats_saves() {
    if(stats_.poll_save()) {
        for(const auto &[id,text]:stats_notices_saving_)if(auto *g=find(id);g && g->handshaken)reply(*g,text);
        stats_notices_saving_.clear();
    }
}
void Host::save_stats(bool wait) {
    if(!config_.player_stats.enabled || stats_failed_)return;
    try {
        poll_stats_saves();
        if(wait) {
            stats_.save();poll_stats_saves();
            for(const auto &[id,text]:stats_notices_)if(auto *g=find(id);g && g->handshaken)reply(*g,text);
            stats_notices_.clear();stats_save_due_=false;
        } else if(stats_.save_async()) {
            stats_notices_saving_=std::move(stats_notices_);stats_notices_.clear();
            stats_save_due_=false;stats_saved_at_=now_;
        } else stats_save_due_=true;
    } catch(const std::exception &e) {
        stats_failed_=true;log_(std::string("[stats] Rewards paused until restart; save failed: ")+e.what());
    }
}
std::string Host::vip_test_command(std::string_view player,std::uint64_t admin) {
    if(!config_.player_stats.enabled)return "MSC rewards are disabled on this server.";
    if(stats_failed_)return "MSC rewards are paused. Resolve the stats save problem and restart before testing.";
    player=trim(player);
    if(player.size()>=2 && player.front()=='\"' && player.back()=='\"')player=player.substr(1,player.size()-2);
    if(player.empty())return "/viptest <player name or SteamID64>";
    auto *g=match_player(player);
    if(!g)return "No single connected player matches. Use the full name, a unique name prefix or SteamID64.";
    if(!g->world_ready)return "That player must finish loading the map before testing VIP.";
    const auto id=g->member.id;
    const auto *role=role_of(id),*ordinary=config_.roles.find("player"),*vip=config_.roles.find(config_.player_stats.vip_role);
    if(!role || role->name!="player")return "VIP testing requires an ordinary player-role account; staff and other roles are unchanged.";
    if(!ordinary || ordinary->granted_all || !ordinary->granted_commands.empty() || !vip || vip->name=="player" ||
       vip->granted_all || !vip->granted_commands.empty() || (vip->granted & ~(ordinary->granted|multiplayer::role_reserved_slot)))
        return "The configured VIP role is not eligible for automatic promotion. Fix the VIP role before testing.";
    if(!config_.roles.players.contains(id) && config_.roles.players.size()>=max_role_players)
        return "The player-role assignment list is full; automatic VIP cannot assign another player.";
    const auto threshold=std::uint64_t{config_.player_stats.vip_hours}*3600000000ULL;
    if(threshold<=60000000)return "Configure a VIP threshold longer than one minute before testing.";
    const auto target=threshold-60000000;
    const auto utc=static_cast<std::int64_t>(std::time(nullptr));
    if(!stats_.players.contains(id))stats_joined(*g);
    try {
        // Settle real elapsed time before the adjustment. Added test time grants no points or daily credit.
        stats_.advance(id,now_,utc,config_.player_stats,[this](std::uint64_t who,const std::string &text){if(auto *to=find(who))reply(*to,text);});
    } catch(const std::exception &e) {
        stats_failed_=true;log_(std::string("[stats] Rewards paused until restart: ")+e.what());
        return "Could not save current stats. VIP test was not prepared.";
    }
    auto &p=stats_.players.at(id);
    if(p.vip_awarded || p.vip_pending)return "That player has already earned VIP or has a pending grant. Use a player who has not earned VIP.";
    if(p.active_us>target)return "That player already has more than the test playtime. VIP testing will not reduce their playtime; use another player.";
    const auto before=p;
    p.active_us=target;
    try {stats_.save();}
    catch(const std::exception &e) {
        p=before;log_(std::string("[stats] VIP test adjustment was not saved: ")+e.what());
        return "VIP test was not changed because the stats could not be saved.";
    }
    // Start the remaining minute at this command, with the normal activity timeout.
    stats_.joined(id,guest_name(*g),now_,utc);
    const auto *actor=admin?find(admin):nullptr;
    log_("[stats] VIP test: "+(actor?guest_name(*actor):std::string("console"))+" ("+std::to_string(admin)+") set "+
         guest_name(*g)+" ("+std::to_string(id)+") from "+std::to_string(before.active_us)+" to "+std::to_string(target)+" active us; 60 s below VIP.");
    reply(*g,"An admin set your playtime to "+duration(target)+" for a VIP test. Earn 1 more active minute to receive VIP.");
    return "VIP test prepared for "+guest_name(*g)+":\n"+duration(target)+", exactly 1 active minute short of VIP. Added time gives no points or daily bonus.";
}
void Host::tick_stats() {
    if(!config_.player_stats.enabled || stats_failed_)return;
    try {poll_stats_saves();}
    catch(const std::exception &e){stats_failed_=true;log_(std::string("[stats] Rewards paused until restart; background save failed: ")+e.what());return;}
    const auto &s=config_.player_stats;
    const auto utc=static_cast<std::int64_t>(std::time(nullptr));
    for(auto &[id,g]:guests_) {
        if(!g->handshaken || !g->world_ready)continue;
        if(!stats_.players.contains(id))stats_joined(*g);
        try {
            auto &p=stats_.players.at(id);
            const auto *current=role_of(id);
            auto vip=std::find_if(config_.roles.list.begin(),config_.roles.list.end(),[&](const auto &r){return r.name==s.vip_role;});
            const auto *ordinary=config_.roles.find("player");
            const bool vip_safe=ordinary && vip!=config_.roles.list.end() && vip->name!="player" &&
                !ordinary->granted_all && ordinary->granted_commands.empty() &&
                !vip->granted_all && vip->granted_commands.empty() &&
                !(vip->granted & ~(ordinary->granted | multiplayer::role_reserved_slot));
            const bool assignment_fits=config_.roles.players.contains(id) || config_.roles.players.size()<max_role_players;
            if(!p.vip_awarded && p.active_us>=std::uint64_t{s.vip_hours}*3600000000ULL && current &&
               current->name=="player" && vip_safe && assignment_fits) {
                // Persist intent before touching roles. If interrupted, the pending promotion resumes.
                p.vip_pending=true; stats_.save();
            }
            if(p.vip_pending && vip_safe && assignment_fits && current &&
               (current->name=="player" || current->name==s.vip_role)) {
                const auto before=config_.roles;
                vip->tag="MSC";
                vip->inherit=config_.roles.default_role=="player";
                // When player is the default, inherit its live permissions rather than
                // copying them into VIP (copies would outlive later player-role changes).
                vip->permissions=multiplayer::role_reserved_slot | (vip->inherit ? 0U : ordinary->granted);
                config_.roles.players[id]=s.vip_role;
                config_.roles.settle();
                try { save_config(config_); }
                catch(...) {config_.roles=before;throw;}
                p.vip_awarded=true; p.vip_pending=false;
                try {stats_.save();}catch(...){p.vip_awarded=false;p.vip_pending=true;roles_changed();throw;}
                roles_changed();
                reply(*g,"You earned MSC VIP after "+std::to_string(s.vip_hours)+" active hours! Your MSC tag and reserved slot are active.");
                send_chat("Congratulations "+guest_name(*g)+"! You earned MSC VIP for "+std::to_string(s.vip_hours)+" active hours.");
                log_("[stats] VIP awarded to "+guest_name(*g)+" ("+std::to_string(id)+")");
            }
        } catch(const std::exception &e) {
            // Stop reward mutation after a persistence error, retaining the last valid file for recovery.
            log_(std::string("[stats] Rewards paused until restart: ")+e.what());
            stats_failed_=true;
            return;
        }
    }
    if(stats_save_due_ || now_<stats_saved_at_ || now_-stats_saved_at_>=60000000)save_stats();
    if(!leaderboard_at_ || !players() || !s.leaderboard_minutes){leaderboard_at_=now_;return;}
    if(now_<leaderboard_at_ || now_-leaderboard_at_<std::uint64_t{s.leaderboard_minutes}*60000000)return;
    if((announcement_.id && now_<announcement_until_) || now_<stats_public_at_+30000000)return;
    leaderboard_at_=now_;
    constexpr std::array<std::string_view,3> kinds={"weekly","points","time"};
    const auto kind=kinds[leaderboard_kind_++%kinds.size()];
    const auto board=eligible_board(stats_,config_.roles,kind,utc); if(board.empty())return;
    const auto text=stats_board(kind,1,3);
    std::size_t at=0;
    while(at<text.size()) {const auto end=text.find('\n',at);send_chat(text.substr(at,end==std::string::npos?end:end-at));if(end==std::string::npos)break;at=end+1;}
    stats_public_at_=now_;
    const auto top=stats_.value(stats_.players.at(board.front()),kind,utc);
    std::string names; unsigned omitted=0;
    for(auto id:board) {
        if(stats_.value(stats_.players.at(id),kind,utc)!=top)break;
        const auto *g=find(id);if(!g || !g->handshaken || !g->world_ready)continue;
        auto short_name=clean_chat_text(guest_name(*g));cut_text(short_name,48);
        auto candidate=(names.empty()?std::string{}:", ")+short_name;
        if(names.size()+candidate.size()>100){++omitted;continue;}names+=candidate;
    }
    if(!names.empty())announce("Congratulations "+names+(omitted?" and "+std::to_string(omitted)+" more":"")+"! Leading "+label(kind)+"!");
}
std::string Host::stats_board(std::string_view kind,unsigned page,unsigned size) {
    const auto utc=static_cast<std::int64_t>(std::time(nullptr));const auto rows=eligible_board(stats_,config_.roles,kind,utc);
    if(rows.empty())return label(kind)+": no entries yet.";
    const auto pages=(rows.size()+size-1)/size;
    if(!page || page>pages)return "Leaderboard page must be 1-"+std::to_string(pages)+".";
    std::string text=label(kind)+(size==10?" (page "+std::to_string(page)+"/"+std::to_string(pages)+")":"")+":";
    for(std::size_t i=(page-1)*size;i<std::min(rows.size(),std::size_t{page}*size);++i) {
        const auto &p=stats_.players.at(rows[i]);const auto value=stats_.value(p,kind,utc);
        std::size_t rank=i;
        while(rank && stats_.value(stats_.players.at(rows[rank-1]),kind,utc)==value)--rank;
        auto name=clean_chat_text(p.name);cut_text(name,80);
        text+="\n"+std::to_string(rank+1)+". "+name+" - "+(kind=="time"?duration(value):std::to_string(value));
    }
    return text;
}
void Host::stats_command(Guest &g,std::string_view verb,std::string_view rest) {
    if(!config_.player_stats.enabled)return reply(g,"MSC rewards are disabled on this server.");
    if(stats_failed_)return reply(g,"MSC rewards are paused because their data could not be saved. Please tell staff.");
    if(!g.world_ready)return reply(g,"Load into the map before checking stats.");
    account_stats();
    tick_stats();
    if(stats_failed_)return reply(g,"MSC rewards are paused. Please tell staff.");
    auto found=stats_.players.find(g.member.id);if(found==stats_.players.end())return reply(g,"Your stats will be available once you finish loading.");
    const auto &p=found->second;const auto &s=config_.player_stats;
    if(verb=="pay") {
        const auto args=trim(rest);const auto space=args.rfind(' ');
        if(space==std::string_view::npos)return reply(g,"Usage: /pay <player> <amount>");
        auto name=trim(args.substr(0,space));const auto amount=number(trim(args.substr(space+1)));
        if(name.size()>=2 && name.front()=='\"' && name.back()=='\"')name=name.substr(1,name.size()-2);
        if(name.empty() || !amount || !*amount || *amount>max_stats_counter)return reply(g,"Use a positive whole number of MSC points within the balance limit.");
        auto *to=match_player(name);
        if(!to || !to->handshaken)return reply(g,"No single connected player matches. Use the full name, a unique name prefix or SteamID64.");
        if(to->member.id==g.member.id)return reply(g,"You cannot give points to yourself.");
        if(unlimited_points(to->member.id))return reply(g,"That player already has unlimited admin points.");
        const auto recipient=stats_.players.find(to->member.id);
        if(!to->world_ready || recipient==stats_.players.end())return reply(g,"That player must finish loading before receiving points.");
        const bool unlimited=unlimited_points(g.member.id);
        if(!unlimited && p.wallet<*amount)return reply(g,"Not enough MSC points. Balance: "+points_balance(g.member.id,p.wallet)+".");
        auto &sender=found->second;auto &receiver=recipient->second;
        if(*amount>receiver.wallet_limit() || receiver.wallet>receiver.wallet_limit()-*amount)return reply(g,"That transfer would exceed the recipient's balance limit.");
        const auto before_sender=sender.wallet,before_receiver=receiver.wallet;
        const auto audit=[&](bool committed) {
            auto row=Json::object();row["event"]=unlimited?"admin_grant":"transfer";row["status"]=committed?"committed":"rolled_back";
            row["sender"]=points_identity(g.member.id,guest_name(g));row["recipient"]=points_identity(to->member.id,guest_name(*to));
            row["amount"]=*amount;row["unlimited_admin"]=unlimited;
            row["sender_balance_before"]=before_sender;row["sender_balance_after"]=sender.wallet;
            row["recipient_balance_before"]=before_receiver;row["recipient_balance_after"]=receiver.wallet;
            row["debited"]=before_sender-sender.wallet;row["credited"]=receiver.wallet-before_receiver;
            if(!committed)row["reason"]="stats_save_failed";
            audit_points(std::move(row));
        };
        if(!unlimited)sender.wallet-=*amount;
        receiver.wallet+=*amount;
        try {stats_.save();}
        catch(const std::exception &e) {
            sender.wallet=before_sender;receiver.wallet=before_receiver;stats_failed_=true;roster_dirty_=true;
            log_(std::string("[points-transfer] Save failed; both balances rolled back; rewards paused: ")+e.what());
            audit(false);
            return reply(g,"Could not save the transfer. Both balances are unchanged. Rewards paused until restart.");
        }
        audit(true);
        log_("[points-transfer] UTC "+std::to_string(std::time(nullptr))+" "+guest_name(g)+" ("+std::to_string(g.member.id)+") gave "+std::to_string(*amount)+" points to "+guest_name(*to)+" ("+std::to_string(to->member.id)+")"+(unlimited?"; unlimited admin grant":"; wallet transfer"));
        reply(g,"Gave "+std::to_string(*amount)+" MSC points to "+guest_name(*to)+". Balance: "+points_balance(g.member.id,sender.wallet)+".");
        return reply(*to,guest_name(g)+" gave you "+std::to_string(*amount)+" MSC points. Balance: "+points_balance(to->member.id,receiver.wallet)+".");
    }
    if(verb=="top") {
        const auto [word,arg]=split(rest);auto kind=lower(word);if(kind.empty())kind="weekly";
        if(kind!="weekly" && kind!="points" && kind!="time" && kind!="events")return reply(g,"/top [weekly|points|time|events] [page]");
        unsigned page=1;
        if(!arg.empty()){const auto n=number(trim(arg));if(!n || !*n || *n>100000)return reply(g,"Use a positive leaderboard page number.");page=static_cast<unsigned>(*n);}
        return reply(g,stats_board(kind,page,10));
    }
    if(!trim(rest).empty())return reply(g,"/"+std::string(verb)+" takes no arguments.");
    if(verb=="bal")return reply(g,"MSC balance: "+points_balance(g.member.id,p.wallet)+".");
    const auto remaining=p.daily_us>=std::uint64_t{s.daily_minutes}*60000000?0:std::uint64_t{s.daily_minutes}*60000000-p.daily_us;
    if(verb=="daily")return reply(g,"Daily bonus: "+(p.last_claim_day==p.day?std::string("claimed today."):std::to_string((remaining+59999999)/60000000)+" active minutes remaining.")+"\nStreak: "+std::to_string(p.last_claim_day>=p.day-1?p.streak:0)+" days. Resets at 00:00 UTC.");
    const auto target=std::uint64_t{s.vip_hours}*3600000000ULL;
    const auto *role=role_of(g.member.id);
    const bool staff_vip=unlimited_points(g.member.id) || can(g.member.id,"kick");
    const bool role_vip=role && role->name==s.vip_role;
    const std::string vip_status=staff_vip?"active (staff)":role_vip?"active (VIP role)":p.vip_awarded?"earned":p.active_us>=target?"milestone reached; requires eligible player and VIP roles":duration(target-p.active_us)+" active time remaining";
    reply(g,"MSC stats: "+guest_name(g)+"\nActive playtime: "+duration(p.active_us)+"\nMSC points: "+std::to_string(p.points)+" lifetime / "+std::to_string(p.weekly_points)+" this week"+
        "\nSpendable balance: "+points_balance(g.member.id,p.wallet)+" | /bal | /shop"+
        "\nDaily streak: "+std::to_string(p.last_claim_day>=p.day-1?p.streak:0)+" days / "+std::to_string(p.qualifying_days)+" qualifying visits"+
        "\nParticipation: S.K.A.T.E. "+std::to_string(p.events[0])+", Spot Battles "+std::to_string(p.events[1])+", Jams "+std::to_string(p.events[2])+
        "\nVIP: "+vip_status+(p.vip_awarded && (staff_vip || role_vip)?"; earned through playtime":"")+
        "\n/bal | /pay <player> <amount> | /stats | /daily | /top [weekly|points|time|events] [page]");
}
} // namespace dingosdk::server
