#pragma once
#include "skate_rule_values.h"
#include <windows.h>
#include "Engine/Core/Config/profile_config.h"
#include <mutex>
namespace dingosdk::skate {
inline Json hosting_json(const HostingPreferences& p) {
    return Json{{"categories",p.categories},{"grind_minimum",p.numerical.grind_minimum},{"spin_offset",p.numerical.spin_offset},{"recovery_limit",p.numerical.recovery_limit},{"grind_combo_ms",p.numerical.grind_combo_ms}};
}
inline HostingPreferences read_hosting_preferences(const std::filesystem::path& path) {
    HostingPreferences p;
    try { if(auto j=profile_config::read(path)) {
        const int categories=j->value("categories",15);
        p.numerical={j->value("grind_minimum",115),j->value("spin_offset",90),j->value("recovery_limit",450),j->value("grind_combo_ms",500)};
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
class FeedbackPreferences {
    std::mutex mutex_;
    std::filesystem::path path_;
    bool detailed_{};
public:
    explicit FeedbackPreferences(std::filesystem::path path=profile_config::directory()/L"skate-feedback.json"):path_(std::move(path)) {
        try {if(auto j=profile_config::read(path_))detailed_=j->value("detailed",false);}catch(...){}
    }
    bool get(){std::lock_guard lock(mutex_);return detailed_;}
    bool set(bool value){std::lock_guard lock(mutex_);if(!profile_config::write(path_,Json{{"detailed",value}}))return false;detailed_=value;return true;}
};
inline FeedbackPreferences& feedback_preferences(){static FeedbackPreferences value;return value;}
inline HostPreferences& host_preferences() {static HostPreferences value;return value;}
}
