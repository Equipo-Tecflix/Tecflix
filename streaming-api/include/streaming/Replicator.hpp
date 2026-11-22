#pragma once
#include <string>
#include <vector>

#include "streaming/VideoStorage.hpp"

namespace tecflix {

/// V2.0 redundancy. After a video has been transcoded on one node, every other
/// node receives a copy so any node behind the load balancer can serve it.
class IReplicator {
public:
    virtual ~IReplicator() = default;
    /// Pushes the whole DASH output of a ready video to all peers.
    virtual bool replicate(const std::string& videoId) = 0;
    /// Propagates a deletion to all peers.
    virtual bool propagateDelete(const std::string& videoId) = 0;
};

/// Single node deployment (V1.0).
class NullReplicator : public IReplicator {
public:
    bool replicate(const std::string&) override { return true; }
    bool propagateDelete(const std::string&) override { return true; }
};

/// Pushes files with PUT /internal/replicate/{id}/{file} (segments first,
/// manifest.mpd last, so a peer only exposes the video once it is complete).
class HttpReplicator : public IReplicator {
public:
    HttpReplicator(std::vector<std::string> peers, std::string token, const VideoStorage& storage);
    bool replicate(const std::string& videoId) override;
    bool propagateDelete(const std::string& videoId) override;

private:
    std::vector<std::string> peers_;
    std::string token_;
    const VideoStorage& storage_;
};

}  // namespace tecflix
