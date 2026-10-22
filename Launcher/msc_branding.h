#pragma once
#include "reskate_version.h"
#include <string>
#include <string_view>
namespace dingosdk::launcher_gui {
inline std::string launcher_branding() {
    return reskate_release_build ? "MSC Launcher v"+std::string(reskate_version) : "MSC Launcher \xE2\x80\x94 Development build";
}
inline std::string runtime_branding(std::string_view version,bool updates_enabled) {
    if(!updates_enabled) return "ReSkate MSC development build";
    return version.empty() ? "ReSkate MSC" : "ReSkate MSC v"+std::string(version);
}
}
