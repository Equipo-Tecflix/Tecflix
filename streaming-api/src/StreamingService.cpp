#include "streaming/StreamingService.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace tecflix {

StreamingService::StreamingService(const Config& config, VideoStorage& storage, ITranscoder& transcoder,
                                   IReplicator& replicator, IMetrics& metrics, JobQueue& queue)
    : config_(config), storage_(storage), transcoder_(transcoder), replicator_(replicator),
      metrics_(metrics), queue_(queue) {}

StreamingService::BeginResult StreamingService::beginUpload(const std::string& id) {
    if (!VideoStorage::isValidId(id)) return BeginResult::InvalidId;
    if (storage_.exists(id)) return BeginResult::Conflict;
    storage_.prepare(id);
    storage_.writeStatus(id, VideoStatus::Uploading);
    return BeginResult::Ok;
}

bool StreamingService::completeUpload(const std::string& id) {
    storage_.writeStatus(id, VideoStatus::Queued);
    metrics_.incCounter("tecflix_uploads_total", {{"result", "accepted"}});
    return schedule(id);
}

void StreamingService::abortUpload(const std::string& id) {
    storage_.remove(id);
    metrics_.incCounter("tecflix_uploads_total", {{"result", "aborted"}});
}

bool StreamingService::schedule(const std::string& id) {
    bool ok = queue_.submit([this, id] { processJob(id); });
    metrics_.setGauge("tecflix_transcode_queue_size", {}, static_cast<double>(queue_.pending()));
    if (!ok) storage_.writeStatus(id, VideoStatus::Failed);
    return ok;
}

VideoStatus StreamingService::status(const std::string& id) const { return storage_.readStatus(id); }

bool StreamingService::remove(const std::string& id) {
    bool removed = storage_.remove(id);
    if (removed) {
        replicator_.propagateDelete(id);
        metrics_.incCounter("tecflix_videos_deleted_total");
    }
    return removed;
}

std::size_t StreamingService::recover() {
    std::size_t requeued = 0;
    for (const auto& id : storage_.listVideoIds()) {
        VideoStatus s = storage_.readStatus(id);
        if (s == VideoStatus::Uploading) {
            storage_.remove(id);  // incomplete upload, client must retry
        } else if (s == VideoStatus::Queued || s == VideoStatus::Processing) {
            std::error_code ec;
            if (std::filesystem::exists(storage_.rawPath(id), ec)) {
                storage_.writeStatus(id, VideoStatus::Queued);
                if (schedule(id)) ++requeued;
            } else {
                storage_.writeStatus(id, VideoStatus::Failed);
            }
        }
    }
    return requeued;
}

void StreamingService::processJob(const std::string& id) {
    using clock = std::chrono::steady_clock;
    storage_.writeStatus(id, VideoStatus::Processing);
    metrics_.setGauge("tecflix_transcode_queue_size", {}, static_cast<double>(queue_.pending()));

    const auto start = clock::now();
    TranscodeRequest req{storage_.rawPath(id), storage_.dashDir(id), storage_.logPath(id),
                         config_.segmentSeconds};
    TranscodeResult result = transcoder_.transcode(req);

    std::error_code ec;
    const bool manifestOk = std::filesystem::exists(storage_.manifestPath(id), ec);
    const double seconds = std::chrono::duration<double>(clock::now() - start).count();
    metrics_.observe("tecflix_transcode_duration_seconds", {}, seconds);

    if (!result.ok || !manifestOk) {
        storage_.writeStatus(id, VideoStatus::Failed);
        metrics_.incCounter("tecflix_transcodes_total", {{"result", "failed"}});
        std::cerr << "[" << config_.nodeId << "] transcode failed for " << id << ": "
                  << result.error << "\n";
        return;
    }

    std::filesystem::remove(storage_.rawPath(id), ec);  // keep only the DASH output
    storage_.writeStatus(id, VideoStatus::Ready);
    metrics_.incCounter("tecflix_transcodes_total", {{"result", "ok"}});

    bool replicated = false;
    for (int attempt = 0; attempt < 3 && !replicated; ++attempt) replicated = replicator_.replicate(id);
    metrics_.incCounter("tecflix_replications_total", {{"result", replicated ? "ok" : "failed"}});
    if (!replicated) std::cerr << "[" << config_.nodeId << "] replication failed for " << id << "\n";
}

bool StreamingService::receiveReplica(const std::string& id, const std::string& file,
                                      const std::string& data) {
    if (!VideoStorage::isValidId(id) || !VideoStorage::isValidFileName(file)) return false;
    if (!storage_.exists(id)) storage_.prepare(id);
    if (!storage_.writeDashFile(id, file, data)) return false;
    if (file == "manifest.mpd") storage_.writeStatus(id, VideoStatus::Ready);  // sent last
    else if (storage_.readStatus(id) == VideoStatus::NotFound) storage_.writeStatus(id, VideoStatus::Processing);
    metrics_.incCounter("tecflix_replica_files_received_total");
    return true;
}

bool StreamingService::removeReplica(const std::string& id) { return storage_.remove(id); }

}  // namespace tecflix
