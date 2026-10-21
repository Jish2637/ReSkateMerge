int run_upgrade_210_host() {
    static_assert(protocol_version==52);
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_upgrade_210_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";
    config.player_stats.hourly_points=0;config.player_stats.daily_minutes=1000;config.player_stats.leaderboard_minutes=0;
    const std::uint64_t admin=76561198000001001ULL,player=admin+1,watch=admin+2,mod=admin+3;
    config.roles.players[admin]="admin";config.roles.players[player]="vip";config.roles.players[mod]="moderator";
    for(auto &role:config.roles.list)if(role.name=="vip"){role.tag="MSC";role.permissions|=role_movement;}
    config.roles.settle();save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");seed.joined(player,"Builder",1,std::time(nullptr));seed.players.at(player).wallet=500;seed.save();}
    auto writer=std::make_shared<SerialTasks>();SteamTransport transport;std::vector<std::string> logs;
    Host host(config,transport,[&](const auto &s){logs.push_back(s);},writer);std::string error;check(host.start(error),"2.1.0 integration host starts: "+error);
    std::map<std::uint64_t,Game> games;std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}
        host.tick(now);writer->submit([]{}).get();for(auto &[id,g]:games)g.read();
    }};
    for(auto id:{admin,player,watch,mod}){auto &g=games[id];g.id=id;g.epoch=16000+id%100;g.name=id==player?"Builder":id==admin?"Admin":id==mod?"Mod":"Watcher";
        g.secret=parse_invite(host.invite())->secret;g.map=map_hash(map_destination(config.map));g.hello(now);}
    run_for(2000000);auto &p=games[player];auto &observer=games[watch];
    auto command=[&](std::string_view text){auto answer=host.command(text);run_for(500000);return answer;};
    auto say=[&](std::string_view text){run_for(2100000);p.say(text,now);run_for(100000);};
    check(config.kick_minutes==0 && config.votes.kick_minutes==5 && config.throwdown_locks==0 && p.sync_drones,"2.1.0 defaults changed");
    check(command("lock Builder all").starts_with("Locked"),"Native lock command unavailable");
    check(!p.noclip && !p.no_bail && !p.boosts && p.enforce_tuning && !(p.role_permissions&role_movement),"Native locks lost to MSC roster");
    const auto observer_lines=observer.chat.size();say("/colour red");
    check(!p.noclip && !p.no_bail && !p.boosts && p.enforce_tuning && !(p.role_permissions&role_movement),"Private preview lifted native locks");
    check(observer.chat.size()==observer_lines,"Private preview leaked to another recipient");
    say("/buy");check(!p.noclip && p.enforce_tuning,"Purchased colour lifted tool lock");
    check(command("announce-to Builder LOCKED_PRIVATE_CARD").starts_with("Announced to Builder"),"Private native announcement failed");
    check(has(p.timed_announcements,"LOCKED_PRIVATE_CARD") && !has(observer.timed_announcements,"LOCKED_PRIVATE_CARD"),"Roster grouping leaked announcement");
    const std::vector<NetworkObject> initial={{1,"own_bk_rail_01",{1,0,0},{0,0,0,1},2.5f},{2,"own_bk_ramp_01",{2,0,0},{0,0,0,1},.5f}};ObjectState layout;layout.replace(initial);
    auto upload=[&]{for(const auto &chunk:layout.updates(0)){auto packet=p.header(PacketKind::objects,now);packet.objects=chunk;p.send(packet);}run_for(1500000);};upload();
    check(command("build save scales").starts_with("Saved"),"2.1.0 shared build snapshot failed");
    const auto original=read_build_set(build_set_folder(folder/"data",map_destination(config.map))/"scales.build.json","scales",map_destination(config.map));
    check(original.objects.size()==2 && original.objects[0].scale==2.5f,"2.1.0 save lost original scales");
    const auto watch_clear=observer.local_clear_requests;check(command("delobj Builder").starts_with("Cleared all 2"),"Targeted clear failed with lock grouping");
    check(p.local_clear_requests>0 && observer.local_clear_requests==watch_clear && !p.noclip && p.enforce_tuning,"Targeted clear affected observer or lifted locks");
    check(command("build load scales").starts_with("Loaded") && observer.park.objects().size()==2,"Server-owned build restore failed");
    command("object-scaling off");run_for(1000000);
    check(original.objects[0].scale==2.5f && observer.park.objects().at(1).scale==2.5f,"Scaling policy rewrote restored server data");
    const auto typing_before=observer.typing_notices.size();
    auto typing=[&]{auto packet=p.header(PacketKind::typing,now);packet.typing=true;p.send(packet);run_for(250000);};typing();
    check(observer.typing_notices.size()==typing_before+1,"Native typing notices not relayed");
    command("timeout Builder 2");typing();check(observer.typing_notices.size()==typing_before+1,"Text timeout leaked typing notices");command("untimeout Builder");
    typing();check(observer.typing_notices.size()==typing_before+2,"Un-timeout failed to restore typing notices");
    command("unlock Builder all");check(p.noclip && p.no_bail && p.boosts && (p.role_permissions&role_movement),"Native unlock did not restore role tools");
    const auto modhelp=host.command("modhelp players",mod);check(modhelp.find("/kick <player> [minutes|restart]")!=std::string::npos && modhelp.find("/lock ")==std::string::npos,"Moderator help includes admin-only locks or old kick syntax");
    check(host.command("lock Builder all",mod).find("cannot")!=std::string::npos,"Upgrade expanded moderator lock permission");
    command("map Isle of Grom");check(observer.park.objects().empty(),"2.1.0 map cleanup retained old build objects");
    check(!p.refused && !observer.refused,"2.1.0 ordinary wire decoding refused MSC objects");
    host.stop("Done");std::filesystem::remove_all(folder);return failures?1:0;
}
