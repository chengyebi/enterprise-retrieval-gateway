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
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>

#include "retrieval_gateway/auth/access_policy_resolver.h"
#include "retrieval_gateway/common/json_util.h"

namespace erg {

namespace {

struct EcJwk {
    std::string kid;
    std::string x;
    std::string y;
};

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

std::optional<std::string> extractJwkString(const std::string& object_json, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(object_json, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    return std::nullopt;
}

std::optional<EcJwk> findEcJwk(const std::string& jwks_json, const std::string& kid) {
    static const std::regex object_pattern("\\{[^{}]*\"kty\"\\s*:\\s*\"EC\"[^{}]*\\}");
    for (std::sregex_iterator it(jwks_json.begin(), jwks_json.end(), object_pattern), end; it != end; ++it) {
        const std::string object_json = it->str();
        const auto alg = extractJwkString(object_json, "alg");
        const auto crv = extractJwkString(object_json, "crv");
        const auto object_kid = extractJwkString(object_json, "kid");
        const auto x = extractJwkString(object_json, "x");
        const auto y = extractJwkString(object_json, "y");
        if (!x || !y || (alg && *alg != "ES256") || (crv && *crv != "P-256")) {
            continue;
        }
        if (!kid.empty() && object_kid && *object_kid != kid) {
            continue;
        }
        if (!kid.empty() && !object_kid) {
            continue;
        }
        return EcJwk{object_kid.value_or(""), *x, *y};
    }
    return std::nullopt;
}

std::optional<std::vector<unsigned char>> es256DerSignature(const std::vector<unsigned char>& raw_signature) {
    if (raw_signature.size() != 64) {
        return std::nullopt;
    }
    BIGNUM* r = BN_bin2bn(raw_signature.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(raw_signature.data() + 32, 32, nullptr);
    if (r == nullptr || s == nullptr) {
        BN_free(r);
        BN_free(s);
        return std::nullopt;
    }
    ECDSA_SIG* signature = ECDSA_SIG_new();
    if (signature == nullptr || ECDSA_SIG_set0(signature, r, s) != 1) {
        BN_free(r);
        BN_free(s);
        ECDSA_SIG_free(signature);
        return std::nullopt;
    }

    const int der_size = i2d_ECDSA_SIG(signature, nullptr);
    if (der_size <= 0) {
        ECDSA_SIG_free(signature);
        return std::nullopt;
    }
    std::vector<unsigned char> der(static_cast<std::size_t>(der_size));
    unsigned char* cursor = der.data();
    if (i2d_ECDSA_SIG(signature, &cursor) != der_size) {
        ECDSA_SIG_free(signature);
        return std::nullopt;
    }
    ECDSA_SIG_free(signature);
    return der;
}

std::optional<std::vector<unsigned char>> p256PublicKeyBytes(const EcJwk& jwk) {
    const auto x = base64UrlDecode(jwk.x);
    const auto y = base64UrlDecode(jwk.y);
    if (x.size() != 32 || y.size() != 32) {
        return std::nullopt;
    }
    std::vector<unsigned char> public_key;
    public_key.reserve(65);
    public_key.push_back(0x04);
    public_key.insert(public_key.end(), x.begin(), x.end());
    public_key.insert(public_key.end(), y.begin(), y.end());
    return public_key;
}

bool verifyEs256(const std::string& jwks_json,
                 const std::string& kid,
                 const std::string& message,
                 const std::vector<unsigned char>& raw_signature) {
    const auto jwk = findEcJwk(jwks_json, kid);
    if (!jwk) {
        return false;
    }
    const auto public_key_bytes = p256PublicKeyBytes(*jwk);
    const auto der_signature = es256DerSignature(raw_signature);
    if (!public_key_bytes || !der_signature) {
        return false;
    }

    EVP_PKEY_CTX* pkey_context = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    if (pkey_context == nullptr) {
        return false;
    }
    EVP_PKEY* pkey = nullptr;
    OSSL_PARAM_BLD* params_builder = OSSL_PARAM_BLD_new();
    bool ok = false;
    if (params_builder != nullptr &&
        OSSL_PARAM_BLD_push_utf8_string(params_builder, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) == 1 &&
        OSSL_PARAM_BLD_push_octet_string(params_builder,
                                         OSSL_PKEY_PARAM_PUB_KEY,
                                         public_key_bytes->data(),
                                         public_key_bytes->size()) == 1) {
        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(params_builder);
        if (params != nullptr &&
            EVP_PKEY_fromdata_init(pkey_context) == 1 &&
            EVP_PKEY_fromdata(pkey_context, &pkey, EVP_PKEY_PUBLIC_KEY, params) == 1) {
            EVP_MD_CTX* verify_context = EVP_MD_CTX_new();
            if (verify_context != nullptr &&
                EVP_DigestVerifyInit(verify_context, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
                EVP_DigestVerifyUpdate(verify_context, message.data(), message.size()) == 1 &&
                EVP_DigestVerifyFinal(verify_context, der_signature->data(), der_signature->size()) == 1) {
                ok = true;
            }
            EVP_MD_CTX_free(verify_context);
        }
        OSSL_PARAM_free(params);
    }
    OSSL_PARAM_BLD_free(params_builder);
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pkey_context);
    return ok;
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
    return !settings_.jwt_secret.empty() || !settings_.jwks_json.empty();
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
    const std::string signed_message = *header_part + "." + *payload_part;
    if (alg == "HS256") {
        if (settings_.jwt_secret.empty()) {
            return fail("HS256 JWT secret is not configured");
        }
        const auto expected_signature = hmacSha256(settings_.jwt_secret, signed_message);
        if (expected_signature.size() != signature_bytes.size() ||
            CRYPTO_memcmp(expected_signature.data(), signature_bytes.data(), signature_bytes.size()) != 0) {
            return fail("JWT signature verification failed");
        }
    } else if (alg == "ES256") {
        if (settings_.jwks_json.empty()) {
            return fail("ES256 JWKS is not configured");
        }
        const std::string kid = extractJsonString(header_json, "kid");
        if (!verifyEs256(settings_.jwks_json, kid, signed_message, signature_bytes)) {
            return fail("JWT signature verification failed");
        }
    } else {
        return fail("unsupported JWT algorithm: " + alg);
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
