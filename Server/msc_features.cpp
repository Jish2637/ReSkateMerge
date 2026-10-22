#include "server_host.h"
#include "server_text.h"
#include "Engine/Game/Multiplayer/msc_interface.h"
#include "Engine/Game/Multiplayer/msc_staff.h"
#include <charconv>
#include <ctime>

namespace dingosdk::server {
Json Host::msc_games(Guest& g) {
    auto games=Json::array();const auto utc=static_cast<std::uint64_t>(std::time(nullptr));
    const auto balance=stats_.players.contains(g.member.id)?stats_.players.at(g.member.id).wallet:0;
    const auto round=[&](const Wager& w) {
        auto j=Json::object();j["id"]=w.id;j["creator"]=std::to_string(w.creator);j["stake"]=w.stake;j["pot"]=w.stake*w.members.size();j["capacity"]=w.capacity;
        j["seconds"]=w.state=="pending" && utc<w.expires?w.expires-utc:0;j["initial"]=w.initial;j["second"]=0U;j["direction"]="";
        j["state"]=w.state;auto result=w.result;cut_text(result,512);j["result"]=result;
        j["winner"]="";j["my_payout"]=w.payouts.contains(g.member.id)?w.payouts.at(g.member.id):0;
        if(w.state=="settled" && w.game!="highlow" && w.game!="slots" && !w.payouts.empty())j["winner"]=std::to_string(w.payouts.begin()->first);
        // Bound the displayed roster even when the server allows 249-player lobbies.
        std::vector<std::uint64_t> shown;const auto include=[&](std::uint64_t id){if(w.members.contains(id) && shown.size()<10 && std::ranges::find(shown,id)==shown.end())shown.push_back(id);};
        include(g.member.id);include(w.creator);if(!w.payouts.empty())include(w.payouts.begin()->first);for(auto id:w.members)include(id);
        auto members=Json::array();for(auto id:shown){auto m=Json::object();m["id"]=std::to_string(id);auto name=stats_.players.at(id).name;cut_text(name,48);m["name"]=name;m["payout"]=w.payouts.contains(id)?w.payouts.at(id):0;members.push_back(std::move(m));}j["members"]=std::move(members);
        auto rolls=Json::array();
        // Optional outcome metadata is presentation-only. Invalid old metadata cannot change the ledger or interrupt play.
        try {
            if(w.detail.contains("second")){const auto second=msc::integer(w.detail,"second");if(second>=1 && second<=13){j["second"]=second;j["direction"]=msc::text(w.detail,"direction",16);}}
            if(w.detail.contains("rolls") && w.detail.at("rolls").is_array()) {
                const auto& source=w.detail.at("rolls");const auto first=source.size()>24?source.size()-24:0;
                for(auto i=first;i<source.size();++i){const auto& v=source.at(i);const auto value=msc::integer(v,"value"),member=msc::identity(v,"player");if(value>=1 && value<=6 && w.members.contains(member)){auto r=Json::object();r["round"]=msc::integer(v,"round");r["player"]=std::to_string(member);r["value"]=value;rolls.push_back(std::move(r));}}
            }
        } catch(const std::exception&) {rolls=Json::array();}
        j["rolls"]=std::move(rolls);
        j["reels"]=Json::array();j["multiplier"]=0U;
        if(w.game=="slots")try {
            const auto& reels=w.detail.at("reels");
            if(reels.is_array() && reels.size()==3) {
                for(const auto& reel:reels){if(!reel.is_number_unsigned() || reel.get<std::uint64_t>()>=5)throw std::runtime_error("Invalid reel");}
                j["reels"]=reels;j["multiplier"]=msc::integer(w.detail,"multiplier");
            }
        }catch(const std::exception&){j["reels"]=Json::array();}
        return j;
    };
    for(const auto* id:{"coinflip","dice","highlow","slots"}) {
        const std::string game=id;
        if(game=="slots" && !g.msc_slots_ui)continue;auto j=Json::object();j["id"]=game;j["label"]=game=="coinflip"?"Coin Flip":game=="dice"?"Dice Duel":game=="slots"?"Slots":"High / Low";
        if(game=="slots") {
            const bool configured=config_.wagers.enabled && config_.wagers.slots_enabled;
            const bool enabled=configured && config_.player_stats.enabled && !stats_failed_ && g.world_ready && !text_timed_out(g.member.id) && may(g.member.id,multiplayer::role_chat) && !config_.file.empty();
            j["enabled"]=enabled;
            j["reason"]=enabled?"":!configured?"Slots are disabled in the server settings.":stats_failed_?"Player data is paused; spins cannot settle.":!g.world_ready?"Finish loading the map.":"Current server permissions do not allow MSC activities.";
            j["minimum"]=config_.wagers.min_stake;j["maximum"]=config_.wagers.slots_max_stake;j["capacity"]=1U;
            j["round"]=nullptr;j["previous"]=nullptr;
            for(auto it=stats_.wagers.rbegin();it!=stats_.wagers.rend();++it)if(it->game=="slots" && it->creator==g.member.id && it->state=="settled"){j["previous"]=round(*it);break;}
            auto actions=Json::array();if(enabled && balance>=config_.wagers.min_stake && now_>=g.msc_slot_next)actions.push_back("spin");j["actions"]=std::move(actions);
            j["cooldown"]=now_<g.msc_slot_next?(g.msc_slot_next-now_+999999)/1000000:0;
            auto wins=Json::array();
            for(auto it=stats_.wagers.rbegin();it!=stats_.wagers.rend() && wins.size()<5;++it) {
                if(it->game!="slots" || it->state!="settled" || it->payouts.empty() || it->payouts.begin()->second<=it->stake)continue;
                const auto player=stats_.players.find(it->creator);if(player==stats_.players.end())continue;
                auto winner=Json::object();auto name=player->second.name;cut_text(name,48);
                winner["name"]=name;winner["stake"]=it->stake;winner["payout"]=it->payouts.begin()->second;wins.push_back(std::move(winner));
            }j["wins"]=std::move(wins);
            games.push_back(std::move(j));continue;
        }
        const bool configured=config_.wagers.enabled && (game=="coinflip"?config_.wagers.coinflip_enabled:game=="dice"?config_.wagers.dice_enabled:config_.wagers.highlow_enabled);
        const bool enabled=configured && config_.player_stats.enabled && !stats_failed_ && g.world_ready && !text_timed_out(g.member.id) && may(g.member.id,multiplayer::role_chat) && !config_.file.empty();
        j["enabled"]=enabled;j["reason"]=enabled?"":!configured?"This game is disabled in the server wager settings.":stats_failed_?"Player data is paused; stakes cannot change.":!g.world_ready?"Finish loading the map.":"Your current server permissions or chat restrictions prevent wagering.";
        j["minimum"]=config_.wagers.min_stake;j["maximum"]=game=="coinflip"?config_.wagers.coinflip_max_stake:game=="dice"?config_.wagers.dice_max_stake:config_.wagers.highlow_max_stake;j["capacity"]=game=="highlow"?1:config_.max_players;
        const Wager* current{};const Wager* previous{};
        for(auto it=stats_.wagers.rbegin();it!=stats_.wagers.rend();++it)if(it->game==game && (game!="highlow" || it->creator==g.member.id)) {
            if(it->state=="pending"){if(!current)current=&*it;}else if(!previous)previous=&*it;
        }
        j["round"]=current?round(*current):Json{};j["previous"]=previous?round(*previous):Json{};
        auto actions=Json::array();
        if(enabled) {
            if(!current){if(balance>=config_.wagers.min_stake && (game=="highlow" || config_.max_players>=2))actions.push_back("create");}
            else if(game=="highlow") {if(current->initial<13)actions.push_back("higher");if(current->initial>1)actions.push_back("lower");actions.push_back("cancel");}
            else {
                const bool member=current->members.contains(g.member.id),creator=current->creator==g.member.id;
                if(!member && current->members.size()<current->capacity && current->capacity==config_.max_players && balance>=current->stake)actions.push_back("join");
                if(member)actions.push_back("leave");if(creator){actions.push_back("cancel");if(current->members.size()>=2 && current->capacity==config_.max_players)actions.push_back("start");}
            }
        }
        j["actions"]=std::move(actions);games.push_back(std::move(j));
    }
    return games;
}
void Host::msc_game_action(Guest& g,const Json& j) {
    const auto game=msc::text(j,"game",32),action=msc::text(j,"action",16);const auto expected=msc::integer(j,"round");
    if(game=="slots") {
        if(action!="spin" || expected!=0)throw std::runtime_error("Invalid slot action.");
        msc_slot_spin(g,msc::integer(j,"stake"));return;
    }
    if(game!="coinflip" && game!="dice" && game!="highlow")throw std::runtime_error("Unknown MSC game.");
    tick_wagers();
    const Wager* current{};for(const auto& w:stats_.wagers)if(w.game==game && w.state=="pending" && (game!="highlow" || w.creator==g.member.id)){current=&w;break;}
    if(expected!=(current?current->id:0))throw std::runtime_error("The game changed. Refresh before acting on another round.");
    std::string argument;
    if(action=="create") {if(expected)throw std::runtime_error("A round is already open.");const auto stake=msc::integer(j,"stake");argument=game=="highlow"?std::to_string(stake):"create "+std::to_string(stake);}
    else if(action=="join" || action=="leave" || action=="start" || action=="cancel") {
        if(game=="highlow" && action!="cancel")throw std::runtime_error("Invalid High / Low action.");argument=action;
    } else if(game=="highlow" && (action=="higher" || action=="lower"))argument=action;
    else throw std::runtime_error("Invalid game action.");
    wager_command(g,game,argument);
}
Json Host::msc_staff(Guest& g) {
    if(!is_staff(g.member.id))return {};
    auto j=Json::object();const auto* own=role_of(g.member.id);j["role"]=own?own->name:"";
    auto actions=Json::array();for(const auto& action:msc::staff_actions())if(can(g.member.id,msc::staff_permission(action)))actions.push_back(action.id);j["actions"]=std::move(actions);
    auto roles=Json::array();roles.push_back("none");for(const auto& role:config_.roles.list)if(role.rank<rank(g.member.id) && roles.size()<64)roles.push_back(role.name);j["roles"]=std::move(roles);
    std::vector<const Guest*> guests;for(const auto& [id,p]:guests_)if(p->handshaken){(void)id;guests.push_back(p.get());}
    const auto pages=std::max<std::uint64_t>(1,(guests.size()+9)/10);g.msc_staff_page=std::clamp(g.msc_staff_page,std::uint64_t{1},pages);j["page"]=g.msc_staff_page;j["pages"]=pages;
    auto players=Json::array();for(auto i=(g.msc_staff_page-1)*10;i<std::min<std::uint64_t>(guests.size(),g.msc_staff_page*10);++i) {
        const auto& who=*guests[static_cast<std::size_t>(i)];auto p=Json::object();const auto* role=role_of(who.member.id);
        p["id"]=std::to_string(who.member.id);p["epoch"]=std::to_string(who.member.epoch);p["name"]=guest_name(who);p["role"]=role?role->name:"";p["rank"]=static_cast<unsigned>(std::max(0,rank(who.member.id)));
        p["voice_muted"]=voice_muted(who.member.id);p["text_muted"]=text_timed_out(who.member.id);p["locks"]=locks_of(who.member.id);players.push_back(std::move(p));
    }j["players"]=std::move(players);
    auto settings=Json::object();const auto flag=[&](const char* key,bool value){settings[key]=value?"on":"off";};const auto number=[&](const char* key,auto value){settings[key]=std::to_string(value);};
    auto name=config_.name;cut_text(name,128);settings["name"]=name;auto current_map=map_name();cut_text(current_map,128);settings["map"]=current_map;
    settings["placement"]=std::string(object_placement_name(config_.object_placement));number("rate",config_.send_rate);
    flag("listed",config_.listed);flag("voice",config_.voice_chat);number("voice-range",config_.voice_range);number("objects",config_.object_limit);
    flag("object-scaling",config_.object_scaling);flag("effects",config_.sync_effects);flag("drones",config_.sync_drones);flag("noclip",config_.noclip);flag("nobail",config_.no_bail);flag("boosts",config_.boosts);flag("tuning",config_.enforce_tuning);flag("layer-sync",config_.world_layer_sync);
    flag("activity-log",config_.activity_log);flag("announce-throwdowns",config_.announce_throwdowns);flag("parties",config_.parties);number("party-size",config_.party_size);number("afk-kick",config_.afk_kick);number("kick-time",config_.kick_minutes);
    number("crowd",config_.crowd_budget);number("bone-scale",config_.bone_scale_limit);number("bone-reach",config_.bone_reach_limit);
    number("rotation",config_.map_rotation);flag("rotation.vote",config_.rotation_vote);number("rotation.stay",config_.rotation_extend);flag("rotation.announce",config_.rotation_announce);
    settings["speed-check"]=config_.speed_check;settings["board-check"]=config_.board_check;settings["score-check"]=config_.score_check;flag("words.global",config_.global_words);
    settings["password"]=password_?"set (hidden)":"off";settings["chat-color"]=config_.chat_color+" / "+config_.chat_text_color;
    j["settings"]=std::move(settings);j["output"]=g.msc_staff_output;
    j["quote"]=nullptr;if(!g.msc_staff_token.empty() && now_<g.msc_staff_until && g.msc_staff_world==world_ && g.msc_staff_epoch==g.member.epoch) {
        auto q=Json::object();q["token"]=g.msc_staff_token;q["label"]=g.msc_staff_label;q["description"]=g.msc_staff_description;q["seconds"]=(g.msc_staff_until-now_+999999)/1000000;j["quote"]=std::move(q);
    }
    return j;
}
std::string Host::msc_staff_guard(Guest& g,std::string_view action,const std::vector<std::string>& args) {
    if(action=="build.confirm") {
        const auto it=build_confirmations_.find(g.member.id);if(it==build_confirmations_.end())return "none";
        const auto& p=it->second;return p.command+":"+p.name+":"+p.file_stamp+":"+std::to_string(p.until)+":"+std::to_string(p.world);
    }
    if(action=="delobj.index" && !args.empty()) {
        const auto it=object_previews_.find(g.member.id);if(it==object_previews_.end())return "none";
        const auto n=number(args[0]);if(!n || !*n || *n>it->second.entries.size())return "invalid";
        const auto& v=it->second.entries[static_cast<std::size_t>(*n-1)];return std::to_string(v.owner)+":"+std::to_string(v.epoch)+":"+std::to_string(v.id)+":"+std::to_string(it->second.until);
    }
    return {};
}
std::string Host::msc_staff_action(Guest& g,const Json& j) {
    if(!is_staff(g.member.id))throw std::runtime_error("Staff permissions are required.");
    const auto op=msc::text(j,"op",24);
    if(op=="staff-cancel"){g.msc_staff_token.clear();return "Staff review cancelled. Existing build confirmations are separate.";}
    const auto run=[&](const msc::StaffAction& spec,const std::string& line) {
        if(!can(g.member.id,msc::staff_permission(spec)))throw std::runtime_error("Your current role cannot use this staff action.");
        auto output=command(line,g.member.id);cut_text(output,2304);g.msc_staff_output=output;
        return std::string("Server response received. See the staff result panel.");
    };
    if(op=="staff-confirm") {
        const auto token=msc::text(j,"token",96);
        if(token.empty() || token!=g.msc_staff_token || now_>=g.msc_staff_until || g.msc_staff_world!=world_ || g.msc_staff_epoch!=g.member.epoch)throw std::runtime_error("Staff review changed or expired. Review the action again.");
        const auto* spec=msc::staff_action(g.msc_staff_action_id);if(!spec || spec->read_only)throw std::runtime_error("Invalid staff review.");
        for(const auto& [id,epoch]:g.msc_staff_targets){const auto* target=find(id);if(!target || !target->handshaken || target->member.epoch!=epoch)throw std::runtime_error("A selected player reconnected or left. Review the action again.");}
        if(msc_staff_guard(g,spec->id,g.msc_staff_args)!=g.msc_staff_guard_value)throw std::runtime_error("The underlying build/object preview changed. Review again.");
        const auto line=g.msc_staff_command;g.msc_staff_token.clear();return run(*spec,line);
    }
    const auto id=msc::text(j,"action",48);const auto* spec=msc::staff_action(id);
    if(!spec || !can(g.member.id,msc::staff_permission(*spec)))throw std::runtime_error("This staff action is not permitted.");
    if((op=="staff-read")!=spec->read_only || (op!="staff-read" && op!="staff-preview"))throw std::runtime_error("This action requires the appropriate read/review operation.");
    const auto& values=j.at("args");if(!values.is_array() || values.size()!=spec->fields.size())throw std::runtime_error("Invalid staff fields.");
    std::string line=spec->prefix,description=spec->label;std::vector<std::string> args;std::map<std::uint64_t,std::uint64_t> targets;bool skipped{};
    for(std::size_t i=0;i<spec->fields.size();++i) {
        const auto& field=spec->fields[i];const auto value=values.at(i).get<std::string>();
        if(value.size()>256 || value.find_first_of("\r\n\0",0,3)!=std::string::npos)throw std::runtime_error("Use bounded single-line staff fields.");
        args.push_back(value);
        if(value.empty()){if(!field.optional)throw std::runtime_error("Complete every required staff field.");skipped=true;continue;}
        if(skipped)throw std::runtime_error("Complete preceding optional fields before later fields.");
        if(!field.choices.empty() && std::ranges::find(field.choices,value)==field.choices.end())throw std::runtime_error("Invalid staff selection.");
        if(field.type=="number" || field.type=="object") {if(!number(value))throw std::runtime_error("Enter a whole nonnegative number.");}
        if(field.type=="identity" && (!number(value) || !individual_steam_id(*number(value))))throw std::runtime_error("Enter an individual Steam ID.");
        if(field.type=="player") {
            const auto player=number(value);const auto* target=player?find(*player):nullptr;
            if(!target || !target->handshaken)throw std::runtime_error("Select a connected player.");targets[*player]=target->member.epoch;
        }
        line+=" ";if(field.type=="object")line+="#";line+=value;
        description+="\n"+field.label+": "+(field.type=="secret"?std::string("hidden"):value);
    }
    if(line.size()>max_admin_text)throw std::runtime_error("Staff command exceeds the existing server command limit.");
    if(spec->read_only)return run(*spec,line);
    g.msc_staff_command=line;g.msc_staff_label=spec->label;g.msc_staff_description=description;g.msc_staff_args=std::move(args);g.msc_staff_targets=std::move(targets);
    g.msc_staff_guard_value=msc_staff_guard(g,id,g.msc_staff_args);g.msc_staff_action_id=id;
    g.msc_staff_token="staff-"+std::to_string(++msc_quote_serial_);g.msc_staff_world=world_;g.msc_staff_epoch=g.member.epoch;g.msc_staff_until=now_+60000000;
    return "Review this staff action before confirming. No change has been made yet.";
}
} // namespace dingosdk::server
