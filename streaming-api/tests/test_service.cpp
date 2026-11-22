#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <thread>

#include "streaming/StreamingService.hpp"
#include "test_support.hpp"

using namespace tecflix;
using testsupport::TempDir;

namespace {
struct Fixture {
    TempDir tmp;
    Config config;
    VideoStorage storage{tmp.path};
    testsupport::FakeTranscoder transcoder;
    testsupport::RecordingReplicator replicator;
    PrometheusMetrics metrics;
    JobQueue queue{1};
    StreamingService service{config, storage, transcoder, replicator, metrics, queue};

    void putRaw(const std::string& id) {
        ASSERT_EQ(service.beginUpload(id), StreamingService::BeginResult::Ok);
        std::ofstream(storage.rawPath(id)) << "raw-bytes";
    }
    bool waitFor(const std::string& id, VideoStatus s) {
        for (int i = 0; i < 300; ++i) {
            if (service.status(id) == s) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};
}  // namespace

TEST(Service, UploadLifecycleReachesReadyAndReplicates) {
    Fixture f;
    f.putRaw("v1");
    EXPECT_EQ(f.service.status("v1"), VideoStatus::Uploading);
    ASSERT_TRUE(f.service.completeUpload("v1"));
    ASSERT_TRUE(f.waitFor("v1", VideoStatus::Ready));
    for (int i = 0; i < 100 && f.replicator.replicated.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(f.replicator.replicated, (std::vector<std::string>{"v1"}));
    EXPECT_FALSE(std::filesystem::exists(f.storage.rawPath("v1")));  // raw deleted
    EXPECT_TRUE(std::filesystem::exists(f.storage.manifestPath("v1")));
    EXPECT_NE(f.metrics.render().find("tecflix_transcodes_total{result=\"ok\"} 1"), std::string::npos);
}

TEST(Service, RejectsInvalidAndDuplicateIds) {
    Fixture f;
    EXPECT_EQ(f.service.beginUpload("../x"), StreamingService::BeginResult::InvalidId);
    EXPECT_EQ(f.service.beginUpload("ok"), StreamingService::BeginResult::Ok);
    EXPECT_EQ(f.service.beginUpload("ok"), StreamingService::BeginResult::Conflict);
}

TEST(Service, AbortUploadForgetsVideo) {
    Fixture f;
    f.putRaw("v");
    f.service.abortUpload("v");
    EXPECT_EQ(f.service.status("v"), VideoStatus::NotFound);
}

TEST(Service, FailedTranscodeMarksFailed) {
    Fixture f;
    f.transcoder.succeed = false;
    f.putRaw("v");
    f.service.completeUpload("v");
    ASSERT_TRUE(f.waitFor("v", VideoStatus::Failed));
    EXPECT_TRUE(f.replicator.replicated.empty());
    EXPECT_NE(f.metrics.render().find("result=\"failed\""), std::string::npos);
}

TEST(Service, ReplicationIsRetried) {
    Fixture f;
    f.replicator.failFirst = 2;
    f.putRaw("v");
    f.service.completeUpload("v");
    for (int i = 0; i < 300 && f.replicator.replicated.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(f.replicator.replicated.size(), 1u);
}

TEST(Service, ReplicationFailureIsCountedButVideoStaysReady) {
    Fixture f;
    f.replicator.failFirst = 99;
    f.putRaw("v");
    f.service.completeUpload("v");
    ASSERT_TRUE(f.waitFor("v", VideoStatus::Ready));
    for (int i = 0; i < 300 && f.metrics.render().find("tecflix_replications_total{result=\"failed\"}") == std::string::npos; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_NE(f.metrics.render().find("tecflix_replications_total{result=\"failed\"} 1"), std::string::npos);
}

TEST(Service, CompleteUploadAfterShutdownFails) {
    Fixture f;
    f.queue.shutdown();
    f.putRaw("v");
    EXPECT_FALSE(f.service.completeUpload("v"));
    EXPECT_EQ(f.service.status("v"), VideoStatus::Failed);
}

TEST(Service, RemoveDeletesLocallyAndOnPeers) {
    Fixture f;
    f.putRaw("v");
    EXPECT_TRUE(f.service.remove("v"));
    EXPECT_EQ(f.replicator.deleted, (std::vector<std::string>{"v"}));
    EXPECT_FALSE(f.service.remove("v"));
    EXPECT_EQ(f.replicator.deleted.size(), 1u);  // nothing to propagate the 2nd time
}

TEST(Service, RecoverRequeuesInterruptedJobsAndDropsPartialUploads) {
    Fixture f;
    f.putRaw("interrupted");
    f.storage.writeStatus("interrupted", VideoStatus::Processing);
    f.putRaw("partial");                 // still "uploading"
    f.putRaw("lostraw");
    f.storage.writeStatus("lostraw", VideoStatus::Queued);
    std::filesystem::remove(f.storage.rawPath("lostraw"));
    f.storage.prepare("done"); f.storage.writeStatus("done", VideoStatus::Ready);

    EXPECT_EQ(f.service.recover(), 1u);
    EXPECT_TRUE(f.waitFor("interrupted", VideoStatus::Ready));
    EXPECT_EQ(f.service.status("partial"), VideoStatus::NotFound);
    EXPECT_EQ(f.service.status("lostraw"), VideoStatus::Failed);
    EXPECT_EQ(f.service.status("done"), VideoStatus::Ready);
}

TEST(Service, ReceiveReplicaStoresFilesAndMarksReadyOnManifest) {
    Fixture f;
    EXPECT_TRUE(f.service.receiveReplica("r", "chunk-0-00001.m4s", "abc"));
    EXPECT_EQ(f.service.status("r"), VideoStatus::Processing);  // incomplete copy not served
    EXPECT_TRUE(f.service.receiveReplica("r", "manifest.mpd", "<MPD/>"));
    EXPECT_EQ(f.service.status("r"), VideoStatus::Ready);
    EXPECT_FALSE(f.service.receiveReplica("r", "../evil", "x"));
    EXPECT_FALSE(f.service.receiveReplica("../r", "a.m4s", "x"));
    EXPECT_TRUE(f.service.removeReplica("r"));
    EXPECT_FALSE(f.service.removeReplica("r"));
}
