#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tecflix {

/// Bounded worker pool. Transcoding is CPU-heavy: limiting how many ffmpeg
/// processes run at once keeps the node responsive while serving segments.
class JobQueue {
public:
    explicit JobQueue(std::size_t workers);
    ~JobQueue();
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    /// Returns false if the queue is shutting down.
    bool submit(std::function<void()> job);
    std::size_t pending() const;
    std::size_t running() const;
    void shutdown();  // waits for running jobs, drops queued ones

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    std::size_t running_ = 0;
    bool stopping_ = false;
};

}  // namespace tecflix
