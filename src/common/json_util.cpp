#include "retrieval_gateway/common/json_util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace erg {

namespace {

std::string findJsonValueSpan(const std::string& body, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    std::size_t key_pos = body.find(quoted_key);
    if (key_pos == std::string::npos) {
        return "";
    }
    std::size_t colon = body.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) {
        return "";
    }
    std::size_t start = colon + 1;
    while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) {
        ++start;
    }
    if (start >= body.size()) {
        return "";
    }

    if (body[start] == '"') {
        bool escaped = false;
        for (std::size_t i = start + 1; i < body.size(); ++i) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (body[i] == '\\') {
                escaped = true;
                continue;
            }
            if (body[i] == '"') {
                return body.substr(start, i - start + 1);
            }
        }
        return "";
    }

    if (body[start] == '[') {
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        for (std::size_t i = start; i < body.size(); ++i) {
            const char c = body[i];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') {
                in_string = !in_string;
                continue;
            }
            if (!in_string && c == '[') {
                ++depth;
            }
            if (!in_string && c == ']') {
                --depth;
                if (depth == 0) {
                    return body.substr(start, i - start + 1);
                }
            }
        }
    }

    std::size_t end = start;
    while (end < body.size() && body[end] != ',' && body[end] != '}' && !std::isspace(static_cast<unsigned char>(body[end]))) {
        ++end;
    }
    return body.substr(start, end - start);
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

