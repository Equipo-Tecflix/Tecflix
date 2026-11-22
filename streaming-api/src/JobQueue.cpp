#include "streaming/JobQueue.hpp"

namespace tecflix {

JobQueue::JobQueue(std::size_t workers) {
    if (workers == 0) workers = 1;
    for (std::size_t i = 0; i < workers; ++i) workers_.emplace_back([this] { workerLoop(); });
}

JobQueue::~JobQueue() { shutdown(); }

bool JobQueue::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
    return true;
}

std::size_t JobQueue::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

std::size_t JobQueue::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void JobQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void JobQueue::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++running_;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --running_;
        }
    }
}

}  // namespace tecflix
