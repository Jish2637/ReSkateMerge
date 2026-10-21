#include "server_host.h"
#include "server_text.h"
#include "shop.h"
#include "msc_presence.h"
#include "Engine/Core/Text/word_filter.h"
#include "Extension/Multiplayer/developer_identity.h"
#include <algorithm>

namespace dingosdk::server {
std::string Host::tag_error(std::string_view value) const {
    if(value.empty() || value.size()>12 || trim(value)!=value)return "Use 1-12 letters, numbers, spaces, hyphens or underscores.";
    for(unsigned char c:value)if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c==' ' || c=='-' || c=='_'))
        return "Use 1-12 letters, numbers, spaces, hyphens or underscores.";
    if(text::contains_bad_words(value))return "That tag is not allowed by the server word filter. No warning was issued.";
    auto words=lower(value);for(auto &c:words)if(c=='-' || c=='_')c=' ';
    for(auto remaining=std::string_view(words);!remaining.empty();) {
        const auto [word,tail]=split(remaining);remaining=tail;
        for(const auto reserved:{"admin","administrator","mod","moderator","staff","server","system","owner","developer","dev","reskate"})
            if(word==reserved)return "That tag is reserved.";
    }
    auto key=lower(value);std::erase_if(key,[](char c){return c==' '||c=='-'||c=='_';});
    if(key.empty())return "Please include a letter or number.";
    for(const auto reserved:{"admin","administrator","mod","moderator","staff","server","system","owner","developer","dev","reskate","reskateadmin","fp","off","default","homie","centrix","creator","contentcreator","support"})
        if(key==reserved)return "That tag is reserved.";
    for(const auto &role:config_.roles.list)for(auto label:{role.name,role.tag}) {
        label=lower(label);std::erase_if(label,[](char c){return c==' '||c=='-'||c=='_'||c=='['||c==']';});
        if(!label.empty() && key==label)return "That tag is reserved for a server role.";
    }
    return {};
}
std::string Host::display_name(const Guest &g) const {
    std::string name=g.member.name;
    if(config_.shop.enabled && config_.player_stats.enabled && !stats_failed_)
        if(const auto it=stats_.players.find(g.member.id);it!=stats_.players.end()) {
            const auto &p=it->second;
            if(!p.equipped_tag.empty() && tag_error(p.equipped_tag).empty() && std::ranges::find(p.disabled_tags,p.equipped_tag)==p.disabled_tags.end())
                name="["+p.equipped_tag+"] "+name;
        }
    cut_text(name,max_member_name);
    name=msc_presence::display_name(name,msc_presence::supported(config_.name) && g.msc_fp && now_<g.msc_fp_until,max_member_name);
    cut_text(name,max_member_name);return name;
}
void Host::audit_points(Json record) {
    record["event_id"]=std::to_string(epoch_)+"-"+std::to_string(++points_audit_sequence_);
    record["server"]=config_.name;record["map"]=map_name();
    std::string line;const bool written=points_audit_.append(std::move(record),line);
    log_("[points-audit] "+line);
    if(!written && !points_audit_failed_) {
        points_audit_failed_=true;
        log_("[warning] Cannot write ReSkateServer-points.jsonl; points audit records continue in the main log. Restart to retry the separate file.");
    }
}
bool Host::shop_commit(Guest &g,const PlayerStats &before,std::string_view action,unsigned listed_price,std::optional<std::uint64_t> actor) {
    const auto audit=[&](bool committed) {
        const auto &after=stats_.players.at(g.member.id);auto row=Json::object();
        row["event"]=action.starts_with("purchased ")?"purchase":"shop_change";
        row["status"]=committed?"committed":"rolled_back";row["action"]=std::string(action);
        const auto by=actor.value_or(g.member.id);const auto *staff=find(by);
        row["actor"]=points_identity(by,staff?guest_name(*staff):by?std::to_string(by):std::string("console"));row["player"]=points_identity(g.member.id,guest_name(g));
        row["listed_price"]=listed_price;row["charged"]=before.wallet>after.wallet?before.wallet-after.wallet:0;
        row["balance_before"]=before.wallet;row["balance_after"]=after.wallet;row["unlimited_admin"]=unlimited_points(g.member.id);
        row["tag_before"]=before.equipped_tag;row["tag_after"]=after.equipped_tag;row["colour_before"]=before.colour;row["colour_after"]=after.colour;
        if(action.starts_with("purchased ")) {
            const auto [kind,value]=split(action.substr(10));row["item_type"]=std::string(kind);row["item"]=std::string(value);
            row["permanent_collectible"]=kind=="tag" || kind=="joinmsg" || kind=="colourbundle" || kind=="shimmerbundle" || shop_shimmer(value);row["ownership_granted"]=committed && (kind=="tag" || kind=="joinmsg" || kind=="colourbundle" || kind=="shimmerbundle" || shop_shimmer(value));
        }
        if(!committed)row["reason"]="stats_save_failed";
        audit_points(std::move(row));
    };
    try {stats_.save();}
    catch(const std::exception &e) {
        stats_.players.at(g.member.id)=before;
        stats_failed_=true;roster_dirty_=true;
        log_(std::string("[shop] Save failed; transaction rolled back; rewards/shop paused: ")+e.what());
        audit(false);
        reply(g,"Could not save your purchase. Your balance and purchases have not changed. Shop paused until restart.");return false;
    }
    roster_dirty_=true;
    audit(true);
    // Publish colour changes now, without waiting for the next roster tick or a rejoin.
    if(before.colour!=stats_.players.at(g.member.id).colour)send_roster();
    log_("[shop] UTC "+std::to_string(std::time(nullptr))+" "+guest_name(g)+" ("+std::to_string(g.member.id)+") "+std::string(action)+"; cost "+std::to_string(before.wallet>stats_.players.at(g.member.id).wallet?before.wallet-stats_.players.at(g.member.id).wallet:0)+"; balance "+std::to_string(stats_.players.at(g.member.id).wallet));
    return true;
}
void Host::shop_command(Guest &g,std::string_view verb,std::string_view argument) {
    if(!config_.shop.enabled || !config_.player_stats.enabled)return reply(g,"MSC shop is disabled on this server.");
    if(stats_failed_)return reply(g,"MSC shop is paused because player data could not be saved or loaded. Contact staff.");
    if(!g.world_ready)return reply(g,"Finish loading the map before using the shop.");
    if(!stats_.players.contains(g.member.id))stats_joined(g);
    if(stats_failed_)return;
    auto &p=stats_.players.at(g.member.id);
    const auto rest=trim(argument);auto key=lower(rest);if(verb=="colour" && key=="shimmer-cyan")key="shimmer-green";
    if(text_timed_out(g.member.id) && timeout_blocks_command(std::string(verb)+" "+std::string(rest)))return reply(g,text_timeout_notice(g.member.id));
    if(!may(g.member.id,multiplayer::role_chat) && ((verb=="tag" && key!="off") || (verb=="buy" && shop_quotes_.contains(g.member.id) && shop_quotes_.at(g.member.id).kind=="tag")))
        return reply(g,"Your role cannot change custom tags. /tag off and /tags remain available.");
    if(expansion_shop(g,verb,rest))return;
    if(verb=="shop") {
        if(!rest.empty())return reply(g,"Usage: /shop");
        return reply(g,"MSC shop (/s) | /joinmsg (/jm) | /colourbundle (/cb) | /shimmerbundle (/sb)\nGames: /coinflip (/cf) | /dice (/dd) | /highlow (/hl)\nBalance: "+points_balance(g.member.id,p.wallet)+"\n/tag <text>: buy a permanent custom tag for "+std::to_string(config_.shop.tag_price)+" points; owned tags equip free. /tags lists your collection.\n/colour <name>: "+std::to_string(config_.shop.colour_price)+" points per change; available to everyone.\nColours: purple, cyan, gold, pink, green, orange, red, white.\nShimmers: "+std::to_string(config_.shop.shimmer_price)+" points each to own permanently; free switching. /shimmers lists them.\n/tag off | /colour default: free. /c or /color = /colour. /buy or /confirm: confirm a preview.\nEquipped tag: "+(p.equipped_tag.empty()?std::string("off"):p.equipped_tag)+" | Colour: "+(p.colour.empty()?std::string("default"):p.colour));
    }
    if(verb=="shimmers") {
        if(!rest.empty())return reply(g,"Usage: /shimmers");
        std::string out="Your shimmer collection | Balance: "+points_balance(g.member.id,p.wallet);
        for(const auto &[name,colour]:shop_shimmers) {
            const bool owned=std::ranges::find(p.shimmers,std::string(name))!=p.shimmers.end();
            out+="\n/colour "+std::string(name=="shimmer-green"?"shimmer-cyan":name)+(owned?(p.colour==name?" - owned, equipped":" - owned, equip free"):" - "+std::to_string(config_.shop.shimmer_price)+" points to own");
        }
        return reply(g,out+"\nPurchased shimmers are permanent; switching and /colour default never remove ownership.");
    }
    if(verb=="tags") {
        const auto page=rest.empty()?std::optional<std::uint64_t>{1}:number(rest);
        const auto pages=std::max<std::size_t>(1,(p.tags.size()+5)/6);
        if(!page || !*page || *page>pages)return reply(g,"Usage: /tags [page 1-"+std::to_string(pages)+"]");
        std::string out="Your permanent tags | page "+std::to_string(*page)+"/"+std::to_string(pages);
        for(auto i=(*page-1)*6;i<std::min<std::size_t>(*page*6,p.tags.size());++i) {
            const auto &tag=p.tags[i];const bool disabled=std::ranges::find(p.disabled_tags,tag)!=p.disabled_tags.end() || !tag_error(tag).empty();
            out+="\n"+tag+(disabled?" (unavailable)":tag==p.equipped_tag?" (equipped)":" - /tag "+tag+" (free)");
        }
        if(p.tags.empty())out+="\nNo tags yet. /tag <text> previews your first purchase.";
        if(*page<pages)out+="\nNext: /tags "+std::to_string(*page+1);
        return reply(g,out);
    }
    if(verb=="buy") {
        if(!rest.empty())return reply(g,"Usage: /buy");
        const auto it=shop_quotes_.find(g.member.id);
        if(it==shop_quotes_.end())return reply(g,"No pending purchase. Use /tag <text> or /colour <name> first.");
        const auto quote=it->second;shop_quotes_.erase(it);
        if(now_>=quote.expires || quote.epoch!=g.member.epoch)return reply(g,"Purchase preview expired. Please preview it again.");
        const auto price=quote.kind=="tag"?config_.shop.tag_price:shop_shimmer(quote.value)?config_.shop.shimmer_price:p.colourbundle_owned?0:config_.shop.colour_price;
        if(price!=quote.price)return reply(g,"The price has changed. Please preview your purchase again.");
        if(quote.kind=="tag") {
            if(const auto error=tag_error(quote.value);!error.empty())return reply(g,error);
            if(std::ranges::any_of(p.tags,[&](const auto &t){return lower(t)==lower(quote.value);}))return reply(g,"You already own that tag. Equip it for free with /tag "+quote.value+".");
        } else {
            if(shop_shimmer(quote.value) && std::ranges::find(p.shimmers,quote.value)!=p.shimmers.end())
                return reply(g,"You already own that shimmer. Equip it free with /colour "+quote.value+".");
            if(p.colour==quote.value)return reply(g,"That colour is already equipped. No points spent.");
        }
        if(!unlimited_points(g.member.id) && p.wallet<price)return reply(g,"Not enough MSC points. Balance: "+std::to_string(p.wallet)+"; price: "+std::to_string(price)+".");
        const auto before=p;if(!unlimited_points(g.member.id))p.wallet-=price;
        if(quote.kind=="tag"){p.tags.push_back(quote.value);p.equipped_tag=quote.value;}
        else {p.colour=quote.value;if(shop_shimmer(quote.value))p.shimmers.push_back(quote.value);}
        if(shop_commit(g,before,"purchased "+quote.kind+" "+quote.value,price))reply(g,"Purchased "+quote.kind+" "+quote.value+" for "+std::to_string(price)+" points. Balance: "+points_balance(g.member.id,p.wallet)+(quote.kind=="tag" || shop_shimmer(quote.value)?". Yours permanently; switching is free.":"."));
        return;
    }
    // A new draft, equip or reset cancels the previous purchase preview.
    shop_quotes_.erase(g.member.id);
    if(verb=="tag") {
        if(key=="off") {
            if(p.equipped_tag.empty())return reply(g,"Your tag is already off. Your collection is unchanged.");
            const auto before=p;p.equipped_tag.clear();
            if(shop_commit(g,before,"tag off"))reply(g,"Tag off. All purchased tags remain in /tags; equip them again for free.");return;
        }
        if(const auto error=tag_error(rest);!error.empty())return reply(g,error);
        const auto owned=std::ranges::find_if(p.tags,[&](const auto &t){return lower(t)==key;});
        if(owned!=p.tags.end()) {
            if(std::ranges::find(p.disabled_tags,*owned)!=p.disabled_tags.end())return reply(g,"Staff disabled that tag. It remains owned but cannot be equipped or bought again.");
            if(p.equipped_tag==*owned)return reply(g,"That tag is already equipped. No points spent.");
            const auto before=p;p.equipped_tag=*owned;
            if(shop_commit(g,before,"equipped owned tag "+p.equipped_tag))reply(g,"Equipped ["+p.equipped_tag+"] for free. Balance: "+points_balance(g.member.id,p.wallet)+".");return;
        }
    } else if(verb=="colour") {
        if(key=="default") {
            const auto before=p;p.colour.clear();
            if(shop_commit(g,before,"colour default"))reply(g,"Role default colour restored for free.");return;
        }
        if(!shop_colour(key))return reply(g,"Choose purple, cyan, gold, pink, green, orange, red or white; /shimmers lists permanent animated colours. /colour default is free.");
        if(p.colour==key)return reply(g,"That colour is already equipped. No points spent.");
        if((!shop_shimmer(key) && p.colourbundle_owned) || (shop_shimmer(key) && std::ranges::find(p.shimmers,key)!=p.shimmers.end())) {
            const auto before=p;p.colour=key;
            if(shop_commit(g,before,(shop_shimmer(key)?"equipped owned shimmer ":"equipped bundle colour ")+key))reply(g,"Equipped "+key+" for free. Balance: "+points_balance(g.member.id,p.wallet)+".");
            return;
        }
    } else return;
    const unsigned price=verb=="tag"?config_.shop.tag_price:shop_shimmer(key)?config_.shop.shimmer_price:config_.shop.colour_price;
    if(!unlimited_points(g.member.id) && p.wallet<price)return reply(g,"Not enough MSC points. Balance: "+std::to_string(p.wallet)+"; price: "+std::to_string(price)+".");
    shop_quotes_[g.member.id]={verb=="tag"?"tag":"colour",std::string(verb=="tag"?rest:std::string_view(key)),price,now_+120000000,g.member.epoch};
    auto preview=g.member.name;
    if(verb=="tag") {preview="["+std::string(rest)+"] "+preview;cut_text(preview,max_member_name);}
    else preview=display_name(g);
    if(verb=="tag")preview=msc_presence::display_name(preview,msc_presence::supported(config_.name)&&g.msc_fp&&now_<g.msc_fp_until,max_member_name);
    const auto *role=role_of(g.member.id);
    if(role && !role->tag.empty())preview="["+role->tag+"] "+preview;
    if(verb=="colour")send_roster(&g,shop_colour(key));
    reply(g,verb=="colour" ? "Preview "+key+": Message text stays white." : "Preview "+std::string(rest)+": "+preview);
    if(verb=="colour")send_roster(&g); // restore BEFORE sending cost/confirmation lines
    reply(g,"Cost: "+std::to_string(price)+" points | Balance after: "+(unlimited_points(g.member.id)?std::string("Unlimited points"):std::to_string(p.wallet-price)+" points")+". /buy within 2 minutes to confirm."+(verb=="colour"?"\nName and badge use this colour; chat message text stays white. Your role and permissions stay unchanged.":"\nPermanent tag: keep it in /tags and switch for free."));
    if(verb=="colour" && shop_shimmer(key))reply(g,"Permanent collectible shimmer: switching to owned shimmers is free, including after /colour default.");
    if(verb=="colour" && multiplayer::identity_mark(g.member.id))reply(g,"Your special ReSkate identity badge can take precedence over this colour on ordinary clients.");
}
std::string Host::clear_tag(std::string_view name,std::uint64_t actor) {
    if(stats_failed_)return "Shop data is unavailable; resolve the save/load problem and restart.";
    auto *g=match_player(name);if(!g)return "Usage: /cleartag <player name or SteamID64>; choose one connected player.";
    if(actor && rank(actor)<=rank(g->member.id))return "You may only moderate players below your rank.";
    auto it=stats_.players.find(g->member.id);if(it==stats_.players.end() || it->second.equipped_tag.empty())return "That player has no equipped custom tag.";
    auto &p=it->second;const auto before=p;const auto tag=p.equipped_tag;
    if(std::ranges::find(p.disabled_tags,tag)==p.disabled_tags.end())p.disabled_tags.push_back(tag);
    p.equipped_tag.clear();shop_quotes_.erase(g->member.id);
    if(!shop_commit(*g,before,"tag disabled by "+std::to_string(actor)+": "+tag,0,actor))return "Could not save tag removal; no change was made.";
    reply(*g,"A moderator has removed and disabled your custom tag. It remains in your collection but cannot be equipped.");
    return "Removed and disabled ["+tag+"] for "+guest_name(*g)+".";
}
void Host::tick_shop() {
    std::erase_if(shop_quotes_,[&](const auto &q){const auto *g=find(q.first);return !g || !g->handshaken || g->member.epoch!=q.second.epoch || now_>=q.second.expires;});
    if(now_<shop_check_at_)return;
    shop_check_at_=now_+1000000;
    // Filter and role lists can change outside the host; outgoing rosters always revalidate.
    // Refresh once per second so peers also lose a newly prohibited decoration.
    for(const auto &[id,g]:guests_)if(g->handshaken) {
        const auto it=stats_.players.find(id);
        if(it==stats_.players.end() || it->second.equipped_tag.empty())continue;
        const auto &p=it->second;const bool invalid=!tag_error(p.equipped_tag).empty() || std::ranges::find(p.disabled_tags,p.equipped_tag)!=p.disabled_tags.end();
        if(invalid && !g->shop_tag_hidden)reply(*g,"Your custom tag is currently unavailable. It is still owned in /tags.");
        if(g->shop_tag_hidden!=invalid){g->shop_tag_hidden=invalid;roster_dirty_=true;}
    }
}
} // namespace dingosdk::server
