#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "httplib.h"
#include "json.hpp"
#include "streaming/OtelTrace.hpp"
#include "streaming/Transcoder.hpp"
#include "test_support.hpp"

using namespace tecflix;
using json = nlohmann::json;

TEST(OtelTrace, ExportsHttpSpansOnShutdown) {
    httplib::Server collector;
    std::mutex mutex;
    std::vector<json> received;
    collector.Post("/v1/traces", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded()) { res.status = 400; return; }
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(std::move(body));
        res.set_content("{}", "application/json");
    });
    const int port = collector.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread server([&] { collector.listen_after_bind(); });
    collector.wait_until_ready();
    {
        OtelTrace trace("http://127.0.0.1:" + std::to_string(port), "test-node");
        const auto start = std::chrono::system_clock::now();
        trace.record("GET", "/videos/:id/manifest.mpd", 200, start);
        trace.record("POST", "/videos/:id/upload", 500, start);
    } // destructor drains queued spans before returning
    collector.stop();
    server.join();
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_FALSE(received.empty());
    json spans = json::array();
    for (const auto& batch : received) {
        const auto& resource = batch.at("resourceSpans").at(0);
        EXPECT_EQ(resource.at("resource").at("attributes").at(1).at("value").at("stringValue"), "test-node");
        for (const auto& span : resource.at("scopeSpans").at(0).at("spans")) spans.push_back(span);
    }
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans.at(0).at("name"), "GET /videos/:id/manifest.mpd");
    EXPECT_EQ(spans.at(0).at("status").at("code"), 0);
    EXPECT_EQ(spans.at(1).at("status").at("code"), 2);
    EXPECT_EQ(spans.at(0).at("traceId").get<std::string>().size(), 32u);
    EXPECT_EQ(spans.at(0).at("spanId").get<std::string>().size(), 16u);
}

TEST(Transcoder, RejectsUnavailableLogDirectory) {
    testsupport::TempDir tmp;
    TranscodeRequest request{tmp.path / "input.mp4", tmp.path,
                             tmp.path / "missing-directory" / "ffmpeg.log", 4};
    FfmpegTranscoder transcoder((tmp.path / "missing-ffmpeg").string());
    const auto result = transcoder.transcode(request);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}
