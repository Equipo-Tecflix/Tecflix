#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace tecflix {

struct TranscodeRequest {
    std::filesystem::path input;       // raw uploaded file
    std::filesystem::path outputDir;   // where manifest.mpd + segments go
    std::filesystem::path logFile;     // ffmpeg stderr
    int segmentSeconds = 4;
};

struct TranscodeResult {
    bool ok = false;
    int exitCode = -1;
    std::string error;
};

class ITranscoder {
public:
    virtual ~ITranscoder() = default;
    virtual TranscodeResult transcode(const TranscodeRequest& request) = 0;
};

/// Converts any video into an adaptive MPEG-DASH ladder (360p + 720p, H.264 + AAC)
/// that Shaka Player / dash.js can play. FFmpeg is started with posix_spawn and an
/// argument vector (no shell), so file names can never be interpreted as commands.
class FfmpegTranscoder : public ITranscoder {
public:
    explicit FfmpegTranscoder(std::string ffmpegPath = "ffmpeg");

    TranscodeResult transcode(const TranscodeRequest& request) override;

    /// Exposed for unit tests.
    std::vector<std::string> buildArgs(const TranscodeRequest& request) const;

private:
    std::string ffmpegPath_;
};

}  // namespace tecflix
