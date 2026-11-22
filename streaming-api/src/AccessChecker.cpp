#include "streaming/AccessChecker.hpp"

#include "httplib.h"

namespace tecflix {

MetadataAccessChecker::MetadataAccessChecker(std::string metadataUrl) : url_(std::move(metadataUrl)) {}

bool MetadataAccessChecker::allowed(const std::string& videoId, Permission permission,
                                    const std::string& authorizationHeader) {
    httplib::Client client(url_);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    httplib::Headers headers;
    if (!authorizationHeader.empty()) headers.emplace("Authorization", authorizationHeader);
    const char* perm = permission == Permission::Read ? "read" : "write";
    auto res = client.Get("/videos/" + videoId + "/access?permission=" + perm, headers);
    return res && res->status == 200;
}

}  // namespace tecflix
