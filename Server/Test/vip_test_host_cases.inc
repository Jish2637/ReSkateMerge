// VIP test command, through the normal command gate and ordinary in-game chat wire.
int run_vip_test_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_vip_test_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";
    config.object_spam_alerts.enabled=false;config.player_stats.leaderboard_minutes=0;
    const std::uint64_t admin=76561198000000301ULL,mod=admin+1,one=admin+2,two=admin+3;
    config.roles.players[admin]="admin";config.roles.players[mod]="moderator";config.roles.settle();save_config(config);
    SteamTransport transport;std::vector<std::string> logs;auto stats_writer=std::make_shared<SerialTasks>();Host host(config,transport,[&](const auto &line){logs.push_back(line);},stats_writer);
    std::string error;check(host.start(error),"VIP test host starts: "+error);
    const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;
    for(auto id:{admin,mod,one,two}) {
        auto &g=games[id];g.id=id;g.epoch=7000+id%100;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));
        g.name=id==admin?"Admin Tester":id==mod?"Mod Tester":id==one?"Learner One":"Learner Two";
    }
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}
        host.tick(now);stats_writer->submit([]{}).get();for(auto &[id,g]:games)g.read();
    }};
    auto saved=[&](){PlayerStatsStore store;store.open(folder/"data/player-stats.json");return store;};
    for(auto &[id,g]:games)g.hello(now);run_for(2000000);
    check(host.command("viptest Learner One",one).find("not an admin")!=std::string::npos,"Ordinary player can run VIP test");
    check(host.command("viptest Learner One",mod).find("cannot use viptest")!=std::string::npos,"Moderator can run VIP test");
    check(host.command("viptest",admin).starts_with("/viptest"),"Bare VIP test targets a player");
    check(host.command("viptest Learner",admin).starts_with("No single"),"Ambiguous VIP test matches a player");
    check(host.command("viptest Nobody",admin).starts_with("No single"),"Missing VIP test target accepted");
    check(host.command("viptest Admin Tester",admin).find("ordinary player-role")!=std::string::npos,"VIP test changes staff roles");
    check(!custom_command_name_free("viptest"),"VIP test name is not registered as native");
    check(host.command("modhelp",admin).find("viptest")==std::string::npos && host.command("help",admin).find("viptest")!=std::string::npos,"Admin test command leaks into moderator help or is missing from server help");
    auto &a=games[admin],&p=games[one],&m=games[mod];
    a.say("/viptest \"Learner One\"",now);run_for(50000);
    auto prepared=saved();const auto target=25ULL*3600000000-60000000;
    check(prepared.players.at(one).active_us==target && !prepared.players.at(one).vip_awarded,"Command did not persist exactly 24h59m before normal grant");
    check(prepared.players.at(one).points==0 && prepared.players.at(one).daily_us<5000000 && prepared.players.at(one).last_claim_day==-1,
          "Artificial test time earns points, daily credit or a login bonus");
    check(has(a.chat,"VIP test prepared for Learner One") && has(p.chat,"An admin set your playtime") &&
          !has(m.chat,"VIP test prepared") && !has(m.chat,"An admin set your playtime"),"Preparation replies are not private to caller and target");
    run_for(59950000);
    check(config.roles.of(one)->name=="player" && !has(p.chat,"You earned MSC VIP after"),"VIP granted before the final active minute");
    run_for(50000);
    check(config.roles.of(one)->name=="vip" && has(p.chat,"You earned MSC VIP after 25 active hours"),"Normal VIP grant did not trigger at the final minute");
    check(config.roles.of(one)->tag=="MSC" && (config.roles.of(one)->granted&role_reserved_slot),"Test VIP lacks MSC tag/reserved slot");
    check(host.command("viptest Learner One",admin).find("ordinary player-role")!=std::string::npos,"VIP test overwrites an existing VIP");
    check(has(logs,"[stats] VIP test: Admin Tester") && has(logs,std::to_string(one)),"Test adjustment is not logged with actor and SteamID");
    config.roles.players[one]="player";config.roles.settle();
    check(host.command("viptest Learner One",admin).find("already earned VIP")!=std::string::npos,"VIP test silently resets a previous VIP grant");
    // Validate save rollback, a custom milestone and persistent adjusted time.
    config.player_stats.vip_hours=1;
    const auto tmp=folder/"data/player-stats.json.tmp";std::filesystem::create_directory(tmp);
    const auto notices=games[two].chat.size();
    check(host.command("viptest Learner Two",admin).find("was not changed")!=std::string::npos && games[two].chat.size()==notices,
          "Failed adjustment save sends a false target receipt");
    std::filesystem::remove(tmp);
    // A normal minute checkpoint must not persist the failed one-hour test adjustment.
    run_for(61000000);auto after_failure=saved();
    check(after_failure.players.at(two).active_us<300000000,"Failed adjustment was not rolled back in memory");
    const auto result=host.command("viptest "+std::to_string(two),0);
    check(result.find("59m")!=std::string::npos,"Console/SteamID test does not use the configured one-hour threshold");
    auto custom=saved();check(custom.players.at(two).active_us==59ULL*60000000,"Custom test target was not exactly one minute below threshold");
    check(host.command("viptest Learner Two",admin).find("VIP test prepared")!=std::string::npos,"An exactly prepared player cannot be prepared again");
    host.stop("VIP test restart");auto persisted=saved();
    check(persisted.players.at(two).active_us>=59ULL*60000000 && !persisted.players.at(two).vip_awarded,"Prepared playtime does not persist across restart");
    // A newly constructed host reads the adjustment without awarding downtime as playtime.
    wire()={};Host restarted(config,transport,[&](const auto &line){logs.push_back(line);});
    check(restarted.start(error),"VIP test restart host starts");
    Game rejoined;rejoined.id=two;rejoined.epoch=9999;rejoined.secret=parse_invite(restarted.invite())->secret;
    rejoined.name="Learner Two";rejoined.map=map_hash(map_destination(config.map));rejoined.hello(now);
    restarted.tick(now);rejoined.read();
    check(config.roles.of(two)->name=="player","Prepared VIP grants immediately on reconnect instead of counting the remaining minute");
    restarted.stop("VIP tests done");std::filesystem::remove_all(folder);
    return failures?1:0;
}
