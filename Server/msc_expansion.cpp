#include "server_host.h"
#include "msc_commands.h"
#include "server_text.h"
#include "shop.h"
#include "wager_rules.h"
#include "Engine/Core/Text/word_filter.h"
#include <algorithm>
#include <ctime>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#endif

namespace dingosdk::server {
namespace {
std::uint64_t utc_now() { return static_cast<std::uint64_t>(std::time(nullptr)); }
// Rejection sampling over OS entropy, with no modulo bias.
std::uint64_t draw(std::uint64_t count) {
    if(!count)throw std::runtime_error("Empty random range");
    const auto threshold=(std::uint64_t{0}-count)%count;
    for(;;) {
        std::uint64_t value{};
#ifdef _WIN32
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
            throw std::runtime_error("Cannot generate wager randomness");
#else
        std::random_device random;
        for(unsigned i=0;i<4;++i)value=(value<<16)^(random()&0xffff);
#endif
        if(value>=threshold)return value%count;
    }
}
unsigned missing_shimmers(const PlayerStats &p) {
    unsigned count=0;for(const auto &[key,value]:shop_shimmers)
        if(std::ranges::find(p.shimmers,std::string(key))==p.shimmers.end())++count;
    return count;
}
bool expansion_kind(std::string_view kind) {return kind=="joinmsg" || kind=="colourbundle" || kind=="shimmerbundle";}
}

std::string Host::joinmsg_error(std::string_view value) const {
    if(value.empty() || trim(value)!=value)return "Please provide a join message.";
    // Validate UTF-8 and count code points, rejecting control characters and invisible formatting.
    std::size_t length=0;
    for(std::size_t i=0;i<value.size();) {
        auto c=static_cast<unsigned char>(value[i++]);std::uint32_t cp=c;unsigned extra=0;
        if(c>=0xc2 && c<=0xdf){cp=c&31;extra=1;}
        else if(c>=0xe0 && c<=0xef){cp=c&15;extra=2;}
        else if(c>=0xf0 && c<=0xf4){cp=c&7;extra=3;}
        else if(c>=0x80)return "Invalid message encoding.";
        for(unsigned j=0;j<extra;++j){if(i==value.size() || (static_cast<unsigned char>(value[i])&0xc0)!=0x80)return "Invalid message encoding.";cp=(cp<<6)|(static_cast<unsigned char>(value[i++])&63);}
        if((extra==1 && cp<0x80)||(extra==2 && cp<0x800)||(extra==3 && cp<0x10000)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff))return "Invalid message encoding.";
        if(cp<32 || (cp>=127 && cp<=159) || cp=='<' || cp=='>' || (cp>=0x200b && cp<=0x200f) || (cp>=0x2028 && cp<=0x202e) || (cp>=0x2060 && cp<=0x206f) || cp==0xfeff)return "Control characters and text formatting are not allowed.";
        ++length;
    }
    if(value.size()>multiplayer_chat_max_bytes-max_member_name-9)return "That message uses too many encoded bytes for a single chat line.";
    if(length>config_.shop.joinmsg_max_length)return "Join message limit: "+std::to_string(config_.shop.joinmsg_max_length)+" characters.";
    if(text::contains_bad_words(value))return "That message is not allowed by the server word filter.";
    auto normal=lower(value);for(auto &c:normal)if(!(c>='a'&&c<='z') && !(c>='0'&&c<='9'))c=' ';
    for(auto rest=std::string_view(normal);!rest.empty();) {
        const auto [word,tail]=split(trim(rest));rest=tail;
        for(const auto reserved:{"admin","administrator","mod","moderator","staff","server","system","owner","developer","dev","reskate"})
            if(word==reserved)return "That message contains a reserved staff or system name.";
    }
    return {};
}
void Host::joinmsg_broadcast(Guest &g) {
    if(!config_.shop.enabled || !config_.shop.joinmsg_enabled || !config_.player_stats.enabled || stats_failed_ ||
       !may(g.member.id,multiplayer::role_chat) || text_timed_out(g.member.id))return;
    const auto it=stats_.players.find(g.member.id);if(it==stats_.players.end())return;
    auto &p=it->second;const auto utc=utc_now();
    if(!p.joinmsg_owned || !p.joinmsg_enabled || !joinmsg_error(p.joinmsg).empty() ||
       (p.joinmsg_last && (utc<p.joinmsg_last || utc-p.joinmsg_last<config_.shop.joinmsg_cooldown_seconds)))return;
    const auto before=p;p.joinmsg_last=utc;
    if(shop_commit(g,before,"join message delivered"))send_chat(guest_name(g)+" joined: "+p.joinmsg);
}
bool Host::expansion_shop(Guest &g,std::string_view verb,std::string_view argument) {
    auto &p=stats_.players.at(g.member.id);
    const auto rest=trim(argument);const auto [sub,message]=split(rest);
    const auto quote_it=shop_quotes_.find(g.member.id);
    const bool confirming=verb=="buy" && quote_it!=shop_quotes_.end() && expansion_kind(quote_it->second.kind);
    if(!expansion_kind(verb) && !confirming)return false;
    const auto answer=[&](const std::string &s){reply(g,s);return true;};
    if(confirming && !rest.empty())return answer("Usage: /buy");
    const std::string kind=confirming?quote_it->second.kind:std::string(verb);
    const bool enabled=kind=="joinmsg"?config_.shop.joinmsg_enabled:kind=="colourbundle"?config_.shop.colourbundle_enabled:config_.shop.shimmerbundle_enabled;
    if(!enabled){shop_quotes_.erase(g.member.id);return answer("That shop feature is disabled.");}
    if(kind=="joinmsg" && (!may(g.member.id,multiplayer::role_chat) || text_timed_out(g.member.id)))return answer("Your current chat restrictions prevent custom join messages.");
    const unsigned missing=missing_shimmers(p);
    const unsigned price=kind=="joinmsg"?config_.shop.joinmsg_price:kind=="colourbundle"?config_.shop.colourbundle_price:
        static_cast<unsigned>((std::uint64_t{config_.shop.shimmerbundle_price}*missing+4)/5);
    const bool owned=kind=="joinmsg"?p.joinmsg_owned:kind=="colourbundle"?p.colourbundle_owned:!missing;
    if(confirming) {
        const auto quote=quote_it->second;shop_quotes_.erase(quote_it);
        if(now_>=quote.expires || quote.epoch!=g.member.epoch)return answer("Purchase preview expired. Please preview it again.");
        if(price!=quote.price)return answer("The price has changed. Please preview your purchase again.");
        if(owned)return answer("You already own this purchase. No points spent.");
        if(kind=="joinmsg")if(const auto error=joinmsg_error(quote.value);!error.empty())return answer(error);
        if(!unlimited_points(g.member.id) && p.wallet<price)return answer("Not enough MSC points.");
        const auto before=p;if(!unlimited_points(g.member.id))p.wallet-=price;
        if(kind=="joinmsg"){p.joinmsg_owned=true;p.joinmsg_enabled=true;p.joinmsg=quote.value;}
        else if(kind=="colourbundle")p.colourbundle_owned=true;
        else for(const auto &[key,value]:shop_shimmers)if(std::ranges::find(p.shimmers,std::string(key))==p.shimmers.end())p.shimmers.push_back(std::string(key));
        if(shop_commit(g,before,"purchased "+kind+" "+quote.value,price))reply(g,"Purchased "+kind+" for "+std::to_string(price)+" MSC. Yours permanently.");
        return true;
    }
    if(rest.empty())return answer(kind+": "+(owned?"owned":"not owned")+" | Price: "+std::to_string(price)+" MSC"+
        (kind=="joinmsg"?" | "+(p.joinmsg_enabled?std::string("on"):std::string("off"))+" | "+p.joinmsg+"\n/joinmsg set <message> | on | off":" | /"+kind+" buy, then /buy"));
    shop_quotes_.erase(g.member.id);
    if(kind=="joinmsg") {
        if(sub=="on" || sub=="off") {
            if(!message.empty())return answer("Usage: /joinmsg on | off");
            if(!owned)return answer("Purchase a join message first: /joinmsg set <message>, then /buy.");
            if(sub=="on")if(const auto error=joinmsg_error(p.joinmsg);!error.empty())return answer(error);
            const auto before=p;p.joinmsg_enabled=sub=="on";
            if(shop_commit(g,before,"join message "+std::string(sub)))reply(g,"Join message "+std::string(sub)+".");return true;
        }
        if(sub!="set")return answer("Usage: /joinmsg set <message> | on | off");
        if(const auto error=joinmsg_error(message);!error.empty())return answer(error);
        if(owned){const auto before=p;p.joinmsg=message;if(shop_commit(g,before,"join message edited"))reply(g,"Join message updated for free.");return true;}
    } else {
        if(rest!="buy")return answer("Usage: /"+kind+" [buy]");
        if(owned)return answer("You already own this bundle. No points spent.");
    }
    if(!unlimited_points(g.member.id) && p.wallet<price)return answer("Not enough MSC points. Price: "+std::to_string(price)+" MSC.");
    shop_quotes_[g.member.id]={kind,kind=="joinmsg"?std::string(message):kind,price,now_+120000000,g.member.epoch};
    return answer("Preview "+kind+(kind=="joinmsg"?": "+std::string(message):std::string{})+"\nCost: "+std::to_string(price)+" MSC. /buy within 2 minutes to confirm.");
}

bool Host::wager_commit(const std::map<std::uint64_t,PlayerStats> &before,const std::vector<Wager> &ledger,std::uint64_t next,std::string_view action) {
    auto row=Json::object();row["event"]="wager";row["action"]=action;
    // Include full changed records: IDs, participants, stakes, results and payouts.
    auto records=Json::array();
    for(const auto &w:stats_.wagers) {
        const auto old=std::ranges::find_if(ledger,[&](const auto &r){return r.id==w.id;});
        if(old!=ledger.end() && old->state==w.state && old->members==w.members && old->payouts==w.payouts && old->result==w.result)continue;
        auto j=Json::object();j["id"]=w.id;j["game"]=w.game;j["state"]=w.state;j["stake"]=w.stake;j["result"]=w.result;
        auto members=w.members;if(old!=ledger.end())members.insert(old->members.begin(),old->members.end());
        auto participants=Json::array();for(auto id:members){auto p=Json::object();p["id"]=std::to_string(id);p["stake"]=w.stake;p["payout"]=w.payouts.contains(id)?w.payouts.at(id):0;
            p["balance_before"]=before.at(id).wallet;p["balance_after"]=stats_.players.at(id).wallet;p["left"]=!w.members.contains(id);participants.push_back(std::move(p));}
        j["participants"]=std::move(participants);records.push_back(std::move(j));
    }
    row["records"]=std::move(records);
    try {stats_.rebuild_headroom();stats_.save();}
    catch(const std::exception &e) {
        stats_.players=before;stats_.wagers=ledger;stats_.next_wager_id=next;stats_failed_=true;
        row["status"]="rolled_back";audit_points(std::move(row));
        log_(std::string("[wagers] Save failed; economy paused until restart: ")+e.what());return false;
    }
    row["status"]="committed";audit_points(std::move(row));roster_dirty_=true;return true;
}
bool Host::wager_refund(std::size_t index,std::string_view reason,std::uint64_t leaving) {
    if(index>=stats_.wagers.size() || stats_.wagers[index].state!="pending")return false;
    const auto before=stats_.players;const auto ledger=stats_.wagers;const auto next=stats_.next_wager_id;
    auto &w=stats_.wagers[index];
    const bool all=!leaving || leaving==w.creator;
    if(!all && !w.members.contains(leaving))return false;
    const auto members=w.members;
    for(auto id:members)if(all || id==leaving) {
        stats_.players.at(id).wallet+=w.stake;
        if(all)w.payouts[id]=w.stake;else w.members.erase(id);
    }
    if(all){w.state="refunded";w.result=reason;}
    if(!wager_commit(before,ledger,next,reason))return false;
    for(auto id:members)if(all || id==leaving)if(auto *g=find(id))reply(*g,w.game+": stake refunded ("+std::string(reason)+").");
    return true;
}
void Host::tick_wagers(bool cancel_all,std::uint64_t leaving) {
    if(stats_failed_)return;
    const auto utc=utc_now();
    for(std::size_t i=0;i<stats_.wagers.size() && !stats_failed_;++i) {
        const auto &w=stats_.wagers[i];if(w.state!="pending")continue;
        const bool disabled=!config_.player_stats.enabled || !config_.wagers.enabled ||
            (w.game=="coinflip"?!config_.wagers.coinflip_enabled:w.game=="dice"?!config_.wagers.dice_enabled:!config_.wagers.highlow_enabled);
        if(cancel_all || disabled || utc>=w.expires)wager_refund(i,cancel_all?"server transition":disabled?"feature disabled":"timeout");
        else if(leaving && w.members.contains(leaving))wager_refund(i,"player left",leaving);
    }
}
void Host::wager_command(Guest &g,std::string_view game,std::string_view argument) {
    const auto answer=[&](const std::string &text){reply(g,text);};
    if(stats_failed_)return answer("MSC wagers are paused because player data could not be saved. Contact staff.");
    if(!config_.player_stats.enabled || !config_.wagers.enabled ||
       (game=="coinflip"?!config_.wagers.coinflip_enabled:game=="dice"?!config_.wagers.dice_enabled:!config_.wagers.highlow_enabled))return answer("That MSC game is disabled.");
    if(config_.file.empty())return answer("Wagering requires a saved server configuration and persistent player data.");
    if(!g.world_ready)return answer("Finish loading before wagering.");
    if(text_timed_out(g.member.id))return answer(text_timeout_notice(g.member.id));
    if(!may(g.member.id,multiplayer::role_chat))return answer("Your role cannot use wager commands.");
    if(!stats_.players.contains(g.member.id))stats_joined(g);if(stats_failed_)return;
    tick_wagers();if(stats_failed_)return;
    const auto [action,rest]=split(trim(argument));const auto verb=resolve_msc_action(game,action);const auto id=g.member.id;
    std::size_t index=stats_.wagers.size();
    for(std::size_t i=0;i<stats_.wagers.size();++i)if(stats_.wagers[i].game==game && stats_.wagers[i].state=="pending" &&
        (game!="highlow" || stats_.wagers[i].creator==id)){index=i;break;}
    const auto help=[&](){answer(game=="highlow"?"/highlow (/hl) <amount> | higher/h | lower/l | cancel/x. Numbers 1-13; ties refund; fair probability-based payouts.":
        "/"+std::string(game)+" ("+(game=="coinflip"?std::string("/cf"):std::string("/dd"))+") create/c <amount> | join/j | leave/l | start/go | status/s | cancel/x. Equal stakes; winner takes the pot; capacity: "+std::to_string(config_.max_players)+".");};
    if(verb.empty()){help();return;}
    if(verb=="cancel" || (verb=="leave" && game!="highlow")) {
        if(!rest.empty()){help();return;}
        if(index==stats_.wagers.size())return answer("No available round.");
        const auto &w=stats_.wagers[index];
        if(verb=="cancel" && w.creator!=id)return answer("Only the creator may cancel the lobby.");
        if(!w.members.contains(id))return answer("You are not in this lobby.");
        if(!wager_refund(index,"cancelled",verb=="leave"?id:0))answer("Could not save refund; economy paused. Restart recovery will retry pending stakes.");return;
    }
    if(verb=="status" && game!="highlow") {
        if(!rest.empty()){help();return;}
        if(index==stats_.wagers.size())return answer("No available lobby. /"+std::string(game)+" create <amount>");
        const auto &w=stats_.wagers[index];std::string text=std::string(game)+" | Stake: "+std::to_string(w.stake)+" | Pot: "+std::to_string(w.stake*w.members.size())+" | Players: "+std::to_string(w.members.size())+"/"+std::to_string(config_.max_players);
        for(auto member:w.members)text+="\n"+stats_.players.at(member).name;
        reply(g,text,static_cast<unsigned>(w.members.size()+1));return;
    }
    const bool creating=game=="highlow"?number(verb).has_value():verb=="create";
    if(creating || (verb=="join" && game!="highlow")) {
        if((game=="highlow" && !rest.empty()) || (verb=="join" && !rest.empty())){help();return;}
        if(creating && index!=stats_.wagers.size())return answer("A round is already open.");
        if(!creating && index==stats_.wagers.size())return answer("No available lobby.");
        const auto amount=creating?number(game=="highlow"?verb:rest):std::optional<std::uint64_t>{stats_.wagers[index].stake};
        const auto maximum=game=="coinflip"?config_.wagers.coinflip_max_stake:game=="dice"?config_.wagers.dice_max_stake:config_.wagers.highlow_max_stake;
        if(!amount || *amount<config_.wagers.min_stake || *amount>maximum || *amount>1000000)return answer("Stake must be "+std::to_string(config_.wagers.min_stake)+"-"+std::to_string(maximum)+" MSC.");
        if(game!="highlow" && config_.max_players<2)return answer("The server cap must allow at least two players.");
        if(!creating && (stats_.wagers[index].members.contains(id) || stats_.wagers[index].members.size()>=config_.max_players))return answer("Already joined or lobby is full.");
        // Capacity is immutable while this lobby is open. If reconfigured, require a fresh lobby.
        if(!creating && stats_.wagers[index].capacity!=config_.max_players)return answer("Server capacity changed. Cancel and recreate this lobby.");
        auto &p=stats_.players.at(id);if(p.wallet<*amount)return answer("Not enough spendable MSC. Administrators also need real wallet funds.");
        const auto capacity=game=="highlow"?1:config_.max_players;
        if(capacity>multiplayer_player_limit-1)return answer("Server player cap exceeds the supported protocol capacity.");
        const auto exposure=*amount*(game=="highlow"?12:capacity);
        if(exposure>p.wallet_limit() || p.wallet-*amount>p.wallet_limit()-exposure)return answer("Wallet is too close to its limit to reserve a safe payout.");
        const auto before=stats_.players;const auto ledger=stats_.wagers;const auto next=stats_.next_wager_id;
        if(creating) {
            if(next>=max_stats_counter)return answer("Wager transaction sequence exhausted.");
            Wager w;w.id=next;w.game=game;w.creator=id;w.stake=*amount;w.capacity=capacity;
            w.expires=utc_now()+(game=="highlow"?config_.wagers.highlow_timeout_seconds:config_.wagers.lobby_timeout_seconds);
            if(game=="highlow")try{w.initial=1+draw(13);}catch(const std::exception &e){log_(e.what());return answer("Random generator unavailable; no stake charged.");}
            // Keep recent terminal records; transaction IDs are never reused.
            if(stats_.wagers.size()>=256){auto it=std::ranges::find_if(stats_.wagers,[](const auto &r){return r.state!="pending";});if(it!=stats_.wagers.end())stats_.wagers.erase(it);}
            stats_.wagers.push_back(std::move(w));index=stats_.wagers.size()-1;++stats_.next_wager_id;
        }
        stats_.wagers[index].members.insert(id);p.wallet-=*amount;
        if(!wager_commit(before,ledger,next,creating?"created":"joined"))return answer("Could not save wager. Your stake has not changed; economy paused.");
        const auto &w=stats_.wagers[index];
        if(creating && game!="highlow")send_chat("[MSC] "+guest_name(g)+" opened a "+std::string(game=="dice"?"Dice Duel":"Coin Flip")+" lobby for "+std::to_string(w.stake)+" MSC. Join with /"+std::string(game)+" join or MSC Hub > Activities.");
        return answer(game=="highlow"?"First number: "+std::to_string(w.initial)+". /hl h | l | x. Stake reserved: "+std::to_string(w.stake)+" MSC.\nWinning gross payout: 12 x stake / winning numbers; fractional MSC uses fair randomised rounding.":
            std::string(game)+": joined for "+std::to_string(w.stake)+" MSC. Players: "+std::to_string(w.members.size())+"/"+std::to_string(config_.max_players)+". /"+std::string(game)+" status");
    }
    if((game=="highlow" && (verb=="higher" || verb=="lower")) || (game!="highlow" && verb=="start")) {
        if(!rest.empty()){help();return;}
        if(index==stats_.wagers.size())return answer("No available round.");
        const auto &current=stats_.wagers[index];
        if(current.creator!=id)return answer("Only the creator may start the lobby.");
        if(game!="highlow" && current.capacity!=config_.max_players)return answer("Server capacity changed. Cancel and recreate this lobby.");
        if(game!="highlow" && current.members.size()<2){wager_refund(index,"not enough players");return;}
        const auto wins=verb=="higher"?13-current.initial:current.initial-1;
        if(game=="highlow" && !wins)return answer("That direction cannot win. Choose the other direction or cancel; your reserved stake is unchanged.");
        // Resolve locally before changing any durable state; RNG failure cannot debit or partially settle.
        auto settled=current;settled.state="settled";
        try {
            if(game=="highlow") {
                const auto second=1+draw(13);
                const auto payout=highlow_payout(current.stake,static_cast<unsigned>(current.initial),static_cast<unsigned>(second),verb=="higher",draw);
                settled.detail["second"]=second;settled.detail["direction"]=std::string(verb);
                settled.payouts[id]=payout;settled.result=std::to_string(current.initial)+" -> "+std::to_string(second)+" ("+std::string(verb)+"); gross payout "+std::to_string(payout)+" MSC";
            } else {
                std::vector<std::uint64_t> contenders(current.members.begin(),current.members.end());
                if(game=="coinflip") {const auto winner=contenders[draw(contenders.size())];settled.payouts[winner]=current.stake*current.members.size();}
                else {
                    settled.detail["rolls"]=Json::array();
                    const auto winner=dice_winner(contenders,[]{return static_cast<unsigned>(1+draw(6));},[&](unsigned round,std::uint64_t member,unsigned roll){
                        auto& rolls=settled.detail["rolls"];if(rolls.size()<128){auto row=Json::object();row["round"]=round;row["player"]=std::to_string(member);row["value"]=roll;rolls.push_back(std::move(row));}
                        settled.result+="Round "+std::to_string(round)+": "+stats_.players.at(member).name+"="+std::to_string(roll)+"\n";
                    });
                    settled.payouts[winner]=current.stake*current.members.size();
                }
                const auto winner=settled.payouts.begin()->first;
                settled.result+=std::string(game)+": "+stats_.players.at(winner).name+" won "+std::to_string(settled.payouts.at(winner))+" MSC!";
            }
        } catch(const std::exception &e){log_(std::string("[wagers] ")+e.what());wager_refund(index,"random outcome unavailable");return;}
        const auto before=stats_.players;const auto ledger=stats_.wagers;const auto next=stats_.next_wager_id;
        stats_.wagers[index]=settled;
        for(const auto &[member,payout]:settled.payouts)stats_.players.at(member).wallet+=payout;
        if(!wager_commit(before,ledger,next,"settled"))return answer("Could not save result. Wager remains pending for restart refund; economy paused.");
        if(game=="highlow")answer(settled.result);
        else for(auto remaining=std::string_view(settled.result);!remaining.empty();) {
            const auto end=remaining.find('\n');send_chat(remaining.substr(0,end));
            if(end==std::string_view::npos)break;remaining.remove_prefix(end+1);
        }
        return;
    }
    help();
}
} // namespace dingosdk::server
