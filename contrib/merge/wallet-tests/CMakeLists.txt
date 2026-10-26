cmake_minimum_required(VERSION 3.24)
project(msc_skate_wallet_checks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT EXISTS "${SOURCE}/Server/player_stats.cpp")
    message(FATAL_ERROR "Full merged server source is missing: ${SOURCE}")
endif()
add_executable(msc_wallet_checks verify.cpp
    "${SOURCE}/Server/player_stats.cpp" "${SOURCE}/Engine/Core/Json/json.cpp")
target_include_directories(msc_wallet_checks PRIVATE "${SOURCE}" "${SOURCE}/External")
target_compile_definitions(msc_wallet_checks PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    TEST_DATA="${CMAKE_CURRENT_BINARY_DIR}/test-data")
add_library(msc_wallet_bridge_syntax OBJECT
    "${SOURCE}/Extension/Plugins/plugin_api_world.cpp"
    "${SOURCE}/Server/server_plugins.cpp")
target_include_directories(msc_wallet_bridge_syntax PRIVATE "${SOURCE}" "${SOURCE}/External"
    "${SOURCE}/External/steam_networking/include" "${SOURCE}/External/luau/VM/include"
    "${SOURCE}/External/luau/Compiler/include")
target_compile_definitions(msc_wallet_bridge_syntax PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
enable_testing()
add_test(NAME msc_wallet_checks COMMAND msc_wallet_checks)
