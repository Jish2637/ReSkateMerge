int run_chat_log_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_chat_log_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    const auto file=folder/"ReSkateServer-chat.log";
    {std::ofstream out(file,std::ios::binary);out<<"Previous chat record\n";}
    auto read=[&](const auto &path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.player_stats.enabled=false;
    config.map="San Vansterdam";config.object_spam_alerts.enabled=false;
    const std::uint64_t player=76561198000000601ULL,muted=player+1,timed=player+2;
    config.roles.players[muted]="muted";config.roles.settle();
    config.text_timeouts.push_back({timed,"Timed",std::time(nullptr),std::time(nullptr)+3600});
    SteamTransport transport;std::vector<std::string> logs;Host host(config,transport,[&](const auto &s){logs.push_back(s);});
    std::string error;check(host.start(error),"Chat log host starts: "+error);
    std::map<std::uint64_t,Game> games;std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}
        host.tick(now);for(auto &[id,g]:games)g.read();
    }};
    auto connect=[&](auto id,const char *name){auto &g=games[id];g={};g.id=id;g.epoch=10000+id%100;g.name=name;g.secret=parse_invite(host.invite())->secret;g.map=map_hash(map_destination(config.map));g.hello(now);};
    connect(player,"Chat Skater");connect(muted,"Muted");connect(timed,"Timed");run_for(2000000);
    auto say=[&](auto id,std::string_view text){run_for(2100000);games[id].say(text,now);run_for(100000);};
    say(player,"A public quote");say(player,"Skate \xF0\x9F\x9B\xB9");
    const auto accepted=read(file);
    check(accepted.starts_with("Previous chat record\n") && accepted.find("[chat] Chat Skater ("+std::to_string(player)+"): A public quote")!=std::string::npos,"Chat record lost history or identity/message");
    check(accepted.find("T")!=std::string::npos && accepted.find("Z] [chat]")!=std::string::npos && std::count(accepted.begin(),accepted.end(),'\n')==3,"Chat timestamps not full UTC or records duplicated");
    check(accepted.find("Skate \xF0\x9F\x9B\xB9")!=std::string::npos,"Chat log lost UTF-8 text");
    check(has(logs,"[chat] Chat Skater: A public quote") && has(games[timed].chat,"A public quote"),"Separate logging removed normal log/console callback or public delivery");
    say(player,"/bal");say(player,"/w Timed Private secret");say(muted,"Muted secret");say(timed,"Timed secret");
    text::set_own_words({},std::vector<std::string>{"blockedchat"},false);say(player,"blockedchat");text::set_own_words({},{});
    host.command("say Server notice");run_for(100000);
    check(read(file)==accepted,"Chat file includes commands, DMs, restricted/filtered chat or server output");
    host.stop("Restart");wire()={};games.clear();check(host.start(error),"Chat log restart failed");
    connect(player,"Chat Skater");run_for(2000000);say(player,"After restart");
    check(read(file).starts_with(accepted) && read(file).find("After restart")!=std::string::npos,"Restart truncated chat history or stopped logging");
    host.stop("Done");wire()={};games.clear();
    // A failed open must not prevent hosting, relaying or the original logger.
    std::filesystem::create_directory(folder/"blocked");config.file=folder/"blocked/ReSkateServer.json";
    std::filesystem::create_directory(config.file.parent_path()/"ReSkateServer-chat.log");
    check(host.start(error),"Chat log open failure stopped server");connect(player,"Chat Skater");run_for(2000000);say(player,"Still chatting");
    check(has(logs,"Cannot open ReSkateServer-chat.log") && has(logs,"[chat] Chat Skater: Still chatting"),"Chat log failure lost warning/main log");
    host.stop("Done");
    {PublicChatLog raw;const auto path=folder/"escaped.log";check(raw.open(path),"Raw chat log open failed");
        check(raw.append(player,"Name\n[forged]","Message\r\n[forged]\\end"),"Raw chat log write failed");raw.close();const auto data=read(path);
        check(std::count(data.begin(),data.end(),'\n')==1 && data.find("\\x0A[forged]")!=std::string::npos && data.find("\\x0D\\x0A")!=std::string::npos,"Chat log permits forged multiline entries");}
    {PointsAuditLog raw;const auto path=folder/"raw-points.jsonl";std::string line;auto record=Json::object();record["event_id"]="test-1";
        record["status"]="committed";record["name"]="Player\n[forged] \xF0\x9F\x9B\xB9";
        check(raw.open(path) && raw.append(record,line),"Points audit append failed");raw.close();
        check(!raw.append(record,line) && !line.empty(),"Failed audit write lost fallback record");
        record["event_id"]="test-2";check(raw.open(path) && raw.append(record,line),"Points audit could not reopen/append");raw.close();
        const auto rows=read_points_records(path);check(rows.size()==2 && rows[0].at("name").string()==record.at("name").string(),"Audit restart/UTF-8/newline escaping failed");
        check(!raw.open(folder),"Directory accepted as points audit file");}
    std::filesystem::remove_all(folder);return failures?1:0;
}
