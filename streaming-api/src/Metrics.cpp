#include "streaming/Metrics.hpp"

#include <sstream>

namespace tecflix {
namespace {

const std::vector<double>& bounds() {
    static const std::vector<double> b = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5,
                                          1,     2.5,  5,     10,   30,  60,   120,
                                          300,   600};
    return b;
}

std::string escape(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out;
}

}  // namespace

std::string PrometheusMetrics::labelString(const Labels& labels, const std::string& extra) {
    std::string out;
    for (const auto& [k, v] : labels) {
        if (!out.empty()) out += ",";
        out += k + "=\"" + escape(v) + "\"";
    }
    if (!extra.empty()) {
        if (!out.empty()) out += ",";
        out += extra;
    }
    return out.empty() ? "" : "{" + out + "}";
}

void PrometheusMetrics::incCounter(const std::string& name, const Labels& labels, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name][labelString(labels)] += value;
}

void PrometheusMetrics::setGauge(const std::string& name, const Labels& labels, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name][labelString(labels)] = value;
}

void PrometheusMetrics::observe(const std::string& name, const Labels& labels, double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& h = histograms_[name][labelString(labels)];
    if (h.buckets.empty()) h.buckets.assign(bounds().size() + 1, 0);
    std::size_t i = 0;
    while (i < bounds().size() && seconds > bounds()[i]) ++i;
    ++h.buckets[i];
    ++h.count;
    h.sum += seconds;
}

std::string PrometheusMetrics::render() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    for (const auto& [name, series] : counters_) {
        out << "# TYPE " << name << " counter\n";
        for (const auto& [labels, v] : series) out << name << labels << ' ' << v << '\n';
    }
    for (const auto& [name, series] : gauges_) {
        out << "# TYPE " << name << " gauge\n";
        for (const auto& [labels, v] : series) out << name << labels << ' ' << v << '\n';
    }
    for (const auto& [name, series] : histograms_) {
        out << "# TYPE " << name << " histogram\n";
        for (const auto& [labels, h] : series) {
            // Re-open the label set so we can append the "le" label.
            std::string base = labels.empty() ? "" : labels.substr(1, labels.size() - 2);
            unsigned long long cumulative = 0;
            for (std::size_t i = 0; i <= bounds().size(); ++i) {
                cumulative += h.buckets[i];
                std::string le = "+Inf";
                if (i < bounds().size()) {
                    std::ostringstream s;
                    s << bounds()[i];
                    le = s.str();
                }
                std::string l = "{" + (base.empty() ? "" : base + ",") + "le=\"" + le + "\"}";
                out << name << "_bucket" << l << ' ' << cumulative << '\n';
            }
            out << name << "_sum" << labels << ' ' << h.sum << '\n';
            out << name << "_count" << labels << ' ' << h.count << '\n';
        }
    }
    return out.str();
}

}  // namespace tecflix
