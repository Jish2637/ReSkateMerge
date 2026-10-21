int run_expansion_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/("msc-expansion-"+std::to_string(now_us()));
    std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.name="MSC expansion test";
    config.player_stats.hourly_points=0;config.player_stats.daily_minutes=100000;
    config.max_players=12;config.wagers.enabled=true;config.afk_kick=0;
    config.roles=default_roles();config.roles.players[76561198000010110ULL]="admin";config.roles.settle();save_config(config);
    constexpr std::uint64_t first=76561198000010100ULL;
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");for(unsigned i=0;i<12;++i){seed.joined(first+i,"Player "+std::to_string(i),1,std::time(nullptr));seed.players.at(first+i).wallet=i==11?0:100000;}seed.save();}
    SteamTransport transport;std::vector<std::string> logs;auto writer=std::make_shared<SerialTasks>();
    Host host(config,transport,[&](const auto &line){logs.push_back(line);},writer);std::string error;
    check(host.start(error),"Expansion host starts: "+error);const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;
    for(unsigned i=0;i<12;++i){auto &g=games[first+i];g.id=first+i;g.name="Player "+std::to_string(i);g.epoch=40000+i;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));}
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}host.tick(now);writer->submit([]{}).get();for(auto &[id,g]:games)g.read();}};
    for(auto &[id,g]:games)g.hello(now);run_for(2000000);
    auto saved=[&](){PlayerStatsStore s;s.open(folder/"data/player-stats.json");return s.players;};
    auto ledger=[&](){PlayerStatsStore s;s.open(folder/"data/player-stats.json");return s.wagers;};
    auto say=[&](std::uint64_t id,std::string_view text){run_for(16000000);games.at(id).say(text,now);run_for(100000);};
    auto &p=games.at(first);
    config.wagers.enabled=false;say(first,"/coinflip create 10");check(ledger().empty(),"Disabled wagering accepted a stake");config.wagers.enabled=true;

    say(first,"/joinmsg set Night skating begins!");check(saved().at(first).wallet==100000 && !saved().at(first).joinmsg_owned,"Join preview grants nothing");
    say(first,"/buy");check(saved().at(first).wallet==97500 && saved().at(first).joinmsg_owned && saved().at(first).joinmsg_enabled,"Join purchase failed");
    say(first,"/joinmsg set Back for more!");say(first,"/joinmsg off");say(first,"/joinmsg on");
    check(saved().at(first).wallet==97500 && saved().at(first).joinmsg=="Back for more!" && saved().at(first).joinmsg_enabled,"Owned join edits/toggles are free");
    say(first,"/joinmsg set Admin has arrived");check(saved().at(first).joinmsg=="Back for more!","Reserved join message accepted");
    say(first,"/joinmsg set <b>Fake</b>");check(saved().at(first).joinmsg=="Back for more!","Formatting injection accepted");
    say(first,"/colourbundle buy");config.shop.colourbundle_price=1600;say(first,"/buy");check(saved().at(first).wallet==97500 && !saved().at(first).colourbundle_owned,"Changed expansion price accepted stale quote");config.shop.colourbundle_price=1500;
    say(first,"/colourbundle buy");run_for(121000000);say(first,"/buy");check(!saved().at(first).colourbundle_owned,"Expired expansion quote granted ownership");
    say(first,"/colourbundle buy");say(first,"/buy");say(first,"/colour red");say(first,"/colour cyan");
    check(saved().at(first).wallet==96000 && saved().at(first).colourbundle_owned && saved().at(first).colour=="cyan","Colour bundle or free switching failed");
    say(first,"/colourbundle buy");say(first,"/buy");check(saved().at(first).wallet==96000,"Duplicate bundle charged");
    say(first,"/colour shimmer-purple");say(first,"/buy");say(first,"/shimmerbundle buy");say(first,"/buy");
    check(saved().at(first).wallet==87100 && saved().at(first).shimmers.size()==5,"Partial shimmer bundle did not charge 6400");
    say(first,"/shimmerbundle buy");say(first,"/buy");say(first,"/colour shimmer-gold");check(saved().at(first).wallet==87100,"Owned shimmer charged twice");
    say(first,"/coinflip create 100");say(first+11,"/coinflip join");check(ledger().back().members.size()==1 && saved().at(first+11).wallet==0,"Insufficient funds accepted wager");
    say(first+1,"/pay 76561198000010111 1000");for(unsigned i=1;i<12;++i)say(first+i,"/coinflip join");
    check(ledger().back().members.size()==12,"Coin Flip did not accept full server capacity above eight");
    check(saved().at(first+10).wallet==99900,"Unlimited administrator did not contribute real wager funds");
    const auto before_duplicate=saved().at(first+1).wallet;say(first+1,"/coinflip join");check(saved().at(first+1).wallet==before_duplicate,"Duplicate wager charged");
    say(first+1,"/coinflip start");check(ledger().back().state=="pending","Noncreator started lobby");
    say(first,"/coinflip status");check(has(p.chat,"Players: 12/12"),"Full-capacity lobby status incorrect");
    auto total=[&](){std::uint64_t sum=0;for(const auto &[id,profile]:saved())sum+=profile.wallet;return sum;};
    const auto reserved_total=total();say(first,"/coinflip start");
    check(ledger().back().state=="settled" && ledger().back().payouts.begin()->second==1200 && total()==reserved_total+1200,"Coin Flip payout not conserved");
    const auto paid_total=total();say(first,"/coinflip start");check(total()==paid_total,"Duplicate start paid twice");
    say(first,"/dice create 10");for(unsigned i=1;i<12;++i)say(first+i,"/dice join");
    const auto dice_total=total();say(first,"/dice start");check(ledger().back().state=="settled" && total()==dice_total+120,"Dice failed at full capacity");
    check(has(p.chat,"Round 1:") && has(p.chat,"won 120 MSC"),"Dice rolls/outcome missing from chat");
    say(first,"/coinflip create 100");say(first+1,"/coinflip join");const auto before_leave=saved().at(first+1).wallet;
    say(first+1,"/coinflip leave");check(saved().at(first+1).wallet==before_leave+100 && ledger().back().members.size()==1,"Leaving lobby did not refund");
    say(first,"/coinflip cancel");check(ledger().back().state=="refunded","Creator cancel did not refund");
    const auto highlow_before=saved().at(first).wallet;say(first,"/highlow 10");check(saved().at(first).wallet==highlow_before-10,"High Low did not reserve stake");
    say(first,"/highlow 10");check(saved().at(first).wallet==highlow_before-10,"Duplicate High Low round accepted");
    say(first,"/highlow cancel");check(saved().at(first).wallet==highlow_before,"High Low cancel lost stake");
    say(first,"/highlow 10");const auto initial=ledger().back().initial;
    say(first,initial==13?"/highlow lower":"/highlow higher");check(ledger().back().state=="settled","High Low did not settle");
    const auto after_guess=saved().at(first).wallet;say(first,"/highlow lower");check(saved().at(first).wallet==after_guess,"Repeat guess paid twice");
    // Disconnect the creator and ensure every participant's stake is returned.
    say(first,"/dice create 10");say(first+1,"/dice join");const auto before_disconnect=total();host.drop(first,"test disconnect");run_for(500000);
    check(total()==before_disconnect+20 && ledger().back().state=="refunded","Creator disconnect lost stakes");
    games.at(first).epoch+=1000;games.at(first).hello(now);run_for(2000000);
    check(has(games.at(first+1).chat,"Player 0 joined: Back for more!"),"Genuine join did not broadcast owned message");
    const auto join_count=std::ranges::count(games.at(first+1).chat,std::string("Player 0 joined: Back for more!"));
    host.drop(first,"cooldown test");run_for(500000);games.at(first).epoch+=1000;games.at(first).hello(now);run_for(2000000);
    check(std::ranges::count(games.at(first+1).chat,std::string("Player 0 joined: Back for more!"))==join_count,"Reconnect bypassed join cooldown");
    // A failed reservation leaves the durable wallet/ledger untouched and blocks further wagers.
    const auto fail_before=saved().at(first).wallet;const auto count_before=ledger().size();
    std::filesystem::create_directory(folder/"data/player-stats.json.tmp");say(first,"/coinflip create 100");
    check(saved().at(first).wallet==fail_before && ledger().size()==count_before,"Failed save debited stake");
    std::filesystem::remove(folder/"data/player-stats.json.tmp");say(first,"/highlow 10");check(saved().at(first).wallet==fail_before,"Save failure did not block further wagering");
    host.stop("expansion complete");
    // Seed pending escrow as if the process stopped abruptly, then construct a fresh host.
    {PlayerStatsStore s;s.open(folder/"data/player-stats.json");Wager w;w.id=s.next_wager_id++;w.creator=first;w.game="coinflip";w.stake=10;w.capacity=12;w.expires=std::time(nullptr)+300;w.members={first,first+1};
     for(auto id:w.members)s.players.at(id).wallet-=10;s.wagers.push_back(w);s.rebuild_headroom();s.save();}
    wire()={};const auto recovery_before=total();
    {Host recovered(config,transport,[&](const auto &line){logs.push_back(line);});check(total()==recovery_before+20 && ledger().back().state=="refunded","Startup recovery did not refund escrow");}
    {Host recovered(config,transport,[&](const auto &line){logs.push_back(line);});check(total()==recovery_before+20,"Repeated startup recovery refunded twice");}
    // Settlement/refund failures retain pending escrow on disk and recover exactly once.
    auto fail_transaction=[&](bool cancel) {
        wire()={};Host failure_host(config,transport,[&](const auto &line){logs.push_back(line);},writer);
        check(failure_host.start(error),"Failure fixture starts");
        const auto failure_invite=parse_invite(failure_host.invite());if(!failure_invite)return;
        Game one,two;one.id=first;two.id=first+1;one.name="Failure One";two.name="Failure Two";
        one.epoch=70001;two.epoch=70002;one.secret=two.secret=failure_invite->secret;one.map=two.map=map_hash(map_destination(config.map));
        std::uint64_t failure_now=now+1000000000;
        auto step=[&](){failure_host.tick(failure_now);writer->submit([]{}).get();one.read();two.read();failure_now+=3000000;};
        one.hello(failure_now);two.hello(failure_now);step();
        const auto wallet_one=saved().at(first).wallet,wallet_two=saved().at(first+1).wallet;
        one.say(cancel?"/highlow 10":"/coinflip create 10",failure_now);step();
        if(!cancel){two.say("/coinflip join",failure_now);step();}
        const auto pending_wallet=saved().at(first).wallet;
        std::filesystem::create_directory(folder/"data/player-stats.json.tmp");
        one.say(cancel?"/highlow cancel":"/coinflip start",failure_now);step();
        check(ledger().back().state=="pending" && saved().at(first).wallet==pending_wallet,"Failed settlement/refund changed durable escrow");
        std::filesystem::remove(folder/"data/player-stats.json.tmp");failure_host.stop("failed transaction");
        {Host recovered(config,transport,[&](const auto &line){logs.push_back(line);});
         check(saved().at(first).wallet==wallet_one && saved().at(first+1).wallet==wallet_two,"Failed transaction restart lost or duplicated stake");}
    };
    fail_transaction(false);fail_transaction(true);
    std::filesystem::remove_all(folder);return failures?1:0;
}
