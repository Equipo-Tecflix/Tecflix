#pragma once
#include <filesystem>
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
    explicit VideoStorage(std::filesystem::path root);

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
    std::filesystem::path root_;
};

}  // namespace tecflix
