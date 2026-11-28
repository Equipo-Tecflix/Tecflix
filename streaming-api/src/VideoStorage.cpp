#include "streaming/VideoStorage.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <fstream>
#include <regex>
#include <system_error>

namespace fs = std::filesystem;

namespace tecflix {

std::string toString(VideoStatus s) {
    switch (s) {
        case VideoStatus::Uploading: return "uploading";
        case VideoStatus::Queued: return "queued";
        case VideoStatus::Processing: return "processing";
        case VideoStatus::Ready: return "ready";
        case VideoStatus::Failed: return "failed";
        case VideoStatus::NotFound: break;
    }
    return "not_found";
}

VideoStatus statusFromString(const std::string& t) {
    if (t == "uploading") return VideoStatus::Uploading;
    if (t == "queued") return VideoStatus::Queued;
    if (t == "processing") return VideoStatus::Processing;
    if (t == "ready") return VideoStatus::Ready;
    if (t == "failed") return VideoStatus::Failed;
    return VideoStatus::NotFound;
}

VideoStorage::VideoStorage(fs::path root, std::size_t cacheBytes)
    : root_(std::move(root)), cacheCapacity_(cacheBytes) {
    std::error_code ec;
    fs::create_directories(root_, ec);
}

bool VideoStorage::isValidId(const std::string& id) {
    static const std::regex re("^[A-Za-z0-9_-]{1,64}$");
    return std::regex_match(id, re);
}

bool VideoStorage::isValidFileName(const std::string& name) {
    static const std::regex re("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$");
    return std::regex_match(name, re) && name.find("..") == std::string::npos;
}

fs::path VideoStorage::videoDir(const std::string& id) const { return root_ / id; }
fs::path VideoStorage::rawPath(const std::string& id) const { return videoDir(id) / "raw.upload"; }
fs::path VideoStorage::dashDir(const std::string& id) const { return videoDir(id) / "dash"; }
fs::path VideoStorage::logPath(const std::string& id) const { return videoDir(id) / "ffmpeg.log"; }
fs::path VideoStorage::manifestPath(const std::string& id) const { return dashDir(id) / "manifest.mpd"; }

std::optional<fs::path> VideoStorage::existingDashFile(const std::string& id,
                                                       const std::string& name) const {
    if (!isValidId(id) || !isValidFileName(name)) return std::nullopt;
    fs::path p = dashDir(id) / name;
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return std::nullopt;
    return p;
}

void VideoStorage::evictLastLocked() {
    const auto& entry = cacheLru_.back();
    cacheUsed_ -= entry.data->size();
    cacheIndex_.erase(entry.key);
    cacheLru_.pop_back();
}

void VideoStorage::invalidateLocked(const std::string& id) {
    for (auto it = cacheLru_.begin(); it != cacheLru_.end();) {
        if (it->key.first != id) { ++it; continue; }
        cacheUsed_ -= it->data->size();
        cacheIndex_.erase(it->key);
        it = cacheLru_.erase(it);
    }
}

VideoStorage::CachedSegment VideoStorage::cachedSegment(const std::string& id, const std::string& name) {
    if (cacheCapacity_ == 0 || !isValidId(id) || !isValidFileName(name) ||
        fs::path(name).extension() != ".m4s") return {nullptr, CacheResult::Bypass};
    std::lock_guard<std::mutex> lock(cacheMutex_);
    const CacheKey key{id, name};
    auto found = cacheIndex_.find(key);
    if (found != cacheIndex_.end()) {
        cacheLru_.splice(cacheLru_.begin(), cacheLru_, found->second);
        return {found->second->data, CacheResult::Hit};
    }
    auto path = existingDashFile(id, name);
    if (!path) return {nullptr, CacheResult::Bypass};
    std::error_code ec;
    auto size = fs::file_size(*path, ec);
    // Bound each allocation as well as total retained payload and entry count.
    if (ec || size == 0 || size > cacheCapacity_ || size > 8 * 1024 * 1024)
        return {nullptr, CacheResult::Bypass};
    std::ifstream in(*path, std::ios::binary);
    if (!in) return {nullptr, CacheResult::Bypass};
    auto data = std::make_shared<std::string>(static_cast<std::size_t>(size), '\0');
    if (!in.read(data->data(), static_cast<std::streamsize>(size)))
        return {nullptr, CacheResult::Bypass};
    while (!cacheLru_.empty() && (cacheUsed_ > cacheCapacity_ - data->size() || cacheLru_.size() >= 4096))
        evictLastLocked();
    cacheLru_.push_front({key, data});
    try { cacheIndex_.emplace(key, cacheLru_.begin()); }
    catch (...) { cacheLru_.pop_front(); throw; }
    cacheUsed_ += data->size();
    return {data, CacheResult::Miss};
}

void VideoStorage::prepare(const std::string& id) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    invalidateLocked(id);
    fs::create_directories(dashDir(id));
}

bool VideoStorage::exists(const std::string& id) const {
    std::error_code ec;
    return isValidId(id) && fs::is_directory(videoDir(id), ec);
}

bool VideoStorage::remove(const std::string& id) {
    if (!isValidId(id)) return false;
    std::lock_guard<std::mutex> lock(cacheMutex_);
    invalidateLocked(id);
    std::error_code ec;
    return fs::remove_all(videoDir(id), ec) > 0;
}

void VideoStorage::writeStatus(const std::string& id, VideoStatus status) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    invalidateLocked(id);
    std::ofstream(videoDir(id) / "status.txt", std::ios::trunc) << toString(status);
}

VideoStatus VideoStorage::readStatus(const std::string& id) const {
    if (!isValidId(id)) return VideoStatus::NotFound;
    std::ifstream in(videoDir(id) / "status.txt");
    std::string text;
    in >> text;
    return statusFromString(text);
}

bool VideoStorage::writeDashFile(const std::string& id, const std::string& name,
                                 const std::string& data) {
    if (!isValidId(id) || !isValidFileName(name)) return false;
    std::lock_guard<std::mutex> lock(cacheMutex_);
    invalidateLocked(id);
    std::error_code ec;
    fs::create_directories(dashDir(id), ec);
    fs::path target = dashDir(id) / name;
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
#ifdef _WIN32
    return MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    fs::rename(tmp, target, ec);
    return !ec;
#endif
}

std::vector<std::string> VideoStorage::dashFilesForReplication(const std::string& id) const {
    std::vector<std::string> files;
    std::error_code ec;
    if (!isValidId(id)) return files;
    for (const auto& entry : fs::directory_iterator(dashDir(id), ec)) {
        std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name != "manifest.mpd" && isValidFileName(name) &&
            entry.path().extension() != ".tmp") {
            files.push_back(name);
        }
    }
    std::sort(files.begin(), files.end());
    if (fs::exists(manifestPath(id), ec)) files.push_back("manifest.mpd");
    return files;
}

std::vector<std::string> VideoStorage::listVideoIds() const {
    std::vector<std::string> ids;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root_, ec)) {
        std::string name = entry.path().filename().string();
        if (entry.is_directory() && isValidId(name)) ids.push_back(name);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace tecflix
