// Structured games and staff through the real server dispatcher.
int run_msc_allin_host() {
  for(const std::string game:{"coinflip","dice","highlow","slots","roulette","blackjack"}) {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_features_host_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";config.wagers.enabled=true;config.wagers.roulette_enabled=true;config.wagers.blackjack_enabled=true;
    config.player_stats.hourly_points=0;config.player_stats.daily_points=0;config.player_stats.leaderboard_minutes=0;
    config.player_stats.streak_step=0;config.object_spam_alerts.enabled=false;
    const std::uint64_t player=76561198000000801ULL,other=player+1;
    config.roles.players[other]="admin";config.roles.settle();save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");for(auto id:{player,other}){seed.joined(id,"Tester",1,std::time(nullptr));seed.players.at(id).wallet=2000001;seed.players.at(id).points=100;seed.players.at(id).weekly_points=100;}seed.save();}
    SteamTransport transport;auto writer=std::make_shared<SerialTasks>();Host host(config,transport,[](const auto&){},writer);
    std::string error;check(host.start(error),"MSC interface server starts: "+error);const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;for(auto id:{player,other}){auto& g=games[id];g.id=id;g.epoch=9000+id%100;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));g.name=id==player?"Buyer":"Recipient";}
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){if(now>=next_ready){next_ready=now+1000000;for(auto& [id,g]:games)if(g.joined)g.ready(now);}host.tick(now);writer->submit([]{}).get();for(auto& [id,g]:games)g.read();}};
    for(auto& [id,g]:games)g.hello(now);run_for(2000000);auto& p=games[player];auto& q=games[other];
    auto saved=[&](std::uint64_t id=76561198000000801ULL){PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");return seed.players.at(id);};

    p.say("/msc-ui hello 1",now);q.say("/msc-ui hello 1",now);run_for(1100000);
    std::map<std::uint64_t,std::uint64_t> serial;
    auto send=[&](Game& g,Json body,bool slots=true){if(slots)body["slots_ui"]=true;body["v"]=1U;body["epoch"]=std::to_string(g.epoch);body["request"]=++serial[g.id];auto out=g.header(PacketKind::msc,now);out.text=body.dump();g.send(out);run_for(1100000);return out.text;};
    auto retry=[&](Game& g,const std::string& text){auto out=g.header(PacketKind::msc,now);out.text=text;g.send(out);run_for(1100000);};
    auto read=[&](Game& g,const char* op){auto j=Json::object();j["op"]=op;send(g,j);};
    auto state=[&](Game& g){check(!g.msc_messages.empty(),"MSC feature reply missing");return msc::snapshot(g.msc_messages.back());};
    auto response=[&](Game& g){for(auto it=g.msc_messages.rbegin();it!=g.msc_messages.rend();++it)if(msc::integer(*it,"request")==serial[g.id])return msc::snapshot(*it);throw std::runtime_error("Missing request acknowledgement");};
    auto view=[&](Game& g,const std::string& name){const auto snap=state(g);for(const auto& v:snap.games)if(v.id==name)return v;throw std::runtime_error("Missing MSC game");};
    auto act=[&](Game& g,const std::string& game,const std::string& action,std::uint64_t round=0){auto j=Json::object();j["op"]="game";j["game"]=game;j["action"]=action;j["round"]=round;j["stake"]=10U;return send(g,j);};
    read(p,"games");
    check(view(p,game).maximum==2000001,"Game still advertises a configured cap: "+game);
    Json wager=Json::object();wager["op"]="game";wager["game"]=game;wager["round"]=0U;wager["stake"]=2000001U;
    wager["action"]=game=="roulette"?"green":game=="slots"?"spin":game=="blackjack"?"deal":"create";
    const auto sent=send(p,wager);
    const auto current=view(p,game);
    check((current.round && current.round->stake==2000001) || (current.previous && current.previous->stake==2000001),"All-in bet did not reach ledger: "+game+" "+response(p).message);
    const auto balance=saved().wallet;retry(p,sent);check(saved().wallet==balance,"All-in request replayed: "+game);
    if(current.round && game!="blackjack"){
        wager["action"]="cancel";wager["round"]=current.round->id;wager["stake"]=0U;send(p,wager);
        check(saved().wallet==2000001,"Unplayed all-in did not refund: "+game);
    }
    host.stop("All-in test complete");std::filesystem::remove_all(folder);
  }
  return failures;
}
