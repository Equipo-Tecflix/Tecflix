#include "streaming/ApiServer.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <regex>

#include "httplib.h"
#include "json.hpp"

namespace tecflix {
namespace {

using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;

thread_local clock_type::time_point g_requestStart;
thread_local std::chrono::system_clock::time_point g_traceStart;

constexpr const char* kIdPattern = "([A-Za-z0-9_-]{1,64})";
constexpr const char* kFilePattern = "([A-Za-z0-9_][A-Za-z0-9_.-]{0,127})";

void sendJson(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

std::string contentTypeFor(const std::string& file) {
    if (file.size() > 4 && file.compare(file.size() - 4, 4, ".mpd") == 0) return "application/dash+xml";
    if (file.size() > 4 && file.compare(file.size() - 4, 4, ".m4s") == 0) return "video/iso.segment";
    return "video/mp4";
}

/// Streams a file in 64 KiB chunks. The size is declared up-front so httplib can
/// answer "Range: bytes=a-b" requests (206 Partial Content) by itself.
void sendFile(httplib::Response& res, const std::filesystem::path& path, const std::string& type,
              IMetrics& metrics) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    auto in = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (ec || !*in) return sendJson(res, 404, {{"error", "file not found"}});
    res.set_content_provider(
        static_cast<std::size_t>(size), type,
        [in, &metrics](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
            constexpr std::size_t kChunk = 64 * 1024;
            std::vector<char> buffer(std::min(length, kChunk));
            in->clear();
            in->seekg(static_cast<std::streamoff>(offset));
            in->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = static_cast<std::size_t>(in->gcount());
            if (got == 0) return false;
            metrics.incCounter("tecflix_bytes_served_total", {}, static_cast<double>(got));
            return sink.write(buffer.data(), got);
        });
}

}  // namespace

std::string ApiServer::routeLabel(const std::string& method, const std::string& path) {
    static const std::regex upload("^/videos/[^/]+/upload$"), status("^/videos/[^/]+/status$"),
        file("^/videos/[^/]+/[^/]+$"), video("^/videos/[^/]+$"),
        replicaFile("^/internal/replicate/[^/]+/[^/]+$"), replica("^/internal/replicate/[^/]+$");
    if (path == "/health" || path == "/metrics") return path;
    if (std::regex_match(path, upload)) return "/videos/{id}/upload";
    if (std::regex_match(path, status)) return "/videos/{id}/status";
    if (std::regex_match(path, file)) return "/videos/{id}/{file}";
    if (std::regex_match(path, video)) return "/videos/{id}";
    if (std::regex_match(path, replicaFile)) return "/internal/replicate/{id}/{file}";
    if (std::regex_match(path, replica)) return "/internal/replicate/{id}";
    return method == "OPTIONS" ? "cors-preflight" : "other";
}

ApiServer::ApiServer(const Config& config, StreamingService& service, VideoStorage& storage,
                     IAccessChecker& access, IMetrics& metrics, JobQueue& queue,
                     std::function<std::string()> metricsRenderer)
    : config_(config), service_(service), storage_(storage), access_(access), metrics_(metrics),
      queue_(queue), metricsRenderer_(std::move(metricsRenderer)),
      traces_(config.otlpEndpoint, config.nodeId, config.traceSampleRate),
      server_(std::make_unique<httplib::Server>()) {
    registerRoutes();
}

ApiServer::~ApiServer() { stop(); }

bool ApiServer::listen(const std::string& host) { return server_->listen(host, config_.port); }

int ApiServer::bindAnyPort(const std::string& host) { return server_->bind_to_any_port(host); }

void ApiServer::serve() { server_->listen_after_bind(); }

void ApiServer::stop() { server_->stop(); }

bool ApiServer::isRunning() const { return server_->is_running(); }

void ApiServer::registerRoutes() {
    httplib::Server& svr = *server_;
    // Our own byte counter enforces maxUploadBytes (-> 413); httplib's limit is only a
    // hard safety net with 1 MiB of slack for multipart framing.
    svr.set_payload_max_length(static_cast<std::size_t>(config_.maxUploadBytes) + 1024 * 1024);
    svr.set_keep_alive_max_count(1);
    svr.set_keep_alive_timeout(1);

    // Explicit HTTP worker pool.
    svr.new_task_queue = [] {
        return new httplib::ThreadPool(32);
    };

    // ---- cross-cutting: timing, metrics, CORS --------------------------------
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response&) {
        g_requestStart = clock_type::now();
        g_traceStart = std::chrono::system_clock::now();
        return httplib::Server::HandlerResponse::Unhandled;
    });
    svr.set_post_routing_handler([this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", config_.corsOrigin);
        res.set_header("Access-Control-Expose-Headers", "Content-Length, Content-Range, Accept-Ranges");
        res.set_header("X-Tecflix-Node", config_.nodeId);
    });
    svr.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
        const double seconds = std::chrono::duration<double>(clock_type::now() - g_requestStart).count();
        const std::string route = routeLabel(req.method, req.path);
        if (req.path != "/health" && req.path != "/metrics") {
            const bool segment = route == "/videos/{id}/{file}" && req.path.size() > 4 &&
                req.path.compare(req.path.size() - 4, 4, ".m4s") == 0;
            traces_.record(req.method, route, res.status, g_traceStart, segment);
        }
        Labels labels{{"method", req.method}, {"route", route}, {"status", std::to_string(res.status)}};
        metrics_.incCounter("tecflix_http_requests_total", labels);
        metrics_.observe("tecflix_http_request_duration_seconds", {{"route", route}, {"method", req.method}}, seconds);
    });
    svr.set_exception_handler([this](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        metrics_.incCounter("tecflix_http_exceptions_total");
        sendJson(res, 500, {{"error", "internal error"}});
    });

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type, Range");
        res.set_header("Access-Control-Max-Age", "86400");
        res.status = 204;
    });

    // ---- operations ------------------------------------------------------------
    svr.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, {{"status", "ok"},
                            {"node", config_.nodeId},
                            {"queue_pending", queue_.pending()},
                            {"jobs_running", queue_.running()}});
    });

    svr.Get("/metrics", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(metricsRenderer_() + traces_.renderMetrics(), "text/plain; version=0.0.4");
    });

    // ---- upload ----------------------------------------------------------------
    svr.Post(std::string("/videos/") + kIdPattern + "/upload",
             [this](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
        const std::string id = req.matches[1];
        if (!access_.allowed(id, Permission::Write, req.get_header_value("Authorization"))) {
            res.set_header("Connection", "close");
            return sendJson(res, 403, {{"error", "forbidden"}});
        }
        auto begin = service_.beginUpload(id);
        if (begin != StreamingService::BeginResult::Ok) {
            res.set_header("Connection", "close");
            return sendJson(res, begin == StreamingService::BeginResult::Conflict ? 409 : 400,
                            {{"error", begin == StreamingService::BeginResult::Conflict
                                           ? "video already exists, delete it first"
                                           : "invalid video id"}});
        }

        std::ofstream out(storage_.rawPath(id), std::ios::binary | std::ios::trunc);
        std::uint64_t total = 0;
        bool tooLarge = false;
        auto write = [&](const char* data, std::size_t len) {
            total += len;
            if (total > config_.maxUploadBytes) { tooLarge = true; return false; }
            out.write(data, static_cast<std::streamsize>(len));
            return static_cast<bool>(out);
        };

        bool ok;
        if (req.is_multipart_form_data()) {
            bool inFile = false;
            ok = reader(
                [&](const httplib::MultipartFormData& part) { inFile = !part.filename.empty(); return true; },
                [&](const char* d, std::size_t n) { return inFile ? write(d, n) : true; });
        } else {
            ok = reader([&](const char* d, std::size_t n) { return write(d, n); });
        }
        out.close();

        if (!ok || total == 0) {
            service_.abortUpload(id);
            res.set_header("Connection", "close");
            return sendJson(res, tooLarge ? 413 : 400,
                            {{"error", tooLarge ? "file too large" : "empty or broken upload"}});
        }
        if (!service_.completeUpload(id)) return sendJson(res, 503, {{"error", "node is shutting down"}});
        sendJson(res, 202, {{"id", id}, {"status", "queued"}, {"bytes", total}});
    });

    // ---- status / delete ------------------------------------------------------
    svr.Get(std::string("/videos/") + kIdPattern + "/status", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (!access_.allowed(id, Permission::Read, req.get_header_value("Authorization")))
            return sendJson(res, 403, {{"error", "forbidden"}});
        VideoStatus s = service_.status(id);
        sendJson(res, s == VideoStatus::NotFound ? 404 : 200, {{"id", id}, {"status", toString(s)}});
    });

    svr.Delete(std::string("/videos/") + kIdPattern, [this](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (!access_.allowed(id, Permission::Write, req.get_header_value("Authorization")))
            return sendJson(res, 403, {{"error", "forbidden"}});
        if (!service_.remove(id)) return sendJson(res, 404, {{"error", "not found"}});
        sendJson(res, 200, {{"id", id}, {"deleted", true}});
    });

    // ---- DASH delivery (manifest + segments) ----------------------------------
    svr.Get(std::string("/videos/") + kIdPattern + "/" + kFilePattern + "\\.(mpd|m4s|mp4)",
            [this](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        const std::string file = std::string(req.matches[2]) + "." + std::string(req.matches[3]);
        if (!access_.allowed(id, Permission::Read, req.get_header_value("Authorization")))
            return sendJson(res, 403, {{"error", "forbidden"}});
        if (service_.status(id) != VideoStatus::Ready) return sendJson(res, 404, {{"error", "video not ready"}});
        auto path = storage_.existingDashFile(id, file);
        if (!path) return sendJson(res, 404, {{"error", "file not found"}});

        const bool manifest = req.matches[3] == "mpd";
        res.set_header("Cache-Control", manifest ? "private, max-age=60" : "private, max-age=3600");
        res.set_header("Accept-Ranges", "bytes");
        if (req.matches[3] == "m4s") {
            auto cached = storage_.cachedSegment(id, file);
            using CR = VideoStorage::CacheResult;
            const char* result = cached.result == CR::Hit ? "hit" :
                                 cached.result == CR::Miss ? "miss" : "bypass";
            metrics_.incCounter("tecflix_segment_cache_requests_total", {{"result", result}});
            if (cached.data) {
                const auto size = cached.data->size();
                res.set_content_provider(size, contentTypeFor(file),
                    [data = std::move(cached.data), this](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                        const auto count = std::min(length, std::size_t(64 * 1024));
                        metrics_.incCounter("tecflix_bytes_served_total", {}, static_cast<double>(count));
                        return sink.write(data->data() + offset, count);
                    });
                return;
            }
        }
        sendFile(res, *path, contentTypeFor(file), metrics_);
    });

    // ---- internal replication (V2.0) -------------------------------------------
    auto internalAuth = [this](const httplib::Request& req) {
        return !config_.internalToken.empty() &&
               req.get_header_value("X-Tecflix-Token") == config_.internalToken;
    };
    svr.Put(std::string("/internal/replicate/") + kIdPattern + "/" + kFilePattern,
            [this, internalAuth](const httplib::Request& req, httplib::Response& res) {
        if (!internalAuth(req)) return sendJson(res, 403, {{"error", "forbidden"}});
        if (!service_.receiveReplica(req.matches[1], req.matches[2], req.body))
            return sendJson(res, 400, {{"error", "invalid replica"}});
        sendJson(res, 200, {{"stored", true}});
    });
    svr.Delete(std::string("/internal/replicate/") + kIdPattern,
               [this, internalAuth](const httplib::Request& req, httplib::Response& res) {
        if (!internalAuth(req)) return sendJson(res, 403, {{"error", "forbidden"}});
        sendJson(res, service_.removeReplica(req.matches[1]) ? 200 : 404, {{"deleted", true}});
    });
}

}  // namespace tecflix



