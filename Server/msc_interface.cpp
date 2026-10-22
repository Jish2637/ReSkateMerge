#include "msc_presence.h"
#include "leaderboard.h"
#include "server_host.h"
#include "server_text.h"
#include "shop.h"
#include "Engine/Game/Multiplayer/msc_interface.h"
#include "Engine/Core/Text/word_filter.h"
#include <ctime>

namespace dingosdk::server {
Json Host::msc_snapshot(Guest& g) {
    auto out=Json::object();
    out["v"]=msc::version;out["epoch"]=std::to_string(g.member.epoch);out["player"]=std::to_string(g.member.id);
    const auto found=stats_.players.find(g.member.id);
    const PlayerStats empty; const auto& p=found==stats_.players.end()?empty:found->second;
    const auto& settings=config_.player_stats;
    const bool ready=g.world_ready && found!=stats_.players.end();
    const bool stats=config_.player_stats.enabled && !stats_failed_ && ready;
    const bool shop=stats && config_.shop.enabled;
    const bool chat=may(g.member.id,multiplayer::role_chat) && !text_timed_out(g.member.id);
    out["stats"]=stats;out["shop"]=shop;out["paused"]=stats_failed_;
    out["can_pay"]=stats && !(text_timed_out(g.member.id) && timeout_blocks_command("pay"));
    out["can_tag"]=shop && chat;out["can_joinmsg"]=shop && chat && config_.shop.joinmsg_enabled;
    auto profile=Json::object();
    auto join_message=p.joinmsg;cut_text(join_message,256);
    profile["name"]=guest_name(g);profile["tag"]=p.equipped_tag;profile["colour"]=p.colour;profile["joinmsg"]=join_message;
    profile["balance"]=p.wallet;profile["unlimited"]=unlimited_points(g.member.id);
    profile["lifetime"]=p.points;profile["weekly"]=p.weekly_points;
    // Display counters need only change every 15 seconds; financial changes publish immediately on the next tick.
    profile["active_seconds"]=(p.active_us/15000000)*15;profile["daily_seconds"]=(p.daily_us/15000000)*15;
    profile["daily_target"]=std::uint64_t{settings.daily_minutes}*60;
    profile["daily_done"]=p.day>=0 && p.last_claim_day==p.day;
    profile["streak"]=p.last_claim_day>=p.day-1?p.streak:0;profile["visits"]=p.qualifying_days;profile["events"]=p.events;
    profile["joinmsg_owned"]=p.joinmsg_owned;profile["joinmsg_on"]=p.joinmsg_enabled;
    const auto target=std::uint64_t{settings.vip_hours}*3600000000ULL;
    profile["vip_seconds"]=p.active_us>=target?0:(target-p.active_us+999999)/1000000;
    const auto* role=role_of(g.member.id);
    profile["vip"]=unlimited_points(g.member.id) || can(g.member.id,"kick")?"Active (staff)":role && role->name==settings.vip_role?"Active (VIP role)":p.vip_awarded?"Earned":p.active_us>=target?"Milestone reached; awaiting eligible roles":"Progress toward VIP";
    out["profile"]=std::move(profile);
    auto products=Json::array();
    const auto add=[&](std::string id,std::string label,std::string value,std::uint64_t price,bool owned,bool equipped,bool enabled,std::uint32_t colour=0) {
        auto c=Json::object();c["id"]=id;c["label"]=label;c["value"]=value;c["price"]=price;c["owned"]=owned;c["equipped"]=equipped;c["enabled"]=enabled;
        c["colour"]=colour;c["reason"]=enabled?"":stats_failed_?"Player data is paused":!ready?"Finish loading the map":!config_.shop.enabled?"Shop disabled":"Unavailable under current server settings or permissions";
        c["action"]=equipped?"equipped":owned?"equip":id.starts_with("colour:")?"change":"buy";
        products.push_back(std::move(c));
    };
    add("tag","Custom tag","",config_.shop.tag_price,false,false,shop && chat);
    for(const auto& [name,value]:shop_colours) {
        (void)value; const bool equipped=p.colour==name;
        add("colour:"+std::string(name),std::string(name),std::string(name),p.colourbundle_owned?0:config_.shop.colour_price,p.colourbundle_owned,equipped,shop && !(text_timed_out(g.member.id)&&timeout_blocks_command("colour "+std::string(name))),shop_colour(name).value());
    }
    unsigned missing{};
    for(const auto& [name,colour]:shop_shimmers) {
        const bool owned=std::ranges::find(p.shimmers,std::string(name))!=p.shimmers.end(); if(!owned)++missing;
        add("shimmer:"+std::string(name),name=="shimmer-green"?"Shimmer cyan":std::string(name),std::string(name),owned?0:config_.shop.shimmer_price,owned,p.colour==name,shop && !(text_timed_out(g.member.id)&&timeout_blocks_command("colour "+std::string(name))),colour);
    }
    add("joinmsg","Custom join message","",p.joinmsg_owned?0:config_.shop.joinmsg_price,p.joinmsg_owned,false,shop && chat && config_.shop.joinmsg_enabled);
    add("colourbundle","All standard colours","",config_.shop.colourbundle_price,p.colourbundle_owned,p.colourbundle_owned,shop && config_.shop.colourbundle_enabled);
    add("shimmerbundle","All missing shimmers","",(std::uint64_t{config_.shop.shimmerbundle_price}*missing+4)/5,!missing,!missing,shop && config_.shop.shimmerbundle_enabled);
    out["products"]=std::move(products);
    const auto pages=std::max<std::uint64_t>(1,(p.tags.size()+msc::page_size-1)/msc::page_size);
    g.msc_collection_page=std::clamp(g.msc_collection_page,std::uint64_t{1},pages);
    out["collection_page"]=g.msc_collection_page;out["collection_pages"]=pages;
    auto tags=Json::array();
    for(auto i=(g.msc_collection_page-1)*msc::page_size;i<std::min<std::uint64_t>(p.tags.size(),g.msc_collection_page*msc::page_size);++i) {
        const auto& t=p.tags[static_cast<std::size_t>(i)];auto row=Json::object();row["text"]=t;
        row["enabled"]=shop && chat && tag_error(t).empty() && std::ranges::find(p.disabled_tags,t)==p.disabled_tags.end();row["equipped"]=t==p.equipped_tag;tags.push_back(std::move(row));
    }
    out["tags"]=std::move(tags);
    auto leaders=Json::array();std::uint64_t leader_pages=1;
    if(g.msc_leader_active && stats) {
        const auto ids=eligible_board(stats_,config_.roles,g.msc_leader_kind,static_cast<std::int64_t>(std::time(nullptr)));
        leader_pages=std::max<std::uint64_t>(1,(ids.size()+msc::page_size-1)/msc::page_size);
        g.msc_leader_page=std::clamp(g.msc_leader_page,std::uint64_t{1},leader_pages);
        for(auto i=(g.msc_leader_page-1)*msc::page_size;i<std::min<std::uint64_t>(ids.size(),g.msc_leader_page*msc::page_size);++i) {
            const auto id=ids[static_cast<std::size_t>(i)];const auto& player=stats_.players.at(id);auto row=Json::object();
            row["id"]=std::to_string(id);std::size_t rank=i;while(rank && stats_.value(stats_.players.at(ids[rank-1]),g.msc_leader_kind,static_cast<std::int64_t>(std::time(nullptr)))==stats_.value(player,g.msc_leader_kind,static_cast<std::int64_t>(std::time(nullptr))))--rank;row["rank"]=rank+1;auto name=player.name;cut_text(name,128);row["name"]=name;
            row["value"]=stats_.value(player,g.msc_leader_kind,static_cast<std::int64_t>(std::time(nullptr)));leaders.push_back(std::move(row));
        }
    }
    out["leader_kind"]=g.msc_leader_kind;out["leader_page"]=g.msc_leader_page;out["leader_pages"]=leader_pages;out["leaders"]=std::move(leaders);
    out["quote"]=nullptr;std::string key;
    const auto it=shop_quotes_.find(g.member.id);
    if(it!=shop_quotes_.end() && now_<it->second.expires && it->second.epoch==g.member.epoch && shop) {
        const auto& q=it->second;
        key=q.kind+":"+q.value+":"+std::to_string(q.price)+":"+std::to_string(q.expires)+":"+std::to_string(q.epoch);
        if(key!=g.msc_quote_key)g.msc_quote_token=std::to_string(++msc_quote_serial_);
        auto quote=Json::object();quote["token"]=g.msc_quote_token;quote["kind"]=q.kind;quote["value"]=q.value;quote["price"]=q.price;quote["seconds"]=(q.expires-now_+999999)/1000000;out["quote"]=std::move(quote);
    } else g.msc_quote_token.clear();
    g.msc_quote_key=std::move(key);
    out["fp_presence"]=msc_presence::supported(config_.name);
    out["games_supported"]=true;out["staff_available"]=is_staff(g.member.id);
    out["games"]=g.msc_panel=="games"?msc_games(g):Json::array();out["staff"]=g.msc_panel=="staff"?msc_staff(g):Json{};
    if(g.msc_panel!="shop"){out["products"]=Json::array();out["tags"]=Json::array();out["leaders"]=Json::array();}
    return out;
}
void Host::msc_send(Guest& g,std::uint64_t request,std::string message) {
    if(!g.msc_ui || !g.handshaken)return;
    auto data=msc_snapshot(g);g.msc_last_state=data.dump();
    data["revision"]=++g.msc_revision;data["request"]=request;data["message"]=message;
    auto p=packet(PacketKind::msc,now_);p.text=data.dump();
    if(p.text.size()>msc::max_payload) {
        // Escaped display text can grow well beyond its UTF-8 byte count. Keep
        // financial/identity fields intact and reduce only presentation strings.
        const auto shorten=[](Json& object,const char* key,std::size_t limit){auto value=object.at(key).get<std::string>();cut_text(value,limit);object[key]=value;};
        for(auto& game:data["games"])for(const auto* key:{"round","previous"})if(!game[key].is_null()) {
            auto& round=game[key];shorten(round,"result",128);for(auto& member:round["members"])shorten(member,"name",16);
        }
        if(!data["staff"].is_null()) {
            auto& staff=data["staff"];shorten(staff,"output",512);for(auto& player:staff["players"])shorten(player,"name",32);
            if(!staff["quote"].is_null())shorten(staff["quote"],"description",256);
        }
        p.text=data.dump();
        if(p.text.size()>msc::max_payload) {
            data["games"]=Json::array();data["staff"]=nullptr;data["products"]=Json::array();data["tags"]=Json::array();data["leaders"]=Json::array();
            data["message"]="MSC display data exceeds the packet limit. Use existing chat commands for this view.";p.text=data.dump();
        }
    }
    if(p.text.size()<=msc::max_payload)send_packet(g,p,true,false);
}
void Host::tick_msc() {
    for(auto& [id,ptr]:guests_) {
        (void)id;auto& g=*ptr;
        if(!g.msc_ui || !g.handshaken || now_>=g.msc_until || now_<g.msc_tick)continue;
        g.msc_tick=now_+1000000;
        if(msc_snapshot(g).dump()!=g.msc_last_state)msc_send(g);
    }
}
void Host::msc_request(Guest& g,std::string_view payload) {
    std::uint64_t request{};
    try {
        const auto j=msc::parse(payload);request=msc::integer(j,"request");
        if(!request || msc::identity(j,"epoch")!=g.member.epoch)return;
        if(const auto old=g.msc_results.find(request);old!=g.msc_results.end()) {
            // Reuse the result message, but always publish fresh state; never execute again.
            msc_send(g,request,old->second.first==payload?old->second.second:"Request identifier already used.");return;
        }
        if(request<=g.msc_highest){msc_send(g,request,"Request expired; refresh before trying again.");return;}
        const auto op=msc::text(j,"op",24);
        const bool reading=op=="snapshot" || op=="collection" || op=="leaderboard" || op=="close" || op=="games" || op=="staff" || op=="staff-read";
        if(!(reading?g.msc_reads:g.msc_mutations).accept(now_,reading?12:6)){msc_send(g,request,"Too many MSC requests. Wait a few seconds.");return;}
        g.msc_highest=request;g.msc_until=now_+30000000;
        std::string message;
        if(j.contains("section")) {const auto section=msc::text(j,"section",16);if(section!="shop" && section!="games" && section!="staff")throw std::runtime_error("Unknown MSC section.");g.msc_panel=section;}
        if(op=="games")g.msc_panel="games";
        else if(op=="staff"){if(!is_staff(g.member.id))throw std::runtime_error("Staff permissions are required.");g.msc_panel="staff";g.msc_staff_page=j.contains("page")?std::max<std::uint64_t>(1,msc::integer(j,"page")):g.msc_staff_page;}
        else if(op=="staff-read") {g.msc_panel="staff";message=msc_staff_action(g,j);}
        else if(op=="close")g.msc_until=0;
        else if(op=="snapshot") { account_stats();tick_stats(); }
        else if(op=="collection")g.msc_collection_page=std::max<std::uint64_t>(1,msc::integer(j,"page"));
        else if(op=="leaderboard") {
            const auto kind=msc::text(j,"kind",16);
            if(kind!="weekly" && kind!="points" && kind!="time" && kind!="events")throw std::runtime_error("Invalid leaderboard category.");
            g.msc_leader_active=true;g.msc_leader_kind=kind;g.msc_leader_page=std::max<std::uint64_t>(1,msc::integer(j,"page"));
        } else {
            g.msc_capture=true;g.msc_message.clear();
            try {
                if(op=="game") {g.msc_panel="games";msc_game_action(g,j);}
                else if(op=="staff-preview" || op=="staff-confirm" || op=="staff-cancel") {g.msc_panel="staff";reply(g,msc_staff_action(g,j));}
                else if(op=="confirm") {
                    (void)msc_snapshot(g);const auto token=msc::text(j,"token",96);
                    if(token.empty() || token!=g.msc_quote_token)reply(g,"Purchase quote changed or expired. Request a fresh quote.");
                    else shop_command(g,"buy","");
                } else if(op=="item") {
                    const auto kind=msc::text(j,"kind",32);const auto value=msc::text(j,"value",256);
                    if(value.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Use single-line text.");
                    if(kind!="tag" && kind!="colour" && kind!="joinmsg" && kind!="colourbundle" && kind!="shimmerbundle")throw std::runtime_error("Invalid MSC item.");
                    const auto argument=kind=="joinmsg"?"set "+value:kind=="colourbundle" || kind=="shimmerbundle"?std::string("buy"):value;
                    shop_command(g,kind,argument);
                } else if(op=="joinmsg-toggle") {
                    shop_command(g,"joinmsg",j.at("enabled").get<bool>()?"on":"off");
                } else if(op=="pay") {
                    const auto to=msc::identity(j,"recipient"),amount=msc::integer(j,"amount");
                    if(!amount)throw std::runtime_error("Enter a positive whole amount.");
                    if(text_timed_out(g.member.id) && timeout_blocks_command("pay"))reply(g,text_timeout_notice(g.member.id));
                    else stats_command(g,"pay",std::to_string(to)+" "+std::to_string(amount));
                } else throw std::runtime_error("Unknown MSC action.");
            } catch(...) {g.msc_capture=false;throw;}
            g.msc_capture=false;message=std::exchange(g.msc_message,{});
        }
        g.msc_results[request]={std::string(payload),message};
        while(g.msc_results.size()>32)g.msc_results.erase(g.msc_results.begin());
        msc_send(g,request,std::move(message));
    } catch(const std::exception& e) {
        g.msc_capture=false;
        if(request) {
            const std::string message=std::string("MSC request refused: ")+e.what();
            if(request>g.msc_highest)g.msc_highest=request;
            g.msc_results[request]={std::string(payload),message};
            while(g.msc_results.size()>32)g.msc_results.erase(g.msc_results.begin());
            msc_send(g,request,message);
        }
    }
}
} // namespace dingosdk::server
