#pragma once
#include "Engine/Game/World/network_objects.h"
#include <filesystem>
#include <string_view>

namespace dingosdk::server {
struct BuildSet { std::string name,map; std::vector<NetworkObject> objects; };
bool valid_build_name(std::string_view name);
std::filesystem::path build_set_folder(const std::filesystem::path &data,std::string_view map);
BuildSet read_build_set(const std::filesystem::path &file,std::string_view name,std::string_view map);
void write_build_set(const std::filesystem::path &data,const BuildSet &,bool replace,std::uint64_t actor);
} // namespace dingosdk::server
