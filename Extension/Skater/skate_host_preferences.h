#pragma once
#include "skate_rule_values.h"
#include <windows.h>
#include "Engine/Core/Config/profile_config.h"
#include <mutex>
namespace dingosdk::skate {
inline Json hosting_json(const HostingPreferences& p) {
    return Json{{"categories",p.categories},{"grind_minimum",p.numerical.grind_minimum},{"spin_offset",p.numerical.spin_offset},{"recovery_limit",p.numerical.recovery_limit}};
}
inline HostingPreferences read_hosting_preferences(const std::filesystem::path& path) {
    HostingPreferences p;
    try { if(auto j=profile_config::read(path)) {
        const int categories=j->value("categories",15);
        p.numerical={j->value("grind_minimum",115),j->value("spin_offset",90),j->value("recovery_limit",450)};
        if(categories<0 || categories>15 || !p.numerical.valid())return {};
        p.categories=static_cast<std::uint8_t>(categories);
    }} catch(...) {return {};}
    return p;
}
class HostPreferences {
    std::mutex mutex_;
    std::filesystem::path path_;
    HostingPreferences value_;
public:
    explicit HostPreferences(std::filesystem::path path=profile_config::directory()/L"skate-host-rules.json")
        :path_(std::move(path)),value_(read_hosting_preferences(path_)) {}
    HostingPreferences get() {std::lock_guard lock(mutex_);return value_;}
    bool set(HostingPreferences p) {
        if(!p.valid())return false;
        std::lock_guard lock(mutex_);
        if(!profile_config::write(path_,hosting_json(p)))return false;
        value_=p;return true;
    }
};
inline HostPreferences& host_preferences() {static HostPreferences value;return value;}
}
