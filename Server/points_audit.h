#pragma once
#include "Engine/Core/Json/json.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::server {
inline Json points_identity(std::uint64_t id,std::string_view name) {
    auto j=Json::object();j["steam_id"]=std::to_string(id);j["name"]=std::string(name);return j;
}
class PointsAuditLog {
    std::ofstream file_;
  public:
    bool open(const std::filesystem::path &path) {
        close();file_.clear();file_.open(path,std::ios::binary|std::ios::app);return bool(file_);
    }
    void close(){if(file_.is_open())file_.close();}
    bool append(Json record,std::string &line) {
        const auto now=std::time(nullptr);std::tm utc{};char stamp[32]{};
#ifdef _WIN32
        const bool date_ok=gmtime_s(&utc,&now)==0;
#else
        const bool date_ok=gmtime_r(&now,&utc)!=nullptr;
#endif
        if(date_ok)std::strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",&utc);
        record["utc"]=stamp;record["unix_utc"]=static_cast<std::int64_t>(now);record["schema_version"]=1;
        line=record.dump();
        if(!file_)return false;
        file_<<line<<std::endl;return bool(file_);
    }
};
} // namespace dingosdk::server
