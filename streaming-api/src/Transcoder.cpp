#include "streaming/Transcoder.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>

#ifndef _WIN32
extern char** environ;
#endif

namespace tecflix {

FfmpegTranscoder::FfmpegTranscoder(std::string ffmpegPath) : ffmpegPath_(std::move(ffmpegPath)) {}

std::vector<std::string> FfmpegTranscoder::buildArgs(const TranscodeRequest& r) const {
    const std::string seg = std::to_string(r.segmentSeconds);
    return {
        ffmpegPath_, "-nostdin", "-hide_banner", "-loglevel", "warning", "-y",
        "-i", r.input.string(),
        "-filter_complex_threads", "1", "-filter_complex",
        "[0:v:0]split=2[a][b];[a]scale=-2:360[v360];[b]scale=-2:720[v720]",
        "-map", "[v360]", "-map", "[v720]", "-map", "0:a:0?",
        "-c:v", "libx264", "-threads:v", "2", "-preset", "veryfast", "-profile:v", "main", "-pix_fmt", "yuv420p",
        "-b:v:0", "800k", "-b:v:1", "2500k",
        "-force_key_frames", "expr:gte(t,n_forced*" + seg + ")", "-sc_threshold", "0",
        "-c:a", "aac", "-b:a", "128k", "-ac", "2",
        "-f", "dash", "-adaptation_sets", "id=0,streams=v id=1,streams=a", "-seg_duration", seg, "-use_template", "1", "-use_timeline", "1",
        "-init_seg_name", "init-$RepresentationID$.m4s",
        "-media_seg_name", "chunk-$RepresentationID$-$Number%05d$.m4s",
        (r.outputDir / "manifest.mpd").generic_string(),
    };
}

TranscodeResult FfmpegTranscoder::transcode(const TranscodeRequest& request) {
    TranscodeResult result;
    std::vector<std::string> args = buildArgs(request);
#ifdef _WIN32
    // Isolate every FFmpeg process in its own DASH directory.
    std::error_code pathError;
    const auto workingDir = std::filesystem::absolute(request.outputDir, pathError);
    if (pathError) { result.error = pathError.message(); return result; }
    const auto absoluteInput = std::filesystem::absolute(request.input, pathError);
    if (pathError) { result.error = pathError.message(); return result; }
    args[7] = absoluteInput.string(); // argument following -i
    args.back() = "manifest.mpd";
    // Windows CRT argument escaping: preserve spaces, quotes and trailing backslashes.
    auto quote = [](const std::wstring& arg) {
        std::wstring out = L"\"";
        size_t slashes = 0;
        for (wchar_t c : arg) {
            if (c == L'\\') { ++slashes; continue; }
            if (c == L'"') out.append(slashes * 2 + 1, L'\\');
            else out.append(slashes, L'\\');
            slashes = 0; out += c;
        }
        out.append(slashes * 2, L'\\');
        return out + L"\"";
    };
    std::wstring command;
    for (const auto& arg : args) {
        if (!command.empty()) command += L' ';
        command += quote(std::filesystem::path(arg).wstring());
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW(request.logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        result.error = "cannot open ffmpeg log or NUL";
        return result;
    }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input; startup.hStdOutput = log; startup.hStdError = log;
    PROCESS_INFORMATION process{};
    // No cmd.exe or PowerShell: arguments never undergo shell expansion.
    BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, workingDir.c_str(), &startup, &process);
    DWORD error = GetLastError();
    CloseHandle(input); CloseHandle(log);
    if (!started) { result.error = "cannot start ffmpeg: " + std::to_string(error); return result; }
    DWORD code = 1;
    bool waited = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
                  GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    result.exitCode = static_cast<int>(code);
    result.ok = waited && code == 0;
    if (!result.ok) result.error = "ffmpeg failed: " + std::to_string(code);
#else
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
    #endif
    if (result.ok) {
        std::error_code ec;
        bool init = false, chunk = false;
        for (std::filesystem::directory_iterator it(request.outputDir, ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (it->path().extension() != ".m4s" || !it->is_regular_file(ec)) continue;
            if (it->file_size(ec) == 0 || ec) continue;
            init |= name.rfind("init-", 0) == 0;
            chunk |= name.rfind("chunk-", 0) == 0;
        }
        if (ec || !init || !chunk) {
            result.ok = false;
            result.error = "FFmpeg did not produce DASH segments in the output directory";
        }
    }
    return result;
}

}  // namespace tecflix
