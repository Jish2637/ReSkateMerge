#pragma once
#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace dingosdk::server {
// Runtime-only shuffle bag. Editing the configured list starts a fresh cycle.
class AnnouncementPool {
    std::vector<std::string> configured_, remaining_;
    std::string last_;
public:
    template<class Random>
    std::string next(const std::vector<std::string> &messages, Random &random) {
        if (configured_ != messages) {
            configured_ = messages;
            remaining_.clear();
        }
        if (remaining_.empty()) {
            for (const auto &message : configured_)
                if (!message.empty() && std::find(remaining_.begin(), remaining_.end(), message) == remaining_.end())
                    remaining_.push_back(message);
            if (remaining_.empty()) return {};
            std::shuffle(remaining_.begin(), remaining_.end(), random);
            if (remaining_.size() > 1 && remaining_.back() == last_) {
                // Choose uniformly from all other messages at a cycle boundary.
                const auto other = std::uniform_int_distribution<std::size_t>(0, remaining_.size() - 2)(random);
                std::swap(remaining_.back(), remaining_[other]);
            }
        }
        last_ = std::move(remaining_.back());
        remaining_.pop_back();
        return last_;
    }
};
} // namespace dingosdk::server
