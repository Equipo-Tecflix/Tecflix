#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "streaming/AccessChecker.hpp"
#include "streaming/Replicator.hpp"
#include "streaming/Transcoder.hpp"

namespace testsupport {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<int> n{0};
        path = std::filesystem::temp_directory_path() /
               ("tecflix_test_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

/// Pretends to be ffmpeg: writes a manifest and two segments.
class FakeTranscoder : public tecflix::ITranscoder {
public:
    bool succeed = true;
    std::atomic<int> calls{0};
    std::atomic<bool> block{false};  // keeps the job "processing" until released
    tecflix::TranscodeResult transcode(const tecflix::TranscodeRequest& r) override {
        ++calls;
        while (block) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!succeed) return {false, 1, "boom"};
        std::filesystem::create_directories(r.outputDir);
        std::ofstream(r.outputDir / "manifest.mpd") << "<MPD/>";
        std::ofstream(r.outputDir / "chunk-0-00001.m4s") << std::string(5000, 'a');
        std::ofstream(r.outputDir / "init-0.m4s") << "init";
        return {true, 0, ""};
    }
};

class RecordingReplicator : public tecflix::IReplicator {
public:
    std::vector<std::string> replicated, deleted;
    int failFirst = 0;
    bool replicate(const std::string& id) override {
        if (failFirst > 0) { --failFirst; return false; }
        replicated.push_back(id);
        return true;
    }
    bool propagateDelete(const std::string& id) override { deleted.push_back(id); return true; }
};

class DenyAccessChecker : public tecflix::IAccessChecker {
public:
    bool allowRead = false, allowWrite = false;
    bool allowed(const std::string&, tecflix::Permission p, const std::string&) override {
        return p == tecflix::Permission::Read ? allowRead : allowWrite;
    }
};

}  // namespace testsupport
