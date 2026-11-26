#include "streaming/OtelTrace.hpp"
#include "httplib.h"
#include "json.hpp"
#include <condition_variable>
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
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<json> queue;
    bool stop=false;
    std::thread worker;
    Impl(std::string e,std::string n):endpoint(std::move(e)),node(std::move(n)),worker([this]{run();}) {}
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
                client.Post("/v1/traces",body.dump(),"application/json");
            } catch (...) { /* telemetry must not interrupt streaming */ }
        }
    }
};
OtelTrace::OtelTrace(std::string endpoint,std::string node){if(!endpoint.empty())impl_=std::make_unique<Impl>(std::move(endpoint),std::move(node));}
OtelTrace::~OtelTrace()=default;
void OtelTrace::record(const std::string& method,const std::string& route,int status,std::chrono::system_clock::time_point start){
    if(!impl_)return;
    try {
        json span={{"traceId",hexId(32)},{"spanId",hexId(16)},{"name",method+" "+route},{"kind",2},
          {"startTimeUnixNano",nanos(start)},{"endTimeUnixNano",nanos(std::chrono::system_clock::now())},
          {"attributes",json::array({attr("http.request.method",method),attr("http.route",route),{{"key","http.response.status_code"},{"value",{{"intValue",std::to_string(status)}}}}})},
          {"status",{{"code",status>=500?2:0}}}};
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if(impl_->queue.size()<2048)impl_->queue.push_back(std::move(span));
        if(impl_->queue.size()>=100)impl_->cv.notify_one();
    } catch (...) { /* best-effort tracing */ }
}
}
