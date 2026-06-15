#include "retrieval_gateway/auth/supabase_auth.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "retrieval_gateway/auth/access_policy_resolver.h"
#include "retrieval_gateway/common/json_util.h"

namespace erg {

namespace {

std::string trim(const std::string& value) {
    const auto left = value.find_first_not_of(" \t\r\n");
    if (left == std::string::npos) {
        return "";
    }
    const auto right = value.find_last_not_of(" \t\r\n");
    return value.substr(left, right - left + 1);
}

std::vector<unsigned char> base64UrlDecode(const std::string& input) {
    std::string normalized = input;
    for (char& c : normalized) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }
    while (normalized.size() % 4 != 0) {
        normalized.push_back('=');
    }

    std::vector<unsigned char> output((normalized.size() * 3) / 4 + 4);
    const int len = EVP_DecodeBlock(output.data(),
                                    reinterpret_cast<const unsigned char*>(normalized.data()),
                                    static_cast<int>(normalized.size()));
    if (len < 0) {
        return {};
    }

    std::size_t padding = 0;
    if (!normalized.empty() && normalized.back() == '=') {
        ++padding;
    }
    if (normalized.size() > 1 && normalized[normalized.size() - 2] == '=') {
        ++padding;
    }
    output.resize(static_cast<std::size_t>(len) - padding);
    return output;
}

std::vector<unsigned char> hmacSha256(const std::string& secret, const std::string& message) {
    std::vector<unsigned char> digest(static_cast<std::size_t>(EVP_MAX_MD_SIZE));
    unsigned int length = 0;
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(secret.data()),
         static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(message.data()),
         message.size(),
         digest.data(),
         &length);
    digest.resize(length);
    return digest;
}

std::optional<std::string> extractHeaderValue(const std::string& authorization_header) {
    const std::string prefix = "Bearer ";
    const std::string trimmed = trim(authorization_header);
    if (trimmed.size() <= prefix.size() || trimmed.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    const std::string token = trim(trimmed.substr(prefix.size()));
    if (token.empty()) {
        return std::nullopt;
    }
    return token;
}

SupabaseAuthResult fail(std::string message) {
    SupabaseAuthResult result;
    result.ok = false;
    result.error = std::move(message);
    return result;
}

std::optional<std::string> parseJwtPart(const std::string& token, std::size_t part_index) {
    std::size_t start = 0;
    std::size_t end = 0;
    for (std::size_t index = 0; index <= part_index; ++index) {
        start = (index == 0) ? 0 : end + 1;
        end = token.find('.', start);
        if (end == std::string::npos) {
            if (index == part_index && start < token.size()) {
                return token.substr(start);
            }
            return std::nullopt;
        }
    }
    if (start >= token.size()) {
        return std::nullopt;
    }
    return token.substr(start, end - start);
}

}  // namespace

SupabaseAuthBindings SupabaseAuthBindings::fromObjectMap(const std::string& json) {
    SupabaseAuthBindings bindings;
    static const std::regex pair_pattern("\"([^\"]+)\"\\s*:\\s*\"([^\"]*)\"");
    for (std::sregex_iterator it(json.begin(), json.end(), pair_pattern), end; it != end; ++it) {
        bindings.upsert((*it)[1].str(), (*it)[2].str());
    }
    return bindings;
}

SupabaseAuthBindings SupabaseAuthBindings::fromFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open Supabase auth bindings file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return fromObjectMap(buffer.str());
}

void SupabaseAuthBindings::upsert(const std::string& auth_user_id, const std::string& acl_user_id) {
    bindings_[auth_user_id] = acl_user_id;
}

std::optional<std::string> SupabaseAuthBindings::findAclUserId(const std::string& auth_user_id) const {
    const auto it = bindings_.find(auth_user_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool SupabaseAuthBindings::empty() const {
    return bindings_.empty();
}

SupabaseAuthManager::SupabaseAuthManager(SupabaseAuthSettings settings, SupabaseAuthBindings bindings)
    : settings_(std::move(settings)), bindings_(std::move(bindings)) {}

bool SupabaseAuthManager::enabled() const {
    return !settings_.jwt_secret.empty();
}

const SupabaseAuthSettings& SupabaseAuthManager::settings() const {
    return settings_;
}

SupabaseAuthResult SupabaseAuthManager::resolveBearerToken(const std::string& authorization_header) const {
    if (!enabled()) {
        return fail("supabase auth is disabled");
    }

    const auto token_opt = extractHeaderValue(authorization_header);
    if (!token_opt) {
        return settings_.require_auth ? fail("missing Bearer authorization header") : fail("supabase bearer token not provided");
    }

    const std::string& token = *token_opt;
    const auto header_part = parseJwtPart(token, 0);
    const auto payload_part = parseJwtPart(token, 1);
    const auto signature_part = parseJwtPart(token, 2);
    if (!header_part || !payload_part || !signature_part) {
        return fail("invalid JWT format");
    }

    const auto header_bytes = base64UrlDecode(*header_part);
    const auto payload_bytes = base64UrlDecode(*payload_part);
    const auto signature_bytes = base64UrlDecode(*signature_part);
    if (header_bytes.empty() || payload_bytes.empty() || signature_bytes.empty()) {
        return fail("invalid JWT encoding");
    }

    const std::string header_json(header_bytes.begin(), header_bytes.end());
    const std::string payload_json(payload_bytes.begin(), payload_bytes.end());
    const std::string alg = extractJsonString(header_json, "alg");
    if (alg != "HS256") {
        return fail("unsupported JWT algorithm: " + alg);
    }

    const auto expected_signature = hmacSha256(settings_.jwt_secret, *header_part + "." + *payload_part);
    if (expected_signature.size() != signature_bytes.size() ||
        CRYPTO_memcmp(expected_signature.data(), signature_bytes.data(), signature_bytes.size()) != 0) {
        return fail("JWT signature verification failed");
    }

    const std::string sub = extractJsonString(payload_json, "sub");
    if (sub.empty()) {
        return fail("JWT payload is missing sub");
    }

    const std::string aud = extractJsonString(payload_json, "aud");
    if (!settings_.expected_audience.empty() && !aud.empty() && aud != settings_.expected_audience) {
        return fail("JWT audience mismatch");
    }

    const std::string iss = extractJsonString(payload_json, "iss");
    if (!settings_.expected_issuer.empty() && !iss.empty() && iss != settings_.expected_issuer) {
        return fail("JWT issuer mismatch");
    }

    const auto exp = extractJsonSize(payload_json, "exp", 0);
    if (exp > 0 && static_cast<std::time_t>(exp) <= std::time(nullptr)) {
        return fail("JWT has expired");
    }

    SupabaseAuthResult result;
    result.ok = true;
    result.auth_user_id = sub;
    result.email = extractJsonString(payload_json, "email");
    if (const auto acl_user_id = bindings_.findAclUserId(sub); acl_user_id) {
        result.acl_user_id = *acl_user_id;
    } else {
        result.ok = false;
        result.error = "no ACL binding for Supabase auth user: " + sub;
    }
    return result;
}

AccessContext SupabaseAuthManager::accessContextForBearerToken(const std::string& authorization_header,
                                                               const AccessPolicyResolver& resolver) const {
    const SupabaseAuthResult result = resolveBearerToken(authorization_header);
    if (!result.ok) {
        throw AccessDenied(result.error);
    }
    return resolver.resolve(result.acl_user_id);
}

}  // namespace erg
