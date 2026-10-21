int run_build_sets_host(const std::filesystem::path &fixtures) {
    wire()={};const auto folder=std::filesystem::temp_directory_path()/"msc_build_sets_cases";
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    ServerConfig config;config.file=folder/"ReSkateServer.json";config.roles=default_roles();config.player_stats.enabled=false;
    config.map="San Vansterdam";config.mods=fixtures;config.object_limit=0;config.object_spam_alerts.enabled=false;
    config.park_mods={{"street","popular skate 2 street park",config.map,"bam"}};
    const auto data=folder/"data";const auto map=map_destination(config.map);
    const auto map_folder=build_set_folder(data,map);const auto file=map_folder/"plaza.build.json";
    const std::uint64_t admin=76561198000000701ULL,mod=admin+1,one=admin+2,two=admin+3,late=admin+4;
    config.roles.players[admin]="admin";config.roles.players[mod]="moderator";config.roles.settle();
    SteamTransport transport;std::vector<std::string> logs;Host host(config,transport,[&](const auto &s){logs.push_back(s);});
    std::string error;check(host.start(error),"Build sets host failed: "+error);
    std::map<std::uint64_t,Game> games;std::map<std::uint64_t,ObjectState> local;
    std::uint64_t now=1000000000,next_ready=now;
    auto run_for=[&](std::uint64_t us){for(const auto end=now+us;now<end;now+=50000){
        if(now>=next_ready){next_ready=now+1000000;for(auto &[id,g]:games)if(g.joined)g.ready(now);}
        host.tick(now);for(auto &[id,g]:games)g.read();
    }};
    auto connect=[&](auto id,const char *name){auto &g=games[id];g={};g.id=id;g.epoch=11000+id%100;g.name=name;g.secret=parse_invite(host.invite())->secret;
        g.map=map_hash(map_destination(config.map));g.hello(now);};
    connect(admin,"Build Admin");connect(mod,"Build Mod");connect(one,"Builder One");connect(two,"Builder Two");run_for(2000000);
    auto cmd=[&](std::string_view text,std::uint64_t actor=0){auto answer=host.command(text,actor);run_for(500000);if(answer.starts_with("Confirm ")){answer=host.command("build confirm",actor);run_for(500000);}return answer;};
    auto upload=[&](auto id,const std::vector<NetworkObject> &objects){auto &state=local[id];state.replace(objects);
        for(const auto &chunk:state.updates(0)){auto p=games[id].header(PacketKind::objects,now);p.objects=chunk;games[id].send(p);}run_for(1500000);};
    auto read=[&](const auto &path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    auto expect_failure=[&](auto action,const char *message){bool failed=false;try{action();}catch(...){failed=true;}check(failed,message);};
    check(!custom_command_name_free("build"),"Build command not reserved");
    check(cmd("build save nope",one).find("not an admin")!=std::string::npos && !std::filesystem::exists(map_folder),"Ordinary player saved build");
    check(cmd("build save empty",mod).find("No shared objects")!=std::string::npos,"Empty save accepted");
    for(const auto invalid:{"../escape","bad/name","a b","con","com1"}) {
        check(cmd(std::string("build load ")+invalid,mod).find("Loaded ")==std::string::npos,"Unsafe build name accepted");
    }
    check(cmd("build save "+std::string(33,'x'),mod).find("1-32")!=std::string::npos,"Oversize build name accepted");
    std::vector<NetworkObject> a={{1,"own_bk_rail_01",{1,2,3},{0,0,0,1},2.5f},{2,"own_bk_ramp_01",{2,0,0},{0,.38268343f,0,.92387953f},.5f}};
    std::vector<NetworkObject> b={{1,"own_bk_pad_01",{20,0,0},{0,0,0,1},1}};
    upload(one,a);upload(two,b);
    check(cmd("build save PLAZA",mod).starts_with("Saved plaza: 3"),"Moderator could not save shared builders");
    const auto original=read(file);auto saved=read_build_set(file,"plaza",map);
    check(saved.objects.size()==3 && std::ranges::any_of(saved.objects,[&](const auto &o){return o.item==a[0].item && o.scale==2.5f && o.position==a[0].position;}) &&
        std::ranges::any_of(saved.objects,[&](const auto &o){return o.rotation==a[1].rotation && o.scale==a[1].scale;}),"Snapshot lost transforms or overlapping owner IDs");
    check(cmd("build save plaza",admin).find("replace")!=std::string::npos && read(file)==original,"Save silently overwrote existing set");
    upload(two,{});check(cmd("build save plaza replace",admin).starts_with("Saved plaza: 2"),"Explicit overwrite failed");
    const auto previous=read(file);std::filesystem::create_directory(file.string()+".tmp");
    check(cmd("build save plaza replace",mod).find("Cannot write")!=std::string::npos && read(file)==previous,"Failed save destroyed prior set");
    std::filesystem::remove(file.string()+".tmp");
    check(host.command("build save plaza replace",mod).starts_with("Confirm ") && read(file)==previous,"Overwrite changed save before confirmation");
    check(host.command("build confirm",admin).starts_with("No pending"),"Another staff member confirmed someone else's action");
    check(host.command("build cancel",mod).starts_with("Build action cancelled") && host.command("build confirm",mod).starts_with("No pending"),"Cancellation did not clear draft");
    host.command("build save plaza replace",mod);run_for(61000000);
    check(!host.command("build confirm",mod).starts_with("Saved") && read(file)==previous,"Expired confirmation overwrote save");
    host.command("build save plaza replace",mod);upload(two,b);
    check(host.command("build confirm",mod).find("objects changed")!=std::string::npos && read(file)==previous,"Changed live snapshot was overwritten without a fresh approval");upload(two,{});
    // Stock chat routing is private; original objects are not cleared or replaced.
    config.object_spam_alerts.enabled=true;
    const auto public_lines=games[one].chat.size();run_for(2100000);games[mod].say("/build load PLAZA",now);run_for(2000000);
    check(has(games[mod].chat,"Loaded plaza: 2") && games[one].chat.size()==public_lines,"In-game build command failed/private reply leaked");
    check(games[admin].park.objects().size()==2 && games[admin].player_objects[one].objects().size()==2,"Load removed originals or failed ordinary decoder");
    const auto first_id=games[admin].park.objects().begin()->first;
    check(cmd("build load plaza",mod).find("already loaded")!=std::string::npos && games[admin].park.objects().size()==2,"Duplicate load accepted");
    check(cmd("build list",mod).find("plaza - 2 saved objects, loaded (2)")!=std::string::npos && cmd("modhelp builds",mod).find("/build unload")!=std::string::npos,"Build listing/help incomplete");
    connect(late,"Late Builder");run_for(3000000);check(games[late].park.objects().size()==2 && !games[late].refused,"Late client missed restored set");
    // Individual staff deletion modifies only the loaded copy, not its persisted source.
    const auto position=games[admin].park.objects().begin()->second.position;auto pose=games[mod].header(PacketKind::pose,now);pose.pose.root.position=position;pose.pose.skater.push_back(pose.pose.root);games[mod].send(pose);run_for(250000);
    cmd("delobj Builder One",mod);
    check(host.command("build unload plaza",mod).starts_with("Confirm ") && games[admin].park.objects().size()==2,"Unload did not require confirmation");
    check(cmd("nearobj rail",mod).find("Saved build: plaza")!=std::string::npos && cmd("delobj #1",mod).starts_with("Removed"),"Restored object cannot be identified/deleted");
    check(games[admin].park.objects().size()==1 && read_build_set(file,"plaza",map).objects.size()==2,"Individual deletion changed saved source or failed delivery");
    check(host.command("build confirm",mod).find("objects changed")!=std::string::npos && games[admin].park.objects().size()==1,"Stale unload removed edited live set");
    check(cmd("park-mod add street").find("spawned")!=std::string::npos,"Park load with saved build failed");run_for(2000000);
    check(games[admin].park.objects().size()==9 && !games[admin].park.objects().contains(first_id),"Publishing parks resurrected deleted build object");
    upload(two,b);
    check(cmd("build save expanded",mod).starts_with("Saved expanded: 10"),"Re-save missed restored pieces, parks or new additions");
    check(cmd("build unload plaza",mod).starts_with("Unloaded") && games[admin].park.objects().size()==8,"Unload removed park or retained build");
    cmd("park-mod clear");check(cmd("build load plaza",mod).starts_with("Loaded") && games[admin].park.objects().size()==2,"Saved set lost after unload");
    // Reject whole corrupt/wrong-map/invalid transforms, without changing live objects.
    auto corrupt=Json::parse(read(file));corrupt["map"]=map_destination("Isle of Grom");
    {std::ofstream out(map_folder/"wrong.build.json");corrupt["name"]="wrong";out<<corrupt.dump();}
    check(cmd("build load wrong",mod).find("does not match")!=std::string::npos && games[admin].park.objects().size()==2,"Wrong-map file affected live set");
    corrupt=Json::parse(read(file));corrupt["name"]="broken";corrupt["objects"][0]["scale"]=-1;
    {std::ofstream out(map_folder/"broken.build.json");out<<corrupt.dump();}
    check(cmd("build load broken",mod).find("invalid object")!=std::string::npos && games[admin].park.objects().size()==2,"Invalid build partially loaded");
    {std::ofstream out(map_folder/"bad.build.json");out<<"broken json";}
    check(cmd("build load bad",mod).starts_with("Build command failed") && cmd("build list",mod).find("unreadable/invalid")!=std::string::npos,"Corrupt build not handled");
    corrupt=Json::parse(read(file));corrupt["name"]="future";corrupt["version"]=4294967297ULL;
    {std::ofstream out(map_folder/"future.build.json");out<<corrupt.dump();}
    check(cmd("build load future",mod).find("Unsupported")!=std::string::npos && games[admin].park.objects().size()==2,"Unsupported version accepted or live state changed");
    BuildSet huge{"huge",map,{}};for(std::uint64_t i=1;i<=1024;++i)huge.objects.push_back({i,"own_bk_rail_01",{float(i),0,0}});
    write_build_set(data,huge,false,admin);check(cmd("build load huge",mod).find("capacity")!=std::string::npos && games[admin].park.objects().size()==2,"Combined load capacity not enforced");
    cmd("build unload plaza",mod);check(cmd("build load huge",mod).starts_with("Loaded"),"Exact 1024 object limit rejected");run_for(3000000);
    check(games[admin].park.objects().size()==1024 && !games[admin].refused,"1024-object multipart layout failed stock decoder");
    check(cmd("park-mod add street").find("1024")!=std::string::npos && games[admin].park.objects().size()==1024,"Park add exceeded shared server capacity");
    check(cmd("build save overflow",mod).find("at most 1024")!=std::string::npos && !std::filesystem::exists(map_folder/"overflow.build.json"),"Oversized combined snapshot saved/truncated");
    huge.objects.push_back({1025,"own_bk_rail_01",{}});expect_failure([&]{write_build_set(data,huge,true,admin);},"Oversize direct save accepted");
    cmd("build unload huge",mod);
    // A disconnected builder is not recovered from placement history.
    host.drop(two,"Left");games.erase(two);run_for(500000);check(cmd("build save departed",mod).find("No shared objects")!=std::string::npos,"Saved departed/cleared player objects");
    cmd("build load plaza",mod);host.command("build remove plaza",mod);cmd("map Isle of Grom");run_for(3000000);
    check(host.command("build confirm",mod).starts_with("No pending") && std::filesystem::exists(file),"Map transfer retained destructive confirmation");
    check(games[admin].park.objects().empty() && cmd("build list",mod).find("No saved builds")!=std::string::npos && cmd("build load plaza",mod).starts_with("Build command failed"),"Map switch kept loaded set or exposed other map's saves");
    cmd("map San Vansterdam");run_for(3000000);check(cmd("build list",mod).find("plaza")!=std::string::npos && cmd("build load plaza",mod).starts_with("Loaded"),"Returning map lost saves");
    check(has(logs,"[build] UTC") && has(logs,std::to_string(mod)) && !has(logs,"Possible object spam"),"Build audit missing or restore triggered player spam alerts");
    for(const auto name:{"page-a","page-b"}){auto page_set=read_build_set(file,"plaza",map);page_set.name=name;write_build_set(data,page_set,false,admin);}
    check(cmd("build list 2",mod).find("plaza")!=std::string::npos && cmd("build list 0",mod).starts_with("Usage:") && cmd("build list 9999",mod).find("page must be")!=std::string::npos,"Build list pagination/bounds failed");
    host.stop("Persistence check");wire()={};games.clear();local.clear();
    check(host.start(error),"Build restart failed");connect(mod,"Build Mod");run_for(2000000);
    check(games[mod].park.objects().empty() && cmd("build list",mod).find("loaded (")==std::string::npos && cmd("build load plaza",mod).starts_with("Loaded"),"Restart automatically loaded a set or lost save");
    auto keep=read_build_set(file,"plaza",map);keep.name="keep";write_build_set(data,keep,false,admin);cmd("build load keep",mod);
    check(cmd("build remove plaza",one).find("not an admin")!=std::string::npos && std::filesystem::exists(file),"Ordinary player removed saved build");
    run_for(2100000);games[mod].say("/build remove PLAZA",now);run_for(2000000);
    check(has(games[mod].chat,"Confirm removal of plaza") && std::filesystem::exists(file) && games[mod].park.objects().size()==4,"Remove changed save/layout before approval");
    run_for(2100000);games[mod].say("/build confirm",now);run_for(2000000);
    check(has(games[mod].chat,"Removed plaza:") && !std::filesystem::exists(file) && games[mod].park.objects().size()==2 && std::filesystem::exists(map_folder/"keep.build.json"),"Remove command failed to delete/unload named set or harmed another set");
    check(cmd("build load plaza",mod).starts_with("Build command failed") && cmd("build remove missing",mod).starts_with("No saved or loaded"),"Removed/missing build still available");
    check(cmd("build remove bad",mod).starts_with("Removed") && !std::filesystem::exists(map_folder/"bad.build.json"),"Bad JSON save could not be removed");
    check(cmd("build remove page-a",mod).starts_with("Removed") && games[mod].park.objects().size()==2,"Removing unloaded save changed live set");
    host.command("build remove page-b",mod);{std::ofstream out(map_folder/"page-b.build.json",std::ios::app);out<<'\n';}
    check(host.command("build confirm",mod).find("save changed")!=std::string::npos && std::filesystem::exists(map_folder/"page-b.build.json"),"Changed file was deleted using stale confirmation");
    host.command("build unload keep",mod);host.drop(mod,"Confirmation reconnect test");games.erase(mod);run_for(100000);connect(mod,"Build Mod");run_for(2000000);
    check(host.command("build confirm",mod).starts_with("No pending") && games[mod].park.objects().size()==2,"Reconnect retained destructive confirmation");
    auto blocked=keep;blocked.name="blocked";write_build_set(data,blocked,false,mod);cmd("build load blocked",mod);
    const auto blocked_file=map_folder/"blocked.build.json",backup=map_folder/"blocked.backup";
    std::filesystem::rename(blocked_file,backup);std::filesystem::create_directory(blocked_file);
    check(cmd("build remove blocked",mod).starts_with("Build command failed") && games[mod].park.objects().size()==4 && std::filesystem::exists(backup),"Failed file deletion unloaded live set or removed directory");
    std::filesystem::remove(blocked_file);std::filesystem::rename(backup,blocked_file);cmd("build remove blocked",mod);
    auto lost=keep;lost.name="lost";write_build_set(data,lost,false,mod);cmd("build load lost",mod);std::filesystem::remove(map_folder/"lost.build.json");
    check(cmd("build remove lost",mod).starts_with("Removed") && games[mod].park.objects().size()==2,"Loaded build with missing save could not be removed");
    auto other_map=keep;other_map.map=map_destination("Isle of Grom");write_build_set(data,other_map,false,admin);
    cmd("build remove keep",mod);check(games[mod].park.objects().empty() && std::filesystem::exists(build_set_folder(data,other_map.map)/"keep.build.json"),"Removal affected same-name build on another map");
    check(cmd("modhelp builds",mod).find("/build remove")!=std::string::npos && has(logs,"removed save plaza"),"Remove command missing help/audit");
    host.stop("Removal persistence check");wire()={};games.clear();check(host.start(error),"Removal restart failed");connect(mod,"Build Mod");run_for(2000000);
    check(cmd("build load plaza",mod).starts_with("Build command failed") && !std::filesystem::exists(file),"Removed build reappeared after restart");
    host.stop("Done");std::filesystem::remove_all(folder);return failures?1:0;
}
