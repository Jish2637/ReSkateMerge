#include "trick_editor.h"
#include "Engine/Core/Platform/path_text.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <map>

namespace dingosdk::trick_terminology::editor {
namespace {
struct Io { std::mutex mutex, writing;std::map<std::string,std::uint64_t> latest;std::string result;unsigned pending{}; };
Io& io() {static auto* value=new Io;return *value;}
struct State {
    bool loaded{}, dirty{};
    std::uint64_t due{}, next_id{1}, revision{}, write_serial{}, next_live_view{};
    std::filesystem::path directory;
    View view;
};
State& state() {static State value;return value;}
std::filesystem::path directory() {
    // Same native Trainer directory and Windows folder lookup as trainer.cpp.
    PWSTR raw{};std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)) && raw)
        result=std::filesystem::path(raw)/L"ReSkate"/L"trainer";
    if (raw) CoTaskMemFree(raw);return result;
}
void publish() {feed().store(std::make_shared<const View>(state().view));}
void load() {
    auto& s=state();s.loaded=true;s.directory=directory();s.view.ready=true;
    if (s.directory.empty()) {s.view.io="Could not locate the native Trainer folder.";publish();return;}
    try {
        std::ifstream file(s.directory/L"trick-terminology-editor.json",std::ios::binary);
        if (file) {
            const std::string text((std::istreambuf_iterator<char>(file)),{});
            if (text.size()>1024*1024) throw std::runtime_error("Rule file is too large.");
            const auto document=Json::parse(text);auto loaded=decode_rules(document);auto settings=decode_detection(document);
            s.view.rules=std::move(loaded);s.view.detection=settings;
            for (const auto& r:s.view.rules) s.next_id=std::max(s.next_id,r.id+1);
            s.view.io="Loaded saved editor rules.";
        } else s.view.io="Empty editor. Load the current setup preset when wanted.";
    } catch (const std::exception& e) {s.view.io=std::string("Could not load rules: ")+e.what();}
    s.view.revision=++s.revision;publish();
}
void write_async(const std::string& filename,const std::string& text) {
    auto& s=state();
    if (s.directory.empty()) throw std::runtime_error("Native Trainer folder is unavailable.");
    const auto serial=++s.write_serial;const auto target=s.directory/std::filesystem::path(filename);
    {std::lock_guard lock(io().mutex);io().latest[filename]=serial;++io().pending;}
    // Trainer's async snapshot/write/rename pattern. A generation check prevents
    // a delayed older thread from replacing a later save of the same file.
    std::thread([filename,target,text,serial] {
        std::lock_guard writing(io().writing);
        {std::lock_guard lock(io().mutex);if (io().latest[filename]!=serial) {--io().pending;return;}}
        std::string result;
        try {
            std::filesystem::create_directories(target.parent_path());
            auto temporary=target;temporary+=L".tmp";
            {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file<<text;
                if (!file) throw std::runtime_error("Could not write rules.");}
            if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Could not replace rule file.");
            result="Wrote "+path_utf8(target);
        } catch (const std::exception& e) {result=std::string("Save/export failed: ")+e.what();}
        {std::lock_guard lock(io().mutex);--io().pending;if (io().latest[filename]==serial) io().result=std::move(result);}
    }).detach();
    s.view.io="Writing "+path_utf8(target)+"…";
}
Json snapshot() {auto j=encode_rules(state().view.rules);j["detection"]=encode_detection(state().view.detection);return j;}
void changed() {
    auto& s=state();alphabetize(s.view.rules);s.view.revision=++s.revision;s.dirty=true;s.due=GetTickCount64()+1500;publish();
}
std::uint64_t parse_id(const std::string& text) {
    std::size_t used{};const auto id=std::stoull(text,&used);if (used!=text.size()) throw std::runtime_error("Invalid rule id.");return id;
}
}
void set_native_names(std::map<std::string,std::string> names) {auto& s=state();s.view.native_names=std::move(names);s.view.revision=++s.revision;publish();}
void set_enabled(bool enabled) {auto& s=state();if (s.view.terminology_enabled!=enabled) {s.view.terminology_enabled=enabled;publish();}}
void service(std::uint64_t now) {
    auto& s=state();if (!s.loaded) load();
    if (s.dirty && now>=s.due) {
        s.dirty=false;
        try {write_async("trick-terminology-editor.json",snapshot().dump(2));}
        catch (const std::exception& e) {s.view.io=e.what();}
        publish();
    }
    std::string result;
    {std::unique_lock lock(io().mutex,std::try_to_lock);if (lock.owns_lock()) result.swap(io().result);}
    if (!result.empty()) {s.view.io=std::move(result);publish();}
}
const std::vector<Rule>& rules() {return state().view.rules;}
const DetectionSettings& detection() {return state().view.detection;}
std::string execute_command(const std::vector<std::string>& args) {
    try {
        service(GetTickCount64());auto& s=state();
        if (args.empty()) return "error: terminology new|preset|upsert|toggle|delete|save|reload|export";
        const auto& op=args[0];
        if (op=="early-spin" && args.size()==3) {
            if (args[1]!="on" && args[1]!="off") throw std::runtime_error("Choose on or off.");
            const auto lead=parse_id(args[2]);if (lead>45) throw std::runtime_error("Spin lead must be 0 to 45 degrees.");
            s.view.detection.early_spin=args[1]=="on";s.view.detection.lead_degrees=static_cast<int>(lead);changed();return "Global spin recognition updated.";
        }
        if (op=="new") {s.view.rules.clear();s.next_id=1;s.view.io="Empty workspace. Current setup remains available as a preset.";changed();return "Started an empty rule list.";}
        if (op=="preset") {s.view.rules=current_preset();s.next_id=45;s.view.io="Loaded current setup preset (44 rules).";changed();return s.view.io;}
        if (op=="upsert" && args.size()==2) {
            auto rule=decode_rule(Json::parse(unhex(args[1])));
            auto found=std::find_if(s.view.rules.begin(),s.view.rules.end(),[&](const Rule& r){return r.id==rule.id;});
            if (rule.id && found==s.view.rules.end()) throw std::runtime_error("This rule was removed. Create a new rule instead.");
            for (const auto& r:s.view.rules) if (r.id!=rule.id && r.enabled && rule.enabled && conflicting_conditions(r,rule))
                throw std::runtime_error("An enabled rule overlaps these stances with equal priority. Narrow the selection or disable that rule.");
            if (found==s.view.rules.end()) {
                if (s.view.rules.size()>=256) throw std::runtime_error("The editor supports 256 rules.");
                rule.id=s.next_id++;s.view.rules.push_back(std::move(rule));
            } else *found=std::move(rule);
            changed();return "Rule applied. Saving locally.";
        }
        if ((op=="toggle" || op=="delete") && args.size()==2) {
            const auto id=parse_id(args[1]);auto found=std::find_if(s.view.rules.begin(),s.view.rules.end(),[&](const Rule& r){return r.id==id;});
            if (found==s.view.rules.end()) throw std::runtime_error("Rule no longer exists.");
            if (op=="delete") s.view.rules.erase(found);
            else {
                if (!found->enabled) for (const auto& r:s.view.rules)
                    if (r.id!=id && r.enabled && conflicting_conditions(r,*found)) throw std::runtime_error("Another enabled rule overlaps these stances with equal priority.");
                found->enabled=!found->enabled;
            }
            changed();return op=="delete"?"Rule deleted.":"Rule updated.";
        }
        if (op=="save") {
            write_async("trick-terminology-editor.json",snapshot().dump(2));s.dirty=false;publish();return "Saving the rule list.";
        }
        if (op=="reload") {
            {std::lock_guard lock(io().mutex);if (io().pending) return "error: A save is in progress. Reload after it finishes.";}
            if (s.dirty) return "error: Save pending changes before reloading.";
            const auto previous=s.view.rules;
            try {
                std::ifstream file(s.directory/L"trick-terminology-editor.json",std::ios::binary);
                if (!file) throw std::runtime_error("No saved rule file yet.");
                const std::string text((std::istreambuf_iterator<char>(file)),{});
                if (text.size()>1024*1024) throw std::runtime_error("Rule file is too large.");
                const auto document=Json::parse(text);auto loaded=decode_rules(document);auto settings=decode_detection(document);s.view.rules=std::move(loaded);s.view.detection=settings;
                for (const auto& r:s.view.rules) s.next_id=std::max(s.next_id,r.id+1);
                s.view.revision=++s.revision;publish();return "Reloaded saved rules.";
            } catch (...) {s.view.rules=previous;throw;}
        }
        if (op=="export") {
            const auto filename="trick-terminology-export-"+std::to_string(GetTickCount64())+".json";
            s.view.export_path=path_utf8(s.directory/filename);
            write_async(filename,snapshot().dump(2));publish();return "Export queued: "+s.view.export_path;
        }
        return "error: Unknown editor command.";
    } catch (const std::exception& e) {return std::string("error: ")+e.what();}
}
std::string command(const std::vector<std::string>& args) {
    const auto result=execute_command(args);state().view.message=result;publish();return result;
}
void observe(const std::vector<Observation>& frame,const std::string& status,std::uint64_t scans) {
    auto& s=state();const Observation* latest{};
    for (const auto& row:frame) {
        const auto base=normalized(row.labels[0]);
        if (base.empty() || base=="revert" || base=="powerslide" || base=="manual" || base=="nosemanual" || base=="pumping" || base=="slam") continue;
        if (!latest || row.first_seen>latest->first_seen || (row.first_seen==latest->first_seen && row.handle>latest->handle)) latest=&row;
    }
    bool update=s.view.status!=status || (s.view.boundary_scans!=scans && GetTickCount64()>=s.next_live_view);
    s.view.status=status;s.view.boundary_scans=scans;
    if (latest) {
        s.view.last=*latest;
        const auto text=std::string(stance_name(stance_index(latest->labels)))+" · "+describe(component(latest->labels))+
            (latest->labels[2].empty()?"":" · "+latest->direction+" "+latest->labels[2]+" body spin");
        const auto choices=resolve_frame(s.view.rules,frame);
        auto result=choices[static_cast<std::size_t>(latest-frame.data())];
        bool secondary{};
        for(unsigned i=0;i<choices.size();++i)if(choices[i].rule && latest->handle!=frame[i].handle) {
            const auto claimed=claimed_handles(choices[i],frame[i].handle,frame);
            if(std::find(claimed.begin(),claimed.end(),latest->handle)!=claimed.end()) {result=choices[i];secondary=true;break;}
        }
        const auto name=result.rule?result.rule->name:result.conflict?"Conflicting rules":"No matching rule";
        std::string output=text;
        if (result.rule && secondary) output="Included in "+result.rule->name;
        else if (result.rule) {
            const auto shown=display(*result.rule,latest->labels);
            output=describe(component(shown));
            if (!shown[3].empty()) output=shown[3]+" "+output;
            if (!shown[6].empty()) output=shown[6]+" "+output;
            if (!shown[2].empty()) output=shown[2]+" "+output;
        }
        if(result.rule && has_spin_only(*result.rule)) {
            for(unsigned i=0;i<choices.size();++i)if(choices[i].rule && choices[i].rule->id==result.rule->id) {
                const auto patches=render_match(*result.rule,choices[i],i,frame,{},s.view.rules);
                if(const auto found=patches.find(latest->handle);found!=patches.end()) {
                    output=describe(component(found->second));
                    if(!found->second[3].empty())output=found->second[3]+" "+output;
                    if(!found->second[6].empty())output=found->second[6]+" "+output;
                }
            }
        }
        if (!s.view.captured || s.view.last.handle!=latest->handle || s.view.native_text!=text || s.view.matched!=name) {
            s.view.captured=true;s.view.last=*latest;s.view.native_text=text;s.view.matched=name;s.view.output=output;
            ++s.view.capture_serial;update=true;
        }
    }
    if (update) {s.next_live_view=GetTickCount64()+100;publish();}
}
} // namespace dingosdk::trick_terminology::editor
