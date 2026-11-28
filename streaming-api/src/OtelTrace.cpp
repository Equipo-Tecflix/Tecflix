#include "streaming/OtelTrace.hpp"
#include "httplib.h"
#include "json.hpp"
#include <condition_variable>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

namespace tecflix {
using json = nlohmann::json;
namespace {
std::string hexId(unsigned count) {
    thread_local std::mt19937_64 rng(std::random_device{}());
    static constexpr char hex[]="0123456789abcdef";
    std::string id(count, '0');
    for (char& c : id) c=hex[rng() & 15];
    if (id.find_first_not_of('0') == std::string::npos) id[0]='1';
    return id;
}
std::string nanos(std::chrono::system_clock::time_point t) {
    return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
}
json attr(const std::string& key, const std::string& value) {
    return {{"key",key},{"value",{{"stringValue",value}}}};
}
}
struct OtelTrace::Impl {
    std::string endpoint,node;
    double sampleRate;
    std::atomic<unsigned long long> generated{0}, sampledOut{0}, queueDropped{0}, exportFailed{0}, exported{0};
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<json> queue;
    bool stop=false;
    std::thread worker;
    Impl(std::string e,std::string n,double rate):endpoint(std::move(e)),node(std::move(n)),sampleRate(rate),worker([this]{run();}) {}
    ~Impl(){ {std::lock_guard<std::mutex> lock(mutex);stop=true;} cv.notify_one();worker.join(); }
    void run(){
        for(;;){
            json batch=json::array();
            {std::unique_lock<std::mutex> lock(mutex);
             cv.wait_for(lock,std::chrono::seconds(1),[&]{return stop||queue.size()>=100;});
             if(stop && queue.empty())return;
             while(!queue.empty() && batch.size()<100){batch.push_back(std::move(queue.front()));queue.pop_front();}}
            if(batch.empty())continue;
            try {
                json resource={{"attributes",json::array({attr("service.name","tecflix-streaming"),attr("service.instance.id",node)})}};
                json scope={{"scope",{{"name","tecflix.http"}}},{"spans",batch}};
                json body={{"resourceSpans",json::array({{{"resource",resource},{"scopeSpans",json::array({scope})}}})}};
                httplib::Client client(endpoint);client.set_connection_timeout(1);client.set_read_timeout(2);client.set_write_timeout(2);
                const auto response = client.Post("/v1/traces",body.dump(),"application/json");
                if (response && response->status >= 200 && response->status < 300)
                    exported.fetch_add(batch.size(), std::memory_order_relaxed);
                else exportFailed.fetch_add(batch.size(), std::memory_order_relaxed);
            } catch (...) { exportFailed.fetch_add(batch.size(), std::memory_order_relaxed); }
        }
    }
};
OtelTrace::OtelTrace(std::string endpoint,std::string node,double rate){
    if (!std::isfinite(rate) || rate < 0 || rate > 1) rate = 1;
    if(!endpoint.empty())impl_=std::make_unique<Impl>(std::move(endpoint),std::move(node),rate);
}
OtelTrace::~OtelTrace()=default;
void OtelTrace::record(const std::string& method,const std::string& route,int status,std::chrono::system_clock::time_point start,bool successfulSegment){
    if(!impl_)return;
    try {
        if (successfulSegment && method == "GET" && (status == 200 || status == 206) && impl_->sampleRate < 1) {
            thread_local std::mt19937_64 rng(std::random_device{}());
            if (impl_->sampleRate == 0 || std::generate_canonical<double, 53>(rng) >= impl_->sampleRate) {
                impl_->sampledOut.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        json span={{"traceId",hexId(32)},{"spanId",hexId(16)},{"name",method+" "+route},{"kind",2},
          {"startTimeUnixNano",nanos(start)},{"endTimeUnixNano",nanos(std::chrono::system_clock::now())},
          {"attributes",json::array({attr("http.request.method",method),attr("http.route",route),{{"key","http.response.status_code"},{"value",{{"intValue",std::to_string(status)}}}}})},
          {"status",{{"code",status>=500?2:0}}}};
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if(impl_->queue.size()<2048) {
            impl_->queue.push_back(std::move(span));
            impl_->generated.fetch_add(1, std::memory_order_relaxed);
        } else impl_->queueDropped.fetch_add(1, std::memory_order_relaxed);
        if(impl_->queue.size()>=100)impl_->cv.notify_one();
    } catch (...) { /* best-effort tracing */ }
}
std::string OtelTrace::renderMetrics() const {
    if (!impl_) return {};
    const auto metric = [](const char* result, unsigned long long count) {
        return std::string("tecflix_trace_spans_total{result=\"") + result + "\"} " + std::to_string(count) + "\n";
    };
    return "# TYPE tecflix_trace_spans_total counter\n" +
        metric("generated", impl_->generated.load(std::memory_order_relaxed)) +
        metric("sampled_out", impl_->sampledOut.load(std::memory_order_relaxed)) +
        metric("queue_dropped", impl_->queueDropped.load(std::memory_order_relaxed)) +
        metric("export_failed", impl_->exportFailed.load(std::memory_order_relaxed)) +
        metric("exported", impl_->exported.load(std::memory_order_relaxed));
}
}
