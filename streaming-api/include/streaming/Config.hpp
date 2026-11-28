#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tecflix {

/// Runtime configuration of one streaming node. Every value can be overridden
/// with an environment variable (see Config::fromEnv).
struct Config {
    int port = 8083;
    std::string nodeId = "node-1";
    std::string storageDir = "./storage";
    std::string ffmpegPath = "ffmpeg";
    std::size_t maxTranscodeJobs = 2;     // concurrent ffmpeg processes
    int segmentSeconds = 4;               // DASH segment duration
    std::uint64_t maxUploadBytes = 2ULL * 1024 * 1024 * 1024;  // 2 GiB
    std::size_t segmentCacheBytes = 0; // 0 disables application cache
    std::size_t fileChunkBytes = 1024 * 1024; // file provider block size
    double traceSampleRate = 0.10; // successful DASH segment GETs only
    std::string otlpEndpoint; // optional http://collector:4318
    std::string corsOrigin = "*";

    // Authorization: when metadataUrl is empty every request is allowed (dev mode).
    std::string metadataUrl;              // e.g. http://10.0.0.5:8081

    // V2.0 redundancy: other streaming nodes that receive a copy of every video.
    std::vector<std::string> peers;       // e.g. http://10.0.0.6:8083
    int replicationRetrySeconds = 5;
    std::string internalToken;            // shared secret for /internal/* routes

    static Config fromEnv();
};

}  // namespace tecflix
