#pragma once
#include <string>

#include "streaming/Config.hpp"
#include "streaming/JobQueue.hpp"
#include "streaming/Metrics.hpp"
#include "streaming/Replicator.hpp"
#include "streaming/Transcoder.hpp"
#include "streaming/VideoStorage.hpp"

namespace tecflix {

/// Business logic of the streaming API, independent from HTTP so that it can be
/// unit tested with fake transcoders / replicators.
class StreamingService {
public:
    enum class BeginResult { Ok, InvalidId, Conflict };

    StreamingService(const Config& config, VideoStorage& storage, ITranscoder& transcoder,
                     IReplicator& replicator, IMetrics& metrics, JobQueue& queue);

    /// Reserves the id and creates the folders. Conflict if the video already exists.
    BeginResult beginUpload(const std::string& id);
    /// Upload finished correctly -> schedule transcoding.
    bool completeUpload(const std::string& id);
    /// Upload failed halfway -> forget everything.
    void abortUpload(const std::string& id);

    VideoStatus status(const std::string& id) const;
    /// Deletes the video locally and on every peer.
    bool remove(const std::string& id);

    /// Re-queues jobs interrupted by a crash/restart (status queued/processing).
    std::size_t recover();

    /// Runs on a worker thread; public so tests can call it synchronously.
    void processJob(const std::string& id);

    /// Stores a file received from a peer. Returns false for invalid names.
    bool receiveReplica(const std::string& id, const std::string& file, const std::string& data);
    bool removeReplica(const std::string& id);

private:
    bool schedule(const std::string& id);

    const Config& config_;
    VideoStorage& storage_;
    ITranscoder& transcoder_;
    IReplicator& replicator_;
    IMetrics& metrics_;
    JobQueue& queue_;
};

}  // namespace tecflix
