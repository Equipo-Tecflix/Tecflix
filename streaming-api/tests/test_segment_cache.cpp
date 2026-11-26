#include <gtest/gtest.h>
#include "streaming/VideoStorage.hpp"
#include "test_support.hpp"
using namespace tecflix;
using testsupport::TempDir;
using CR = VideoStorage::CacheResult;

TEST(SegmentCache, LruEvictionAndSnapshotLifetime) {
    TempDir t;
    VideoStorage s(t.path, 8);
    ASSERT_TRUE(s.writeDashFile("v", "a.m4s", "aaaa"));
    ASSERT_TRUE(s.writeDashFile("v", "b.m4s", "bbbb"));
    ASSERT_TRUE(s.writeDashFile("v", "c.m4s", "cccc"));
    auto a = s.cachedSegment("v", "a.m4s");
    ASSERT_TRUE(a.data);
    EXPECT_EQ(a.result, CR::Miss);
    EXPECT_EQ(s.cachedSegment("v", "b.m4s").result, CR::Miss);
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Hit);
    EXPECT_EQ(s.cachedSegment("v", "c.m4s").result, CR::Miss);
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Hit);
    EXPECT_EQ(s.cachedSegment("v", "b.m4s").result, CR::Miss);
    EXPECT_EQ(*a.data, "aaaa");
    ASSERT_TRUE(s.remove("v"));
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Bypass);
    EXPECT_EQ(*a.data, "aaaa");
}

TEST(SegmentCache, BypassAndInvalidation) {
    TempDir t;
    VideoStorage s(t.path, 4);
    ASSERT_TRUE(s.writeDashFile("v", "a.m4s", "1234"));
    ASSERT_TRUE(s.writeDashFile("v", "big.m4s", "12345"));
    ASSERT_TRUE(s.writeDashFile("v", "empty.m4s", ""));
    EXPECT_EQ(s.cachedSegment("v", "big.m4s").result, CR::Bypass);
    EXPECT_EQ(s.cachedSegment("v", "empty.m4s").result, CR::Bypass);
    EXPECT_EQ(s.cachedSegment("v", "missing.m4s").result, CR::Bypass);
    EXPECT_EQ(s.cachedSegment("../v", "a.m4s").result, CR::Bypass);
    EXPECT_EQ(s.cachedSegment("v", "../a.m4s").result, CR::Bypass);
    EXPECT_EQ(s.cachedSegment("v", "manifest.mpd").result, CR::Bypass);
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Miss);
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Hit);
    ASSERT_TRUE(s.writeDashFile("v", "a.m4s", "5678"));
    auto replacement = s.cachedSegment("v", "a.m4s");
    ASSERT_TRUE(replacement.data);
    EXPECT_EQ(replacement.result, CR::Miss);
    EXPECT_EQ(*replacement.data, "5678");
    s.writeStatus("v", VideoStatus::Processing);
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Miss);
    s.prepare("v");
    EXPECT_EQ(s.cachedSegment("v", "a.m4s").result, CR::Miss);
    VideoStorage disabled(t.path, 0);
    EXPECT_EQ(disabled.cachedSegment("v", "a.m4s").result, CR::Bypass);
}

TEST(SegmentCache, ConcurrentReadersShareImmutablePayload) {
    TempDir t;
    VideoStorage s(t.path, 1024);
    ASSERT_TRUE(s.writeDashFile("v", "a.m4s", "payload"));
    auto first = s.cachedSegment("v", "a.m4s");
    ASSERT_TRUE(first.data);
    std::atomic<int> hits{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 100; ++j) {
            auto r = s.cachedSegment("v", "a.m4s");
            if (r.result == CR::Hit && r.data == first.data) ++hits;
        }
    });
    for (auto& worker : workers) worker.join();
    EXPECT_EQ(hits, 800);
}
