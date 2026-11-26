#pragma once
#include <chrono>
#include <memory>
#include <string>
namespace tecflix {
// Optional bounded asynchronous OTLP/HTTP JSON exporter. No SDK dependency.
class OtelTrace {
public:
    OtelTrace(std::string endpoint, std::string node, double segmentSampleRate = 1.0);
    ~OtelTrace();
    void record(const std::string& method, const std::string& route, int status,
                std::chrono::system_clock::time_point start, bool successfulSegment = false);
    std::string renderMetrics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
