// Included by server_park_tests.cpp: exercise the normal server with stock wire packets.
int run_stats_host() {
    wire() = {};
    const auto folder=std::filesystem::temp_directory_path()/"msc_stats_host_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();
    config.map="San Vansterdam";config.auto_update=false;config.listed=false;
    config.object_spam_alerts.enabled=false;config.player_stats.leaderboard_minutes=1;
    const std::uint64_t player=76561198000000101ULL,admin=player+1,restricted=player+2,offline=player+3;
    config.roles.players[admin]="admin";config.roles.players[restricted]="muted";config.roles.settle();
    for(auto &role:config.roles.list)if(role.name=="vip")role.permissions|=role_alerts;
    config.roles.settle();
    save_config(config);
    const auto utc=static_cast<std::int64_t>(std::time(nullptr));
    PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");
    for(auto id:{player,admin,restricted}){seed.joined(id,"Saved",1,utc);seed.players[id].active_us=25ULL*3600000000-1000000;}
    seed.players[player].points=seed.players[player].weekly_points=100;
    seed.players[admin].points=seed.players[admin].weekly_points=20000;
    seed.joined(offline,"Offline Leader",1,utc);seed.players[offline].points=10000;seed.save();
    std::vector<std::string> logs;SteamTransport transport;auto stats_writer=std::make_shared<SerialTasks>();Host host(config,transport,[&](const auto &line){logs.push_back(line);},stats_writer);
    std::string error;check(host.start(error),"Stats integration host starts: "+error);
    const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;
    for(auto id:{player,admin,restricted}) {
        auto &g=games[id];g.id=id;g.epoch=5000+id%1000;g.secret=invite->secret;
        g.map=map_hash(map_destination(config.map));g.name=id==player?"Loyal Skater":id==admin?"Staff Skater":"Restricted Skater";
    }
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}
        host.tick(now);stats_writer->submit([]{}).get();for(auto &[id,g]:games)g.read();
    }};
    for(auto &[id,g]:games)g.hello(now);
    run_for(3000000);
    check(config.roles.of(player)->name=="player","Custom VIP staff capabilities prevent automatic assignment");
    for(auto &role:config.roles.list)if(role.name=="vip")role.permissions&=~role_alerts;
    config.roles.settle();run_for(3000000);
    check(config.roles.of(player)->name=="vip" && config.roles.of(admin)->name=="admin" && config.roles.of(restricted)->name=="muted","VIP promotion protects staff and restricted roles");
    const auto *vip=config.roles.of(player),*ordinary=config.roles.find("player");
    check(vip && vip->tag=="MSC" && vip->granted==(ordinary->granted|role_reserved_slot),"MSC VIP inherits ordinary permissions and reserved slot");
    for(auto &role:config.roles.list)if(role.name=="player")role.permissions&=~role_chat;
    config.roles.settle();check(!(config.roles.of(player)->granted&role_chat),"VIP continues to follow later ordinary-player permission changes");
    for(auto &role:config.roles.list)if(role.name=="player")role.permissions|=role_chat;
    config.roles.settle();
    check(load_config(config.file).roles.of(player)->name=="vip","VIP role persisted through native config save");
    check(has(games[player].chat,"You earned MSC VIP after") && !has(games[restricted].chat,"You earned MSC VIP after"),"Private VIP receipt reaches recipient only");
    auto &p=games[player],&a=games[admin],&r=games[restricted];
    const auto other_before=a.chat.size();p.say("/stats",now);run_for(500000);
    check(has(p.chat,"MSC stats: Loyal Skater") && a.chat.size()==other_before,"Stats command replies privately through stock chat");
    r.say("/daily",now);run_for(500000);check(has(r.chat,"Resets at 00:00 UTC"),"Restricted player can read their reward status");
    p.say("/top points",now);run_for(500000);check(has(p.chat,"Offline Leader"),"Private board includes offline player");
    p.say("/top time 99999",now);run_for(500000);check(has(p.chat,"Leaderboard page must be"),"Leaderboard pagination validates bounds");
    check(!custom_command_name_free("stats") && !custom_command_name_free("daily") && !custom_command_name_free("top"),"Player commands registered as native names");
    config.activity_log=false;config.announce_throwdowns=false;
    for(unsigned mode=0;mode<3;++mode) {
        ThrowdownMessage m;m.leader=player;m.id=100+mode;m.series=mode==0?"ThrowdownSkate":mode==1?"SpotBattle":"JamSession";
        auto event=[&](ThrowdownMessage::Kind kind){m.kind=kind;auto packet=p.header(PacketKind::throwdown,now);packet.throwdown=encode_throwdown(m);p.send(packet);run_for(100000);};
        m.placement={1};event(ThrowdownMessage::Kind::offer);
        m.order={player,admin};event(ThrowdownMessage::Kind::start);
        m.value=100;
        event(mode==0?ThrowdownMessage::Kind::attempt:mode==1?ThrowdownMessage::Kind::turn_end:ThrowdownMessage::Kind::score);
        event(mode==0?ThrowdownMessage::Kind::attempt:mode==1?ThrowdownMessage::Kind::turn_end:ThrowdownMessage::Kind::score);
    }
    PlayerStatsStore counted;counted.open(folder/"data/player-stats.json");
    check(counted.players.at(player).events==std::array<std::uint64_t,3>{1,1,1},"Structured stock throwdowns count once with console activity logging disabled");
    config.roles.players[player]="player";config.roles.settle();run_for(1000000);
    check(config.roles.of(player)->name=="player","Manual removal does not immediately regrant VIP");
    run_for(60000000);
    check(has(p.chat,"Weekly MSC points:") && has(p.timed_announcements,"Congratulations Loyal Skater"),"Scheduled weekly board congratulates online leader");
    const auto cards=p.timed_announcements.size();run_for(61000000);
    check(has(p.chat,"Lifetime MSC points:") && p.timed_announcements.size()==cards,"Offline overall leader gets no card or substitute online winner");
    check(!has(p.timed_announcements,"Congratulations Staff Skater"),"Admin receives leaderboard congratulations");
    host.stop("Stats test restart");PlayerStatsStore saved;saved.open(folder/"data/player-stats.json");
    check(saved.players.at(player).vip_awarded && saved.players.at(player).active_us>=25ULL*3600000000,"Shutdown persists stats and completed VIP grant");
    // Corrupt reward data leaves the normal server available and is never overwritten.
    {std::ofstream bad(folder/"data/player-stats.json");bad<<"do not overwrite";}
    wire()={};Host bad(config,transport,[&](const auto &line){logs.push_back(line);});
    check(bad.start(error),"Corrupt stats do not crash the server");bad.stop("Test done");
    std::ifstream in(folder/"data/player-stats.json");std::string retained;std::getline(in,retained);
    in.close();
    check(retained=="do not overwrite" && has(logs,"Rewards disabled; existing data preserved"),"Corrupt stats are preserved with a diagnostic");
    // An interrupted native roles write retains the pending grant and no success receipt.
    saved.players.at(player).vip_awarded=false;saved.players.at(player).vip_pending=true;saved.save();
    save_config(config);
    const auto blocked=folder/"data/roles.json.tmp";std::filesystem::create_directory(blocked);
    wire()={};Host interrupted(config,transport,[&](const auto &line){logs.push_back(line);});
    check(interrupted.start(error),"Pending-grant failure host starts");
    auto connect=[&](Host &server,std::uint64_t epoch){
        Game game;game.id=player;game.epoch=epoch;game.secret=parse_invite(server.invite())->secret;
        game.name="Loyal Skater";game.map=map_hash(map_destination(config.map));game.hello(now);
        server.tick(now);game.read();now+=50000;game.ready(now);server.tick(now);game.read();return game;
    };
    auto failed=connect(interrupted,222);
    check(config.roles.of(player)->name=="player" && !has(failed.chat,"You earned MSC VIP after"),"Failed roles save rolls back promotion without success receipt");
    interrupted.stop("Failed-grant restart");std::filesystem::remove(blocked);
    PlayerStatsStore pending;pending.open(folder/"data/player-stats.json");
    check(pending.players.at(player).vip_pending && !pending.players.at(player).vip_awarded,"Failed grant intent remains on disk");
    wire()={};Host recovered(config,transport,[&](const auto &line){logs.push_back(line);});
    check(recovered.start(error),"Pending-grant recovery host starts");
    auto recovered_game=connect(recovered,333);
    check(config.roles.of(player)->name=="vip" && has(recovered_game.chat,"You earned MSC VIP after"),"Restart recovers pending VIP grant");
    recovered.stop("Recovery complete");
    pending.open(folder/"data/player-stats.json");
    check(pending.players.at(player).vip_awarded && !pending.players.at(player).vip_pending,"Recovered grant finalizes exactly once");
    std::filesystem::remove_all(folder);
    return failures?1:0;
}
