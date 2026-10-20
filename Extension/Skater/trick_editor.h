#pragma once
#include "trick_rules.h"
#include <atomic>
#include <memory>

namespace dingosdk::trick_terminology::editor {
struct View {
    bool ready{}, terminology_enabled{};
    DetectionSettings detection;
    std::map<std::string,std::string> native_names;
    std::uint64_t revision{}, capture_serial{};
    std::vector<Rule> rules;
    bool captured{};
    Observation last;
    std::string native_text, output, matched, status, io, export_path, message;
    std::uint64_t boundary_scans{};
};
// Like Trainer's snapshots: immutable values cross into the presentation thread.
inline std::atomic<std::shared_ptr<const View>>& feed() {
    static auto* value=new std::atomic<std::shared_ptr<const View>>(std::make_shared<const View>());return *value;
}
inline std::shared_ptr<const View> view() {return feed().load();}
void set_enabled(bool enabled);
void set_native_names(std::map<std::string,std::string> names);
void service(std::uint64_t now);
const std::vector<Rule>& rules();
const DetectionSettings& detection();
std::string command(const std::vector<std::string>& arguments);
void observe(const std::vector<Observation>& frame,const std::string& status,std::uint64_t boundary_scans);
} // namespace dingosdk::trick_terminology::editor
