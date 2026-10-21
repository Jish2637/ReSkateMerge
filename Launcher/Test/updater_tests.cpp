#include "Launcher/updater.h"
#include "Engine/Core/Json/json.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "reskate_version.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
namespace fs=std::filesystem;
namespace update=dingosdk::launcher_update;
using dingosdk::Json;
namespace {
std::map<std::wstring,std::string> responses;
constexpr auto repo=L"https://api.github.com/repos/Jish2637/ReSkateMerge";
constexpr auto prefix=L"https://github.com/Jish2637/ReSkateMerge/releases/download/";
void check(bool ok,const char* reason) {if(!ok) throw std::runtime_error(reason);}
std::string read(const fs::path& path) {std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& path,std::string_view data) {fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<data;check(bool(output),"Fixture write failed");}
void get(const std::wstring& url,std::uint64_t limit,const std::function<void(const char*,DWORD)>& sink) {
    const auto found=responses.find(url);if(found==responses.end()) throw std::runtime_error("Fixture HTTP 404");
    check(found->second.size()<=limit,"Response exceeds declared size");
    sink(found->second.data(),static_cast<DWORD>(found->second.size()));
}
template<class Action> void rejects(Action action,const char* reason) {bool threw{};try {action();} catch(const std::exception&) {threw=true;}check(threw,reason);}
fs::path self() {std::wstring name(32768,L'\0');name.resize(GetModuleFileNameW(nullptr,name.data(),static_cast<DWORD>(name.size())));return name;}
Json release(const std::string& version,bool preview=false,bool draft=false) {
    Json value=Json::object();value["tag_name"]="v"+version;value["prerelease"]=preview;value["draft"]=draft;value["assets"]=Json::array();
    for(const auto name:{"launcher.json","ReSkate.dll","ReSkateLauncher.exe"}) {
        Json asset=Json::object();asset["name"]=name;asset["browser_download_url"]="https://github.com/Jish2637/ReSkateMerge/releases/download/v"+version+"/"+name;
        value["assets"].push_back(asset);
    }
    return value;
}
void self_fixture(const fs::path& root) {
    const auto target=root/"ReSkateLauncher.exe";
    const auto expected=root/"replacement.bin";
    update::RemoteFile file{"test",std::wstring(prefix)+L"vtest/ReSkateLauncher.exe",dingosdk::launcher::sha256_file(expected),fs::file_size(expected)};
    responses[file.url]=read(expected);update::set_test_get(get);
    check(update::launcher_update_needed(false,target,file),"Old running launcher not recognized");
    update::replace_running_launcher(target,file);
    check(update::file_matches(target,file),"Running launcher replacement not verified");
    check(!update::launcher_update_needed(false,target,file) && !update::launcher_update_needed(true,root/"absent.exe",file),"Self-update restart would loop");
}
void live(const fs::path& root) {
    // Only explicit --live runs use the network. All files stay in this fixture.
    const auto config=update::fetch_config();check(config.has_value(),"Anonymous release discovery failed");
    check(config->tag=="v"+std::string(dingosdk::reskate_version) && !config->prerelease,"Expected this build's stable release");
    for(const auto& pair:{std::pair{root/"ReSkate.dll",config->runtime},std::pair{root/"ReSkateLauncher.exe",config->launcher}}) {
        check(fs::exists(pair.first) && !update::file_matches(pair.first,pair.second),"Live fixture must start with an older client");
        update::replace_file(pair.first,pair.second);
        check(update::file_matches(pair.first,pair.second),"Live update hash mismatch");
    }
    const auto again=update::fetch_config();check(again.has_value(),"Second anonymous check failed");
    check(update::file_matches(root/"ReSkate.dll",again->runtime) && !update::launcher_update_needed(false,root/"ReSkateLauncher.exe",again->launcher),"Second check is not up to date");
    std::cout<<"Anonymous release discovery, older-client update and second up-to-date check passed\n";
}
}
int main(int argc,char** argv) {try {
    if(argc==4 && std::string_view(argv[1])=="--server-archive") {
        const fs::path archive=argv[2],folder=argv[3];fs::create_directories(folder);
        write(folder/"ReSkateServer.json","preserved server settings");write(folder/"data/player-stats.json","preserved wallet data");
        const auto expected=update::archive_entry_sha256(archive,"ReSkateServer.exe");
        update::install_archive(archive,folder);
        check(dingosdk::launcher::sha256_file(folder/"ReSkateServer.exe")==expected,"Server archive installed wrong executable");
        check(read(folder/"ReSkateServer.json")=="preserved server settings" && read(folder/"data/player-stats.json")=="preserved wallet data","Server update replaced user data");
        std::cout<<"Server archive installation, executable hash and existing data preservation passed\n";return 0;
    }
    if(argc==3 && std::string_view(argv[1])=="--self-fixture") {self_fixture(argv[2]);return 0;}
    if(argc==3 && std::string_view(argv[1])=="--live") {live(argv[2]);return 0;}
    check(argc==3,"Expected base manifest and fixture folder");const fs::path root=fs::path(argv[2])/std::to_wstring(GetCurrentProcessId());fs::create_directories(root);
    auto base=Json::parse(read(argv[1]));
    write(root/"launcher.bin","verified launcher replacement");write(root/"runtime.bin","verified runtime replacement");
    auto manifest=[&](const std::string& version) {
        auto value=base;
        for(const auto& pair:{std::pair{"launcher","launcher.bin"},std::pair{"runtime","runtime.bin"}}) {
            Json file=Json::object();file["version"]=version;file["url"]=std::string("asset:")+(pair.first==std::string_view("launcher")?"ReSkateLauncher.exe":"ReSkate.dll");
            file["sha256"]=dingosdk::launcher::sha256_file(root/pair.second);file["size"]=fs::file_size(root/pair.second);value[pair.first]=file;
        }
        return value;
    };
    const std::string stable="0.1.0-merged.6",preview="0.1.0-merged.7";
    Json list=Json::array();list.push_back(release("0.1.0-merged.8",false,true));list.push_back(release(preview,true));list.push_back(release(stable));
    responses[std::wstring(repo)+L"/releases?per_page=10"]=list.dump();
    responses[std::wstring(prefix)+L"v0.1.0-merged.6/launcher.json"]=manifest(stable).dump();
    responses[std::wstring(prefix)+L"v0.1.0-merged.7/launcher.json"]=manifest(preview).dump();
    update::set_test_get(get);
    check(update::binary_updates_enabled() && update::updates_allowed(true,false),"Merged build updates disabled");
    check(!update::updates_allowed(false,false) && !update::updates_allowed(true,true),"User update controls bypassed");
    check(update::releases_page()=="https://github.com/Jish2637/ReSkateMerge/releases","Wrong repository");
    const auto config=update::fetch_config();check(config && config->tag=="v"+stable && config->prerelease_available && !config->prerelease,"Stable release selection failed");
    const auto next=update::fetch_config(true);check(next && next->tag=="v"+preview && next->prerelease,"Preview selection failed");
    check(config->runtime.url==std::wstring(prefix)+L"v0.1.0-merged.6/ReSkate.dll","Asset resolution failed");
    write(root/"ReSkate.dll","old runtime");responses[config->runtime.url]=read(root/"runtime.bin");
    auto corrupt=config->runtime;corrupt.sha256=std::string(64,'0');
    rejects([&]{update::replace_file(root/"ReSkate.dll",corrupt);},"Corrupt update accepted");check(read(root/"ReSkate.dll")=="old runtime","Checksum failure replaced old binary");
    responses.erase(config->runtime.url);
    rejects([&]{update::replace_file(root/"ReSkate.dll",config->runtime);},"Missing download accepted");check(read(root/"ReSkate.dll")=="old runtime","Failed download replaced old binary");
    responses[config->runtime.url]=read(root/"runtime.bin");update::replace_file(root/"ReSkate.dll",config->runtime);check(update::file_matches(root/"ReSkate.dll",config->runtime),"Verified runtime replacement failed");
    auto foreign=manifest(stable);foreign["runtime"]["url"]="https://github.com/Dingo-Shenanigans/ReSkate/releases/download/v2.1.0/ReSkate.dll";
    rejects([&]{update::parse_config(foreign.dump());},"Upstream manifest binary accepted");
    auto wrong=config->runtime;wrong.url=L"https://github.com/Dingo-Shenanigans/ReSkate/releases/download/v2.1.0/ReSkate.dll";
    rejects([&]{update::replace_file(root/"ReSkate.dll",wrong);},"Upstream download accepted");
    auto broken=release(stable);broken["assets"][1]["browser_download_url"]="https://github.com/Dingo-Shenanigans/ReSkate/releases/download/v2.1.0/ReSkate.dll";
    responses[std::wstring(repo)+L"/releases?per_page=10"]=Json::array({broken}).dump();
    check(!update::fetch_config(),"Foreign resolved asset accepted");
    broken=release(stable);broken["tag_name"]="vwrong";
    responses[std::wstring(repo)+L"/releases?per_page=10"]=Json::array({broken}).dump();
    check(!update::fetch_config(),"Mismatched release tag accepted");
    auto missing=release(stable);missing["assets"]=Json::array();responses[std::wstring(repo)+L"/releases?per_page=10"]=Json::array({missing}).dump();responses.erase(std::wstring(repo)+L"/releases/latest");
    check(!update::fetch_config(),"Missing manifest fell back to upstream");
    auto server_manifest=manifest(stable);auto server=server_manifest.at("runtime");server["version"]="2.1.2";server["url"]="https://github.com/Jish2637/ReSkateMerge/releases/download/v0.1.3/ReSkateServer-v2.1.2-Windows-x64.zip";server["exe_sha256"]=server.at("sha256");server_manifest["server"]=server;
    check(update::parse_config(server_manifest.dump()).server.version=="2.1.2","Independent server version rejected");
    server_manifest["server"]["exe_sha256"]="invalid";rejects([&]{update::parse_config(server_manifest.dump());},"Invalid server executable hash accepted");
    const auto restart=root/"restart";fs::create_directories(restart);fs::copy_file(self(),restart/"ReSkateLauncher.exe");write(restart/"replacement.bin",read(root/"launcher.bin"));
    std::wstring command=L"\""+(restart/"ReSkateLauncher.exe").wstring()+L"\" --self-fixture \""+restart.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"Cannot start self-update fixture");
    const auto wait=WaitForSingleObject(child.hProcess,30000);DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);CloseHandle(child.hThread);check(wait==WAIT_OBJECT_0 && code==0,"Running self-update fixture failed");
    check(read(restart/"ReSkateLauncher.exe")==read(root/"launcher.bin"),"Self-update installed wrong bytes");update::remove_previous_launcher(restart/"ReSkateLauncher.exe");
    check(!fs::exists(restart/"ReSkateLauncher.exe.old"),"Previous launcher was not cleaned up");
    std::cout<<"Release selection, controls, pinned assets, failures and running self-update passed\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
