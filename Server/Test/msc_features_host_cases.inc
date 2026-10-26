// Structured games and staff through the real server dispatcher.
int run_msc_features_host() {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_features_host_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.map="San Vansterdam";config.wagers.enabled=true;
    config.player_stats.hourly_points=0;config.player_stats.daily_points=0;config.player_stats.leaderboard_minutes=0;
    config.player_stats.streak_step=0;config.object_spam_alerts.enabled=false;
    const std::uint64_t player=76561198000000801ULL,other=player+1;
    config.roles.players[other]="admin";config.roles.settle();save_config(config);
    {PlayerStatsStore seed;seed.open(folder/"data/player-stats.json");for(auto id:{player,other}){seed.joined(id,"Tester",1,std::time(nullptr));seed.players.at(id).wallet=20000;seed.players.at(id).points=100;seed.players.at(id).weekly_points=100;}seed.save();}
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
    // Old Hub clients omit the opt-in flag, even if they forge a Slots action.
    Json legacy=Json::object();legacy["op"]="games";
    send(p,legacy,false);check(state(p).games_supported && state(p).games.size()==3,"Legacy Hub catalogue changed");
    const auto legacy_balance=saved().wallet;
    legacy["op"]="game";legacy["game"]="slots";legacy["action"]="spin";legacy["stake"]=10U;legacy["round"]=0U;
    send(p,legacy,false);check(saved().wallet==legacy_balance && state(p).games.size()==3,"Non-opted-in client could spin Slots");
    read(p,"games");check(state(p).games_supported && state(p).games.size()==4,"Extended game catalogue missing");
    const auto base=saved().wallet,total=base+saved(other).wallet;
    const auto create=act(p,"coinflip","create");
    const auto announcements=std::ranges::count_if(q.chat,[](const auto& line){return line.find("opened a Coin Flip lobby")!=std::string::npos;});check(announcements==1,"UI lobby creation did not announce once to other players");const auto round=view(p,"coinflip").round;
    check(round && saved().wallet==base-10,"Coin Flip did not reserve exactly one stake");
    if(!round)return 1;
    retry(p,create);check(std::ranges::count_if(q.chat,[](const auto& line){return line.find("opened a Coin Flip lobby")!=std::string::npos;})==announcements,"Duplicate UI create repeated announcement");check(saved().wallet==base-10,"Duplicate create reserved twice");
    act(q,"coinflip","join",round->id);check(saved().wallet+saved(other).wallet==total-20 && saved(other).wallet==20000-10,"Join did not reserve real administrator funds");
    const auto resultChats=[&](const Game& guest,std::string_view name){return std::ranges::count_if(guest.chat,[&](const auto& line){return line.find(name)!=std::string::npos && line.find(" won ")!=std::string::npos;});};
    const auto settle=act(p,"coinflip","start",round->id);check(saved().wallet+saved(other).wallet==total,"Coin Flip settlement lost or created points");
    retry(p,settle);check(saved().wallet+saved(other).wallet==total,"Duplicate start paid twice");
    check(resultChats(p,"coinflip:")==0 && resultChats(q,"coinflip:")==0,"Coin Flip announced its result before the reveal");
    run_for(4000000);
    check(resultChats(p,"coinflip:")==1 && resultChats(q,"coinflip:")==1,"Coin Flip result was not announced once after the reveal");
    retry(p,settle);run_for(5500000);
    check(resultChats(q,"coinflip:")==1,"A duplicate settlement re-announced its result");
    check(!view(p,"coinflip").round && view(p,"coinflip").previous.has_value(),"Coin Flip result missing");
    act(p,"coinflip","create");const auto replacement=view(p,"coinflip").round->id;
    act(p,"coinflip","cancel",round->id);check(view(p,"coinflip").round->id==replacement,"Stale action cancelled a different round");
    act(p,"coinflip","cancel",replacement);check(saved().wallet+saved(other).wallet==total,"Cancellation failed to refund");
    act(p,"coinflip","create");const auto leave_round=view(p,"coinflip").round->id;
    act(q,"coinflip","join",leave_round);const auto leave=act(q,"coinflip","leave",leave_round);const auto after_leave=saved(other).wallet;
    retry(q,leave);check(saved(other).wallet==after_leave && saved().wallet+saved(other).wallet==total-10,"Leave refund repeated or failed");
    act(p,"coinflip","cancel",leave_round);check(saved().wallet+saved(other).wallet==total,"Creator cancellation after leave failed refund");
    act(p,"dice","create");const auto dice=view(p,"dice").round->id;act(q,"dice","join",dice);act(p,"dice","start",dice);
    check(resultChats(q,"dice:")==0,"Dice announced its result before the reveal");run_for(2000000);
    check(resultChats(q,"dice:")==0,"Dice announced during the reveal window");run_for(3000000);
    check(resultChats(q,"dice:")==1,"Dice result was not announced after the reveal");
    const auto outcome=view(p,"dice").previous;check(outcome && !outcome->rolls.empty(),"Dice faces missing from authoritative result");
    if(outcome)for(const auto& roll:outcome->rolls)check(roll.value>=1 && roll.value<=6,"Invalid authoritative dice face");
    check(saved().wallet+saved(other).wallet==total,"Dice settlement failed conservation");
    act(p,"highlow","create");const auto high=view(p,"highlow").round;check(high && high->initial>=1 && high->initial<=13,"High / Low initial card missing");
    read(q,"games");check(!view(q,"highlow").round,"Private High / Low round leaked");
    if(high){act(p,"highlow",high->initial<13?"higher":"lower",high->id);const auto result=view(p,"highlow").previous;check(result && result->second>=1 && result->second<=13 && !result->direction.empty(),"High / Low outcome metadata missing");}
    // Slots are playable only through the structured MSC interface. Every spin
    // is settled and saved before the response; its reels belong only to the caller.
    const auto chat_before=p.chat.size(),other_chat_before=q.chat.size();
    const auto pre_spin=saved().wallet;
    const auto spin_request=act(p,"slots","spin");
    const auto spun=view(p,"slots").previous;
    check(spun && spun->has_reels && spun->state=="settled" && spun->members.size()==1,"Slot spin was not durably settled with three reels");
    if(spun) {
        const auto reward=spun->my_payout;
        check(spun->multiplier==reward/10 && (reward==0 || reward==10 || reward==30 || reward==50 || reward==80 || reward==150 || reward==250),"Invalid slot payout");
        check(saved().wallet==pre_spin-10+reward,"Slot stake and payout did not balance");
        check(!view(p,"slots").round,"Slot spin unexpectedly opened a lobby");
        const auto post_spin=saved().wallet;
        retry(p,spin_request);check(saved().wallet==post_spin,"Duplicate slot request paid twice");
        act(p,"slots","spin");check(saved().wallet==post_spin,"Slot spin cooldown was bypassed");
        read(q,"games");check(!view(q,"slots").previous,"Slot reels leaked into another player's result");
        // Wins, when present, are advertised in the Hub only.
        if(reward>10)check(!view(q,"slots").wins.empty(),"Winning slots result missing from Hub announcements");
        check(p.chat.size()==chat_before && q.chat.size()==other_chat_before,"Slot spin sent chat output");
        run_for(4200000);act(p,"slots","spin");check(view(p,"slots").previous->id!=spun->id,"Subsequent slot spin failed after cooldown");
    }
    config.wagers.enabled=false;read(p,"games");check(!view(p,"dice").enabled && view(p,"dice").actions.empty(),"Disabled game exposed actions");
    const auto disabled_balance=saved().wallet;act(p,"dice","create");act(p,"slots","spin");check(saved().wallet==disabled_balance,"Disabled game charged stake");config.wagers.enabled=true;
    read(p,"staff");check(!state(p).staff && !state(p).staff_available,"Ordinary client received staff data");
    read(q,"staff");check(state(q).staff && state(q).staff_available,"Admin staff screen unavailable");
    if(!state(q).staff)return 1;
    check(state(q).staff->actions.size()>=130,"Admin tools missing");
    auto staff=[&](Game& g,const char* op,const char* action,Json args){auto j=Json::object();j["op"]=op;j["action"]=action;j["args"]=args;return send(g,j);};
    auto confirm=[&](Game& g,const std::string& token){auto j=Json::object();j["op"]="staff-confirm";j["token"]=token;return send(g,j);};
    // Quick actions submit this same action/Steam-ID pair through the bridge.
    // Verify the target receives the stock client's clear signal, not just chat.
    auto place=[&](Game& g,unsigned id){ObjectState objects;const std::vector<NetworkObject> layout={{id,"own_bk_rail_01",{1,2,3},{0,0,0,1},1}};objects.replace(layout);
        for(const auto& chunk:objects.updates(0)){auto packet=g.header(PacketKind::objects,now);packet.objects=chunk;g.send(packet);}run_for(1500000);};
    place(p,101);place(q,102);
    const auto player_clear=p.object_clears,admin_clear=q.object_clears;
    staff(p,"staff-preview","delobj.player",Json::array({std::to_string(other)}));
    check(q.object_clears==admin_clear,"Unprivileged quick action cleared another player");
    staff(q,"staff-preview","delobj.player",Json::array({std::to_string(other)}));
    check(state(q).staff->quote.has_value() && q.object_clears==admin_clear,"Self-clear preview changed objects or had no review");
    const auto self_clear_request=confirm(q,state(q).staff->quote->token);
    check(q.object_clears==admin_clear+1 && p.object_clears==player_clear,"Self-clear did not reach only the selected player's roster");
    retry(q,self_clear_request);check(q.object_clears==admin_clear+1,"Duplicate quick confirmation cleared objects twice");
    staff(q,"staff-preview","delobj.player",Json::array({std::to_string(player)}));
    confirm(q,state(q).staff->quote->token);
    check(p.object_clears==player_clear+1 && q.object_clears==admin_clear+1,"Other-player clear did not reach only its target");
    run_for(3000000);check(p.object_clears==player_clear+1 && q.object_clears==admin_clear+1,"Periodic roster repeated a local clear");
    host.command("clear-objects",other);run_for(1100000);
    check(p.object_clears==player_clear+2 && q.object_clears==admin_clear+2,"Global clear stopped working after a targeted clear");
    const auto original_voice=config.voice_chat;
    staff(q,"staff-preview","voice",Json::array({original_voice?"off":"on"}));check(config.voice_chat==original_voice && state(q).staff->quote.has_value(),"Staff preview changed settings");
    auto token=state(q).staff->quote->token;const auto confirmed=confirm(q,token);check(config.voice_chat!=original_voice,"Staff confirmation did not execute");
    retry(q,confirmed);check(config.voice_chat!=original_voice,"Duplicate staff action repeated");
    staff(q,"staff-preview","voice",Json::array({original_voice?"on":"off"}));token=state(q).staff->quote->token;
    staff(q,"staff-preview","voice",Json::array({original_voice?"off":"on"}));confirm(q,token);check(config.voice_chat!=original_voice,"Replaced staff review executed");
    token=state(q).staff->quote->token;run_for(61000000);confirm(q,token);check(!state(q).staff->quote,"Expired staff review remained actionable");
    staff(q,"staff-preview","voice",Json::array({"on\noff"}));check(response(q).message.find("refused")!=std::string::npos,"Multiline staff field accepted");
    staff(q,"staff-preview","quit",Json::array());check(response(q).message.find("refused")!=std::string::npos,"Console-only action accepted");
    config.roles.players[other]="moderator";config.roles.settle();read(q,"staff");
    const auto moderator=state(q);check(moderator.staff && std::ranges::find(moderator.staff->actions,"voice")==moderator.staff->actions.end(),"Moderator received server-wide permission");
    staff(q,"staff-preview","voice",Json::array({"on"}));check(config.voice_chat!=original_voice,"Moderator bypassed server settings permissions");
    config.roles.players[other]="admin";config.roles.settle();
    staff(q,"staff-preview","voice",Json::array({original_voice?"on":"off"}));token=state(q).staff->quote->token;
    config.roles.players[other]="moderator";config.roles.settle();confirm(q,token);check(config.voice_chat!=original_voice,"Revoked permission executed pending review");
    config.roles.players[other]="admin";config.roles.settle();
    config.roles.players[player]="admin";config.roles.settle();
    staff(q,"staff-preview","kick",Json::array({std::to_string(player),""}));token=state(q).staff->quote->token;confirm(q,token);
    check(!wire().closed.contains(player),"Staff action bypassed equal-rank protection");config.roles.players.erase(player);config.roles.settle();
    staff(q,"staff-preview","kick",Json::array({std::to_string(player),""}));token=state(q).staff->quote->token;
    host.drop(player,"Feature reconnect test");run_for(100000);p.epoch+=1000;p.joined=false;p.receiver={};wire().closed.erase(player);p.hello(now);run_for(2000000);
    confirm(q,token);check(response(q).message.find("reconnected or left")!=std::string::npos,"Staff confirmation crossed target session");
    // Inject an actual filesystem write failure at the transaction boundary.
    p.say("/msc-ui hello 1",now);run_for(1100000);
    Json reconnect=Json::object();reconnect["op"]="games";send(p,reconnect,false);
    check(state(p).games.size()==3,"Slots opt-in leaked across reconnect");
    read(p,"games");run_for(4200000);
    const auto stats_path=folder/"data/player-stats.json";
    PlayerStatsStore durable;durable.open(stats_path);
    const auto before_failure=durable.players.at(player).wallet;
    const auto ledger_size=durable.wagers.size(),next_id=durable.next_wager_id;
    writer->submit([]{}).get();
    std::filesystem::create_directory(stats_path.string()+".tmp");
    const auto failed_spin=act(p,"slots","spin");
    check(response(p).message.find("could not be saved")!=std::string::npos,"Slot save failure was not reported");
    check(state(p).paused && state(p).profile.balance==before_failure,"Slot save failure did not roll back and pause economy");
    std::filesystem::remove(stats_path.string()+".tmp");
    retry(p,failed_spin);act(p,"slots","spin");
    PlayerStatsStore unchanged;unchanged.open(stats_path);
    check(unchanged.players.at(player).wallet==before_failure && unchanged.wagers.size()==ledger_size && unchanged.next_wager_id==next_id,"Failed/retried slot save changed durable balance or ledger");
    host.stop("MSC feature tests complete");
    // A new Host must load the settled ledger and recover after a storage failure.
    wire()={};SteamTransport restarted_transport;
    Host restarted(config,restarted_transport,[](const auto&){},writer);
    check(restarted.start(error),"Slots restart failed: "+error);
    const auto restarted_invite=parse_invite(restarted.invite());if(!restarted_invite)return 1;
    Game fresh;fresh.id=player;fresh.epoch=p.epoch+1000;fresh.secret=restarted_invite->secret;fresh.map=map_hash(map_destination(config.map));fresh.name="Buyer";
    auto restart_run=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){if(fresh.joined)fresh.ready(now);restarted.tick(now);writer->submit([]{}).get();fresh.read();}};
    fresh.hello(now);restart_run(2000000);fresh.say("/msc-ui hello 1",now);restart_run(1100000);
    Json request=Json::object();request["v"]=1U;request["epoch"]=std::to_string(fresh.epoch);request["request"]=1U;request["slots_ui"]=true;request["op"]="games";
    auto packet=fresh.header(PacketKind::msc,now);packet.text=request.dump();fresh.send(packet);restart_run(1100000);
    const auto recovered=state(fresh);
    check(!recovered.paused && recovered.profile.balance==before_failure,"Host restart lost settled balance or remained paused");
    const auto result=view(fresh,"slots").previous;
    check(result && result->has_reels,"Host restart lost private Slots result");
    request["request"]=2U;request["op"]="game";request["game"]="slots";request["action"]="spin";request["stake"]=10U;request["round"]=0U;
    packet=fresh.header(PacketKind::msc,now);packet.text=request.dump();fresh.send(packet);restart_run(1100000);
    check(view(fresh,"slots").previous->id==next_id,"Slots did not recover after restart: "+state(fresh).message+" expected "+std::to_string(next_id)+" got "+std::to_string(view(fresh,"slots").previous->id));
    restarted.stop("Slots restart complete");std::filesystem::remove_all(folder);return failures;
}
