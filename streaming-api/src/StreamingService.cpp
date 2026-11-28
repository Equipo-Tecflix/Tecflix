#include "streaming/StreamingService.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace tecflix {

StreamingService::StreamingService(const Config& config, VideoStorage& storage, ITranscoder& transcoder,
                                   IReplicator& replicator, IMetrics& metrics, JobQueue& queue)
    : config_(config), storage_(storage), transcoder_(transcoder), replicator_(replicator),
      metrics_(metrics), queue_(queue) {}

StreamingService::~StreamingService() {
    stopRecovery();
    queue_.shutdown();
}

std::filesystem::path StreamingService::journal(const std::string& id, const char* suffix) const {
    return storage_.videoDir(".replication") / (id + suffix);
}
bool StreamingService::marked(const std::string& id, const char* suffix) const {
    return std::filesystem::exists(journal(id, suffix));
}
void StreamingService::mark(const std::string& id, const char* suffix) {
    auto path = journal(id, suffix);
    std::filesystem::create_directories(path.parent_path());
    // The existence of an empty file is the durable operation record.
    std::ofstream out(path, std::ios::app);
    out.close();
    if (!out) throw std::runtime_error("cannot persist replication operation");
}
void StreamingService::clear(const std::string& id, const char* suffix) {
    std::filesystem::remove(journal(id, suffix));
}
void StreamingService::updatePending() {
    std::size_t count = 0;
    auto dir = storage_.videoDir(".replication");
    if (std::filesystem::exists(dir))
        for (const auto& e : std::filesystem::directory_iterator(dir))
            if (e.is_regular_file() && (e.path().extension() == ".push" || e.path().extension() == ".delete")) ++count;
    metrics_.setGauge("tecflix_replication_pending", {}, static_cast<double>(count));
}
void StreamingService::attempt(const std::string& id, bool deletion, bool retry) {
    // Never hold the local state mutex across HTTP: peers may send requests back.
    std::lock_guard<std::mutex> outbound(outboundMutex_);
    const char* suffix = deletion ? ".delete" : ".push";
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!marked(id, suffix)) return;
        if (!deletion && (marked(id, ".tombstone") || storage_.readStatus(id) != VideoStatus::Ready)) return;
    }
    if (retry) metrics_.incCounter("tecflix_replication_retries_total");
    bool ok = false;
    for (int i = 0; i < (retry || deletion ? 1 : 3) && !ok; ++i)
        ok = deletion ? replicator_.propagateDelete(id) : replicator_.replicate(id);
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (ok && marked(id, suffix)) {
        clear(id, suffix);
        if (retry) metrics_.incCounter("tecflix_replication_recovered_total");
    }
    metrics_.incCounter(deletion ? "tecflix_replication_deletes_total" : "tecflix_replications_total", {{"result", ok ? "ok" : "failed"}});
    updatePending();
}
void StreamingService::retryPending() {
    std::vector<std::pair<std::string, bool>> work;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto dir = storage_.videoDir(".replication");
        if (std::filesystem::exists(dir))
            for (const auto& e : std::filesystem::directory_iterator(dir)) {
                auto ext = e.path().extension();
                auto id = e.path().stem().string();
                if (e.is_regular_file() && VideoStorage::isValidId(id) && (ext == ".delete" || ext == ".push"))
                    work.emplace_back(id, ext == ".delete");
            }
        updatePending();
    }
    for (const auto& item : work) if (item.second) attempt(item.first, true, true);
    for (const auto& item : work) if (!item.second) attempt(item.first, false, true);
}
void StreamingService::startRecovery() {
    std::lock_guard<std::mutex> lock(workerMutex_);
    if (worker_.joinable() || config_.peers.empty()) return;
    stopping_ = false;
    worker_ = std::thread([this] { recoveryLoop(); });
}
void StreamingService::stopRecovery() {
    { std::lock_guard<std::mutex> lock(workerMutex_); stopping_ = true; }
    workerCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void StreamingService::recoveryLoop() {
    std::unique_lock<std::mutex> lock(workerMutex_);
    while (!workerCv_.wait_for(lock, std::chrono::seconds(config_.replicationRetrySeconds), [this] { return stopping_; })) {
        lock.unlock();
        try { retryPending(); }
        catch (const std::exception& e) { std::cerr << "replication recovery: " << e.what() << "\n"; }
        lock.lock();
    }
}

StreamingService::BeginResult StreamingService::beginUpload(const std::string& id) {
    if (!VideoStorage::isValidId(id)) return BeginResult::InvalidId;
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (storage_.exists(id) || marked(id, ".tombstone")) return BeginResult::Conflict;
    storage_.prepare(id);
    storage_.writeStatus(id, VideoStatus::Uploading);
    return BeginResult::Ok;
}

bool StreamingService::completeUpload(const std::string& id) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!storage_.exists(id) || marked(id, ".tombstone")) return false;
    storage_.writeStatus(id, VideoStatus::Queued);
    metrics_.incCounter("tecflix_uploads_total", {{"result", "accepted"}});
    return schedule(id);
}

void StreamingService::abortUpload(const std::string& id) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    storage_.remove(id);
    metrics_.incCounter("tecflix_uploads_total", {{"result", "aborted"}});
}

bool StreamingService::schedule(const std::string& id) {
    bool ok = queue_.submit([this, id] {
        try { processJob(id); }
        catch (const std::exception& e) { std::cerr << "transcode/replication job: " << e.what() << "\n"; }
    });
    metrics_.setGauge("tecflix_transcode_queue_size", {}, static_cast<double>(queue_.pending()));
    if (!ok) storage_.writeStatus(id, VideoStatus::Failed);
    return ok;
}

VideoStatus StreamingService::status(const std::string& id) const { return storage_.readStatus(id); }

bool StreamingService::remove(const std::string& id) {
    if (!VideoStorage::isValidId(id)) return false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!storage_.exists(id)) return false;
        mark(id, ".delete");
        mark(id, ".tombstone");
        clear(id, ".push");
        storage_.writeStatus(id, VideoStatus::Failed);
        storage_.remove(id);
        updatePending();
    }
    attempt(id, true, false);
    metrics_.incCounter("tecflix_videos_deleted_total");
    return true;
}

std::size_t StreamingService::recover() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto dir = storage_.videoDir(".replication");
    if (std::filesystem::exists(dir)) {
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            auto id = e.path().stem().string();
            if (VideoStorage::isValidId(id) && e.path().extension() == ".delete") mark(id, ".tombstone");
        }
    }
    std::size_t requeued = 0;
    for (const auto& id : storage_.listVideoIds()) {
        if (marked(id, ".tombstone")) { storage_.remove(id); clear(id, ".push"); continue; }
        VideoStatus s = storage_.readStatus(id);
        if (s == VideoStatus::Uploading) {
            storage_.remove(id);  // incomplete upload, client must retry
        } else if (s == VideoStatus::Queued || s == VideoStatus::Processing) {
            std::error_code ec;
            if (std::filesystem::exists(storage_.rawPath(id), ec)) {
                storage_.writeStatus(id, VideoStatus::Queued);
                if (schedule(id)) ++requeued;
            } else {
                // A partial incoming replica has no raw upload; the sender retries it.
                storage_.writeStatus(id, VideoStatus::Failed);
                clear(id, ".push");
            }
        }
    }
    updatePending();
    return requeued;
}

void StreamingService::processJob(const std::string& id) {
    using clock = std::chrono::steady_clock;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!storage_.exists(id) || marked(id, ".tombstone")) return;
        storage_.writeStatus(id, VideoStatus::Processing);
    }
    metrics_.setGauge("tecflix_transcode_queue_size", {}, static_cast<double>(queue_.pending()));

    const auto start = clock::now();
    TranscodeRequest req{storage_.rawPath(id), storage_.dashDir(id), storage_.logPath(id),
                         config_.segmentSeconds};
    TranscodeResult result = transcoder_.transcode(req);

    std::error_code ec;
    const bool manifestOk = std::filesystem::exists(storage_.manifestPath(id), ec);
    const double seconds = std::chrono::duration<double>(clock::now() - start).count();
    metrics_.observe("tecflix_transcode_duration_seconds", {}, seconds);

    {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (marked(id, ".tombstone")) { storage_.remove(id); return; }
    if (!result.ok || !manifestOk) {
        clear(id, ".push");
        updatePending();
        storage_.writeStatus(id, VideoStatus::Failed);
        metrics_.incCounter("tecflix_transcodes_total", {{"result", "failed"}});
        std::cerr << "[" << config_.nodeId << "] transcode failed for " << id << ": "
                  << result.error << "\n";
        return;
    }

    // Persist before publishing Ready, so a restart cannot forget the push.
    try { mark(id, ".push"); }
    catch (const std::exception& e) {
        storage_.writeStatus(id, VideoStatus::Failed);
        std::cerr << e.what() << "\n";
        return;
    }
    storage_.writeStatus(id, VideoStatus::Ready);
    std::filesystem::remove(storage_.rawPath(id), ec);
    metrics_.incCounter("tecflix_transcodes_total", {{"result", "ok"}});
    updatePending();
    }
    attempt(id, false, false);
}

bool StreamingService::receiveReplica(const std::string& id, const std::string& file,
                                      const std::string& data) {
    if (!VideoStorage::isValidId(id) || !VideoStorage::isValidFileName(file)) return false;
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (marked(id, ".tombstone")) return false;
    if (!storage_.exists(id)) storage_.prepare(id);
    if (!storage_.writeDashFile(id, file, data)) return false;
    if (file == "manifest.mpd") storage_.writeStatus(id, VideoStatus::Ready);  // sent last
    else if (storage_.readStatus(id) == VideoStatus::NotFound) storage_.writeStatus(id, VideoStatus::Processing);
    metrics_.incCounter("tecflix_replica_files_received_total");
    return true;
}

bool StreamingService::removeReplica(const std::string& id) {
    if (!VideoStorage::isValidId(id)) return false;
    std::lock_guard<std::mutex> lock(stateMutex_);
    mark(id, ".tombstone");
    clear(id, ".push");
    if (storage_.exists(id)) storage_.writeStatus(id, VideoStatus::Failed);
    const bool removed = storage_.remove(id);
    updatePending();
    return removed;
}

}  // namespace tecflix
