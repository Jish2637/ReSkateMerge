#include "server_host.h"
#include "server_text.h"
#include "build_sets.h"
#include "Engine/Game/World/world_names.h"
#include "Engine/Core/Json/json.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace dingosdk::server {
namespace fs=std::filesystem;
namespace {
constexpr std::size_t max_build_bytes=2*1024*1024;
constexpr std::string_view suffix=".build.json";
void validate(const BuildSet &set) {
    if(!valid_build_name(set.name))throw std::runtime_error("Use 1-32 letters, numbers, hyphens or underscores for the build name.");
    if(set.map.empty() || !valid_map_destination(set.map))throw std::runtime_error("Build does not identify a map.");
    if(set.objects.empty())throw std::runtime_error("No shared objects to save or load.");
    if(set.objects.size()>max_owned_objects)throw std::runtime_error("A build can contain at most 1024 objects; nothing was saved or loaded.");
    for(auto object:set.objects){object.id=1;if(!valid_network_object(object))throw std::runtime_error("Build contains an invalid object; nothing was saved or loaded.");}
}
template<std::size_t N> void floats(const Json &j,std::array<float,N> &out) {
    if(!j.is_array() || j.size()!=N)throw std::runtime_error("Invalid build transform.");
    for(std::size_t i=0;i<N;++i){if(!j.at(i).is_number())throw std::runtime_error("Invalid build transform.");out[i]=j.at(i).get<float>();}
}
Json numbers(const auto &values) {auto j=Json::array();for(auto v:values)j.push_back(v);return j;}
std::string file_stamp(const fs::path &file) {
    std::error_code error;const auto status=fs::symlink_status(file,error);
    if(error && error!=std::errc::no_such_file_or_directory)throw fs::filesystem_error("Cannot inspect build save",file,error);
    if(!fs::exists(status))return "missing";
    if(!fs::is_regular_file(status))throw std::runtime_error("Saved build path is not a regular file; nothing changed.");
    const auto size=fs::file_size(file);const auto time=fs::last_write_time(file).time_since_epoch().count();
    std::ifstream in(file,std::ios::binary);std::uint64_t hash=14695981039346656037ULL;
    // Native saves are bounded at 2 MB. Still permit removing oversized corrupt files.
    for(std::uintmax_t i=0;i<std::min<std::uintmax_t>(size,max_build_bytes);++i) {
        char c;if(!in.get(c))throw std::runtime_error("Cannot read build save; nothing changed.");
        hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;
    }
    return std::to_string(size)+":"+std::to_string(time)+":"+std::to_string(hash);
}
}
bool valid_build_name(std::string_view name) {
    if(name.empty() || name.size()>32)return false;
    if(name=="con" || name=="prn" || name=="aux" || name=="nul" ||
       (name.size()==4 && (name.starts_with("com") || name.starts_with("lpt")) && name[3]>='1' && name[3]<='9'))return false;
    return std::ranges::all_of(name,[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
}
fs::path build_set_folder(const fs::path &data,std::string_view map) {
    return data/"builds"/std::to_string(map_hash(map));
}
BuildSet read_build_set(const fs::path &file,std::string_view name,std::string_view map) {
    if(!valid_build_name(name))throw std::runtime_error("Invalid build name.");
    if(!fs::is_regular_file(file))throw std::runtime_error("No saved build called "+std::string(name)+" on this map.");
    const auto size=fs::file_size(file);
    if(size>max_build_bytes)throw std::runtime_error("Build file exceeds 2 MB.");
    std::ifstream in(file,std::ios::binary);std::string text(static_cast<std::size_t>(size),'\0');
    if(!in.read(text.data(),static_cast<std::streamsize>(text.size())))throw std::runtime_error("Cannot read build file.");
    const auto j=Json::parse(text);
    if(j.at("format").get<std::string>()!="msc-build-set" || !j.at("version").is_number_integer() || j.at("version").get<double>()!=1)
        throw std::runtime_error("Unsupported build file format/version.");
    BuildSet set;set.name=j.at("name").get<std::string>();set.map=j.at("map").get<std::string>();
    if(set.name!=name || set.map!=map)throw std::runtime_error("Saved build name/map does not match the current map.");
    const auto &list=j.at("objects");if(!list.is_array() || list.size()>max_owned_objects)throw std::runtime_error("Invalid build object count (maximum 1024).");
    for(const auto &entry:list) {
        NetworkObject object;object.id=set.objects.size()+1;object.item=entry.at("item").get<std::string>();
        floats(entry.at("position"),object.position);floats(entry.at("rotation"),object.rotation);
        if(!entry.at("scale").is_number())throw std::runtime_error("Invalid build scale.");
        object.scale=entry.at("scale").get<float>();set.objects.push_back(std::move(object));
    }
    validate(set);return set;
}
void write_build_set(const fs::path &data,const BuildSet &set,bool replace,std::uint64_t actor) {
    validate(set);const auto folder=build_set_folder(data,set.map);
    fs::create_directories(folder);const auto file=folder/(set.name+std::string(suffix));
    if(!replace && fs::exists(file))throw std::runtime_error("That save exists. Use /build save "+set.name+" replace to overwrite it.");
    auto j=Json::object();j["format"]="msc-build-set";j["version"]=1;j["name"]=set.name;j["map"]=set.map;
    j["saved_utc"]=static_cast<std::int64_t>(std::time(nullptr));j["saved_by"]=std::to_string(actor);j["objects"]=Json::array();
    for(const auto &o:set.objects){auto row=Json::object();row["item"]=o.item;row["position"]=numbers(o.position);row["rotation"]=numbers(o.rotation);row["scale"]=o.scale;j["objects"].push_back(std::move(row));}
    const auto text=j.dump(2);if(text.size()>max_build_bytes)throw std::runtime_error("Build file exceeds 2 MB; previous save unchanged.");
    auto temp=file;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out || !out.write(text.data(),text.size()) || !out.flush())throw std::runtime_error("Cannot write build save; previous save unchanged.");}
#ifdef _WIN32
    const DWORD flags=MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0);
    if(!MoveFileExW(temp.c_str(),file.c_str(),flags))throw std::runtime_error("Cannot commit build save; previous save unchanged.");
#else
    if(replace)fs::rename(temp,file);
    else {fs::create_hard_link(temp,file);fs::remove(temp);}
#endif
}
std::string Host::build_command(std::string_view argument,std::uint64_t actor,bool confirmed) {
    const auto [first,rest]=split(argument);const auto verb=lower(first);
    const std::string usage="/build save <name> [replace] | /build load <name> | /build unload <name> | /build remove <name> | /build list [page]\n/build confirm | /build cancel: removal, unloading and overwrites require confirmation within 60s.\nLoading adds server-owned objects; originals remain and may overlap.";
    if(verb.empty() || verb=="help")return usage;
    const auto map=map_destination(config_.map);const auto data=config_.file.parent_path()/"data";
    const auto folder=build_set_folder(data,map);
    const auto loaded=[&](std::string_view name){return std::ranges::find_if(builds_loaded_,[&](const auto &b){return b.name==name;});};
    const auto capture=[&] {
        std::vector<NetworkObject> objects;
        for(const auto &[id,g]:guests_)if(g->handshaken && g->world_ready){const auto layout=g->shared.layout();objects.insert(objects.end(),layout.begin(),layout.end());}
        const auto server=park_objects_.layout();objects.insert(objects.end(),server.begin(),server.end());return objects;
    };
    const auto session=[&]{const auto *g=actor?find(actor):nullptr;return g?g->member.epoch:actor?std::uint64_t{}:epoch_;};
    const auto propose=[&](const std::string &command,const std::string &name,const std::string &summary,std::vector<NetworkObject> snapshot) {
        BuildConfirmation pending;pending.command=command;pending.name=name;pending.world=world_;pending.epoch=session();pending.until=now_+60000000;
        pending.file_stamp=file_stamp(folder/(name+std::string(suffix)));pending.snapshot=std::move(snapshot);
        build_confirmations_[actor]=std::move(pending);
        return "Confirm "+summary+"\n/build confirm within 60s, or /build cancel. Nothing changed yet.";
    };
    const auto audit=[&](std::string_view action,std::string_view name,std::size_t count){
        const auto *g=actor?find(actor):nullptr;
        log_("[build] UTC "+std::to_string(std::time(nullptr))+" "+(g?guest_name(*g):actor?std::to_string(actor):std::string("console"))+" ("+std::to_string(actor)+") "+std::string(action)+" "+std::string(name)+"; "+std::to_string(count)+" objects; map "+map);
    };
    try {
        if(verb=="cancel") {
            if(!rest.empty())return "Usage: /build cancel";
            return build_confirmations_.erase(actor)?"Build action cancelled. Nothing changed.":"No pending build action.";
        }
        if(verb=="confirm") {
            if(!rest.empty())return "Usage: /build confirm";
            const auto at=build_confirmations_.find(actor);
            if(at==build_confirmations_.end())return "No pending build action. Run the original command first.";
            auto pending=std::move(at->second);build_confirmations_.erase(at);
            if(now_>=pending.until || pending.world!=world_ || pending.epoch!=session())return "Build confirmation expired or session changed. Run the original command again.";
            if(pending.file_stamp!=file_stamp(folder/(pending.name+std::string(suffix))))return "Build save changed. Nothing changed; run the original command again.";
            std::vector<NetworkObject> snapshot;
            if(pending.command.starts_with("save "))snapshot=capture();
            else if(const auto up=loaded(pending.name);up!=builds_loaded_.end())snapshot=up->objects;
            if(snapshot!=pending.snapshot)return "Build objects changed. Nothing changed; run the original command again.";
            return build_command(pending.command,actor,true);
        }
        if(verb=="list") {
            const auto page=rest.empty()?std::optional<std::uint64_t>{1}:number(rest);
            if(!page || !*page)return "Usage: /build list [positive page number]";
            std::vector<std::string> names;
            if(fs::exists(folder))for(const auto &entry:fs::directory_iterator(folder)) {
                const auto filename=entry.path().filename().string();
                if(entry.is_regular_file() && filename.ends_with(suffix)){
                    auto name=filename.substr(0,filename.size()-suffix.size());if(valid_build_name(name))names.push_back(std::move(name));
                }
            }
            std::ranges::sort(names);const auto pages=std::max<std::size_t>(1,(names.size()+5)/6);
            if(*page>pages)return "Build list page must be 1-"+std::to_string(pages)+".";
            std::string out="Saved builds on "+map_name()+" | page "+std::to_string(*page)+"/"+std::to_string(pages);
            if(names.empty())out+="\nNo saved builds on this map.";
            for(auto i=(*page-1)*6;i<std::min<std::size_t>(*page*6,names.size());++i) {
                out+="\n"+names[i];
                try {const auto set=read_build_set(folder/(names[i]+std::string(suffix)),names[i],map);out+=" - "+std::to_string(set.objects.size())+" saved objects";}
                catch(const std::exception &){out+=" - unreadable/invalid save";}
                if(const auto up=loaded(names[i]);up!=builds_loaded_.end())out+=", loaded ("+std::to_string(up->objects.size())+")";
            }
            return out+"\n/build load <name> | /build save <name> [replace] | /build unload <name> | /build remove <name>\n/build confirm | /build cancel";
        }
        const auto [word,tail]=split(rest);const auto name=lower(word);
        if(!valid_build_name(name))return "Use a build name of 1-32 letters, numbers, hyphens or underscores.";
        if(verb=="save") {
            if(!tail.empty() && lower(tail)!="replace")return usage;
            BuildSet set{name,map,capture()};validate(set);
            if(!confirmed && !tail.empty() && fs::exists(folder/(name+std::string(suffix))))
                return propose("save "+name+" replace",name,"overwrite "+name+" on "+map_name()+" with "+std::to_string(set.objects.size())+" shared objects.",set.objects);
            write_build_set(data,set,!tail.empty(),actor);audit("saved",name,set.objects.size());
            return "Saved "+name+": "+std::to_string(set.objects.size())+" objects on "+map_name()+". Loading adds a copy; existing objects remain.";
        }
        if(!tail.empty())return usage;
        if(verb=="remove") {
            const auto file=folder/(name+std::string(suffix));std::error_code inspect_error;
            const auto status=fs::symlink_status(file,inspect_error);
            if(inspect_error && inspect_error!=std::errc::no_such_file_or_directory)throw fs::filesystem_error("Cannot inspect saved build",file,inspect_error);
            const bool exists=fs::exists(status);const auto up=loaded(name);
            if(!exists && up==builds_loaded_.end())return "No saved or loaded build called "+name+" on this map.";
            if(!confirmed)return propose("remove "+name,name,"removal of "+name+" on "+map_name()+": delete its save and unload "+std::to_string(up==builds_loaded_.end()?0:up->objects.size())+" objects.",up==builds_loaded_.end()?std::vector<NetworkObject>{}:up->objects);
            // Delete exactly this file, never a directory or a symlink. Bad JSON is removable.
            // File errors leave the live set alone, so removal cannot report a partial success.
            std::size_t saved_count{};
            if(exists) {
                if(!fs::is_regular_file(status))throw std::runtime_error("Saved build path is not a regular file; nothing removed.");
                try {saved_count=read_build_set(file,name,map).objects.size();}catch(const std::exception &){}
                fs::remove(file);
                audit("removed save",name,saved_count);
            }
            std::size_t count{};
            if(up!=builds_loaded_.end()) {
                count=up->objects.size();builds_loaded_.erase(up);publish_parks();audit("unloaded (save removed)",name,count);
            }
            return "Removed "+name+": save deleted; "+std::to_string(count)+" loaded objects removed. Other builds and player objects kept.";
        }
        if(verb=="load") {
            if(loaded(name)!=builds_loaded_.end())return "That build is already loaded. /build unload "+name+" first.";
            auto set=read_build_set(folder/(name+std::string(suffix)),name,map);
            if(park_objects_.objects().size()+set.objects.size()>max_owned_objects)return "Not enough server object capacity: loaded builds and parks together may contain at most 1024 objects. Nothing loaded.";
            if(park_object_ids_>std::numeric_limits<std::uint64_t>::max()-set.objects.size())return "Server object ID range exhausted. Nothing loaded.";
            SpawnedBuild build;build.name=name;build.objects=std::move(set.objects);
            auto next=park_object_ids_;for(auto &o:build.objects)o.id=++next;
            const auto count=build.objects.size();builds_loaded_.push_back(std::move(build));park_object_ids_=next;publish_parks();audit("loaded",name,count);
            return "Loaded "+name+": "+std::to_string(count)+" objects. Existing objects kept; restored objects are server-owned.";
        }
        if(verb=="unload") {
            const auto up=loaded(name);if(up==builds_loaded_.end())return "That build is not loaded on this map.";
            if(!confirmed)return propose("unload "+name,name,"unload of "+name+" on "+map_name()+": remove "+std::to_string(up->objects.size())+" objects; keep the save.",up->objects);
            const auto count=up->objects.size();builds_loaded_.erase(up);publish_parks();audit("unloaded",name,count);
            return "Unloaded "+name+": "+std::to_string(count)+" objects removed. Save retained.";
        }
        return usage;
    } catch(const std::exception &e){return std::string("Build command failed: ")+e.what();}
}
} // namespace dingosdk::server
