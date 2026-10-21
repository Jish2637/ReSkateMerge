#pragma once
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace dingosdk::server {
// Structured public chat only; never infer messages from arbitrary log text.
class PublicChatLog {
    std::ofstream file_;
    static std::string escaped(std::string_view text) {
        constexpr char hex[]="0123456789ABCDEF";
        std::string out;
        for(unsigned char c:text) {
            if(c=='\\')out+="\\\\";
            else if(c<32 || c==127){out+="\\x";out+=hex[c>>4];out+=hex[c&15];}
            else out+=static_cast<char>(c);
        }
        return out;
    }
  public:
    bool open(const std::filesystem::path &path) {
        close();file_.clear();file_.open(path,std::ios::binary|std::ios::app);return bool(file_);
    }
    void close() { if(file_.is_open())file_.close(); }
    bool append(std::uint64_t id,std::string_view name,std::string_view message,std::string_view category="chat") {
        if(!file_)return false;
        const auto now=std::time(nullptr);std::tm utc{};
#ifdef _WIN32
        if(gmtime_s(&utc,&now))return false;
#else
        if(!gmtime_r(&now,&utc))return false;
#endif
        char stamp[32]{};
        if(!std::strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",&utc))return false;
        file_<<'['<<stamp<<"] ["<<escaped(category)<<"] "<<escaped(name)<<" ("<<id<<"): "<<escaped(message)<<std::endl;
        return bool(file_);
    }
};
} // namespace dingosdk::server
