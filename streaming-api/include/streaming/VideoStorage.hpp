#pragma once
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tecflix {

enum class VideoStatus { NotFound, Uploading, Queued, Processing, Ready, Failed };

std::string toString(VideoStatus status);
VideoStatus statusFromString(const std::string& text);

/// Owns the on-disk layout of the node:
///   <root>/<videoId>/raw.upload       original file (deleted after transcoding)
///   <root>/<videoId>/status.txt       lifecycle state
///   <root>/<videoId>/ffmpeg.log       transcoder stderr
///   <root>/<videoId>/dash/            manifest.mpd + init/chunk .m4s files
/// Every externally supplied id / file name is validated here, so no other
/// component can build a path outside <root> (path traversal protection).
class VideoStorage {
public:
    explicit VideoStorage(std::filesystem::path root, std::size_t cacheBytes = 64 * 1024 * 1024);

    enum class CacheResult { Hit, Miss, Bypass };
    struct CachedSegment {
        std::shared_ptr<const std::string> data;
        CacheResult result;
    };
    CachedSegment cachedSegment(const std::string& id, const std::string& name);


    static bool isValidId(const std::string& id);
    static bool isValidFileName(const std::string& name);

    std::filesystem::path videoDir(const std::string& id) const;
    std::filesystem::path rawPath(const std::string& id) const;
    std::filesystem::path dashDir(const std::string& id) const;
    std::filesystem::path logPath(const std::string& id) const;
    std::filesystem::path manifestPath(const std::string& id) const;
    /// Path of a DASH file or nullopt when the name is invalid / file missing.
    std::optional<std::filesystem::path> existingDashFile(const std::string& id,
                                                          const std::string& name) const;

    void prepare(const std::string& id);   // creates the directories
    bool exists(const std::string& id) const;
    bool remove(const std::string& id);    // true if something was deleted

    void writeStatus(const std::string& id, VideoStatus status);
    VideoStatus readStatus(const std::string& id) const;

    /// Atomic write (temp file + rename) of a DASH file; used by replication.
    bool writeDashFile(const std::string& id, const std::string& name, const std::string& data);

    /// DASH files that make up the video, manifest.mpd always LAST.
    std::vector<std::string> dashFilesForReplication(const std::string& id) const;
    std::vector<std::string> listVideoIds() const;

private:
    using CacheKey = std::pair<std::string, std::string>;
    struct CacheEntry {
        CacheKey key;
        std::shared_ptr<const std::string> data;
    };
    void invalidateLocked(const std::string& id);
    void evictLastLocked();
    std::filesystem::path root_;
    const std::size_t cacheCapacity_;
    std::size_t cacheUsed_ = 0;
    std::mutex cacheMutex_;
    std::list<CacheEntry> cacheLru_;
    std::map<CacheKey, std::list<CacheEntry>::iterator> cacheIndex_;
};

}  // namespace tecflix
