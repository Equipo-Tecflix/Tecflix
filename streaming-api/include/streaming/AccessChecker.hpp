#pragma once
#include <string>

namespace tecflix {

enum class Permission { Read, Write };

/// Decides whether a caller may read (play) or write (upload/delete) a video.
class IAccessChecker {
public:
    virtual ~IAccessChecker() = default;
    virtual bool allowed(const std::string& videoId, Permission permission,
                         const std::string& authorizationHeader) = 0;
};

/// Development mode: everything is allowed.
class AllowAllAccessChecker : public IAccessChecker {
public:
    bool allowed(const std::string&, Permission, const std::string&) override { return true; }
};

/// Asks the Metadata API (Persona 2). Contract documented in SPEC.md:
///   GET {metadataUrl}/videos/{id}/access?permission=read|write
///   Authorization: <same header the browser sent>
///   200 -> allowed, anything else (401/403/404/timeout) -> denied.
class MetadataAccessChecker : public IAccessChecker {
public:
    explicit MetadataAccessChecker(std::string metadataUrl);
    bool allowed(const std::string& videoId, Permission permission,
                 const std::string& authorizationHeader) override;

private:
    std::string url_;
};

}  // namespace tecflix
