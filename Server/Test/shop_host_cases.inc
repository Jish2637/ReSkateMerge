// Ordinary-wire shop integration: no client extension or public purchase echoes.
int run_shop_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_shop_host_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";
    config.name="-MSC-Midnight Skate Club-Jishs Test Server";
    config.object_spam_alerts.enabled=false;config.player_stats.leaderboard_minutes=0;
    config.player_stats.hourly_points=0;config.player_stats.daily_points=0;config.player_stats.streak_step=0;
    const std::uint64_t player=76561198000000401ULL,mod=player+1,watch=player+2;
    // Fixture only: owner supplies badge text for the ordinary role; shop never edits roles.
    for(auto &role:config.roles.list)if(role.name=="player")role.tag="Skater";
    config.roles.players[mod]="moderator";config.roles.players[watch]="admin";config.roles.settle();save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");seed.joined(watch,"Observer",1,std::time(nullptr));seed.players.at(watch).wallet=15000;seed.save();}
    SteamTransport transport;std::vector<std::string> logs;
    auto stats_writer=std::make_shared<SerialTasks>();Host host(config,transport,[&](const auto &line){logs.push_back(line);},stats_writer);
    std::string error;check(host.start(error),"Shop host starts: "+error);
    const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;
    for(auto id:{player,mod,watch}) {
        auto &g=games[id];g.id=id;g.epoch=8000+id%100;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));
        g.name=id==player?"Shopper":id==mod?"Shop Mod":"Observer";
    }
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}
        host.tick(now);stats_writer->submit([]{}).get();for(auto &[id,g]:games)g.read();
    }};
    auto saved=[&](){PlayerStatsStore s;s.open(folder/"data/player-stats.json");return s.players.at(player);};
    for(auto &[id,g]:games)g.hello(now);run_for(2000000);
    auto &p=games[player];auto &m=games[mod];auto &a=games[watch];
    std::string previous_command;
    auto say=[&](std::string_view line){
        // Preserve native flood/repeat limits while replaying commands through stock packets.
        run_for(line==previous_command?15100000:2100000);p.say(line,now);previous_command=line;run_for(100000);
    };
    {PlayerStatsStore store;store.open(folder/"data/player-stats.json");check(store.players.at(watch).wallet==15000,"Test login reduces or increases an already higher balance");}
    check(saved().wallet==10000 && saved().points==0 && saved().weekly_points==0,"Test top-up changes earned points or wrong balance");
    check(has(p.chat,"topped up to 10,000"),"Missing private login top-up receipt");
    say("/tag NightOwl");check(saved().tags.empty() && saved().wallet==10000,"Preview spent points");
    say("/buy");check(saved().wallet==9750 && saved().tags==std::vector<std::string>{"NightOwl"} && saved().equipped_tag=="NightOwl","Purchase not atomic/owned/equipped");
    check(!has(a.chat,"Purchased tag") && !has(m.chat,"Preview NightOwl"),"Shop answers leaked to other players");
    say("/buy");check(saved().wallet==9750,"Duplicate confirmation charged twice");
    say("/tag off");check(saved().equipped_tag.empty() && saved().tags.size()==1,"Tag off destroys ownership");
    say("/tag nightowl");check(saved().equipped_tag=="NightOwl" && saved().wallet==9750,"Owned tag case-insensitive equip costs points");
    say("/tag Moon");say("/buy");say("/tag NightOwl");check(saved().tags.size()==2 && saved().wallet==9500 && saved().equipped_tag=="NightOwl","Collection or free switching failed");
    say("/tags");check(has(p.chat,"Moon - /tag Moon (free)"),"Owned tags not discoverable");
    const auto observer_lines=a.chat.size(),moderator_lines=m.chat.size();
    const auto ordinary_role=*config.roles.of(player);
    say("/colour gold");
    bool saw_preview=false,saw_cost=false;
    for(std::size_t i=0;i<p.chat.size();++i) {
        if(p.chat[i].starts_with("Preview gold:")) {
            saw_preview=true;check(p.chat_colours[i]==std::pair{parse_colour("#FFD700").value(),0xffffffffU},"Preview must colour the badge/name but keep message text white");
        }
        if(p.chat[i].starts_with("Cost: 50 points")) {
            saw_cost=true;check(p.chat_colours[i]==std::pair{parse_colour(config.chat_color).value(),parse_colour(config.chat_text_color).value()},"Cost line retained preview colour");
        }
    }
    check(saw_preview && saw_cost && saved().wallet==9500 && saved().colour.empty(),"Ordinary-player preview charged or applied colour");
    check(a.chat.size()==observer_lines && m.chat.size()==moderator_lines,"Colour preview leaked to other players");
    check(p.server_chat_badge==parse_colour(config.chat_color).value() && p.server_chat_text==parse_colour(config.chat_text_color).value() &&
          a.server_chat_badge==parse_colour(config.chat_color).value(),"Private preview did not restore normal server colours");
    run_for(2100000);p.say("/buy",now);
    // The purchase must publish its roster in this receive/tick, with the same player epoch.
    host.tick(now);stats_writer->submit([]{}).get();for(auto &[id,g]:games)g.read();now+=50000;previous_command="/buy";
    check(saved().colour=="gold" && saved().wallet==9450,"Ordinary player cannot buy a colour");
    const auto ordinary_member=std::ranges::find_if(a.roster,[&](const auto &v){return v.id==player;});
    check(ordinary_member!=a.roster.end() && ordinary_member->tag==ordinary_role.tag && ordinary_member->tag_color==parse_colour("#FFD700").value() && ordinary_member->epoch==p.epoch,"Colour update was delayed or changed player session identity");
    check(config.roles.of(player)->name==ordinary_role.name && config.roles.of(player)->tag==ordinary_role.tag && config.roles.of(player)->granted==ordinary_role.granted &&
          !config.roles.players.contains(player),"Ordinary colour purchase changes role assignment or permissions");
    say("/colour default");
    const auto reset_member=std::ranges::find_if(a.roster,[&](const auto &v){return v.id==player;});
    check(reset_member!=a.roster.end() && reset_member->tag==ordinary_role.tag && reset_member->tag_color==parse_colour(ordinary_role.color).value() && saved().wallet==9450,"Default reset changes role badge or charges points");
    // Preview again, then acquire VIP before confirming: no role lock and no VIP text change.
    say("/colour gold");config.roles.players[player]="vip";config.roles.settle();
    say("/buy");check(saved().colour=="gold" && saved().wallet==9400,"VIP colour purchase failed");
    const auto role_before=*config.roles.of(player);
    say("/colour gold");check(saved().wallet==9400,"Already-active colour costs points");
    say("/jish-fp 1 1");run_for(1000000);
    const auto member=std::ranges::find_if(a.roster,[&](const auto &v){return v.id==player;});
    check(member!=a.roster.end() && member->name=="[FP] [NightOwl] Shopper" && member->tag==role_before.tag && member->tag_color==parse_colour("#FFD700").value(),"FP/name/role/colour stock roster composition is wrong");
    check(config.roles.of(player)->granted==role_before.granted,"Cosmetics change permissions");
    check(host.command("cleartag Shopper",player).find("not an admin")!=std::string::npos,"Ordinary player can clear tags");
    check(host.command("cleartag [FP] [NightOwl] Shopper",mod).find("Removed and disabled")!=std::string::npos,"Moderator tag clearing failed");run_for(100000);
    check(saved().tags.size()==2 && saved().disabled_tags==std::vector<std::string>{"NightOwl"} && saved().equipped_tag.empty(),"Staff removal deletes ownership or fails to disable");
    say("/tag NightOwl");check(saved().equipped_tag.empty() && saved().wallet==9400,"Disabled tag can be re-equipped or rebought");
    check(has(p.chat,"A moderator has removed"),"No target moderation receipt");
    check(host.command("modhelp chat",mod).find("cleartag")!=std::string::npos,"Moderator help misses cleartag");
    // Pure validation rejects blocked/obfuscated text without warning strikes or disconnect.
    text::set_own_words(std::vector<std::string>{"badword"},std::vector<std::string>{"forbidden"},false);
    say("/tag forbidden");say("/tag b4dword");say("/tag f o r b i d");
    check(!wire().closed.contains(player) && !has(p.chat,"Warning") && !has(logs,"[words]") && saved().wallet==9400,"Rejected tag warned, kicked or charged player");
    check(!has(a.chat,"forbidden"),"Rejected tag broadcast publicly");
    text::set_own_words({},{});
    say("/tag admin");check(saved().equipped_tag.empty(),"Impersonation accepted");
    say("/tag MoreThan12Letters");say("/tag <red>");check(saved().wallet==9400,"Invalid tag charged");
    say("/tag Moon");const auto before_filtered=saved().wallet;
    text::set_own_words(std::vector<std::string>{"moon"},{},false);run_for(2000000);
    const auto hidden=std::ranges::find_if(a.roster,[&](const auto &v){return v.id==player;});
    check(hidden!=a.roster.end() && hidden->name.find("Moon")==std::string::npos && saved().tags.size()==2,"Filter reload fails to hide decoration or destroys collection");
    say("/tag Moon");check(saved().wallet==before_filtered,"Unavailable owned tag charged");text::set_own_words({},{});run_for(2000000);
    say("/tag ClearLine");text::set_own_words(std::vector<std::string>{"clearline"},{},false);say("/buy");
    check(saved().wallet==9400 && saved().tags.size()==2,"Purchase confirmation did not revalidate the word filter");text::set_own_words({},{});
    say("/tag Later");run_for(120000000);say("/buy");check(saved().wallet==9400,"Expired preview charged");
    say("/tag PriceTest");++config.shop.tag_price;say("/buy");check(saved().wallet==9400,"Changed-price quote accepted");--config.shop.tag_price;
    check(host.command("timeout Shopper 10",mod).find("text")!=std::string::npos,"Timeout setup failed");
    say("/tag NewTag");check(saved().tags.size()==2,"Text-timeout allows new public tag");say("/tag off");check(saved().equipped_tag.empty(),"Timed-out player cannot remove own tag");
    host.command("untimeout Shopper",mod);
    say("/tag Moon");say("/colour default");check(saved().colour.empty() && saved().wallet==9400,"Default reset costs points");
    say("/colour red");say("/buy");say("/colour gold");say("/buy");check(saved().wallet==9300,"Colour changes incorrectly collected as free unlocks");
    // Built-in shimmers are permanent collectibles; they do not need animation traffic.
    say("/shimmers");check(has(p.chat,"shimmer-purple - 2500 points to own"),"Shimmers are not discoverable or price is wrong");
    const std::pair<std::string_view,std::uint32_t> native_shimmers[]={
        {"purple",0xffff78b4U},{"red",0xff6464ffU},{"gold",0xff7ae2ffU},{"blue",0xffff7b1fU},{"green",0xfffff9b2U}};
    for(const auto &[name,colour]:native_shimmers) {
        say("/colour shimmer-"+std::string(name));
        const auto line=std::ranges::find_if(p.chat,[&](const auto &text){return text.starts_with("Preview shimmer-"+std::string(name)+":");});
        check(line!=p.chat.end(),"Shimmer preview missing");
        if(line!=p.chat.end())check(p.chat_colours[line-p.chat.begin()]==std::pair{colour,0xffffffffU},"Shimmer does not use native animated endpoint/white message text");
        check(saved().wallet==9300 && saved().shimmers.empty(),"Shimmer preview granted ownership or spent funds");
    }
    say("/colour shimmer-purple");say("/buy");
    check(saved().wallet==6800 && saved().shimmers==std::vector<std::string>{"shimmer-purple"} && saved().colour=="shimmer-purple","First shimmer did not charge 2500 and atomically grant ownership/equip");
    say("/buy");say("/colour shimmer-purple");check(saved().wallet==6800,"Duplicate confirmation/active selection charges for owned shimmer");
    say("/colour shimmer-blue");say("/buy");say("/colour shimmer-purple");
    check(saved().wallet==4300 && saved().shimmers.size()==2 && saved().colour=="shimmer-purple","Switching between owned shimmers is not free");
    config.shop.shimmer_price=3000;say("/colour shimmer-red");config.shop.shimmer_price=2500;say("/buy");
    check(saved().wallet==4300 && saved().shimmers.size()==2,"Changed shimmer price accepted without fresh preview");
    say("/colour shimmer-green");say("/buy");say("/colour shimmer-gold");
    check(saved().wallet==1800 && saved().shimmers.size()==3 && saved().colour=="shimmer-green","Insufficient balance grants fourth shimmer");
    say("/colour red");say("/buy");say("/colour shimmer-purple");
    check(saved().wallet==1750 && saved().shimmers.size()==3 && saved().colour=="shimmer-purple","Normal colour changed shimmer ownership or re-equipping costs points");
    say("/colour default");check(saved().colour.empty() && saved().shimmers.size()==3 && saved().wallet==1750,"Default reset destroys shimmer purchases");
    say("/colour shimmer-blue");say("/shimmers");check(has(p.chat,"shimmer-blue - owned, equipped"),"Collection does not show equipped owned shimmer");
    const auto shimmer_member=std::ranges::find_if(a.roster,[&](const auto &v){return v.id==player;});
    check(shimmer_member!=a.roster.end() && shimmer_member->tag==role_before.tag && shimmer_member->tag_color==0xffff7b1fU && shimmer_member->epoch==p.epoch,
          "Equipped shimmer changes role text/session or uses incorrect native animation colour");
    const auto roster_count=a.roster_updates;run_for(10000000);
    check(a.roster_updates-roster_count<=6,"Client-side shimmer generates extra animation roster traffic");
    const auto map_wallet=saved().wallet;host.change_map(config.map);run_for(3000000);
    check(saved().wallet==map_wallet,"Map transfer tops up test funds again");
    say("/tag Session");host.drop(player,"Test reconnect");run_for(100000);
    p.epoch+=1000;p.joined=false;p.receiver={};wire().closed.erase(player);p.hello(now);run_for(2000000);
    check(saved().wallet==10000 && saved().tags.size()==2 && saved().shimmers.size()==3 && saved().colour=="shimmer-blue","Reconnect top-up accumulates or loses collection");
    say("/buy");check(saved().tags.size()==2,"Reconnect retained purchase quote");
    // Save failure rolls back the debit AND ownership; last durable profile survives.
    say("/colour shimmer-red");const auto before=saved();
    std::filesystem::create_directory(folder/"data/player-stats.json.tmp");say("/buy");
    check(saved().wallet==before.wallet && saved().tags==before.tags && saved().shimmers==before.shimmers && saved().colour==before.colour && has(p.chat,"balance and purchases have not changed"),"Failed save charged or granted purchase");
    const auto audit_file=folder/"ReSkateServer-points.jsonl";const auto audit_rows=read_points_records(audit_file);
    check(std::count_if(audit_rows.begin(),audit_rows.end(),[&](const Json &r){return r.at("event").string()=="purchase" && r.at("status").string()=="committed" && r.at("item").string()=="NightOwl";})==1,"Preview/repeated buy falsely recorded as a purchase");
    check(std::ranges::any_of(audit_rows,[&](const Json &r){return r.at("event").string()=="purchase" && r.at("status").string()=="committed" && r.at("item").string()=="NightOwl" && r.at("listed_price").get<unsigned>()==250 && r.at("charged").get<unsigned>()==250 && r.at("balance_before").get<unsigned>()==10000 && r.at("balance_after").get<unsigned>()==9750 && r.at("player").at("steam_id").string()==std::to_string(player);}),"Purchase audit lacks exact price, charge, identity or balances");
    check(audit_rows.back().at("status").string()=="rolled_back" && audit_rows.back().at("charged").get<unsigned>()==0 && !audit_rows.back().at("ownership_granted").get<bool>(),"Failed purchase logged as committed or granted ownership");
    check(std::ranges::any_of(audit_rows,[&](const Json &r){return r.at("action").string().starts_with("tag disabled by") && r.at("actor").at("steam_id").string()==std::to_string(mod);}),"Tag moderation audit lost actor");
    check(!has(p.chat,"points-audit") && !has(a.chat,"points-audit"),"Audit records leaked into game chat");
    std::filesystem::remove(folder/"data/player-stats.json.tmp");host.stop("Shop restart test");
    std::filesystem::rename(audit_file,folder/"points-audit-before-restart.jsonl");std::filesystem::create_directory(audit_file);
    // Different server name: load collection unchanged, no test top-up, and quotes are gone.
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");seed.players.at(player).wallet=42;seed.save();}
    wire()={};config.name="-MSC-Midnight Skate Club-Live";
    Host restarted(config,transport,[&](const auto &line){logs.push_back(line);});check(restarted.start(error),"Shop restart failed");
    Game again;again.id=player;again.epoch=9999;again.secret=parse_invite(restarted.invite())->secret;again.map=map_hash(map_destination(config.map));again.name="Shopper";
    again.hello(now);restarted.tick(now);again.read();now+=3000000;again.say("/tags",now);restarted.tick(now);again.read();
    check(has(again.chat,"NightOwl (unavailable)") && has(again.chat,"Moon (equipped)"),"Restart lost collection/disabled/equipped tags");
    check(saved().wallet==42 && saved().shimmers.size()==3 && saved().colour=="shimmer-blue","Restart lost shimmer collection/equipped item or changed wallet");
    now+=3000000;again.say("/tag Costly",now);restarted.tick(now);again.read();
    check(has(again.chat,"Not enough MSC points") && saved().wallet==42,"Insufficient balance allows purchase");
    check(!has(again.chat,"topped up"),"Non-test server granted test points");
    text::set_own_words({},std::vector<std::string>{"blockedterm"},false);
    now+=3000000;again.say("blockedterm",now);restarted.tick(now);again.read();
    check(has(logs,"[words]") && !wire().closed.contains(player),"Shop filter bypass changed normal chat enforcement");text::set_own_words({},{});
    check(has(logs,"UTC ") && has(logs,std::to_string(player)) && has(logs,"cost 250"),"Purchase audit incomplete");
    now+=3000000;again.say("/colour default",now);restarted.tick(now);again.read();
    check(has(logs,"Cannot open ReSkateServer-points.jsonl") && has(logs,"[points-audit]") && saved().colour.empty() && saved().wallet==42,"Audit file failure stopped transaction or lost main-log fallback");
    restarted.stop("Shop tests done");std::filesystem::remove_all(folder);return failures?1:0;
}
