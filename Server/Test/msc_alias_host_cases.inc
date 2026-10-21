// Exercise aliases through the ordinary client wire format, including moderation.
int run_msc_alias_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_alias_host";
    std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.name="MSC aliases test";
    config.wagers.enabled=true;config.player_stats.hourly_points=0;config.player_stats.daily_minutes=100000;
    config.player_stats.leaderboard_minutes=0;config.roles=default_roles();
    constexpr std::uint64_t player=76561198000012100ULL,other=player+1;
    config.roles.settle();save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");
     seed.joined(player,"Alias Player",1,std::time(nullptr));seed.joined(other,"Target Case",1,std::time(nullptr));
     seed.players.at(player).wallet=20000;seed.players.at(other).wallet=10000;
     auto &profile=seed.players.at(player);profile.tags={"MiXeD Tag"};profile.joinmsg_owned=true;profile.joinmsg="Keep This CASE";
     seed.save();}
    SteamTransport transport;auto writer=std::make_shared<SerialTasks>();std::vector<std::string> logs;
    Host host(config,transport,[&](const auto &line){logs.push_back(line);},writer);std::string error;
    check(host.start(error),"Alias host starts: "+error);const auto invite=parse_invite(host.invite());if(!invite)return 1;
    Game p,q;p.id=player;q.id=other;p.name="Alias Player";q.name="Target Case";p.epoch=12100;q.epoch=12101;
    p.secret=q.secret=invite->secret;p.map=q.map=map_hash(map_destination(config.map));
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;if(p.joined)p.ready(now);if(q.joined)q.ready(now);}
        host.tick(now);writer->submit([]{}).get();p.read();q.read();
    }};
    p.hello(now);q.hello(now);run_for(2000000);
    auto say=[&](Game &g,const std::string &text){run_for(16000000);const auto start=g.chat.size();g.say(text,now);run_for(100000);return std::vector<std::string>(g.chat.begin()+start,g.chat.end());};
    auto saved=[&](){PlayerStatsStore state;state.open(folder/"data/player-stats.json");return state.players.at(player);};
    auto ledger=[&](){PlayerStatsStore state;state.open(folder/"data/player-stats.json");return state.wagers;};
    struct AliasCase {std::string_view canonical,aliases,argument;};
    const AliasCase cases[]={
        {"bal","b balance points",""},{"pay","give send","\"Target Case\" 0"},
        {"stats","st mystats",""},{"daily","d bonus",""},{"top","lb leaderboard","points"},
        {"shop","s store",""},{"tag","t nametag","MiXeD Tag"},{"tags","tl mytags",""},
        {"colour","c color","default"},{"shimmers","sh",""},{"buy","confirm purchase",""},
        {"joinmsg","jm joinmessage","off"},{"colourbundle","cb colorbundle",""},
        {"shimmerbundle","sb effectbundle",""},{"coinflip","cf flip",""},
        {"dice","dd diceduel",""},{"highlow","hl hilo",""}
    };
    for(const auto &entry:cases) {
        const auto suffix=entry.argument.empty()?std::string{}:" "+std::string(entry.argument);
        say(p,"/"+std::string(entry.canonical)+suffix); // settle any free equip first
        const auto expected=say(p,"/"+std::string(entry.canonical)+suffix);
        const auto equivalent=[&](const std::vector<std::string> &response) {
            if(entry.canonical=="stats")return !response.empty() && !expected.empty() && response.front()==expected.front();
            if(entry.canonical=="daily")return response.size()==2 && expected.size()==2 && response.back()==expected.back();
            return response==expected;
        };
        check(!expected.empty(),"Canonical command gave no reference response");
        for(auto remaining=entry.aliases;!remaining.empty();) {
            const auto [alias,tail]=split(remaining);remaining=tail;
            check(equivalent(say(p,"/"+std::string(alias)+suffix)),"Alias differs from canonical command: "+std::string(alias));
            auto mixed=std::string(alias);for(std::size_t i=0;i<mixed.size();i+=2)if(mixed[i]>='a' && mixed[i]<='z')mixed[i]-=32;
            check(equivalent(say(p,"/"+mixed+suffix)),"Mixed-case alias differs: "+mixed);
        }
    }
    const auto balance=saved().wallet;
    say(p,"/give \"Target Case\" 5");say(p,"/send \"Target Case\" 5");
    check(saved().wallet==balance-10,"Payment aliases lost quoted name or charged incorrectly");
    say(p,"/jm set Keep THIS c AND h");check(saved().joinmsg=="Keep THIS c AND h","Resolver changed message arguments");
    say(p,"/jm off");check(!saved().joinmsg_enabled,"Join-message short form did not toggle");
    say(p,"/c red");say(p,"/confirm");const auto after_colour=saved().wallet;
    check(saved().colour=="red" && after_colour==balance-60,"Colour alias/confirmation did not purchase normally");
    say(p,"/purchase");check(saved().wallet==after_colour,"Duplicate confirmation alias charged twice");
    say(p,"/color gold");run_for(121000000);say(p,"/confirm");check(saved().wallet==after_colour && saved().colour=="red","Alias bypassed quote expiry");
    say(p,"/cb buy");say(p,"/purchase");const auto after_bundle=saved().wallet;
    say(p,"/color white");say(p,"/c cyan");check(saved().colourbundle_owned && saved().wallet==after_bundle,"UK/US aliases lost free bundle switching");
    say(p,"/sb buy");say(p,"/confirm");check(saved().shimmers.size()==5,"Shimmer bundle alias did not grant ownership");
    // All short multiplayer actions, with a genuine two-player pot.
    say(p,"/cf c 500");say(q,"/cf j");check(ledger().back().members.size()==2,"Short join/create did not reserve both stakes");
    const auto second_before=say(q,"/cf go");check(has(second_before,"Only the creator"),"Short start bypassed creator permission");
    say(q,"/cf l");say(q,"/cf j");const auto lobby_status=say(p,"/cf s");check(has(lobby_status,"Pot: 1000"),"Short status incorrect");
    say(p,"/cf go");check(ledger().back().state=="settled" && ledger().back().payouts.begin()->second==1000,"Short start did not pay pot");
    const auto paid=saved().wallet;say(p,"/cf go");check(saved().wallet==paid,"Short start paid twice");
    say(p,"/dd c 10");check(has(say(p,"/dd s"),"Players: 1/"),"Dice short status not routed");say(p,"/dd x");check(ledger().back().state=="refunded","Short cancel lost stake");
    say(p,"/hl 100");check(ledger().back().stake==100,"High Low numeric stake changed");
    if(ledger().back().initial==13){say(p,"/hl h");check(ledger().back().state=="pending","Impossible short higher settled");say(p,"/hl l");}
    else say(p,"/hl h");check(ledger().back().state=="settled","Short higher did not settle");
    say(p,"/hilo 10");say(p,"/hl x");check(ledger().back().state=="refunded","High Low short cancel did not refund");
    check(has(say(p,"/cf h"),"create/c") && has(say(p,"/hl c 10"),"higher/h"),"Actions leaked between different games");
    const auto funds_before=saved().wallet;const auto games_before=ledger().size();
    say(p,"/send \"Target Case\" "+std::to_string(funds_before-5));
    say(p,"/cf c 10");say(p,"/hl 10");check(saved().wallet==5 && ledger().size()==games_before,"Alias bypassed insufficient funds");
    say(q,"/pay \"Alias Player\" "+std::to_string(funds_before-5));
    text::set_own_words({},std::vector<std::string>{"blockedaliasword"},false);
    say(p,"/t blockedaliasword");check(!has(logs,"[words]") && !wire().closed.contains(player),"Tag alias bypassed pure shop validation");text::set_own_words({},{});
    const auto before_restricted=saved().wallet;
    host.command("timeout Alias Player 10");
    for(const auto text:{"/t NewTag","/nametag NewTag","/confirm","/purchase","/cf c 10","/hl 10","/jm set blocked"})say(p,text);
    check(saved().wallet==before_restricted && saved().tags.size()==1 && saved().joinmsg=="Keep THIS c AND h","Alias bypassed text timeout");
    say(p,"/t off");check(saved().equipped_tag.empty(),"Alias blocked permitted tag removal during timeout");
    host.command("untimeout Alias Player");
    say(p,"/t PendingTag");
    config.roles.players[player]="muted";config.roles.settle();
    say(p,"/confirm");say(p,"/t NewTag");say(p,"/jm on");say(p,"/cf c 10");
    check(saved().wallet==before_restricted && !saved().joinmsg_enabled,"Alias bypassed role chat permission");
    config.roles.players.erase(player);config.roles.settle();
    check(has(say(p,"/colr red"),"Unknown command /colr"),"Unknown command was fuzzy matched");
    check(has(say(p,"/l"),"Unknown command /l"),"Game action became a global alias");
    check(has(say(p,"/msc"),"/coinflip (/cf)") && !has(say(p,"/msc"),"Unknown command"),"MSC help falls through or misses aliases");
    check(say(p,"/msc").size()<=12 && say(p,"/shop").size()<=12 && say(p,"/help").size()<=12,"Alias help exceeds reply budget");
    config.roles.players[other]="admin";config.roles.settle();
    const auto admin_help=say(q,"/help");check(has(admin_help,"Next: /help 2") && !say(q,"/help 2").empty(),"Long help did not paginate");
    say(q,"/effects off");check(!config.sync_effects,"Shimmer alias shadowed built-in effects command");
    say(q,"/effects on");check(config.sync_effects,"Built-in effects on changed");
    check(!host.command("build confirm").empty(),"Build confirmation routing changed");
    check(!p.refused && !q.refused,"Alias responses failed stock wire validation");
    host.stop("alias tests complete");std::filesystem::remove_all(folder);return failures?1:0;
}
