#pragma once
#include "player_stats.h"
#include "roles.h"
#include <algorithm>
namespace dingosdk::server {
inline std::vector<std::uint64_t> eligible_board(const PlayerStatsStore& stats,const Roles& roles,std::string_view kind,std::int64_t utc) {
    auto rows=stats.board(kind,utc);
    std::erase_if(rows,[&](auto id){return staff(roles.of(id));});
    return rows;
}
}
