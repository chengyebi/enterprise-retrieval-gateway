#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "retrieval_gateway/auth/access_context.h"
#include "retrieval_gateway/auth/access_policy_resolver.h"

namespace erg {

struct SupabaseAuthSettings {
    std::string jwt_secret;
    std::string jwks_json;
    std::string expected_audience{"authenticated"};
    std::string expected_issuer;
    bool require_auth{false};
};

struct SupabaseAuthResult {
    bool ok{false};
    std::string error;
    std::string auth_user_id;
    std::string acl_user_id;
    std::string email;
};

class SupabaseAuthBindings {
public:
    static SupabaseAuthBindings fromObjectMap(const std::string& json);
    static SupabaseAuthBindings fromFile(const std::filesystem::path& path);

    void upsert(const std::string& auth_user_id, const std::string& acl_user_id);
    std::optional<std::string> findAclUserId(const std::string& auth_user_id) const;
    bool empty() const;

private:
    std::map<std::string, std::string> bindings_;
};

class SupabaseAuthManager {
public:
    SupabaseAuthManager() = default;
    explicit SupabaseAuthManager(SupabaseAuthSettings settings, SupabaseAuthBindings bindings = {});

    bool enabled() const;
    SupabaseAuthResult resolveBearerToken(const std::string& authorization_header) const;
    AccessContext accessContextForBearerToken(const std::string& authorization_header,
                                              const AccessPolicyResolver& resolver) const;
    const SupabaseAuthSettings& settings() const;

private:
    SupabaseAuthSettings settings_;
    SupabaseAuthBindings bindings_;
};

}  // namespace erg
