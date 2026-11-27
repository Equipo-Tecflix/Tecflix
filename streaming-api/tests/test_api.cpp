#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include "httplib.h"
#include "json.hpp"
#include "streaming/ApiServer.hpp"
#include "test_support.hpp"

using namespace tecflix;
using json = nlohmann::json;
using testsupport::TempDir;

namespace {

/// A complete node (storage + service + HTTP server) on an ephemeral port.
struct Node {
    TempDir tmp;
    Config config;
    VideoStorage storage;
    testsupport::FakeTranscoder transcoder;
    PrometheusMetrics metrics;
    std::unique_ptr<IReplicator> ownedReplicator;
    IReplicator* replicator;
    AllowAllAccessChecker allowAll;
    IAccessChecker* access;
    JobQueue queue;
    StreamingService service;
    ApiServer server;
    std::thread thread;
    int port = 0;

    static Config makeConfig(const std::string& id, std::uint64_t maxUpload, std::size_t chunkBytes = 64 * 1024) {
        Config c;
        c.nodeId = id;
        c.internalToken = "secret";
        c.maxUploadBytes = maxUpload;
        c.fileChunkBytes = chunkBytes;
        return c;
    }

    explicit Node(const std::string& id = "n1", std::uint64_t maxUpload = 10 * 1024 * 1024,
                  IReplicator* rep = nullptr, IAccessChecker* acc = nullptr,
                  std::size_t chunkBytes = 64 * 1024, std::size_t cacheBytes = 64 * 1024 * 1024)
        : config(makeConfig(id, maxUpload, chunkBytes)), storage(tmp.path, cacheBytes),
          ownedReplicator(rep ? nullptr : std::make_unique<NullReplicator>()),
          replicator(rep ? rep : ownedReplicator.get()), access(acc ? acc : &allowAll), queue(1),
          service(config, storage, transcoder, *replicator, metrics, queue),
          server(config, service, storage, *access, metrics, queue, [this] { return metrics.render(); }) {
        port = server.bindAnyPort();
        thread = std::thread([this] { server.serve(); });
        for (int i = 0; i < 200 && !server.isRunning(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ~Node() {
        server.stop();
        if (thread.joinable()) thread.join();
        queue.shutdown();
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
    httplib::Client client() const { return httplib::Client("127.0.0.1", port); }

    bool waitStatus(const std::string& id, const std::string& wanted) {
        auto c = client();
        for (int i = 0; i < 400; ++i) {
            auto r = c.Get("/videos/" + id + "/status");
            if (r && json::parse(r->body)["status"] == wanted) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};

}  // namespace

TEST(Api, HealthAndMetrics) {
    Node n("nodeA");
    auto c = n.client();
    auto h = c.Get("/health");
    ASSERT_TRUE(h);
    EXPECT_EQ(h->status, 200);
    EXPECT_EQ(json::parse(h->body)["node"], "nodeA");
    EXPECT_EQ(h->get_header_value("X-Tecflix-Node"), "nodeA");
    auto m = c.Get("/metrics");
    ASSERT_TRUE(m);
    EXPECT_NE(m->body.find("tecflix_http_requests_total"), std::string::npos);
    EXPECT_NE(m->body.find("route=\"/health\""), std::string::npos);
}

TEST(Api, FullVideoLifecycle) {
    Node n;
    auto c = n.client();
    auto up = c.Post("/videos/clip1/upload", std::string(2000, 'v'), "video/mp4");
    ASSERT_TRUE(up);
    EXPECT_EQ(up->status, 202);
    ASSERT_TRUE(n.waitStatus("clip1", "ready"));

    auto mpd = c.Get("/videos/clip1/manifest.mpd");
    ASSERT_TRUE(mpd);
    EXPECT_EQ(mpd->status, 200);
    EXPECT_EQ(mpd->get_header_value("Content-Type"), "application/dash+xml");
    EXPECT_EQ(mpd->get_header_value("Access-Control-Allow-Origin"), "*");
    EXPECT_EQ(mpd->body, "<MPD/>");

    auto seg = c.Get("/videos/clip1/chunk-0-00001.m4s");
    ASSERT_TRUE(seg);
    EXPECT_EQ(seg->status, 200);
    EXPECT_EQ(seg->body.size(), 5000u);
    EXPECT_EQ(seg->get_header_value("Content-Type"), "video/iso.segment");
    EXPECT_EQ(seg->get_header_value("Accept-Ranges"), "bytes");

    auto part = c.Get("/videos/clip1/chunk-0-00001.m4s", {{"Range", "bytes=10-19"}});
    ASSERT_TRUE(part);
    EXPECT_EQ(part->status, 206);
    EXPECT_EQ(part->body.size(), 10u);
    EXPECT_EQ(part->get_header_value("Content-Range"), "bytes 10-19/5000");

    auto bad = c.Get("/videos/clip1/chunk-0-00001.m4s", {{"Range", "bytes=99999-"}});
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->status, 416);

    EXPECT_NE(n.metrics.render().find("tecflix_bytes_served_total"), std::string::npos);

    auto del = c.Delete("/videos/clip1");
    ASSERT_TRUE(del);
    EXPECT_EQ(del->status, 200);
    EXPECT_EQ(c.Get("/videos/clip1/status")->status, 404);
    EXPECT_EQ(c.Delete("/videos/clip1")->status, 404);
}

TEST(Api, MultipartUpload) {
    Node n;
    auto c = n.client();
    httplib::MultipartFormDataItems items = {
        {"title", "ignored text field", "", ""},
        {"file", std::string(1234, 'm'), "movie.mp4", "video/mp4"},
    };
    auto up = c.Post("/videos/multi/upload", items);
    ASSERT_TRUE(up);
    EXPECT_EQ(up->status, 202);
    EXPECT_EQ(json::parse(up->body)["bytes"], 1234);  // only the file part is stored
    EXPECT_TRUE(n.waitStatus("multi", "ready"));
}

TEST(Api, UploadValidation) {
    Node n("n", 1000);
    auto c = n.client();
    EXPECT_EQ(c.Post("/videos/e/upload", "", "video/mp4")->status, 400);          // empty
    EXPECT_EQ(c.Get("/videos/e/status")->status, 404);                              // cleaned up
    EXPECT_EQ(c.Post("/videos/big/upload", std::string(5000, 'x'), "video/mp4")->status, 413);
    EXPECT_EQ(c.Get("/videos/big/status")->status, 404);
    EXPECT_EQ(c.Post("/videos/" + std::string(65, 'a') + "/upload", "x", "video/mp4")->status, 404);
    ASSERT_EQ(c.Post("/videos/dup/upload", "data", "video/mp4")->status, 202);
    auto dup = c.Post("/videos/dup/upload", "data", "video/mp4");
    ASSERT_TRUE(dup);
    EXPECT_EQ(dup->status, 409);
}

TEST(Api, MultipartTooLargeAndBrokenMultipart) {
    Node n("n", 1000);
    auto c = n.client();
    httplib::MultipartFormDataItems big = {{"file", std::string(3000, 'x'), "a.mp4", "video/mp4"}};
    auto r = c.Post("/videos/bigmp/upload", big);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 413);
    httplib::MultipartFormDataItems textOnly = {{"title", "no file here", "", ""}};
    EXPECT_EQ(c.Post("/videos/nofile/upload", textOnly)->status, 400);
}

TEST(Api, NotReadyThenFailedVideosAreNotServed) {
    Node n;
    n.transcoder.block = true;
    auto c = n.client();
    ASSERT_EQ(c.Post("/videos/slow/upload", "data", "video/mp4")->status, 202);
    ASSERT_TRUE(n.waitStatus("slow", "processing"));
    EXPECT_EQ(c.Get("/videos/slow/manifest.mpd")->status, 404);
    n.transcoder.succeed = false;
    n.transcoder.block = false;
    ASSERT_TRUE(n.waitStatus("slow", "failed"));
    EXPECT_EQ(c.Get("/videos/slow/manifest.mpd")->status, 404);
}

TEST(Api, MissingFilesAndTraversalAreRejected) {
    Node n;
    auto c = n.client();
    ASSERT_EQ(c.Post("/videos/v/upload", "data", "video/mp4")->status, 202);
    ASSERT_TRUE(n.waitStatus("v", "ready"));
    EXPECT_EQ(c.Get("/videos/v/nothere.m4s")->status, 404);
    EXPECT_EQ(c.Get("/videos/v/status.txt")->status, 404);  // extension not allowed
    EXPECT_EQ(c.Get("/videos/v/..%2F..%2Fx.mpd")->status, 404);
    EXPECT_EQ(c.Get("/unknown")->status, 404);
}

TEST(Api, AccessDeniedReturns403ForEveryRoute) {
    testsupport::DenyAccessChecker deny;
    Node n("n", 1024 * 1024, nullptr, &deny);
    auto c = n.client();
    EXPECT_EQ(c.Post("/videos/x/upload", "data", "video/mp4")->status, 403);
    EXPECT_EQ(c.Get("/videos/x/status")->status, 403);
    EXPECT_EQ(c.Get("/videos/x/manifest.mpd")->status, 403);
    EXPECT_EQ(c.Delete("/videos/x")->status, 403);
    deny.allowWrite = true;
    EXPECT_EQ(c.Post("/videos/x/upload", "data", "video/mp4")->status, 202);
    EXPECT_EQ(c.Get("/videos/x/manifest.mpd")->status, 403);  // still no read access
}

TEST(Api, CorsPreflight) {
    Node n;
    auto c = n.client();
    auto r = c.Options("/videos/x/manifest.mpd");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 204);
    EXPECT_NE(r->get_header_value("Access-Control-Allow-Headers").find("Range"), std::string::npos);
    EXPECT_NE(r->get_header_value("Access-Control-Allow-Methods").find("DELETE"), std::string::npos);
}

TEST(Api, InternalRoutesRequireToken) {
    Node n;
    auto c = n.client();
    EXPECT_EQ(c.Put("/internal/replicate/r/a.m4s", "x", "application/octet-stream")->status, 403);
    httplib::Headers h{{"X-Tecflix-Token", "secret"}};
    EXPECT_EQ(c.Put("/internal/replicate/r/a.m4s", h, "x", "application/octet-stream")->status, 200);
    EXPECT_EQ(c.Put("/internal/replicate/r/manifest.mpd", h, "<MPD/>", "application/octet-stream")->status, 200);
    EXPECT_EQ(c.Get("/videos/r/manifest.mpd")->status, 200);
    EXPECT_EQ(c.Delete("/internal/replicate/r")->status, 403);
    EXPECT_EQ(c.Delete("/internal/replicate/r", h)->status, 200);
    EXPECT_EQ(c.Delete("/internal/replicate/r", h)->status, 404);
}

TEST(Api, InternalTokenEmptyMeansDisabled) {
    Node n;
    n.config.internalToken.clear();
    auto c = n.client();
    EXPECT_EQ(c.Put("/internal/replicate/r/a.m4s", {{"X-Tecflix-Token", ""}}, "x", "application/octet-stream")->status, 403);
}

TEST(Api, RouteLabelsHaveLowCardinality) {
    EXPECT_EQ(ApiServer::routeLabel("GET", "/health"), "/health");
    EXPECT_EQ(ApiServer::routeLabel("GET", "/metrics"), "/metrics");
    EXPECT_EQ(ApiServer::routeLabel("POST", "/videos/abc/upload"), "/videos/{id}/upload");
    EXPECT_EQ(ApiServer::routeLabel("GET", "/videos/abc/status"), "/videos/{id}/status");
    EXPECT_EQ(ApiServer::routeLabel("GET", "/videos/abc/chunk-1.m4s"), "/videos/{id}/{file}");
    EXPECT_EQ(ApiServer::routeLabel("DELETE", "/videos/abc"), "/videos/{id}");
    EXPECT_EQ(ApiServer::routeLabel("PUT", "/internal/replicate/a/b.m4s"), "/internal/replicate/{id}/{file}");
    EXPECT_EQ(ApiServer::routeLabel("DELETE", "/internal/replicate/a"), "/internal/replicate/{id}");
    EXPECT_EQ(ApiServer::routeLabel("OPTIONS", "/whatever/x"), "cors-preflight");
    EXPECT_EQ(ApiServer::routeLabel("GET", "/whatever/x"), "other");
}

// ------------------------------------------------------------ V2.0 redundancy
TEST(Redundancy, VideoUploadedToNodeAIsServedByNodeBAndDeletedOnBoth) {
    Node b("nodeB");
    VideoStorage* storageA = nullptr;
    // Node A needs a storage reference for its replicator, so build the pieces by hand.
    TempDir tmpA;
    VideoStorage storage(tmpA.path);
    storageA = &storage;
    HttpReplicator replicator({b.url()}, "secret", *storageA);

    Config cfgA = Node::makeConfig("nodeA", 10 * 1024 * 1024);
    testsupport::FakeTranscoder transcoder;
    PrometheusMetrics metrics;
    AllowAllAccessChecker allow;
    JobQueue queue(1);
    StreamingService service(cfgA, storage, transcoder, replicator, metrics, queue);
    ApiServer server(cfgA, service, storage, allow, metrics, queue, [&] { return metrics.render(); });
    int port = server.bindAnyPort();
    std::thread t([&] { server.serve(); });
    for (int i = 0; i < 200 && !server.isRunning(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));

    httplib::Client a("127.0.0.1", port);
    ASSERT_EQ(a.Post("/videos/shared/upload", "payload", "video/mp4")->status, 202);

    ASSERT_TRUE(b.waitStatus("shared", "ready"));          // copy arrived on the other node
    auto fromB = b.client().Get("/videos/shared/chunk-0-00001.m4s");
    ASSERT_TRUE(fromB);
    EXPECT_EQ(fromB->status, 200);
    EXPECT_EQ(fromB->get_header_value("X-Tecflix-Node"), "nodeB");
    EXPECT_EQ(fromB->body.size(), 5000u);

    ASSERT_EQ(a.Delete("/videos/shared")->status, 200);     // deletion propagates
    EXPECT_EQ(b.client().Get("/videos/shared/status")->status, 404);

    server.stop(); t.join(); queue.shutdown();
}

TEST(Redundancy, ReplicatorReportsFailureWhenPeerIsDown) {
    TempDir tmp;
    VideoStorage storage(tmp.path);
    storage.writeDashFile("v", "a.m4s", "x");
    storage.writeDashFile("v", "manifest.mpd", "<MPD/>");
    HttpReplicator r({"http://127.0.0.1:1"}, "secret", storage);  // nothing listens on port 1
    EXPECT_FALSE(r.replicate("v"));
    EXPECT_FALSE(r.propagateDelete("v"));
}

TEST(Redundancy, ReplicatorRejectedByPeerWithWrongToken) {
    Node peer;
    TempDir tmp;
    VideoStorage storage(tmp.path);
    storage.writeDashFile("v", "a.m4s", "x");
    storage.writeDashFile("v", "manifest.mpd", "<MPD/>");
    HttpReplicator r({peer.url()}, "wrong-token", storage);
    EXPECT_FALSE(r.replicate("v"));
    EXPECT_EQ(peer.client().Get("/videos/v/manifest.mpd")->status, 404);  // never exposed
}

TEST(NullReplicator, AlwaysSucceeds) {
    NullReplicator r;
    EXPECT_TRUE(r.replicate("x"));
    EXPECT_TRUE(r.propagateDelete("x"));
}

// ------------------------------------------------------------ access checker
TEST(MetadataAccessChecker, AsksMetadataApiAndForwardsAuthorization) {
    httplib::Server meta;
    std::string seenAuth, seenPerm;
    meta.Get(R"(/videos/([a-z]+)/access)", [&](const httplib::Request& req, httplib::Response& res) {
        seenAuth = req.get_header_value("Authorization");
        seenPerm = req.get_param_value("permission");
        res.status = req.matches[1] == "mine" ? 200 : 403;
    });
    int port = meta.bind_to_any_port("127.0.0.1");
    std::thread t([&] { meta.listen_after_bind(); });
    meta.wait_until_ready();

    MetadataAccessChecker checker("http://127.0.0.1:" + std::to_string(port));
    EXPECT_TRUE(checker.allowed("mine", Permission::Read, "Bearer abc"));
    EXPECT_EQ(seenAuth, "Bearer abc");
    EXPECT_EQ(seenPerm, "read");
    EXPECT_TRUE(checker.allowed("mine", Permission::Write, ""));
    EXPECT_EQ(seenPerm, "write");
    EXPECT_FALSE(checker.allowed("other", Permission::Read, "Bearer abc"));
    meta.stop(); t.join();
    EXPECT_FALSE(checker.allowed("mine", Permission::Read, "Bearer abc"));  // API down -> deny
}

TEST(AllowAllAccessChecker, AllowsEverything) {
    AllowAllAccessChecker a;
    EXPECT_TRUE(a.allowed("x", Permission::Write, ""));
}

TEST(Api, SegmentCachePreservesRangeAndInvalidatesReplicas) {
    Node n;
    auto c = n.client();
    const httplib::Headers h{{"X-Tecflix-Token", "secret"}};
    const std::string segment = "/videos/cache/a.m4s";
    ASSERT_EQ(c.Put("/internal/replicate/cache/a.m4s", h, "abcdef", "application/octet-stream")->status, 200);
    ASSERT_EQ(c.Put("/internal/replicate/cache/manifest.mpd", h, "<MPD/>", "application/octet-stream")->status, 200);
    auto first = c.Get(segment);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->body, "abcdef");
    auto range = c.Get(segment, {{"Range", "bytes=1-3"}});
    ASSERT_TRUE(range);
    EXPECT_EQ(range->status, 206);
    EXPECT_EQ(range->body, "bcd");
    EXPECT_EQ(range->get_header_value("Content-Range"), "bytes 1-3/6");
    auto metrics = c.Get("/metrics");
    ASSERT_TRUE(metrics);
    EXPECT_NE(metrics->body.find("tecflix_segment_cache_requests_total{result=\"miss\"} 1"), std::string::npos);
    EXPECT_NE(metrics->body.find("tecflix_segment_cache_requests_total{result=\"hit\"} 1"), std::string::npos);
    ASSERT_EQ(c.Put("/internal/replicate/cache/a.m4s", h, "UVWXYZ", "application/octet-stream")->status, 200);
    auto replaced = c.Get(segment);
    ASSERT_TRUE(replaced);
    EXPECT_EQ(replaced->body, "UVWXYZ");
    ASSERT_EQ(c.Delete("/internal/replicate/cache", h)->status, 200);
    auto removed = c.Get(segment);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->status, 404);
}

TEST(Api, ConfigurableFileChunksPreserveContentAndRanges) {
    std::string payload(1024 * 1024 + 37, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 31 + i / 251) % 256);
    for (std::size_t kib : {64u, 128u, 256u, 512u, 1024u}) {
        SCOPED_TRACE(kib);
        Node n("chunk", 10 * 1024 * 1024, nullptr, nullptr, kib * 1024, 0);
        auto c = n.client();
        const httplib::Headers auth{{"X-Tecflix-Token", "secret"}};
        auto put = c.Put("/internal/replicate/blocks/a.m4s", auth, payload, "application/octet-stream");
        ASSERT_TRUE(put);
        ASSERT_EQ(put->status, 200);
        auto manifest = c.Put("/internal/replicate/blocks/manifest.mpd", auth, "<MPD/>", "application/octet-stream");
        ASSERT_TRUE(manifest);
        ASSERT_EQ(manifest->status, 200);
        auto full = c.Get("/videos/blocks/a.m4s");
        ASSERT_TRUE(full);
        EXPECT_EQ(full->status, 200);
        EXPECT_EQ(full->body, payload);
        const auto start = kib * 1024 - 13;
        const auto end = payload.size() - 2;
        auto range = c.Get("/videos/blocks/a.m4s", {{"Range", "bytes=" + std::to_string(start) + "-" + std::to_string(end)}});
        ASSERT_TRUE(range);
        EXPECT_EQ(range->status, 206);
        EXPECT_EQ(range->body, payload.substr(start, end - start + 1));
        EXPECT_EQ(range->get_header_value("Content-Range"), "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(payload.size()));
    }
}
