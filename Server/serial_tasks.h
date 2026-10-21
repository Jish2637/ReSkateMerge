#pragma once
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace dingosdk::server {
// One ordered owner for persistence jobs. Jobs own their data, never live Host references.
// Destruction drains accepted jobs; errors belong to their futures, not this thread.
class SerialTasks {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::packaged_task<void()>> jobs_;
    bool closing_{};
    std::thread worker_{[this] {
        for(;;) {
            std::packaged_task<void()> task;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock,[&]{return closing_ || !jobs_.empty();});
                if(jobs_.empty())return;
                task=std::move(jobs_.front());jobs_.pop_front();
            }
            task();
        }
    }};
  public:
    ~SerialTasks() {
        {std::lock_guard lock(mutex_);closing_=true;}
        wake_.notify_one();worker_.join();
    }
    template<class Job> std::future<void> submit(Job job) {
        std::packaged_task<void()> task(std::move(job));auto done=task.get_future();
        {std::lock_guard lock(mutex_);jobs_.push_back(std::move(task));}
        wake_.notify_one();return done;
    }
};
} // namespace dingosdk::server
