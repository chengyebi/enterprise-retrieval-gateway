#include "retrieval_gateway/common/json_util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace erg {

namespace {

void skipWhitespace(const std::string& body, std::size_t& pos) {
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
}

bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

bool scanJsonStringEnd(const std::string& body, std::size_t start, std::size_t& end) {
    if (start >= body.size() || body[start] != '"') {
        return false;
    }
    for (std::size_t i = start + 1; i < body.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(body[i]);
        if (c == '"') {
            end = i + 1;
            return true;
        }
        if (c < 0x20) {
            return false;
        }
        if (c != '\\') {
            continue;
        }
        if (++i >= body.size()) {
            return false;
        }
        const char escaped = body[i];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u':
                if (i + 4 >= body.size()) {
                    return false;
                }
                for (std::size_t j = i + 1; j <= i + 4; ++j) {
                    if (!isHexDigit(body[j])) {
                        return false;
                    }
                }
                i += 4;
                break;
            default:
                return false;
        }
    }
    return false;
}

bool scanJsonValueEnd(const std::string& body, std::size_t start, std::size_t& end) {
    skipWhitespace(body, start);
    if (start >= body.size()) {
        return false;
    }
    if (body[start] == '"') {
        return scanJsonStringEnd(body, start, end);
    }
    if (body[start] == '[' || body[start] == '{') {
        std::vector<char> closers;
        for (std::size_t i = start; i < body.size(); ++i) {
            if (body[i] == '"') {
                std::size_t string_end = 0;
                if (!scanJsonStringEnd(body, i, string_end)) {
                    return false;
                }
                i = string_end - 1;
                continue;
            }
            if (body[i] == '[') {
                closers.push_back(']');
                continue;
            }
            if (body[i] == '{') {
                closers.push_back('}');
                continue;
            }
            if (body[i] == ']' || body[i] == '}') {
                if (closers.empty() || closers.back() != body[i]) {
                    return false;
                }
                closers.pop_back();
                if (closers.empty()) {
                    end = i + 1;
                    return true;
                }
            }
        }
        return false;
    }

    end = start;
    while (end < body.size() && body[end] != ',' && body[end] != '}' && body[end] != ']' &&
           !std::isspace(static_cast<unsigned char>(body[end]))) {
        ++end;
    }
    return end > start;
}

class JsonValidator {
public:
    explicit JsonValidator(const std::string& body) : body_(body) {}

    bool validObject() {
        skipWhitespace(body_, pos_);
        if (!parseObject()) {
            return false;
        }
        skipWhitespace(body_, pos_);
        return pos_ == body_.size();
    }

private:
    bool consume(char expected) {
        if (pos_ >= body_.size() || body_[pos_] != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    bool parseValue() {
        skipWhitespace(body_, pos_);
        if (pos_ >= body_.size()) {
            return false;
        }
        switch (body_[pos_]) {
            case '"':
                return parseString();
            case '{':
                return parseObject();
            case '[':
                return parseArray();
            case 't':
                return parseLiteral("true");
            case 'f':
                return parseLiteral("false");
            case 'n':
                return parseLiteral("null");
            default:
                return parseNumber();
        }
    }

    bool parseObject() {
        if (!consume('{')) {
            return false;
        }
        skipWhitespace(body_, pos_);
        if (consume('}')) {
            return true;
        }
        while (true) {
            skipWhitespace(body_, pos_);
            if (!parseString()) {
                return false;
            }
            skipWhitespace(body_, pos_);
            if (!consume(':')) {
                return false;
            }
            if (!parseValue()) {
                return false;
            }
            skipWhitespace(body_, pos_);
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skipWhitespace(body_, pos_);
            if (pos_ < body_.size() && body_[pos_] == '}') {
                return false;
            }
        }
    }

    bool parseArray() {
        if (!consume('[')) {
            return false;
        }
        skipWhitespace(body_, pos_);
        if (consume(']')) {
            return true;
        }
        while (true) {
            if (!parseValue()) {
                return false;
            }
            skipWhitespace(body_, pos_);
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skipWhitespace(body_, pos_);
            if (pos_ < body_.size() && body_[pos_] == ']') {
                return false;
            }
        }
    }

    bool parseString() {
        std::size_t end = 0;
        if (!scanJsonStringEnd(body_, pos_, end)) {
            return false;
        }
        pos_ = end;
        return true;
    }

    bool parseLiteral(const std::string& literal) {
        if (body_.compare(pos_, literal.size(), literal) != 0) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    bool parseNumber() {
        if (pos_ >= body_.size()) {
            return false;
        }
        if (body_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= body_.size()) {
            return false;
        }
        if (body_[pos_] == '0') {
            ++pos_;
        } else if (body_[pos_] >= '1' && body_[pos_] <= '9') {
            while (pos_ < body_.size() && std::isdigit(static_cast<unsigned char>(body_[pos_]))) {
                ++pos_;
            }
        } else {
            return false;
        }
        if (pos_ < body_.size() && body_[pos_] == '.') {
            ++pos_;
            const std::size_t first_fraction = pos_;
            while (pos_ < body_.size() && std::isdigit(static_cast<unsigned char>(body_[pos_]))) {
                ++pos_;
            }
            if (pos_ == first_fraction) {
                return false;
            }
        }
        if (pos_ < body_.size() && (body_[pos_] == 'e' || body_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < body_.size() && (body_[pos_] == '+' || body_[pos_] == '-')) {
                ++pos_;
            }
            const std::size_t first_exponent = pos_;
            while (pos_ < body_.size() && std::isdigit(static_cast<unsigned char>(body_[pos_]))) {
                ++pos_;
            }
            if (pos_ == first_exponent) {
                return false;
            }
        }
        return true;
    }

    const std::string& body_;
    std::size_t pos_{0};
};

std::string findJsonValueSpan(const std::string& body, const std::string& key) {
    std::size_t pos = 0;
    skipWhitespace(body, pos);
    if (pos >= body.size() || body[pos] != '{') {
        return "";
    }
    ++pos;
    while (true) {
        skipWhitespace(body, pos);
        if (pos >= body.size() || body[pos] == '}') {
            return "";
        }
        const std::size_t key_start = pos;
        std::size_t key_end = 0;
        if (!scanJsonStringEnd(body, key_start, key_end)) {
            return "";
        }
        const bool key_matches = key_end == key_start + key.size() + 2 &&
                                 body.compare(key_start + 1, key.size(), key) == 0;
        pos = key_end;
        skipWhitespace(body, pos);
        if (pos >= body.size() || body[pos] != ':') {
            return "";
        }
        ++pos;
        skipWhitespace(body, pos);
        const std::size_t value_start = pos;
        std::size_t value_end = 0;
        if (!scanJsonValueEnd(body, value_start, value_end)) {
            return "";
        }
        if (key_matches) {
            return body.substr(value_start, value_end - value_start);
        }
        pos = value_end;
        skipWhitespace(body, pos);
        if (pos < body.size() && body[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < body.size() && body[pos] == '}') {
            return "";
        }
        return "";
    }
}

std::string unquote(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        return raw;
    }
    std::string out;
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (escaped) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(c); break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00";
                    const char* hex = "0123456789abcdef";
                    out << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string jsonArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << jsonEscape(values[i]) << "\"";
    }
    out << "]";
    return out.str();
}

std::string hitToJson(const SearchHit& hit) {
    std::ostringstream out;
    out << "{"
        << "\"document_id\":\"" << jsonEscape(hit.document_id) << "\","
        << "\"chunk_id\":\"" << jsonEscape(hit.chunk_id) << "\","
        << "\"title\":\"" << jsonEscape(hit.title) << "\","
        << "\"snippet\":\"" << jsonEscape(hit.snippet) << "\","
        << "\"department\":\"" << jsonEscape(hit.department) << "\","
        << "\"project_id\":\"" << jsonEscape(hit.project_id) << "\","
        << "\"document_type\":\"" << jsonEscape(hit.document_type) << "\","
        << "\"source\":\"" << jsonEscape(hit.source) << "\","
        << "\"lexical_score\":" << hit.lexical_score << ","
        << "\"semantic_score\":" << hit.semantic_score << ","
        << "\"fusion_score\":" << hit.fusion_score
        << "}";
    return out.str();
}

std::string responseToJson(const SearchResponse& response) {
    std::ostringstream out;
    out << "{"
        << "\"ok\":" << (response.ok ? "true" : "false") << ","
        << "\"query_id\":\"" << jsonEscape(response.query_id) << "\","
        << "\"mode\":\"" << jsonEscape(toString(response.mode)) << "\","
        << "\"fallback_triggered\":" << (response.fallback_triggered ? "true" : "false") << ","
        << "\"final_candidate_limit\":" << response.final_candidate_limit << ",";
    if (!response.ok) {
        out << "\"error\":\"" << jsonEscape(response.error) << "\",";
    }
    out << "\"hits\":[";
    for (std::size_t i = 0; i < response.hits.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << hitToJson(response.hits[i]);
    }
    out << "]}";
    return out.str();
}

std::string traceToJson(const SearchTrace& trace) {
    std::ostringstream out;
    out << "{"
        << "\"query_id\":\"" << jsonEscape(trace.query_id) << "\","
        << "\"user_id\":\"" << jsonEscape(trace.user_id) << "\","
        << "\"mode\":\"" << jsonEscape(toString(trace.mode)) << "\","
        << "\"acl_filter_summary\":\"" << jsonEscape(trace.acl_filter_summary) << "\","
        << "\"requested_top_k\":" << trace.requested_top_k << ","
        << "\"returned_hits\":" << trace.returned_hits << ","
        << "\"candidate_limit\":" << trace.candidate_limit << ","
        << "\"estimated_authorized_candidates\":" << trace.estimated_authorized_candidates << ","
        << "\"fallback_triggered\":" << (trace.fallback_triggered ? "true" : "false") << ","
        << "\"acl_resolve_latency_ms\":" << trace.acl_resolve_latency_ms << ","
        << "\"backend_latency_ms\":" << trace.backend_latency_ms << ","
        << "\"fusion_latency_ms\":" << trace.fusion_latency_ms << ","
        << "\"total_latency_ms\":" << trace.total_latency_ms
        << "}";
    return out.str();
}

std::string stringMapToJson(const std::map<std::string, std::string>& values) {
    std::ostringstream out;
    out << "{";
    std::size_t index = 0;
    for (const auto& item : values) {
        if (index++ > 0) {
            out << ",";
        }
        out << "\"" << jsonEscape(item.first) << "\":\"" << jsonEscape(item.second) << "\"";
    }
    out << "}";
    return out.str();
}

bool isValidJsonObject(const std::string& body) {
    JsonValidator validator(body);
    return validator.validObject();
}

bool hasJsonKey(const std::string& body, const std::string& key) {
    return !findJsonValueSpan(body, key).empty();
}

std::string extractJsonRawValue(const std::string& body, const std::string& key) {
    return findJsonValueSpan(body, key);
}

std::string extractJsonString(const std::string& body, const std::string& key, const std::string& default_value) {
    const std::string raw = findJsonValueSpan(body, key);
    if (raw.empty()) {
        return default_value;
    }
    return unquote(raw);
}

std::size_t extractJsonSize(const std::string& body, const std::string& key, std::size_t default_value) {
    const std::string raw = findJsonValueSpan(body, key);
    if (raw.empty()) {
        return default_value;
    }
    try {
        return static_cast<std::size_t>(std::stoull(raw));
    } catch (...) {
        return default_value;
    }
}

bool extractJsonBool(const std::string& body, const std::string& key, bool default_value) {
    const std::string raw = findJsonValueSpan(body, key);
    if (raw.empty()) {
        return default_value;
    }
    if (raw == "true") {
        return true;
    }
    if (raw == "false") {
        return false;
    }
    return default_value;
}

std::vector<std::string> extractJsonStringArray(const std::string& body, const std::string& key) {
    std::vector<std::string> values;
    const std::string raw = findJsonValueSpan(body, key);
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') {
        return values;
    }

    bool in_string = false;
    bool escaped = false;
    std::string current;
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (!in_string) {
            if (c == '"') {
                in_string = true;
                current.clear();
            }
            continue;
        }
        if (escaped) {
            current.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            in_string = false;
            values.push_back(current);
            continue;
        }
        current.push_back(c);
    }
    return values;
}

}  // namespace erg
