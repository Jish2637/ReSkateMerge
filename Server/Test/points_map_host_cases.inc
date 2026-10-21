// Real command dispatch over ordinary client packets, with atomic wallet persistence.
int run_points_map_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_points_map_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();
    config.name="MSC points tests";config.map="San Vansterdam";config.default_map="Isle of Grom";
    config.object_spam_alerts.enabled=false;config.player_stats.leaderboard_minutes=0;
    config.player_stats.hourly_points=0;config.player_stats.daily_points=0;config.player_stats.streak_step=0;
    const std::uint64_t player=76561198000000501ULL,mod=player+1,admin=player+2,other=player+3;
    config.roles.players[mod]="moderator";config.roles.players[admin]="admin";config.roles.players[other]="vip";config.roles.settle();
    const auto offline_admin=other+1;config.roles.players[offline_admin]="admin";config.roles.settle();
    save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");
        for(auto id:{player,mod,admin,other,offline_admin}){
            seed.joined(id,id==offline_admin?"Offline Admin":"Saved",1,std::time(nullptr));auto &p=seed.players.at(id);p.wallet=100;
            p.points=p.weekly_points=(id==admin || id==offline_admin)?1000:10;
            p.active_us=(id==admin || id==offline_admin)?900000000:60000000;p.events={1,1,1};
        }
        seed.players.at(mod).vip_awarded=true;
        seed.save();}
    SteamTransport transport;std::vector<std::string> logs;auto stats_writer=std::make_shared<SerialTasks>();Host host(config,transport,[&](const auto &line){logs.push_back(line);},stats_writer);
    std::string error;check(host.start(error),"Points/default map host starts: "+error);
    check(config.map=="Isle of Grom" && load_config(config.file).map==config.map,"Default map not selected/saved at startup");
    check(host.command("map San Vansterdam").starts_with("Changing"),"Manual empty map change failed");
    std::uint64_t now=1000000000,next_ready=now;
    std::map<std::uint64_t,Game> games;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined && !wire().closed.contains(id))g.ready(now);}
        host.tick(now);stats_writer->submit([]{}).get();for(auto &[id,g]:games)g.read();
    }};
    run_for(1000000);check(config.map=="San Vansterdam","Repeated empty ticks reload default map");
    const auto invite=parse_invite(host.invite());if(!invite)return 1;
    for(auto id:{player,mod,admin,other}){
        auto &g=games[id];g.id=id;g.epoch=9000+id%100;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));
        g.world=3; // startup default selection, then the manual empty-map change
        g.name=id==player?"Wallet One":id==mod?"Builder Mod":id==admin?"Bank Admin":"Wallet Two";g.hello(now);
    }
    run_for(2000000);
    auto snapshot=[&](){PlayerStatsStore s;s.open(folder/"data/player-stats.json");return s.players;};
    auto say=[&](std::uint64_t id,std::string_view line){run_for(15100000);games.at(id).say(line,now);run_for(100000);};
    const auto before=snapshot();
    say(player,"/bal");check(has(games[player].chat,"MSC balance: 100 points.") && !has(games[mod].chat,"MSC balance:"),"Balance wrong or public");
    config.shop.enabled=false;say(player,"/pay Builder 25");
    check(snapshot().at(player).wallet==75 && snapshot().at(mod).wallet==125,"Shop-disabled wallet transfer failed");
    check(has(games[mod].chat,"Wallet One gave you 25 MSC points") && !has(games[other].chat,"gave you"),"Transfer receipts missing/private delivery failed");
    say(player,"/bal");check(has(games[player].chat,"MSC balance: 75 points."),"Balance requires enabled shop");
    config.player_stats.enabled=false;say(player,"/pay Builder 1");say(player,"/bal");
    check(has(games[player].chat,"MSC rewards are disabled") && snapshot().at(player).wallet==75,"Disabled stats allow wallet commands");config.player_stats.enabled=true;
    say(mod,"/pay Wallet One 5");check(snapshot().at(mod).wallet==120 && snapshot().at(player).wallet==80,"Moderator spending was unlimited");
    for(auto command:{"/pay Wallet 1","/pay Wallet One 1","/pay Nobody 1","/pay Bank 1","/pay Builder 0","/pay Builder -1","/pay Builder +1","/pay Builder 1.5","/pay Builder 1000","/pay Builder 18446744073709551616"})say(player,command);
    check(snapshot().at(player).wallet==80 && snapshot().at(mod).wallet==120,"Invalid/ambiguous/self/admin/insufficient transfer changed wallet");
    say(admin,"/bal");say(admin,"/pay Wallet One 500");check(snapshot().at(admin).wallet==100 && snapshot().at(player).wallet==580,"Admin grant deducted wallet or failed");
    check(has(games[admin].chat,"MSC balance: Unlimited points."),"Admin balance not unlimited");
    config.shop.enabled=true;
    say(admin,"/colour shimmer-purple");say(admin,"/buy");say(admin,"/tag Spark");say(admin,"/buy");
    check(snapshot().at(admin).wallet==100 && snapshot().at(admin).shimmers.size()==1 && snapshot().at(admin).tags.size()==1,"Admin shop spending not unlimited/collectible");
    check(has(games[admin].chat,"Balance after: Unlimited points") && has(games[admin].chat,"Balance: Unlimited points"),"Admin preview/receipt misleading");
    for(auto id:{player,mod,admin,other})say(id,"/stats");
    check(has(games[mod].chat,"VIP: active (staff)") && has(games[admin].chat,"VIP: active (staff)") && has(games[other].chat,"VIP: active (VIP role)"),"Existing VIP/staff status missing");
    check(!has(games[player].chat,"VIP: active"),"Ordinary player given VIP");
    check(has(games[mod].chat,"earned through playtime"),"Staff stats lost earned VIP history");
    for(const auto kind:{"weekly","points","time","events"}) {
        games[player].chat.clear();say(player,std::string("/top ")+kind);
        check(!has(games[player].chat,"Bank Admin") && !has(games[player].chat,"Offline Admin") && has(games[player].chat,"Builder Mod"),"Leaderboard includes online/offline admins or excludes moderators");
        check(has(games[player].chat,"page 1/1"),"Filtering changed leaderboard pagination");
    }
    {const auto roles=config.roles;
        for(auto id:{player,mod,other})config.roles.players[id]="admin";config.roles.settle();
        games[player].chat.clear();say(player,"/top points");check(has(games[player].chat,"no entries yet"),"Admin-only leaderboard is not empty");
        config.roles=roles;
    }
    config.roles.players[admin]="moderator";config.roles.settle();
    games[player].chat.clear();say(player,"/top points");check(has(games[player].chat,"1. Bank Admin"),"Demoted administrator did not return to leaderboard with saved points");
    say(admin,"/bal");say(admin,"/colour gold");say(admin,"/buy");
    check(has(games[admin].chat,"MSC balance: 100 points.") && snapshot().at(admin).wallet==50,"Removing admin failed to restore normal wallet");
    config.roles.players[admin]="admin";config.roles.settle();
    games[player].chat.clear();say(player,"/top points");check(!has(games[player].chat,"Bank Admin"),"Promoted administrator remains on leaderboard");
    // Reach the persistent counter limit, then reject overflow without corrupting either record.
    const auto remainder=max_stats_counter-snapshot().at(player).wallet;
    say(admin,"/pay Wallet One "+std::to_string(remainder));say(admin,"/pay Wallet One 1");
    check(snapshot().at(player).wallet==max_stats_counter && has(games[admin].chat,"exceed the recipient's balance limit"),"Wallet overflow accepted");
    const auto current=snapshot();
    for(auto id:{player,mod,admin,other})check(current.at(id).points==before.at(id).points && current.at(id).weekly_points==before.at(id).weekly_points && current.at(id).vip_awarded==before.at(id).vip_awarded,"Transfers/purchases changed earnings or earned VIP");
    check(has(logs,"[points-transfer] UTC") && has(logs,std::to_string(player)) && has(logs,std::to_string(mod)),"Transfer audit incomplete");
    // Block the atomic-save temporary path: both balances must roll back, with no success receipt.
    run_for(15100000);const auto receipts=games[other].chat.size();std::filesystem::create_directory(folder/"data/player-stats.json.tmp");
    games[player].say("/pay Wallet Two 1",now);run_for(100000);
    check(snapshot().at(player).wallet==current.at(player).wallet && snapshot().at(other).wallet==current.at(other).wallet,"Failed transfer partially persisted");
    check(has(games[player].chat,"Both balances are unchanged") && games[other].chat.size()==receipts,"Failed transfer confirmed to recipient");
    const auto audit_file=folder/"ReSkateServer-points.jsonl";const auto audit_rows=read_points_records(audit_file);
    check(std::ranges::any_of(audit_rows,[&](const Json &r){return r.at("event").string()=="transfer" && r.at("status").string()=="committed" && r.at("amount").get<unsigned>()==25 && r.at("sender").at("steam_id").string()==std::to_string(player) && r.at("recipient").at("steam_id").string()==std::to_string(mod) && r.at("sender_balance_before").get<unsigned>()==100 && r.at("sender_balance_after").get<unsigned>()==75 && r.at("recipient_balance_after").get<unsigned>()==125;}),"Transfer audit lacks participants/amount/balances");
    check(std::ranges::any_of(audit_rows,[&](const Json &r){return r.at("event").string()=="admin_grant" && r.at("amount").get<std::uint64_t>()==500 && r.at("sender_balance_before").get<unsigned>()==100 && r.at("sender_balance_after").get<unsigned>()==100 && r.at("debited").get<unsigned>()==0 && r.at("credited").get<unsigned>()==500;}),"Admin grant audit confused with normal transfer");
    check(std::ranges::any_of(audit_rows,[&](const Json &r){return r.at("event").string()=="purchase" && r.at("status").string()=="committed" && r.at("unlimited_admin").get<bool>() && r.at("listed_price").get<unsigned>()==2500 && r.at("charged").get<unsigned>()==0;}),"Unlimited admin purchase price/actual charge not recorded");
    check(audit_rows.back().at("status").string()=="rolled_back" && audit_rows.back().at("credited").get<unsigned>()==0 && audit_rows.back().at("sender_balance_before").get<std::uint64_t>()==audit_rows.back().at("sender_balance_after").get<std::uint64_t>(),"Failed transfer audit claims points moved");
    std::filesystem::remove(folder/"data/player-stats.json.tmp");
    for(auto id:{player,mod,admin,other})host.drop(id,"Map test departure");run_for(100000);
    check(config.map=="Isle of Grom","Last departure did not reset map");
    host.command("map San Vansterdam");run_for(1000000);check(config.map=="San Vansterdam","Empty reset repeated without rearming");
    host.stop("Points restart");wire()={};games.clear();
    // Restart retains gifted balances and honours a disabled default.
    config.default_map.clear();Host again(config,transport,[&](const auto &line){logs.push_back(line);});check(again.start(error),"Restart failed");
    check(config.map=="San Vansterdam","Disabled default changed map");
    Game rejoin;rejoin.id=player;rejoin.epoch=10001;rejoin.secret=parse_invite(again.invite())->secret;rejoin.map=map_hash(map_destination(config.map));rejoin.name="Wallet One";
    rejoin.hello(now);again.tick(now);rejoin.read();now+=3000000;rejoin.say("/bal",now);again.tick(now);rejoin.read();
    check(has(rejoin.chat,"MSC balance: 9000000000000000 points."),"Restart lost gifted wallet or rollback");
    // Hold an unadmitted map downloader when the last admitted player leaves.
    Game pending;pending.id=other;pending.epoch=11001;pending.secret=parse_invite(again.invite())->secret;pending.map=map_hash(map_destination(config.map));pending.name="Pending";
    wire().status.peers.push_back({other,true});
    auto hello=pending.header(PacketKind::map_request,now);hello.map_fetching=true;pending.send(hello);again.tick(now);
    config.default_map="Isle of Grom";again.drop(player,"Last admitted leaves");again.tick(now+=50000);
    check(config.map=="San Vansterdam","Default map interrupted pending downloader");
    again.drop(other,"Download cancelled");again.tick(now+=50000);check(config.map=="Isle of Grom","Default map not selected after pending connection ends");
    again.stop("Done");wire()={};config.default_map="not-an-installed-map";const auto log_start=logs.size();
    Host invalid(config,transport,[&](const auto &line){logs.push_back(line);});check(invalid.start(error),"Invalid default should not prevent startup");
    invalid.tick(now+=50000);invalid.tick(now+=50000);
    check(config.map=="Isle of Grom" && std::count_if(logs.begin()+log_start,logs.end(),[](const auto &line){return line.starts_with("[default-map]");})==1,"Invalid default changed map or repeatedly logged");
    invalid.stop("Done");check(read_points_records(audit_file).size()==audit_rows.size(),"Restart truncated or duplicated points audit");
    std::filesystem::remove_all(folder);return failures?1:0;
}
