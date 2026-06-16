#include "retrieval_gateway/api/request_mapper.h"

#include <cctype>
#include <stdexcept>

#include "retrieval_gateway/common/json_util.h"
#include "retrieval_gateway/common/parse_util.h"

namespace erg {

namespace {

bool isBlank(const std::string& value) {
    for (const unsigned char c : value) {
        if (!std::isspace(c)) {
            return false;
        }
    }
    return true;
}

bool isJsonString(const std::string& raw) {
    return raw.size() >= 2 && raw.front() == '"' && raw.back() == '"';
}

void skipWhitespace(const std::string& value, std::size_t& pos) {
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos]))) {
        ++pos;
    }
}

bool skipJsonString(const std::string& value, std::size_t& pos) {
    if (pos >= value.size() || value[pos] != '"') {
        return false;
    }
    ++pos;
    while (pos < value.size()) {
        if (value[pos] == '"') {
            ++pos;
            return true;
        }
        if (value[pos] == '\\') {
            ++pos;
            if (pos >= value.size()) {
                return false;
            }
            if (value[pos] == 'u') {
                pos += 5;
            } else {
                ++pos;
            }
            continue;
        }
        ++pos;
    }
    return false;
}

bool isJsonStringArray(const std::string& raw) {
    std::size_t pos = 0;
    skipWhitespace(raw, pos);
    if (pos >= raw.size() || raw[pos] != '[') {
        return false;
    }
    ++pos;
    skipWhitespace(raw, pos);
    if (pos < raw.size() && raw[pos] == ']') {
        ++pos;
        skipWhitespace(raw, pos);
        return pos == raw.size();
    }
    while (pos < raw.size()) {
        skipWhitespace(raw, pos);
        if (!skipJsonString(raw, pos)) {
            return false;
        }
        skipWhitespace(raw, pos);
        if (pos < raw.size() && raw[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < raw.size() && raw[pos] == ']') {
            ++pos;
            skipWhitespace(raw, pos);
            return pos == raw.size();
        }
        return false;
    }
    return false;
}

std::string requiredString(const std::string& body, const std::string& key) {
    if (!hasJsonKey(body, key)) {
        throw std::invalid_argument("missing required field: " + key);
    }
    const std::string raw = extractJsonRawValue(body, key);
    if (!isJsonString(raw)) {
        throw std::invalid_argument(key + " must be a string");
    }
    const std::string value = extractJsonString(body, key);
    if (isBlank(value)) {
        throw std::invalid_argument(key + " must not be empty");
    }
    return value;
}

std::size_t parseTopK(const std::string& body) {
    if (!hasJsonKey(body, "top_k")) {
        return kDefaultTopK;
    }
    return parseBoundedSize(extractJsonRawValue(body, "top_k"), "top_k", 1, kMaxTopK);
}

bool optionalBool(const std::string& body, const std::string& key, bool default_value) {
    if (!hasJsonKey(body, key)) {
        return default_value;
    }
    const std::string raw = extractJsonRawValue(body, key);
    if (raw == "true") {
        return true;
    }
    if (raw == "false") {
        return false;
    }
    throw std::invalid_argument(key + " must be a boolean");
}

std::vector<std::string> optionalStringArray(const std::string& body, const std::string& key) {
    if (!hasJsonKey(body, key)) {
        return {};
    }
    const std::string raw = extractJsonRawValue(body, key);
    if (!isJsonStringArray(raw)) {
        throw std::invalid_argument(key + " must be an array of strings");
    }
    return extractJsonStringArray(body, key);
}

}  // namespace

SearchRequest searchRequestFromJson(const std::string& body) {
    if (!isValidJsonObject(body)) {
        throw std::invalid_argument("request body must be a valid JSON object");
    }

    SearchRequest request;
    request.user_id = requiredString(body, "user_id");
    request.query = requiredString(body, "query");
    request.top_k = parseTopK(body);
    request.project_ids = optionalStringArray(body, "project_ids");
    request.document_types = optionalStringArray(body, "document_types");
    request.enable_vector_search = optionalBool(body, "enable_vector_search", true);
    request.enable_keyword_search = optionalBool(body, "enable_keyword_search", true);
    return request;
}

std::string searchResponseToJson(const SearchResponse& response) {
    return responseToJson(response);
}

}  // namespace erg
