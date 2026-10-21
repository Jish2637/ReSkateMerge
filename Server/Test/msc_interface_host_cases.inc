// End-to-end opt-in MSC packets through the real host and simulated stock-wire clients.
int run_msc_interface_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_interface_host_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";
    config.player_stats.hourly_points=0;config.player_stats.daily_points=0;config.player_stats.leaderboard_minutes=0;
    config.player_stats.streak_step=0;config.object_spam_alerts.enabled=false;
    const std::uint64_t player=76561198000000801ULL,other=player+1;
    save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");for(auto id:{player,other}){seed.joined(id,"Tester",1,std::time(nullptr));seed.players.at(id).wallet=20000;seed.players.at(id).points=100;seed.players.at(id).weekly_points=100;}seed.save();}
    SteamTransport transport;auto writer=std::make_shared<SerialTasks>();Host host(config,transport,[](const auto&){},writer);
    std::string error;check(host.start(error),"MSC interface server starts: "+error);const auto invite=parse_invite(host.invite());if(!invite)return 1;
    std::map<std::uint64_t,Game> games;for(auto id:{player,other}){auto& g=games[id];g.id=id;g.epoch=9000+id%100;g.secret=invite->secret;g.map=map_hash(map_destination(config.map));g.name=id==player?"Buyer":"Recipient";}
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){if(now>=next_ready){next_ready=now+1000000;for(auto& [id,g]:games)if(g.joined)g.ready(now);}host.tick(now);writer->submit([]{}).get();for(auto& [id,g]:games)g.read();}};
    for(auto& [id,g]:games)g.hello(now);run_for(2000000);auto& p=games[player];auto& q=games[other];
    auto saved=[&](std::uint64_t id=76561198000000801ULL){PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");return seed.players.at(id);};
    auto packet=p.header(PacketKind::msc,now);packet.text="{\"v\":1}";p.send(packet);run_for(100000);check(p.msc_messages.empty(),"MSC sent state before capability negotiation");
    p.say("/msc-ui hello 1",now);run_for(100000);check(!p.msc_messages.empty(),"MSC hello did not return structured state");
    if(p.msc_messages.empty())return 1;
    check(q.msc_messages.empty(),"Legacy client received structured state");
    auto state=[&](){return msc::snapshot(p.msc_messages.back());};
    check(state().profile.balance==20000 && state().products.size()==17,"MSC catalogue or profile is incomplete");
    std::uint64_t serial{};
    auto send=[&](Json body){body["v"]=1U;body["epoch"]=std::to_string(p.epoch);body["request"]=++serial;auto out=p.header(PacketKind::msc,now);out.text=body.dump();p.send(out);run_for(1100000);return out;};
    auto item=[&](std::string kind,std::string value){auto b=Json::object();b["op"]="item";b["kind"]=kind;b["value"]=value;return send(b);};
    item("tag","Fresh");check(state().quote.has_value() && saved().wallet==20000,"MSC tag preview charged or omitted quote");
    const auto token=state().quote->token;
    auto confirm=Json::object();confirm["op"]="confirm";confirm["token"]="wrong";send(confirm);check(saved().wallet==20000,"MSC wrong quote token charged points");
    confirm["token"]=token;const auto purchased=send(confirm);check(saved().wallet==19750 && saved().equipped_tag=="Fresh","MSC tag purchase failed");
    auto purchase_retry=p.header(PacketKind::msc,now);purchase_retry.text=purchased.text;p.send(purchase_retry);run_for(1100000);check(saved().wallet==19750,"MSC duplicate request charged twice");
    item("tag","off");item("tag","Fresh");check(saved().wallet==19750 && saved().equipped_tag=="Fresh","MSC owned tag equip charged points");
    item("colour","red");check(state().quote.has_value(),"MSC standard colour quote missing");
    const auto replaced=state().quote->token;item("colour","cyan");confirm["token"]=replaced;send(confirm);check(saved().colour.empty(),"MSC replaced quote token confirmed another item");
    confirm["token"]=state().quote->token;send(confirm);check(saved().colour=="cyan" && saved().wallet==19700,"MSC colour purchase failed");
    auto pay=Json::object();pay["op"]="pay";pay["recipient"]=std::to_string(other);pay["amount"]=100U;
    const auto transfer=send(pay);check(saved().wallet==19600 && saved(other).wallet==20100,"MSC transfer did not use selected Steam ID");
    auto transfer_retry=p.header(PacketKind::msc,now);transfer_retry.text=transfer.text;p.send(transfer_retry);run_for(1100000);check(saved(other).wallet==20100,"MSC duplicate transfer paid twice");
    auto conflicting=Json::parse(transfer.text);conflicting["amount"]=999U;auto changed=p.header(PacketKind::msc,now);changed.text=conflicting.dump();p.send(changed);run_for(1100000);
    check(saved(other).wallet==20100,"MSC reused request ID executed different transfer contents");
    pay["amount"]=0U;send(pay);check(saved().wallet==19600,"MSC accepted zero transfer");
    item("joinmsg","Hello from MSC");check(state().quote.has_value(),"MSC join message set did not create quote");
    confirm["token"]=state().quote->token;send(confirm);check(saved().joinmsg_owned,"MSC join message purchase failed");
    const auto balance=saved().wallet;item("joinmsg","Updated message");check(saved().wallet==balance && saved().joinmsg=="Updated message","MSC owned join message edit was not free");
    item("colourbundle","");confirm["token"]=state().quote->token;send(confirm);const auto bundle_balance=saved().wallet;
    item("colour","gold");check(saved().wallet==bundle_balance && saved().colour=="gold","MSC bundle colour equip charged points");
    item("colour","shimmer-purple");confirm["token"]=state().quote->token;send(confirm);const auto shimmer_balance=saved().wallet;
    item("colour","default");item("colour","shimmer-purple");check(saved().wallet==shimmer_balance,"MSC owned shimmer equip charged points");
    const auto snapshot=state();const auto bundle=std::ranges::find_if(snapshot.products,[](const auto& c){return c.id=="shimmerbundle";});
    check(bundle!=snapshot.products.end() && bundle->price==6400,"MSC missing-shimmer discount is wrong");
    item("colour","shimmer-cyan");confirm["token"]=state().quote->token;send(confirm);
    const auto cyan_balance=saved().wallet;check(saved().colour=="shimmer-green" && std::ranges::count(saved().shimmers,"shimmer-green")==1,"Cyan shimmer alias did not preserve existing ownership identity");
    item("colour","default");item("colour","shimmer-green");check(saved().wallet==cyan_balance,"Existing cyan/green shimmer was repurchased");
    const auto cyan_catalogue=state();const auto cyan=std::ranges::find_if(cyan_catalogue.products,[](const auto& c){return c.id=="shimmer:shimmer-green";});
    check(cyan!=cyan_catalogue.products.end() && cyan->label=="Shimmer cyan" && std::ranges::count_if(cyan_catalogue.products,[](const auto& c){return c.id.starts_with("shimmer:");})==5,"Shimmer was duplicated or label still describes green");
    item("tag","admin");check(saved().equipped_tag=="Fresh","MSC reserved tag accepted");
    auto board=Json::object();board["op"]="leaderboard";board["kind"]="points";board["page"]=1U;send(board);check(state().leader_kind=="points" && !state().leaders.empty(),"MSC structured leaderboard missing");
    item("tag","Expiry");const auto expires=state().quote->token;run_for(121000000);confirm["token"]=expires;send(confirm);check(saved().equipped_tag=="Fresh","MSC expired quote purchased");
    check(q.msc_messages.empty(),"MSC leaked profile updates to a legacy client");
    // An ordinary chat command changes the same account and triggers structured refresh.
    p.say("/tag off",now);run_for(2200000);check(state().profile.tag.empty(),"MSC state did not refresh after chat equip");
    // A new transport session requires opt-in again; a queued old-session mutation cannot be replayed.
    const auto reconnect_wallet=saved().wallet;
    host.drop(player,"MSC reconnect test");run_for(100000);
    p.epoch+=1000;p.joined=false;p.receiver={};wire().closed.erase(player);p.hello(now);run_for(2000000);
    auto replay=p.header(PacketKind::msc,now);replay.text=transfer.text;p.send(replay);run_for(1100000);
    check(saved().wallet==reconnect_wallet,"MSC mutation executed before reconnect negotiation");
    p.say("/msc-ui hello 1",now);run_for(1100000);serial=0;
    check(state().epoch==p.epoch,"MSC reconnect snapshot retained old epoch");
    replay=p.header(PacketKind::msc,now);replay.text=transfer.text;p.send(replay);run_for(1100000);
    check(saved().wallet==reconnect_wallet,"MSC old-session mutation executed after reconnect negotiation");
    config.shop.enabled=false;run_for(1500000);check(!state().shop,"MSC shop disable did not refresh capabilities");
    config.shop.enabled=true;config.shop.tag_price=300;run_for(1500000);
    const auto catalog=state();const auto tag_product=std::ranges::find_if(catalog.products,[](const auto& c){return c.id=="tag";});
    check(tag_product!=catalog.products.end() && tag_product->price==300,"MSC configured price did not refresh");
    item("tag","NewPrice");const auto old_price=state().quote->token;config.shop.tag_price=400;confirm["token"]=old_price;const auto before_price=saved().wallet;send(confirm);
    check(saved().wallet==before_price && saved().equipped_tag.empty(),"MSC confirmed a changed-price quote");
    item("colour","red"); // Owned bundle equips freely; no pending quote remains.
    const auto sender_before=saved().wallet,recipient_before=saved(other).wallet;
    std::filesystem::create_directory(folder/"data/player-stats.json.tmp");
    pay["amount"]=10U;send(pay);
    check(saved().wallet==sender_before && saved(other).wallet==recipient_before && state().paused,"MSC failed transfer did not roll back and report paused storage");
    std::filesystem::remove(folder/"data/player-stats.json.tmp");
    host.stop("MSC interface test complete");std::filesystem::remove_all(folder);return failures;

}
