#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tecflix {

using Labels = std::map<std::string, std::string>;

/// Abstraction over the telemetry backend. The default implementation exposes
/// Prometheus text format; an OpenTelemetry implementation can be plugged in
/// by implementing this same interface (see SPEC.md, "Observabilidad").
class IMetrics {
public:
    virtual ~IMetrics() = default;
    virtual void incCounter(const std::string& name, const Labels& labels = {}, double value = 1) = 0;
    virtual void observe(const std::string& name, const Labels& labels, double seconds) = 0;
    virtual void setGauge(const std::string& name, const Labels& labels, double value) = 0;
};

class PrometheusMetrics : public IMetrics {
public:
    void incCounter(const std::string& name, const Labels& labels = {}, double value = 1) override;
    void observe(const std::string& name, const Labels& labels, double seconds) override;
    void setGauge(const std::string& name, const Labels& labels, double value) override;

    /// Prometheus text exposition format (scrape this from /metrics).
    std::string render() const;

private:
    struct Histogram {
        std::vector<unsigned long long> buckets;  // per-bucket (non cumulative)
        unsigned long long count = 0;
        double sum = 0;
    };
    static std::string labelString(const Labels& labels, const std::string& extra = "");

    mutable std::mutex mutex_;
    std::map<std::string, std::map<std::string, double>> counters_;
    std::map<std::string, std::map<std::string, double>> gauges_;
    std::map<std::string, std::map<std::string, Histogram>> histograms_;
};

}  // namespace tecflix
