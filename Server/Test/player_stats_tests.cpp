#include "Server/leaderboard.h"
#include "Server/player_stats.h"
#include "Server/wager_rules.h"
#include "Server/slot_rules.h"
#include <cstdio>
#include <fstream>
#include <chrono>

using namespace dingosdk;
using namespace dingosdk::server;
using M=multiplayer::ThrowdownMessage;
int main() {
    unsigned failed=0,notices=0;
    auto check=[&](bool yes,const char *what){if(!yes){++failed;std::fprintf(stderr,"FAIL: %s\n",what);}};
    const auto folder=std::filesystem::temp_directory_path()/ ("msc-stats-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file=folder/"player-stats.json";
    PlayerStatsStore store;store.open(file);StatsSettings s;
    auto notice=[&](std::uint64_t,const std::string &){++notices;};
    const std::int64_t monday=2075846400; // UTC calendar boundaries are also checked independently below.
    check(PlayerStatsStore::week_at(4*86400)==1 && PlayerStatsStore::week_at(3*86400)==0,"Monday UTC week anchor");
    store.joined(1,"One",1000000,monday);
    store.advance(1,300999999,monday+299,s,notice);
    check(store.players[1].points==0 && notices==0,"Daily reward does not arrive before five active minutes");
    store.advance(1,301000000,monday+300,s,notice);
    check(store.players[1].active_us==300000000 && notices==1 && store.players[1].points==30,"Daily reward arrives at exactly five active minutes");
    store.advance(1,601000000,monday+600,s,notice);
    check(store.players[1].active_us==300000000,"Idle time stops at five minutes");
    store.active(1,601000000);store.advance(1,901000000,monday+900,s,notice);
    check(store.players[1].active_us==600000000 && store.players[1].points==31 && notices==1,"Further playtime adds hourly points without a second daily bonus");
    store.save();PlayerStatsStore restarted;restarted.open(file);
    check(restarted.players[1].points==31 && restarted.players[1].point_remainder==store.players[1].point_remainder,"Persistent totals and fractional points");
    restarted.joined(1,"Renamed",1,monday+1000);restarted.active(1,1);
    restarted.advance(1,300000001,monday+1300,s,notice);
    check(notices==1 && restarted.players[1].name=="Renamed","Reconnect does not reclaim daily bonus or lose identity");
    auto &p=restarted.players[1];
    const auto before=p.points;
    restarted.joined(1,"Renamed",1,monday+86400);
    for(unsigned i=1;i<=2;++i){restarted.active(1,(i-1)*300000000ULL+1);restarted.advance(1,i*300000000ULL+1,monday+86400+i*300,s,notice);}
    check(p.streak==2 && p.points>=before+35,"Second qualifying day streak increases bonus");
    restarted.joined(1,"Renamed",1,monday+3*86400);
    for(unsigned i=1;i<=2;++i){restarted.active(1,(i-1)*300000000ULL+1);restarted.advance(1,i*300000000ULL+1,monday+3*86400+i*300,s,notice);}
    check(p.streak==1,"Missed qualifying day resets streak");
    PlayerStatsStore cap;cap.open({});StatsSettings cap_settings=s;cap_settings.daily_minutes=1;cap_settings.hourly_points=0;
    cap.joined(1,"One",1,100*86400);cap.players[1].last_claim_day=99;cap.players[1].streak=99;
    cap.advance(1,60000000,100*86400+59,cap_settings,{});
    check(cap.players[1].points==0,"Daily bonus waits for exact active threshold");
    cap.advance(1,60000001,100*86400+60,cap_settings,{});
    check(cap.players[1].points==75 && cap.players[1].streak==100,"Long streak bonus caps at 50 extra points at exact threshold");
    // Splitting at midnight: five seconds each side, with no daylight-saving adjustments.
    PlayerStatsStore boundary;boundary.open({});boundary.joined(1,"One",1000000,86400-5);
    boundary.advance(1,11000000,86400+5,s,{});
    check(boundary.players[1].active_us==10000000 && boundary.players[1].daily_us==5000000,"UTC midnight interval split");
    auto weekly=boundary.players[1];weekly.weekly_points=123;
    PlayerStatsStore::calendar(weekly,4*86400);check(weekly.weekly_points==0,"Monday weekly reset");
    weekly.weekly_points=77;PlayerStatsStore::calendar(weekly,3*86400);check(weekly.weekly_points==77,"Backward wall clock does not reopen week");
    PlayerStatsStore rollover;rollover.open({});StatsSettings rate=s;rate.hourly_points=3600;
    rollover.joined(1,"One",1,4*86400-5);rollover.advance(1,10000001,4*86400+5,rate,{});
    check(rollover.players[1].points==10 && rollover.players[1].weekly_points==5,"Playtime points split across Monday 00:00 UTC");
    // Every supported mode, replay resistance, solo exclusion, activity and daily cap.
    store.joined(2,"Two",1,monday);store.joined(1,"One",1,monday);
    const auto initial=store.players[1].points;
    for(unsigned event=0;event<9;++event){
        const unsigned mode=event%3;
        M m;m.leader=1;m.id=event;m.kind=M::Kind::offer;m.series=mode==0?"ThrowdownSkate":mode==1?"SpotBattle":"JamSession";
        store.throwdown(1,m,1,monday,s,notice);
        m.kind=M::Kind::start;m.order={1,2};store.throwdown(1,m,1,monday,s,notice);
        m.kind=mode==0?M::Kind::attempt:mode==1?M::Kind::turn_end:M::Kind::score;m.value=100;
        store.throwdown(1,m,2,monday,s,notice);store.throwdown(1,m,2,monday,s,notice);
        m.kind=M::Kind::start;store.throwdown(1,m,3,monday,s,notice);
    }
    check(store.players[1].events==std::array<std::uint64_t,3>{3,3,3},"All modes count once despite duplicate packets/start");
    check(store.players[1].points==initial+60 && store.players[1].daily_events==6,"Participation rewards capped at six per UTC day");
    M solo;solo.leader=1;solo.id=50;solo.kind=M::Kind::offer;solo.series="JamSession";store.throwdown(1,solo,3,monday,s,{});
    solo.kind=M::Kind::start;solo.order={1,1};store.throwdown(1,solo,3,monday,s,{});
    solo.kind=M::Kind::score;store.throwdown(1,solo,3,monday,s,{});
    check(store.players[1].events[2]==3,"Duplicate IDs do not qualify as two players");
    solo.id=51;store.throwdown(1,solo,3,monday,s,{});check(store.players[1].events[2]==3,"Events with missing start are ignored");
    store.left(1);solo.id=0;store.throwdown(1,solo,4,monday,s,{});check(store.players[1].events[2]==3,"Disconnected players cannot earn");
    store.world_changed();store.advance(2,999999999,monday+1000,s,{});check(store.players[2].active_us==0,"Map change pauses until readiness");
    check(store.board("weekly",monday+7*86400).empty(),"Previous week excluded without resetting offline records");
    store.joined(1,"One",1,monday+86400);store.joined(2,"Two",1,monday+86400);
    const auto next_day_points=store.players[1].points;
    solo.id=60;solo.kind=M::Kind::offer;store.throwdown(1,solo,1,monday+86400,s,{});
    solo.kind=M::Kind::start;solo.order={1,2};store.throwdown(1,solo,1,monday+86400,s,{});
    solo.kind=M::Kind::score;solo.value=100;store.throwdown(1,solo,1,monday+86400,s,{});
    check(store.players[1].daily_events==1 && store.players[1].points==next_day_points+10,"UTC day rollover resets event reward cap");
    store.players[1].vip_pending=true;store.save();restarted.open(file);check(restarted.players[1].vip_pending,"Pending role grant survives restart");
    // v1 earned totals become spendable exactly once; v2 wallet/collection survive reload.
    {
        std::ifstream in(file);auto legacy=Json::parse(std::string(std::istreambuf_iterator<char>(in),{}));in.close();
        legacy["version"]=1;
        for(auto &row:legacy["players"])for(const auto key:{"wallet","tags","disabled_tags","equipped_tag","colour","shimmers"})row.erase(key);
        {std::ofstream out(file);out<<legacy.dump();}
        PlayerStatsStore migrated;migrated.open(file);
        check(migrated.players.at(1).wallet==migrated.players.at(1).points,"Legacy earned points did not migrate to wallet");
        auto &profile=migrated.players.at(1);const auto earned=profile.points;profile.wallet=7;
        profile.tags={"NightOwl","Moon"};profile.disabled_tags={"NightOwl"};profile.equipped_tag="Moon";profile.colour="shimmer-purple";profile.shimmers={"shimmer-purple","shimmer-blue"};
        migrated.save();PlayerStatsStore again;again.open(file);
        check(again.players.at(1).wallet==7 && again.players.at(1).points==earned && again.players.at(1).tags==profile.tags &&
              again.players.at(1).disabled_tags==profile.disabled_tags && again.players.at(1).equipped_tag=="Moon" && again.players.at(1).colour=="shimmer-purple" && again.players.at(1).shimmers==profile.shimmers,
              "Shop reload recredits wallet or loses cosmetics");
        {
            std::ifstream old_file(file);auto old=Json::parse(std::string(std::istreambuf_iterator<char>(old_file),{}));old_file.close();
            for(auto &row:old["players"]){row.erase("shimmers");row["colour"]="gold";}
            {std::ofstream out(file);out<<old.dump();}
            PlayerStatsStore old_shop;old_shop.open(file);check(old_shop.players.at(1).shimmers.empty() && old_shop.players.at(1).wallet==7,"Older shop profile without shimmer field lost wallet or gained purchases");
            again.save();
        }
        again.joined(1,"One",1,monday+7*86400);again.active(1,3600000001);StatsSettings earn;earn.idle_seconds=10000;earn.daily_minutes=10000;
        again.advance(1,3600000001,monday+7*86400+3600,earn,{});
        check(again.players.at(1).wallet==17 && again.players.at(1).points==earned+10,"New earnings do not credit both wallet and lifetime totals");
        again.save();std::ifstream f(file);auto invalid=Json::parse(std::string(std::istreambuf_iterator<char>(f),{}));f.close();
        const auto valid_profile=invalid;
        for(const auto &owned:{Json::array(),Json::array({"shimmer-blue"}),Json::array({"shimmer-purple","shimmer-purple"}),Json::array({"shimmer-purple","shimmer-unknown"})}) {
            auto bad=valid_profile;bad["players"][0]["shimmers"]=owned;
            {std::ofstream out(file);out<<bad.dump();}
            bool rejected=false;try{PlayerStatsStore broken;broken.open(file);}catch(...){rejected=true;}
            check(rejected,"Invalid collection/unowned equipped shimmer accepted");
        }
        invalid["players"][0]["tags"]=Json::array({"Moon","moon"});{std::ofstream out(file);out<<invalid.dump();}
        bool rejected=false;try{PlayerStatsStore bad;bad.open(file);}catch(...){rejected=true;}
        check(rejected,"Duplicate collectible tag accepted in saved data");store.save();
    }
    // Gifts may fill a wallet to its persistent limit; subsequent earnings must stay reloadable.
    {
        PlayerStatsStore capped;capped.open(folder/"capped.json");capped.joined(99,"Capped",1,monday);
        capped.players.at(99).wallet=max_stats_counter;StatsSettings earn;earn.idle_seconds=10000;earn.daily_minutes=10000;
        capped.active(99,3600000001);capped.advance(99,3600000001,monday+3600,earn,{});capped.save();
        PlayerStatsStore loaded;loaded.open(folder/"capped.json");
        check(loaded.players.at(99).wallet==max_stats_counter && loaded.players.at(99).points==10,"Full wallet overflows on normal earnings");
    }
    // Background snapshots are owned copies, ordered with durable transaction barriers.
    {
        const auto ordered_file=folder/"ordered.json";auto writer=std::make_shared<SerialTasks>();
        PlayerStatsStore ordered(writer);ordered.open(ordered_file);ordered.joined(99,"Ordered",1,monday);ordered.players.at(99).wallet=50;ordered.save();
        std::promise<void> release,started;auto gate=release.get_future().share();
        auto held=writer->submit([&]{started.set_value();gate.wait();});started.get_future().wait();
        check(ordered.save_async(),"Autosave was not scheduled");
        check(!ordered.save_async() && !ordered.poll_save(),"Busy autosave queue grew or completed before disk barrier");
        ordered.players.at(99).wallet=77;
        auto transaction=std::async(std::launch::async,[&]{ordered.save();});
        check(transaction.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,"Transaction acknowledged ahead of older save");
        release.set_value();transaction.get();held.get();
        PlayerStatsStore result;result.open(ordered_file);
        check(result.players.at(99).wallet==77,"Older autosave overwrote newer wallet");
        check(ordered.poll_save(),"Transaction barrier lost autosave completion");
        std::filesystem::create_directory(ordered_file.string()+".tmp");
        ordered.players.at(99).wallet=88;check(ordered.save_async(),"Failing autosave did not schedule");writer->submit([]{}).get();
        bool rejected=false;try{ordered.poll_save();}catch(...){rejected=true;}
        result.open(ordered_file);check(rejected && result.players.at(99).wallet==77,"Background failure lost prior wallet or was hidden");
        std::filesystem::remove(ordered_file.string()+".tmp");
    }
    // Enumerate every second number and every rounding residue. Ties
    // forfeit the stake; only the winning outcomes pay out.
    for(unsigned starting=1;starting<=13;++starting)for(bool higher:{false,true}) {
        const auto wins=higher?13-starting:starting-1;
        if(!wins){bool rejected=false;try{highlow_payout(10,starting,1,higher,[](auto){return 0;});}catch(...){rejected=true;}check(rejected,"Impossible prediction accepted");continue;}
        for(std::uint64_t stake:{10ULL,11ULL,499ULL,500ULL}) {
            std::uint64_t sum=0;
            for(unsigned second=1;second<=13;++second)for(unsigned residue=0;residue<wins;++residue)
                sum+=highlow_payout(stake,starting,second,higher,[&](auto){return residue;});
            check(sum==12*stake*wins,"High Low return differs from winning-only payout");
            check(highlow_payout(stake,starting,starting,higher,[](auto){return 0;})==0,"High Low tie refunded stake");
        }
    }
    {std::vector<unsigned> rolls={6,6,1,2,5};unsigned at=0,roll_notices=0;
     const auto winner=dice_winner(std::vector<std::uint64_t>{1,2,3},[&]{return rolls.at(at++);},[&](auto,auto,auto){++roll_notices;});
     check(winner==2 && roll_notices==5,"Dice tie did not reroll only highest contenders");
     bool bounded=false;try{dice_winner(std::vector<std::uint64_t>{1,2},[]{return 6U;},[](auto,auto,auto){});}catch(...){bounded=true;}check(bounded,"Unending dice tie blocks forever");}
    {PlayerStatsStore escrow;const auto path=folder/"escrow.json";escrow.open(path);escrow.joined(1,"One",1,monday);escrow.joined(2,"Two",1,monday);
     escrow.players.at(1).wallet=90;escrow.players.at(2).wallet=90;
     Wager w;w.id=1;w.creator=1;w.game="coinflip";w.capacity=16;w.stake=10;w.expires=monday+300;w.members={1,2};
     escrow.wagers.push_back(w);escrow.next_wager_id=2;escrow.rebuild_headroom();escrow.save();
     PlayerStatsStore recovered;recovered.open(path);check(recovered.players.at(1).wager_headroom==160,"Escrow headroom not restored");
     recovered.refund_pending();recovered.save();PlayerStatsStore twice;twice.open(path);twice.refund_pending();twice.save();
     check(twice.players.at(1).wallet==100 && twice.players.at(2).wallet==100 && twice.wagers.at(0).state=="refunded","Restart refunds are not idempotent");}
    // Every settled Slots outcome survives restart without a second payout/refund.
    {PlayerStatsStore slots;const auto path=folder/"slots.json";slots.open(path);slots.joined(1,"Spinner",1,monday);
     std::uint64_t balance=20000;
     for(unsigned a=0;a<5;++a)for(unsigned b=0;b<5;++b)for(unsigned c=0;c<5;++c){
         const auto multiplier=slot_multiplier({a,b,c});Wager w;w.id=slots.next_wager_id++;w.creator=1;w.game="slots";w.state="settled";
         w.stake=10;w.capacity=1;w.expires=monday;w.members={1};w.payouts[1]=10*multiplier;
         w.detail["reels"]=Json::array({a,b,c});w.detail["multiplier"]=multiplier;
         slots.wagers.push_back(w);balance=balance-10+10*multiplier;
     }
     slots.players.at(1).wallet=balance;slots.save();PlayerStatsStore recovered;recovered.open(path);
     check(recovered.wagers.size()==125 && recovered.next_wager_id==126 && recovered.players.at(1).wallet==balance,"Slots ledger or balance lost on restart");
     for(std::size_t i=0;i<slots.wagers.size();++i)check(recovered.wagers.at(i).detail==slots.wagers.at(i).detail && recovered.wagers.at(i).payouts==slots.wagers.at(i).payouts,"Slots outcome changed on restart");
     recovered.refund_pending();recovered.save();PlayerStatsStore twice;twice.open(path);
     check(twice.players.at(1).wallet==balance && twice.wagers.size()==125,"Restart paid/refunded settled Slots again");}
    // Casino ledger records, concurrent private hands and settled losses survive restart.
    {PlayerStatsStore casino;const auto path=folder/"casino.json";casino.open(path);
     casino.joined(1,"One",1,monday);casino.joined(2,"Two",1,monday);
     casino.players[1].wallet=500;casino.players[2].wallet=500;
     Wager roulette;roulette.id=casino.next_wager_id++;roulette.creator=1;roulette.game="roulette";
     roulette.state="settled";roulette.stake=10;roulette.capacity=1;roulette.members={1};
     roulette.payouts[1]=0;roulette.detail["number"]=0U;roulette.detail["colour"]="green";roulette.detail["choice"]="red";
     casino.wagers.push_back(roulette);casino.players[1].wallet-=10;
     Wager one;one.id=casino.next_wager_id++;one.creator=1;one.game="blackjack";one.state="pending";
     one.stake=10;one.capacity=3;one.members={1};one.expires=monday+120;one.detail["player"]=Json::array({1,2});
     Wager two=one;two.id=casino.next_wager_id++;two.creator=2;two.members={2};
     casino.wagers.push_back(one);casino.wagers.push_back(two);
     casino.players[1].wallet-=10;casino.players[2].wallet-=10;
     casino.rebuild_headroom();casino.save();
     PlayerStatsStore revived;revived.open(path);
     check(revived.wagers.size()==3 && revived.wagers[0].payouts.at(1)==0 && revived.wagers[0].detail.at("number")==0,
         "Roulette settlement lost after restart");
     check(revived.players[1].wager_headroom==30 && revived.players[2].wager_headroom==30,
         "Concurrent Blackjack hands did not recover their own escrow");
     revived.refund_pending();revived.save();PlayerStatsStore recovered;recovered.open(path);
     check(recovered.players[1].wallet==490 && recovered.players[2].wallet==500,
         "Restart recovery double-charged or refunded a settled roulette bet");
     check(recovered.wagers[1].state=="refunded" && recovered.wagers[2].state=="refunded",
         "Blackjack recovery left pending hands");}
    // Stakes above the old million-point cap and green wins survive persistence.
    {PlayerStatsStore large;const auto path=folder/"all-in.json";large.open(path);
     large.joined(1,"One",1,monday);large.joined(2,"Two",1,monday);
     const std::uint64_t stake=2000001;
     large.players[1].wallet=100000000;large.players[2].wallet=100000000;
     for(const auto* game:{"coinflip","dice","highlow","slots","roulette","blackjack"}) {
         Wager w;w.id=large.next_wager_id++;w.game=game;w.creator=1;w.stake=stake;w.state="settled";w.members={1};w.capacity=1;
         if(w.game=="coinflip" || w.game=="dice"){w.members.insert(2);w.capacity=2;w.payouts[1]=2*stake;}
         else if(w.game=="roulette"){w.payouts[1]=20*stake;w.detail["number"]=0U;w.detail["colour"]="green";w.detail["choice"]="green";}
         else if(w.game=="slots")w.payouts[1]=25*stake;
         else if(w.game=="blackjack"){w.capacity=3;w.payouts[1]=2*stake+stake/2;}
         else {w.initial=12;w.payouts[1]=12*stake;}
         large.wagers.push_back(w);
     }
     large.save();PlayerStatsStore restored;restored.open(path);
     check(restored.wagers.size()==6 && restored.wagers[4].payouts.at(1)==20*stake,"Large wager or green payout failed to load");
     Wager pending;pending.id=restored.next_wager_id++;pending.game="highlow";pending.creator=1;pending.members={1};pending.stake=stake;pending.initial=12;pending.capacity=1;
     restored.wagers.push_back(pending);restored.players[1].wallet-=stake;restored.rebuild_headroom();restored.save();
     PlayerStatsStore allin_recovered;allin_recovered.open(path);allin_recovered.refund_pending();allin_recovered.save();
     check(allin_recovered.players[1].wallet==100000000,"Large unplayed stake was not refunded on restart");}
    // Failed write preserves the last file. Malformed data is never replaced on load.
    std::filesystem::create_directory(file.string()+".tmp");bool threw=false;
    try{store.save();}catch(...){threw=true;}check(threw,"Save failure is reported");
    std::filesystem::remove(file.string()+".tmp");
    {std::ofstream out(file);out<<"broken";}threw=false;try{restarted.open(file);}catch(...){threw=true;}check(threw,"Corrupt data fails closed");
    std::filesystem::remove_all(folder);
    {
        Roles roles;Role player;player.name="player";Role mod;mod.name="moderator";mod.commands={"kick"};Role admin;admin.name="admin";admin.all_commands=true;Role custom=mod;custom.name="marshal";Role vip=player;vip.name="vip";
        roles.list={player,mod,admin,custom,vip};roles.default_role="player";roles.players={{2,"moderator"},{3,"admin"},{4,"marshal"},{5,"vip"}};roles.settle();
        PlayerStatsStore board;for(std::uint64_t id=1;id<=5;++id){auto& entry=board.players[id];entry.points=100;entry.weekly_points=100;entry.week=PlayerStatsStore::week_at(monday);entry.active_us=100;entry.events={1,1,1};}
        for(const auto kind:{"weekly","points","time","events"}) {
            auto rows=eligible_board(board,roles,kind,monday);check(std::ranges::find(rows,2)==rows.end() && std::ranges::find(rows,3)==rows.end() && std::ranges::find(rows,4)==rows.end(),"Staff included in leaderboard");
            check(rows.size()==2 && std::ranges::find(rows,5)!=rows.end(),"Ordinary/VIP excluded from leaderboard");
        }
        roles.players[2]="vip";roles.settle();const auto rows=eligible_board(board,roles,"points",monday);check(std::ranges::find(rows,2)!=rows.end() && board.players[2].points==100,"Role change did not restore saved player statistics");
    }
    if(!failed)std::puts("player stats tests passed");return failed?1:0;
}
