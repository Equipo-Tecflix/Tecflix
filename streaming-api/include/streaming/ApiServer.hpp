#pragma once
#include <functional>
#include <memory>
#include <string>

#include "streaming/AccessChecker.hpp"
#include "streaming/Config.hpp"
#include "streaming/JobQueue.hpp"
#include "streaming/Metrics.hpp"
#include "streaming/OtelTrace.hpp"
#include "streaming/StreamingService.hpp"
#include "streaming/VideoStorage.hpp"

namespace httplib { class Server; }

namespace tecflix {

/// HTTP layer (REST + DASH delivery). Routes:
///   POST   /videos/{id}/upload            raw body or multipart, starts transcoding
///   GET    /videos/{id}/status            {"id","status"}
///   GET    /videos/{id}/manifest.mpd      DASH manifest
///   GET    /videos/{id}/{file}.m4s        init / media segments (Range supported)
///   DELETE /videos/{id}
///   GET    /health   GET /metrics
///   PUT    /internal/replicate/{id}/{file}, DELETE /internal/replicate/{id}  (peers only)
class ApiServer {
public:
    ApiServer(const Config& config, StreamingService& service, VideoStorage& storage,
              IAccessChecker& access, IMetrics& metrics, JobQueue& queue,
              std::function<std::string()> metricsRenderer);
    ~ApiServer();

    /// Blocks until stop() is called. Returns false if the port cannot be bound.
    bool listen(const std::string& host = "0.0.0.0");
    /// Binds on an ephemeral port (tests) and returns it, 0 on failure. Then call serve().
    int bindAnyPort(const std::string& host = "127.0.0.1");
    void serve();
    void stop();
    bool isRunning() const;

    /// Low-cardinality route name used as metric label (e.g. "/videos/{id}/{file}").
    static std::string routeLabel(const std::string& method, const std::string& path);

private:
    void registerRoutes();

    const Config& config_;
    StreamingService& service_;
    VideoStorage& storage_;
    IAccessChecker& access_;
    IMetrics& metrics_;
    JobQueue& queue_;
    std::function<std::string()> metricsRenderer_;
    OtelTrace traces_;
    std::unique_ptr<httplib::Server> server_;
};

}  // namespace tecflix
