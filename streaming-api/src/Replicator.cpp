#include "streaming/Replicator.hpp"

#include <fstream>
#include <sstream>

#include "httplib.h"

namespace tecflix {
namespace {

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

HttpReplicator::HttpReplicator(std::vector<std::string> peers, std::string token,
                               const VideoStorage& storage)
    : peers_(std::move(peers)), token_(std::move(token)), storage_(storage) {}

bool HttpReplicator::replicate(const std::string& id) {
    bool allOk = true;
    const auto files = storage_.dashFilesForReplication(id);
    for (const auto& peer : peers_) {
        httplib::Client client(peer);
        client.set_connection_timeout(3);
        client.set_read_timeout(60);
        client.set_write_timeout(60);
        httplib::Headers headers{{"X-Tecflix-Token", token_}};
        for (const auto& file : files) {
            auto data = readFile(storage_.dashDir(id) / file);
            auto res = client.Put("/internal/replicate/" + id + "/" + file, headers, data,
                                  "application/octet-stream");
            if (!res || res->status != 200) {
                allOk = false;
                break;  // do not send the manifest of an incomplete copy
            }
        }
    }
    return allOk;
}

bool HttpReplicator::propagateDelete(const std::string& id) {
    bool allOk = true;
    for (const auto& peer : peers_) {
        httplib::Client client(peer);
        client.set_connection_timeout(3);
        client.set_read_timeout(10);
        auto res = client.Delete("/internal/replicate/" + id, httplib::Headers{{"X-Tecflix-Token", token_}});
        if (!res || (res->status != 200 && res->status != 404)) allOk = false;
    }
    return allOk;
}

}  // namespace tecflix
