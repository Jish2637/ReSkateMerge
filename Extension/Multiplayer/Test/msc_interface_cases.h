#pragma once
#include "Engine/Game/Multiplayer/msc_interface.h"
#include "Engine/Game/Multiplayer/msc_actions.h"
#include "Engine/Game/Multiplayer/msc_staff.h"
#include <set>
inline void msc_interface_cases() {
    using dingosdk::Json;
    namespace msc=dingosdk::msc;
    auto data=Json::object();data["v"]=1U;data["revision"]=1U;data["request"]=0U;
    data["epoch"]="99";data["player"]="76561198000000001";data["shop"]=true;data["stats"]=true;data["paused"]=false;data["can_pay"]=true;data["can_tag"]=true;data["can_joinmsg"]=true;data["message"]="";
    auto p=Json::object();for(const auto* k:{"name","tag","colour","joinmsg","vip"})p[k]="";
    for(const auto* k:{"balance","lifetime","weekly","active_seconds","daily_seconds","daily_target","streak","visits","vip_seconds"})p[k]=0U;
    for(const auto* k:{"unlimited","daily_done","joinmsg_owned","joinmsg_on"})p[k]=false;
    p["events"]=Json::array({0U,0U,0U});data["profile"]=p;
    data["products"]=Json::array();data["tags"]=Json::array();data["leaders"]=Json::array();data["quote"]=nullptr;
    data["collection_page"]=1U;data["collection_pages"]=1U;data["leader_page"]=1U;data["leader_pages"]=1U;data["leader_kind"]="weekly";
    for(const auto* id:{"tag","joinmsg","colourbundle","shimmerbundle","colour:purple","colour:cyan","colour:gold","colour:pink","colour:green","colour:orange","colour:red","colour:white","shimmer:shimmer-purple","shimmer:shimmer-red","shimmer:shimmer-gold","shimmer:shimmer-blue","shimmer:shimmer-green"}) {
        msc::Product c;c.id=id;c.value=c.id.substr(c.id.find(':')==std::string::npos?0:c.id.find(':')+1);c.enabled=true;c.price=250;c.action="buy";
        const auto action=msc::product_action(c,"Fresh","Welcome");check(action.has_value(),"MSC product has no action mapping");
        check(action->at("op").string()=="item","MSC product routed outside item actions");
        if(c.id=="tag")check(action->at("value").string()=="Fresh","MSC tag draft mapping failed");
        if(c.id=="joinmsg")check(action->at("value").string()=="Welcome","MSC join message draft mapping failed");
        if(c.id.find(':')!=std::string::npos)check(action->at("kind").string()=="colour" && action->at("value").string()==c.value,"MSC cosmetic action mapping failed");
        check(msc::product_label(c)=="Buy - 250 MSC","MSC buy label failed");
        c.owned=true;check(msc::product_label(c)==(c.id=="joinmsg"?"Save message":"Equip"),"MSC ownership action label failed");
        c.equipped=true;check(msc::product_label(c)=="Equipped","MSC equipped action failed");c.equipped=false;c.enabled=false;check(msc::product_label(c)=="Unavailable","MSC restricted action label failed");
    }
    msc::Product paid;paid.enabled=true;paid.action="change";paid.price=50;check(msc::product_label(paid)=="Change colour - 50 MSC","MSC paid colour action failed");
    msc::Product unknown;unknown.id="unrecognized";check(!msc::product_action(unknown,"",""),"MSC unknown product gained an action");
    std::set<std::string> action_ids;
    for(const auto& action:msc::staff_actions()) {
        check(action_ids.insert(action.id).second,"Duplicate MSC staff action");
        check(action.fields.size()<=6 && !action.prefix.empty(),"Invalid MSC staff form");
        check(action.prefix!="quit" && action.prefix!="update" && action.prefix!="poll-run","Console-only command exposed");
        check(msc::staff_action(action.id)!=nullptr && !msc::staff_permission(action).empty(),"Missing staff permission mapping");
    }
    check(action_ids.size()>=130,"Staff tools unexpectedly missing");
    check(!msc::staff_action("unknown"),"Unknown staff action accepted");
    msc::GameView game;game.actions={"join"};check(msc::game_allows(game,"join") && !msc::game_allows(game,"start"),"Invalid game action state");
    check(!msc::snapshot(data).games_supported && !msc::snapshot(data).staff,"Legacy snapshot lost compatibility");
    auto oversized=data;oversized["games"]=Json::array();for(int i=0;i<9;++i)oversized["games"].push_back(Json::object());
    bool invalid_games=false;try{(void)msc::snapshot(oversized);}catch(...){invalid_games=true;}check(invalid_games,"Unbounded game catalogue accepted");
    const auto payload=data.dump();
    auto snap=msc::snapshot(msc::parse(payload));check(snap.epoch==99 && snap.products.empty(),"MSC typed snapshot failed");
    check(msc::accepts(snap,99,0) && !msc::accepts(snap,98,0) && !msc::accepts(snap,99,1),"MSC stale/session guard failed");
    msc::ClientState state;state.observe({true,42,99,""},10);state.attempted=true;state.opened=10;
    check(!state.fallback(14) && state.fallback(15),"MSC legacy fallback timing failed");
    state.observe({true,42,99,payload},16);check(state.data.has_value() && !state.stale(17),"MSC negotiation failed");
    state.pending=7;state.sent=16;state.timeout(25);check(!state.pending && state.stale(25),"MSC timeout did not disable mutations");
    state.observe({false,0,0,""},26);check(!state.data && !state.pending && !state.attempted,"MSC disconnect did not clear state");
    state.observe({true,42,100,payload},27);check(!state.data,"MSC response crossed client sessions");
    state.observe({true,42,99,payload},28);check(state.data.has_value(),"MSC reconnect failed");
    data["revision"]=2U;data["request"]=8U;state.pending=8;state.observe({true,42,99,data.dump()},29);check(!state.pending && state.data->revision==2,"MSC acknowledgement did not clear pending request");
    state.observe({true,42,99,payload},30);check(state.data->revision==2,"MSC stale revision replaced fresh state");
    bool refused=false;try {auto j=data;j["v"]=2U;(void)msc::parse(j.dump());}catch(...){refused=true;}check(refused,"MSC unknown schema accepted");
    refused=false;try {auto j=data;j["profile"]["balance"]=-1;(void)msc::snapshot(j);}catch(...){refused=true;}check(refused,"MSC negative balance accepted");
    refused=false;try {(void)msc::parse(std::string(msc::max_payload+1,'x'));}catch(...){refused=true;}check(refused,"MSC oversized JSON accepted");
    auto packet=frame(1,0);packet.kind=PacketKind::msc;packet.source=76561198000000001ULL;packet.world=1;packet.text=payload;
    const auto bytes=encode(packet);const auto decoded=decode(bytes);check(decoded && decoded->kind==PacketKind::msc && decoded->text==payload,"MSC packet roundtrip failed");
    auto truncated=bytes;truncated.pop_back();check(!decode(truncated),"MSC truncated packet accepted");
    packet.text=std::string(msc::max_payload+1,'x');refused=false;try{(void)encode(packet);}catch(...){refused=true;}check(refused,"MSC oversized packet accepted");
    packet.text=std::string("a\0b",3);refused=false;try{(void)encode(packet);}catch(...){refused=true;}check(refused,"MSC NUL payload accepted");
}
