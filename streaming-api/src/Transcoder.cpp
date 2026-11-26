#include "streaming/Transcoder.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

extern char** environ;

namespace tecflix {

FfmpegTranscoder::FfmpegTranscoder(std::string ffmpegPath) : ffmpegPath_(std::move(ffmpegPath)) {}

std::vector<std::string> FfmpegTranscoder::buildArgs(const TranscodeRequest& r) const {
    const std::string seg = std::to_string(r.segmentSeconds);
    return {
        ffmpegPath_, "-nostdin", "-hide_banner", "-loglevel", "warning", "-y",
        "-i", r.input.string(),
        "-filter_complex",
        "[0:v:0]split=2[a][b];[a]scale=-2:360[v360];[b]scale=-2:720[v720]",
        "-map", "[v360]", "-map", "[v720]", "-map", "0:a:0?",
        "-c:v", "libx264", "-preset", "veryfast", "-profile:v", "main", "-pix_fmt", "yuv420p",
        "-b:v:0", "800k", "-b:v:1", "2500k",
        "-force_key_frames", "expr:gte(t,n_forced*" + seg + ")", "-sc_threshold", "0",
        "-c:a", "aac", "-b:a", "128k", "-ac", "2",
        "-f", "dash", "-seg_duration", seg, "-use_template", "1", "-use_timeline", "1",
        "-init_seg_name", "init-$RepresentationID$.m4s",
        "-media_seg_name", "chunk-$RepresentationID$-$Number%05d$.m4s",
        (r.outputDir / "manifest.mpd").generic_string(),
    };
}

TranscodeResult FfmpegTranscoder::transcode(const TranscodeRequest& request) {
    TranscodeResult result;
    std::vector<std::string> args = buildArgs(request);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, request.logFile.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        result.error = std::string("cannot start ffmpeg: ") + std::strerror(rc);
        return result;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            result.error = std::string("waitpid failed: ") + std::strerror(errno);
            return result;
        }
    }
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.ok = (result.exitCode == 0);
        if (!result.ok) result.error = "ffmpeg exited with code " + std::to_string(result.exitCode);
    } else {
        result.error = "ffmpeg terminated by signal";
    }
    return result;
}

}  // namespace tecflix
