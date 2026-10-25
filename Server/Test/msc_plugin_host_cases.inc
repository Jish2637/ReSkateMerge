// Plugin-only stock-wire clients exercise the bridge against the real Host.
int run_msc_plugin_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_plugin_host_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";
    config.player_stats.hourly_points=0;config.player_stats.daily_points=0;config.player_stats.streak_step=0;config.player_stats.leaderboard_minutes=0;
    config.object_spam_alerts.enabled=false;
    const std::uint64_t player=76561198000000801ULL,other=player+1;
    save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");for(auto id:{player,other}){seed.joined(id,"Tester",1,std::time(nullptr));seed.players.at(id).wallet=20000;}seed.save();}
    SteamTransport transport;auto writer=std::make_shared<SerialTasks>();Host host(config,transport,[](const auto&){},writer);
    std::string error;check(host.start(error),"Plugin server starts: "+error);const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;for(auto id:{player,other}){auto& g=games[id];g.id=id;g.epoch=9000+id%100;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));g.name="Plugin tester";}
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){if(now>=next_ready){next_ready=now+1000000;for(auto& [id,g]:games)if(g.joined)g.ready(now);}host.tick(now);writer->submit([]{}).get();for(auto& [id,g]:games)g.read();}};
    for(auto& [id,g]:games)g.hello(now);run_for(2000000);auto& p=games[player];auto& q=games[other];
    auto saved=[&](std::uint64_t id){PlayerStatsStore store;store.open(folder/"data/player-stats.json");return store.players.at(id).wallet;};
    auto send=[&](Game& game,const Json& body,bool authority=false,std::uint64_t to=plugins::wire::authority){
        auto packet=game.header(PacketKind::throwdown,now);packet.throwdown=plugins::wire::encode(plugins::wire::channel_of("MSC_Hub"),to,body.dump(),authority);game.send(packet);run_for(2100000);
    };
    auto hello=Json::object();hello["v"]=1U;hello["op"]="hello";hello["nonce"]="1000-1-123456";
    send(p,hello,true);check(p.plugin_frames.empty(),"Forged authority was accepted");
    send(p,hello,false,other);check(q.plugin_frames.empty(),"Reserved Hub channel was relayed to a guest");
    send(p,hello);check(!p.plugin_frames.empty(),"Plugin hello got no chunks");
    check(p.msc_messages.empty() && q.plugin_frames.empty() && q.msc_messages.empty(),"Plugin leaked custom packets or private state");
    const auto state=[&](){
        std::map<std::size_t,std::string> parts;std::string token,revision;std::size_t count{};
        for(const auto& frame:p.plugin_frames){
            std::vector<std::string> fields;std::size_t at=0;
            for(int i=0;i<6;++i){const auto end=frame.find('|',at);if(end==std::string::npos)throw std::runtime_error("Bad bridge frame");fields.push_back(frame.substr(at,end-at));at=end+1;}
            if(fields[3]!=revision){parts.clear();revision=fields[3];}
            token=fields[2];count=std::stoull(fields[5]);parts[std::stoull(fields[4])]=frame.substr(at);
            check(frame.size()<=4082 && count<=6,"Bridge chunk exceeds official limits");
        }
        std::string text;for(std::size_t i=1;i<=count;++i)text+=parts.at(i);
        return std::pair{Json::parse(text),token};
    };
    auto [snapshot,token]=state();check(snapshot.at("profile").at("balance").get<std::uint64_t>()==20000,"Plugin profile differs from ledger");
    check(snapshot.at("bridge").get<unsigned>()==1 && snapshot.at("epoch").string()==std::to_string(p.epoch),"Missing bridge capabilities or connection epoch");
    std::uint64_t serial{};
    auto request=[&](Json body,std::string use_token={}) {
        body["v"]=1U;body["epoch"]=std::to_string(p.epoch);body["request"]=++serial;body["slots_ui"]=true;
        auto envelope=hello;envelope["op"]="request";envelope["token"]=use_token.empty()?token:use_token;envelope["body"]=body;send(p,envelope);return envelope;
    };
    auto pay=Json::object();pay["op"]="pay";pay["recipient"]=std::to_string(other);pay["amount"]=100U;
    request(pay,"wrong");check(saved(player)==20000 && saved(other)==20000,"Wrong bridge token transferred funds");
    const auto transfer=request(pay);check(saved(player)==19900 && saved(other)==20100,"Plugin transfer did not use real wallet");
    send(p,transfer);check(saved(other)==20100,"Duplicate plugin transfer executed twice");
    auto item=Json::object();item["op"]="item";item["kind"]="tag";item["value"]="Plugin";request(item);
    snapshot=state().first;check(!snapshot.at("quote").is_null() && saved(player)==19900,"Plugin purchase preview charged funds");
    auto confirm=Json::object();confirm["op"]="confirm";confirm["token"]=snapshot.at("quote").at("token");
    request(confirm);check(saved(player)==19650,"Plugin purchase did not settle");
    auto staff=Json::object();staff["op"]="staff-preview";staff["action"]="name";staff["args"]=Json::array({"Unauthorized"});request(staff);
    check(config.name!="Unauthorized","Player used plugin bridge to gain staff permissions");
    auto games_request=Json::object();games_request["op"]="games";request(games_request);
    check(state().first.at("games").size()==4,"Plugin catalogue is missing Slots");
    // A reload negotiates a new token, preserving the shared mutation high-water mark.
    hello["nonce"]="1000-2-123456";send(p,hello);
    const auto old=token;token=state().second;check(token!=old,"Reload retained bridge token");
    send(p,transfer);check(saved(other)==20100,"Old plugin session replayed a mutation");
    q.say("/msc-ui hello 1",now);run_for(1100000);check(!q.msc_messages.empty(),"Legacy MSC transport stopped working");
    // Storage failure still rolls back both wallets through the original dispatcher.
    const auto before_sender=saved(player),before_other=saved(other);
    std::filesystem::create_directory(folder/"data/player-stats.json.tmp");request(pay);
    check(saved(player)==before_sender && saved(other)==before_other && state().first.at("paused").get<bool>(),"Plugin transfer lost persistence rollback");
    std::filesystem::remove(folder/"data/player-stats.json.tmp");
    host.stop("Plugin test complete");std::filesystem::remove_all(folder);return failures;
}
