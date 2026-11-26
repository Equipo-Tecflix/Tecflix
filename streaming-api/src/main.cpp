#include <csignal>
#include <iostream>
#include <memory>

#include "streaming/AccessChecker.hpp"
#include "streaming/ApiServer.hpp"
#include "streaming/Config.hpp"
#include "streaming/JobQueue.hpp"
#include "streaming/Metrics.hpp"
#include "streaming/Replicator.hpp"
#include "streaming/StreamingService.hpp"
#include "streaming/Transcoder.hpp"
#include "streaming/VideoStorage.hpp"

namespace {
tecflix::ApiServer* g_server = nullptr;
void onSignal(int) {
    if (g_server != nullptr) g_server->stop();
}
}  // namespace

int main() {
    using namespace tecflix;
    const Config config = Config::fromEnv();

    if (!config.peers.empty() && config.internalToken.empty()) {
        std::cerr << "TECFLIX_INTERNAL_TOKEN required for replication\n";
        return 1;
    }
    VideoStorage storage(config.storageDir, config.segmentCacheBytes);
    PrometheusMetrics metrics;
    FfmpegTranscoder transcoder(config.ffmpegPath);

    std::unique_ptr<IReplicator> replicator;
    if (config.peers.empty()) replicator = std::make_unique<NullReplicator>();
    else replicator = std::make_unique<HttpReplicator>(config.peers, config.internalToken, storage);

    std::unique_ptr<IAccessChecker> access;
    if (config.metadataUrl.empty()) access = std::make_unique<AllowAllAccessChecker>();
    else access = std::make_unique<MetadataAccessChecker>(config.metadataUrl);

    JobQueue queue(config.maxTranscodeJobs);
    StreamingService service(config, storage, transcoder, *replicator, metrics, queue);
    ApiServer server(config, service, storage, *access, metrics, queue, [&] { return metrics.render(); });

    g_server = &server;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const std::size_t requeued = service.recover();
    std::cout << "[" << config.nodeId << "] Tecflix streaming API on :" << config.port
              << " storage=" << config.storageDir << " peers=" << config.peers.size()
              << " auth=" << (config.metadataUrl.empty() ? "off" : "metadata-api")
              << " cache_bytes=" << config.segmentCacheBytes
              << " trace_sample_rate=" << config.traceSampleRate
              << " traces=" << (config.otlpEndpoint.empty() ? "off" : "on")
              << " requeued=" << requeued << std::endl;

    if (!server.listen()) {
        std::cerr << "cannot bind port " << config.port << std::endl;
        return 1;
    }
    queue.shutdown();
    return 0;
}
