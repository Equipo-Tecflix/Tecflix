#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include "streaming/Config.hpp"
#include "streaming/JobQueue.hpp"
#include "streaming/Metrics.hpp"
#include "streaming/Transcoder.hpp"
#include "streaming/VideoStorage.hpp"
#include "test_support.hpp"

using namespace tecflix;
using testsupport::TempDir;

// ---------------------------------------------------------------- VideoStorage
TEST(VideoStorage, ValidatesIds) {
    EXPECT_TRUE(VideoStorage::isValidId("abc_DEF-123"));
    EXPECT_FALSE(VideoStorage::isValidId(""));
    EXPECT_FALSE(VideoStorage::isValidId("../etc"));
    EXPECT_FALSE(VideoStorage::isValidId("a/b"));
    EXPECT_FALSE(VideoStorage::isValidId(std::string(65, 'a')));
}

TEST(VideoStorage, ValidatesFileNames) {
    EXPECT_TRUE(VideoStorage::isValidFileName("manifest.mpd"));
    EXPECT_TRUE(VideoStorage::isValidFileName("chunk-0-00001.m4s"));
    EXPECT_FALSE(VideoStorage::isValidFileName(".hidden"));
    EXPECT_FALSE(VideoStorage::isValidFileName("a..b"));
    EXPECT_FALSE(VideoStorage::isValidFileName("../x"));
    EXPECT_FALSE(VideoStorage::isValidFileName("a/b.mpd"));
    EXPECT_FALSE(VideoStorage::isValidFileName(""));
}

TEST(VideoStorage, StatusRoundTrip) {
    TempDir tmp;
    VideoStorage s(tmp.path);
    EXPECT_EQ(s.readStatus("v1"), VideoStatus::NotFound);
    s.prepare("v1");
    for (auto st : {VideoStatus::Uploading, VideoStatus::Queued, VideoStatus::Processing,
                    VideoStatus::Ready, VideoStatus::Failed}) {
        s.writeStatus("v1", st);
        EXPECT_EQ(s.readStatus("v1"), st);
        EXPECT_EQ(statusFromString(toString(st)), st);
    }
    EXPECT_EQ(toString(VideoStatus::NotFound), "not_found");
    EXPECT_EQ(statusFromString("garbage"), VideoStatus::NotFound);
    EXPECT_EQ(s.readStatus("../bad"), VideoStatus::NotFound);
}

TEST(VideoStorage, ExistsRemoveAndList) {
    TempDir tmp;
    VideoStorage s(tmp.path);
    EXPECT_FALSE(s.exists("a"));
    s.prepare("b"); s.prepare("a");
    EXPECT_TRUE(s.exists("a"));
    EXPECT_EQ(s.listVideoIds(), (std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(s.remove("a"));
    EXPECT_FALSE(s.remove("a"));
    EXPECT_FALSE(s.remove("../a"));
    EXPECT_FALSE(s.exists("../a"));
}

TEST(VideoStorage, DashFilesAndAtomicWrite) {
    TempDir tmp;
    VideoStorage s(tmp.path);
    EXPECT_TRUE(s.writeDashFile("v", "chunk-1.m4s", "data1"));
    EXPECT_TRUE(s.writeDashFile("v", "manifest.mpd", "<MPD/>"));
    EXPECT_TRUE(s.writeDashFile("v", "init-0.m4s", "x"));
    EXPECT_FALSE(s.writeDashFile("v", "../evil", "x"));
    EXPECT_FALSE(s.writeDashFile("../v", "a.m4s", "x"));
    auto files = s.dashFilesForReplication("v");
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files.back(), "manifest.mpd");  // manifest always last
    EXPECT_TRUE(s.existingDashFile("v", "chunk-1.m4s").has_value());
    EXPECT_FALSE(s.existingDashFile("v", "nope.m4s").has_value());
    EXPECT_FALSE(s.existingDashFile("v", "../v/status.txt").has_value());
    EXPECT_TRUE(s.dashFilesForReplication("../x").empty());
}

TEST(VideoStorage, ReplicationSkipsTmpFilesAndMissingManifest) {
    TempDir tmp;
    VideoStorage s(tmp.path);
    s.prepare("v");
    std::ofstream(s.dashDir("v") / "a.m4s.tmp") << "partial";
    std::ofstream(s.dashDir("v") / "a.m4s") << "ok";
    auto files = s.dashFilesForReplication("v");
    EXPECT_EQ(files, (std::vector<std::string>{"a.m4s"}));
}

// ---------------------------------------------------------------- Metrics
TEST(Metrics, RendersCountersGaugesHistograms) {
    PrometheusMetrics m;
    m.incCounter("req_total", {{"route", "/a"}});
    m.incCounter("req_total", {{"route", "/a"}}, 2);
    m.incCounter("plain_total");
    m.setGauge("queue", {}, 7);
    m.observe("lat_seconds", {{"route", "/a"}}, 0.02);
    m.observe("lat_seconds", {{"route", "/a"}}, 1000);
    m.observe("nolabel_seconds", {}, 0.001);
    const std::string out = m.render();
    EXPECT_NE(out.find("req_total{route=\"/a\"} 3"), std::string::npos);
    EXPECT_NE(out.find("plain_total 1"), std::string::npos);
    EXPECT_NE(out.find("queue 7"), std::string::npos);
    EXPECT_NE(out.find("lat_seconds_bucket{route=\"/a\",le=\"0.025\"} 1"), std::string::npos);
    EXPECT_NE(out.find("lat_seconds_bucket{route=\"/a\",le=\"+Inf\"} 2"), std::string::npos);
    EXPECT_NE(out.find("lat_seconds_count{route=\"/a\"} 2"), std::string::npos);
    EXPECT_NE(out.find("nolabel_seconds_bucket{le=\"0.005\"} 1"), std::string::npos);
}

TEST(Metrics, EscapesLabelValues) {
    PrometheusMetrics m;
    m.incCounter("x_total", {{"k", "a\"b\\c\nd"}});
    EXPECT_NE(m.render().find("k=\"a\\\"b\\\\c\\nd\""), std::string::npos);
}

// ---------------------------------------------------------------- JobQueue
TEST(JobQueue, RunsAllJobs) {
    std::atomic<int> done{0};
    {
        JobQueue q(2);
        for (int i = 0; i < 10; ++i) q.submit([&] { ++done; });
        for (int i = 0; i < 200 && done < 10; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(done, 10);
}

TEST(JobQueue, RespectsMaxConcurrency) {
    std::atomic<int> current{0}, peak{0}, done{0};
    JobQueue q(2);
    for (int i = 0; i < 6; ++i) {
        q.submit([&] {
            int c = ++current;
            int p = peak.load();
            while (c > p && !peak.compare_exchange_weak(p, c)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            --current;
            ++done;
        });
    }
    for (int i = 0; i < 300 && done < 6; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(done, 6);
    EXPECT_LE(peak, 2);
    EXPECT_EQ(q.pending(), 0u);
}

TEST(JobQueue, RejectsAfterShutdownAndZeroWorkersBecomesOne) {
    JobQueue q(0);
    q.shutdown();
    EXPECT_FALSE(q.submit([] {}));
    q.shutdown();  // idempotent
}

TEST(JobQueue, ReportsRunning) {
    JobQueue q(1);
    std::atomic<bool> release{false}, started{false};
    q.submit([&] { started = true; while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    q.submit([] {});
    for (int i = 0; i < 200 && !started; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_EQ(q.running(), 1u);
    EXPECT_EQ(q.pending(), 1u);
    release = true;
}

// ---------------------------------------------------------------- Transcoder
TEST(Transcoder, BuildsArgumentVectorWithoutShell) {
    FfmpegTranscoder t("/usr/bin/ffmpeg");
    TranscodeRequest r{"/tmp/in put; rm -rf x.mp4", "/tmp/out", "/tmp/log", 6};
    auto args = t.buildArgs(r);
    EXPECT_EQ(args.front(), "/usr/bin/ffmpeg");
    // The hostile file name stays ONE argument, untouched.
    EXPECT_NE(std::find(args.begin(), args.end(), "/tmp/in put; rm -rf x.mp4"), args.end());
    EXPECT_NE(std::find(args.begin(), args.end(), "dash"), args.end());
    EXPECT_NE(std::find(args.begin(), args.end(), "6"), args.end());
    EXPECT_EQ(args.back(), "/tmp/out/manifest.mpd");
}

namespace {
std::string writeScript(const std::filesystem::path& dir, const std::string& name, const std::string& body) {
    auto p = dir / name;
    std::ofstream(p) << "#!/bin/sh\n" << body << "\n";
    std::filesystem::permissions(p, std::filesystem::perms::owner_all);
    return p.string();
}
}  // namespace

TEST(Transcoder, SuccessAndFailureExitCodes) {
    TempDir tmp;
    TranscodeRequest r{tmp.path / "in", tmp.path, tmp.path / "log", 4};
    EXPECT_TRUE(FfmpegTranscoder(writeScript(tmp.path, "ok.sh", "exit 0")).transcode(r).ok);
    auto bad = FfmpegTranscoder(writeScript(tmp.path, "bad.sh", "echo oops >&2; exit 3")).transcode(r);
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.exitCode, 3);
    std::ifstream log(r.logFile);
    std::string line; std::getline(log, line);
    EXPECT_EQ(line, "oops");  // stderr captured
}

TEST(Transcoder, MissingBinaryAndKilledProcess) {
    TempDir tmp;
    TranscodeRequest r{tmp.path / "in", tmp.path, tmp.path / "log", 4};
    auto missing = FfmpegTranscoder("/definitely/not/here").transcode(r);
    EXPECT_FALSE(missing.ok);
    EXPECT_FALSE(missing.error.empty());
    auto killed = FfmpegTranscoder(writeScript(tmp.path, "kill.sh", "kill -9 $$")).transcode(r);
    EXPECT_FALSE(killed.ok);
    EXPECT_EQ(killed.error, "ffmpeg terminated by signal");
}

// ---------------------------------------------------------------- Config
TEST(Config, DefaultsAndEnvOverrides) {
    unsetenv("TECFLIX_PORT");
    EXPECT_EQ(Config::fromEnv().port, 8083);
    setenv("TECFLIX_PORT", "9000", 1);
    setenv("TECFLIX_PEERS", "http://a:1,http://b:2,", 1);
    setenv("TECFLIX_MAX_JOBS", "0", 1);
    setenv("TECFLIX_NODE_ID", "n9", 1);
    setenv("TECFLIX_MAX_UPLOAD_BYTES", "notanumber", 1);
    Config c = Config::fromEnv();
    EXPECT_EQ(c.port, 9000);
    EXPECT_EQ(c.peers, (std::vector<std::string>{"http://a:1", "http://b:2"}));
    EXPECT_EQ(c.maxTranscodeJobs, 1u);
    EXPECT_EQ(c.nodeId, "n9");
    EXPECT_EQ(c.maxUploadBytes, 2ULL * 1024 * 1024 * 1024);  // invalid -> default
    for (auto v : {"TECFLIX_PORT", "TECFLIX_PEERS", "TECFLIX_MAX_JOBS", "TECFLIX_NODE_ID", "TECFLIX_MAX_UPLOAD_BYTES"})
        unsetenv(v);
}
