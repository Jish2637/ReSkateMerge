int run_map_persistence_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_map_persistence_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder/"data");
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";
    config.object_spam_alerts.enabled=false;config.player_stats.daily_minutes=1;config.player_stats.hourly_points=0;
    auto writer=std::make_shared<SerialTasks>();SteamTransport transport;std::vector<std::string> logs;
    Host host(config,transport,[&](const auto &s){logs.push_back(s);},writer);std::string error;check(host.start(error),"Persistence host starts: "+error);
    std::promise<void> release,started;auto gate=release.get_future().share();
    auto held=writer->submit([&]{started.set_value();gate.wait();});started.get_future().wait();
    const std::uint64_t id=76561198000000901ULL;Game game;game.id=id;game.epoch=14001;game.name="Map Skater";
    game.secret=parse_invite(host.invite())->secret;game.map=map_hash(map_destination(config.map));
    std::uint64_t now=1000000000,next_ready=now;bool ready=true;game.hello(now);
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(ready && game.joined && now>=next_ready){next_ready=now+1000000;game.ready(now);}
        host.tick(now);game.read();std::this_thread::yield();
    }};
    run_for(62000000);
    check(!has(game.chat,"Daily bonus:"),"Uncommitted background bonus was announced");
    // Files that did not change must not be rewritten by a native map selection.
    const auto mute=folder/"data/voice-mutes.json",timeout=folder/"data/text-timeouts.json";
    {std::ofstream out(mute);out<<"preserve mute bytes";}{std::ofstream out(timeout);out<<"preserve timeout bytes";}
    std::filesystem::create_directory(mute.string()+".tmp");std::filesystem::create_directory(timeout.string()+".tmp");
    ready=false;const auto before_world=game.world;const auto start=std::chrono::steady_clock::now();
    const auto answer=host.command("map Isle of Grom");
    check(answer.find("Changing map")!=std::string::npos && std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"Map command waited for blocked statistics disk writer");
    run_for(3000000);
    check(game.world==before_world+1 && game.map==map_hash(map_destination(config.map)) && !wire().closed.contains(id),"Native transfer did not progress while stats writer blocked");
    check(!has(logs,"Could not save the config"),"Map command rewrote unchanged restriction files");
    const auto read=[](const auto &path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    check(read(mute)=="preserve mute bytes" && read(timeout)=="preserve timeout bytes","Map command changed moderation persistence");
    release.set_value();held.get();writer->submit([]{}).get();run_for(1000000);writer->submit([]{}).get();run_for(1000000);
    host.stop("Durable shutdown");game.read();
    PlayerStatsStore saved;saved.open(folder/"data/player-stats.json");const auto &p=saved.players.at(id);
    check(p.active_us>=60000000 && p.active_us<63000000 && p.last_claim_day==p.day && p.qualifying_days==1,"Map accounting lost elapsed time, counted loading or duplicated bonus");
    check(has(game.chat,"Daily bonus:") && p.wallet==30,"Durable bonus receipt or balance was lost");
    check(!wire().closed.contains(id) || wire().closed[id]=="Durable shutdown","Persistence delay added a disconnect");
    std::filesystem::remove_all(folder);return failures?1:0;
}
