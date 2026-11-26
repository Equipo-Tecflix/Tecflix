#include "streaming/Config.hpp"

#include <cstdlib>
#include <sstream>

namespace tecflix {
namespace {

std::string env(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : fallback;
}

std::uint64_t envNumber(const char* name, std::uint64_t fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    char* end = nullptr;
    unsigned long long n = std::strtoull(v, &end, 10);
    return (end != nullptr && *end == '\0') ? n : fallback;
}

}  // namespace

Config Config::fromEnv() {
    Config c;
    c.port = static_cast<int>(envNumber("TECFLIX_PORT", c.port));
    c.nodeId = env("TECFLIX_NODE_ID", c.nodeId);
    c.storageDir = env("TECFLIX_STORAGE_DIR", c.storageDir);
    c.ffmpegPath = env("TECFLIX_FFMPEG", c.ffmpegPath);
    c.maxTranscodeJobs = envNumber("TECFLIX_MAX_JOBS", c.maxTranscodeJobs);
    if (c.maxTranscodeJobs == 0) c.maxTranscodeJobs = 1;
    c.segmentSeconds = static_cast<int>(envNumber("TECFLIX_SEGMENT_SECONDS", c.segmentSeconds));
    c.maxUploadBytes = envNumber("TECFLIX_MAX_UPLOAD_BYTES", c.maxUploadBytes);
    const auto cacheMiB = envNumber("TECFLIX_SEGMENT_CACHE_MIB", 64);
    c.segmentCacheBytes = static_cast<std::size_t>(cacheMiB <= 1024 ? cacheMiB : 64) * 1024 * 1024;
    c.otlpEndpoint = env("TECFLIX_OTLP_ENDPOINT", "");
    c.corsOrigin = env("TECFLIX_CORS_ORIGIN", c.corsOrigin);
    c.metadataUrl = env("TECFLIX_METADATA_URL", "");
    c.internalToken = env("TECFLIX_INTERNAL_TOKEN", "");

    std::stringstream peers(env("TECFLIX_PEERS", ""));
    std::string item;
    while (std::getline(peers, item, ',')) {
        if (!item.empty()) c.peers.push_back(item);
    }
    return c;
}

}  // namespace tecflix
