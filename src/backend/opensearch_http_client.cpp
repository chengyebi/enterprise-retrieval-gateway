#include "retrieval_gateway/backend/opensearch_http_client.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "retrieval_gateway/common/json_util.h"
#include "retrieval_gateway/common/text.h"

namespace erg {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalid_socket_handle = INVALID_SOCKET;

void closeSocket(SocketHandle socket) {
    closesocket(socket);
}

void ensureSocketRuntime() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
    initialized = true;
}
#else
using SocketHandle = int;
constexpr SocketHandle invalid_socket_handle = -1;

void closeSocket(SocketHandle socket) {
    close(socket);
}

void ensureSocketRuntime() {}
#endif

struct ParsedUrl {
    std::string host;
    std::string port;
    std::string base_path;
};

struct JsonSpan {
    bool found{false};
    std::size_t start{std::string::npos};
    std::size_t end{std::string::npos};
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

ParsedUrl parseUrl(const std::string& raw_url) {
    std::string url = raw_url;
    const std::string http_prefix = "http://";
    if (url.rfind(http_prefix, 0) == 0) {
        url = url.substr(http_prefix.size());
    } else if (url.rfind("https://", 0) == 0) {
        throw std::runtime_error("OpenSearchHttpClient supports http:// URLs only");
    }

    std::string host_port = url;
    std::string base_path;
    const auto slash = url.find('/');
    if (slash != std::string::npos) {
        host_port = url.substr(0, slash);
        base_path = url.substr(slash);
        while (base_path.size() > 1 && base_path.back() == '/') {
            base_path.pop_back();
        }
    }

    std::string host = host_port;
    std::string port = "80";
    const auto colon = host_port.rfind(':');
    if (colon != std::string::npos) {
        host = host_port.substr(0, colon);
        port = host_port.substr(colon + 1);
    }
    if (host.empty() || port.empty()) {
        throw std::runtime_error("invalid OpenSearch URL: " + raw_url);
    }
    return ParsedUrl{host, port, base_path};
}

bool isHttpSuccess(int status) {
    return status >= 200 && status < 300;
}

std::string joinJson(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

std::string jsonString(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string numberArray(const std::vector<double>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << std::setprecision(17) << values[i];
    }
    out << "]";
    return out.str();
}

std::string termFilter(const std::string& field, const std::string& value) {
    return "{\"term\":{" + jsonString(field) + ":" + jsonString(value) + "}}";
}

std::string termsFilter(const std::string& field, const std::vector<std::string>& values) {
    return "{\"terms\":{" + jsonString(field) + ":" + jsonArray(values) + "}}";
}

std::string sourceFieldsJson() {
    return "[\"tenant_id\",\"document_id\",\"chunk_id\",\"title\",\"content\",\"department\",\"project_id\","
           "\"allowed_groups\",\"document_type\",\"document_version\",\"content_hash\",\"updated_at\","
           "\"embedding_model_version\"]";
}

std::string documentToJson(const DocumentChunk& chunk) {
    std::ostringstream out;
    out << "{"
        << "\"tenant_id\":" << jsonString(chunk.tenant_id) << ","
        << "\"document_id\":" << jsonString(chunk.document_id) << ","
        << "\"chunk_id\":" << jsonString(chunk.chunk_id) << ","
        << "\"title\":" << jsonString(chunk.title) << ","
        << "\"content\":" << jsonString(chunk.content) << ","
        << "\"department\":" << jsonString(chunk.department) << ","
        << "\"project_id\":" << jsonString(chunk.project_id) << ","
        << "\"allowed_groups\":" << jsonArray(chunk.allowed_groups) << ","
        << "\"document_type\":" << jsonString(chunk.document_type) << ","
        << "\"document_version\":" << chunk.document_version << ","
        << "\"content_hash\":" << jsonString(chunk.content_hash) << ","
        << "\"updated_at\":" << (chunk.updated_at.empty() ? std::string("null") : jsonString(chunk.updated_at)) << ","
        << "\"embedding_model_version\":" << jsonString(chunk.embedding_model_version) << ","
        << "\"embedding\":" << numberArray(chunk.embedding)
        << "}";
    return out.str();
}

std::size_t scanJsonValueEnd(const std::string& body, std::size_t start) {
    if (start >= body.size()) {
        return start;
    }

    const char first = body[start];
    if (first == '"') {
        bool escaped = false;
        for (std::size_t i = start + 1; i < body.size(); ++i) {
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
                return i + 1;
            }
        }
        return body.size();
    }

    if (first == '{' || first == '[') {
        const char open = first;
        const char close = first == '{' ? '}' : ']';
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
            if (in_string) {
                continue;
            }
            if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
                if (depth == 0) {
                    return i + 1;
                }
            }
        }
        return body.size();
    }

    std::size_t end = start;
    while (end < body.size() && body[end] != ',' && body[end] != '}' && body[end] != ']' &&
           !std::isspace(static_cast<unsigned char>(body[end]))) {
        ++end;
    }
    return end;
}

JsonSpan findJsonValue(const std::string& body, const std::string& key, std::size_t from = 0) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::size_t key_pos = body.find(quoted_key, from);
    if (key_pos == std::string::npos) {
        return {};
    }
    const std::size_t colon = body.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) {
        return {};
    }
    std::size_t start = colon + 1;
    while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) {
        ++start;
    }
    if (start >= body.size()) {
        return {};
    }
    return JsonSpan{true, start, scanJsonValueEnd(body, start)};
}

std::string spanValue(const std::string& body, const JsonSpan& span) {
    if (!span.found || span.start >= body.size() || span.end < span.start) {
        return "";
    }
    return body.substr(span.start, span.end - span.start);
}

double parseDoubleValue(const std::string& value, double default_value = 0.0) {
    try {
        return std::stod(value);
    } catch (...) {
        return default_value;
    }
}

std::size_t parseSizeValue(const std::string& value, std::size_t default_value = 0) {
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (...) {
        return default_value;
    }
}

std::size_t parseNamedSize(const std::string& body, const std::string& key) {
    return parseSizeValue(spanValue(body, findJsonValue(body, key)));
}

SocketHandle connectSocket(const std::string& host, const std::string& port) {
    ensureSocketRuntime();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const int lookup = getaddrinfo(host.c_str(), port.c_str(), &hints, &results);
    if (lookup != 0 || results == nullptr) {
        throw std::runtime_error("could not resolve OpenSearch host " + host + ":" + port);
    }

    SocketHandle connected = invalid_socket_handle;
    for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
        SocketHandle socket = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (socket == invalid_socket_handle) {
            continue;
        }
        if (::connect(socket, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
            connected = socket;
            break;
        }
        closeSocket(socket);
    }

    freeaddrinfo(results);
    if (connected == invalid_socket_handle) {
        throw std::runtime_error("could not connect to OpenSearch at " + host + ":" + port);
    }
    return connected;
}

void sendAll(SocketHandle socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const std::size_t remaining = data.size() - sent;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, 64 * 1024));
        const int written = ::send(socket, data.data() + sent, chunk, 0);
        if (written <= 0) {
            throw std::runtime_error("failed to send request to OpenSearch");
        }
        sent += static_cast<std::size_t>(written);
    }
}

std::string receiveSome(SocketHandle socket) {
    char buffer[8192];
    const int read = recv(socket, buffer, sizeof(buffer), 0);
    if (read < 0) {
        throw std::runtime_error("failed to receive response from OpenSearch");
    }
    if (read == 0) {
        return "";
    }
    return std::string(buffer, static_cast<std::size_t>(read));
}

std::size_t parseContentLengthHeader(const std::string& headers) {
    const std::string lower = lowerAscii(headers);
    const std::string key = "content-length:";
    const std::size_t pos = lower.find(key);
    if (pos == std::string::npos) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::size_t start = pos + key.size();
    while (start < headers.size() && std::isspace(static_cast<unsigned char>(headers[start]))) {
        ++start;
    }
    std::size_t end = start;
    while (end < headers.size() && std::isdigit(static_cast<unsigned char>(headers[end]))) {
        ++end;
    }
    return parseSizeValue(headers.substr(start, end - start), std::numeric_limits<std::size_t>::max());
}

std::string receiveHttpResponse(SocketHandle socket) {
    std::string raw;
    std::size_t header_end = std::string::npos;
    while (true) {
        const std::string chunk = receiveSome(socket);
        if (chunk.empty()) {
            return raw;
        }
        raw += chunk;
        header_end = raw.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;
        }
    }

    const std::size_t first_line_end = raw.find("\r\n");
    if (first_line_end == std::string::npos) {
        return raw;
    }
    const std::string headers = raw.substr(first_line_end + 2, header_end - first_line_end - 2);
    const std::size_t expected_body_size = parseContentLengthHeader(headers);
    if (expected_body_size == std::numeric_limits<std::size_t>::max()) {
        while (true) {
            const std::string chunk = receiveSome(socket);
            if (chunk.empty()) {
                return raw;
            }
            raw += chunk;
            if (lowerAscii(headers).find("transfer-encoding: chunked") != std::string::npos &&
                raw.find("\r\n0\r\n", header_end + 4) != std::string::npos) {
                return raw;
            }
        }
    }

    while (raw.size() < header_end + 4 + expected_body_size) {
        const std::string chunk = receiveSome(socket);
        if (chunk.empty()) {
            break;
        }
        raw += chunk;
    }
    return raw;
}

std::string decodeChunkedBody(const std::string& body) {
    std::string decoded;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t line_end = body.find("\r\n", pos);
        if (line_end == std::string::npos) {
            return body;
        }
        const std::string size_hex = body.substr(pos, line_end - pos);
        std::size_t chunk_size = 0;
        try {
            chunk_size = static_cast<std::size_t>(std::stoull(size_hex, nullptr, 16));
        } catch (...) {
            return body;
        }
        pos = line_end + 2;
        if (chunk_size == 0) {
            break;
        }
        if (pos + chunk_size > body.size()) {
            return body;
        }
        decoded.append(body, pos, chunk_size);
        pos += chunk_size;
        if (pos + 2 <= body.size() && body.substr(pos, 2) == "\r\n") {
            pos += 2;
        }
    }
    return decoded;
}

}  // namespace

OpenSearchHttpClient::OpenSearchHttpClient(OpenSearchOptions options)
    : options_(std::move(options)) {
    const auto parsed = parseUrl(options_.url);
    host_ = parsed.host;
    port_ = parsed.port;
    base_path_ = parsed.base_path;
    if (options_.index.empty()) {
        throw std::runtime_error("OpenSearch index must not be empty");
    }
}

bool OpenSearchHttpClient::isAvailable() const {
    try {
        const auto response = request("GET", "/");
        return isHttpSuccess(response.status);
    } catch (...) {
        return false;
    }
}

BulkResult OpenSearchHttpClient::bulkUpsert(const std::vector<DocumentChunk>& chunks) {
    BulkResult result;
    if (chunks.empty()) {
        return result;
    }

    std::ostringstream payload;
    for (const auto& chunk : chunks) {
        if (chunk.document_id.empty() || chunk.chunk_id.empty()) {
            result.ok = false;
            result.errors.push_back("document_id and chunk_id are required");
            continue;
        }
        payload << "{\"index\":{\"_index\":" << jsonString(options_.index)
                << ",\"_id\":" << jsonString(chunk.chunk_id) << "}}\n"
                << documentToJson(chunk) << "\n";
        ++result.indexed;
    }

    if (!result.ok) {
        return result;
    }

    const auto response = request("POST", "/_bulk?refresh=true", payload.str(), "application/x-ndjson");
    const bool errors = extractJsonBool(response.body, "errors", true);
    if (!isHttpSuccess(response.status) || errors) {
        result.ok = false;
        result.errors.push_back("OpenSearch bulk upsert failed with HTTP " + std::to_string(response.status));
        if (!response.body.empty()) {
            result.errors.push_back(response.body);
        }
    }
    return result;
}

bool OpenSearchHttpClient::deleteDocument(const std::string& document_id) {
    const std::string body =
        "{\"query\":{\"term\":{\"document_id\":" + jsonString(document_id) + "}}}";
    const auto response = request("POST", "/" + options_.index + "/_delete_by_query?refresh=true", body);
    return isHttpSuccess(response.status) && parseNamedSize(response.body, "deleted") > 0;
}

bool OpenSearchHttpClient::updateAcl(const std::string& document_id,
                                     const std::string& department,
                                     const std::string& project_id,
                                     const std::vector<std::string>& allowed_groups) {
    const std::string body =
        "{\"script\":{\"source\":\"ctx._source.department=params.department;"
        "ctx._source.project_id=params.project_id;"
        "ctx._source.allowed_groups=params.allowed_groups\","
        "\"lang\":\"painless\",\"params\":{\"department\":" + jsonString(department) +
        ",\"project_id\":" + jsonString(project_id) +
        ",\"allowed_groups\":" + jsonArray(allowed_groups) +
        "}},\"query\":{\"term\":{\"document_id\":" + jsonString(document_id) + "}}}";
    const auto response = request("POST", "/" + options_.index + "/_update_by_query?refresh=true", body);
    return isHttpSuccess(response.status) && parseNamedSize(response.body, "updated") > 0;
}

std::vector<SearchHit> OpenSearchHttpClient::keywordSearch(const SearchRequest& request,
                                                           const AccessContext& access,
                                                           std::size_t limit) const {
    const auto response = this->request("POST", "/" + options_.index + "/_search",
                                        buildKeywordQuery(request, access, std::max<std::size_t>(limit, 1)));
    if (!isHttpSuccess(response.status)) {
        throw std::runtime_error("OpenSearch keyword search failed with HTTP " + std::to_string(response.status));
    }
    return parseSearchHits(response.body, "keyword", true);
}

std::vector<SearchHit> OpenSearchHttpClient::vectorSearch(const SearchRequest& request,
                                                          const AccessContext& access,
                                                          const std::vector<double>& query_embedding,
                                                          std::size_t limit,
                                                          bool exact) const {
    (void)exact;
    const std::size_t candidate_limit = std::max<std::size_t>(limit, 1);
    const auto response = this->request("POST", "/" + options_.index + "/_search",
                                        buildVectorQuery(request, access, query_embedding, candidate_limit));
    if (!isHttpSuccess(response.status)) {
        throw std::runtime_error("OpenSearch vector search failed with HTTP " + std::to_string(response.status));
    }
    return parseSearchHits(response.body, "vector", false);
}

std::size_t OpenSearchHttpClient::estimateAuthorizedCandidates(const SearchRequest& request,
                                                               const AccessContext& access) const {
    const std::string body = "{\"query\":" + buildAclFilterQuery(access, request) + "}";
    const auto response = this->request("POST", "/" + options_.index + "/_count", body);
    if (!isHttpSuccess(response.status)) {
        throw std::runtime_error("OpenSearch count failed with HTTP " + std::to_string(response.status));
    }
    return parseCount(response.body);
}

std::size_t OpenSearchHttpClient::chunkCount() const {
    const auto response = request("POST", "/" + options_.index + "/_count", "{\"query\":{\"match_all\":{}}}");
    if (!isHttpSuccess(response.status)) {
        return 0;
    }
    return parseCount(response.body);
}

std::size_t OpenSearchHttpClient::documentCount() const {
    const std::string body =
        "{\"size\":0,\"aggs\":{\"documents\":{\"cardinality\":{\"field\":\"document_id\"}}}}";
    const auto response = request("POST", "/" + options_.index + "/_search", body);
    if (!isHttpSuccess(response.status)) {
        return 0;
    }
    return parseAggregationValue(response.body, "documents");
}

std::string OpenSearchHttpClient::backendName() const {
    return "opensearch_http";
}

OpenSearchHttpClient::HttpResponse OpenSearchHttpClient::request(const std::string& method,
                                                                  const std::string& path,
                                                                  const std::string& body,
                                                                  const std::string& content_type) const {
    SocketHandle socket = connectSocket(host_, port_);
    try {
        std::ostringstream wire;
        wire << method << " " << buildPath(path) << " HTTP/1.1\r\n"
             << "Host: " << host_ << ":" << port_ << "\r\n"
             << "Connection: close\r\n";
        if (!body.empty()) {
            wire << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n";
        }
        wire << "\r\n" << body;

        sendAll(socket, wire.str());
        const std::string raw = receiveHttpResponse(socket);
        closeSocket(socket);

        const std::size_t first_line_end = raw.find("\r\n");
        if (first_line_end == std::string::npos) {
            return HttpResponse{0, raw};
        }
        std::istringstream first_line(raw.substr(0, first_line_end));
        std::string protocol;
        int status = 0;
        first_line >> protocol >> status;

        const std::size_t header_end = raw.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            return HttpResponse{status, ""};
        }
        const std::string headers = raw.substr(first_line_end + 2, header_end - first_line_end - 2);
        std::string response_body = raw.substr(header_end + 4);
        if (lowerAscii(headers).find("transfer-encoding: chunked") != std::string::npos) {
            response_body = decodeChunkedBody(response_body);
        }
        return HttpResponse{status, response_body};
    } catch (...) {
        closeSocket(socket);
        throw;
    }
}

std::string OpenSearchHttpClient::buildPath(const std::string& path) const {
    if (base_path_.empty()) {
        return path.empty() ? "/" : path;
    }
    if (path.empty() || path == "/") {
        return base_path_;
    }
    return base_path_ + path;
}

std::string OpenSearchHttpClient::buildAclFilterQuery(const AccessContext& access,
                                                      const SearchRequest& request) const {
    if (access.tenant_id.empty()) {
        return "{\"match_none\":{}}";
    }
    if (!access.is_admin && (access.department.empty() || access.project_ids.empty() || access.groups.empty())) {
        return "{\"match_none\":{}}";
    }

    std::vector<std::string> filters;
    filters.push_back(termFilter("tenant_id", access.tenant_id));
    if (!access.is_admin) {
        filters.push_back(termFilter("department", access.department));
        filters.push_back(termsFilter("project_id", access.project_ids));
        filters.push_back(termsFilter("allowed_groups", access.groups));
    }
    if (!request.project_ids.empty()) {
        filters.push_back(termsFilter("project_id", request.project_ids));
    }
    if (!request.document_types.empty()) {
        filters.push_back(termsFilter("document_type", request.document_types));
    }
    return "{\"bool\":{\"filter\":[" + joinJson(filters) + "]}}";
}

std::string OpenSearchHttpClient::buildKeywordQuery(const SearchRequest& request,
                                                    const AccessContext& access,
                                                    std::size_t limit) const {
    const std::string filter_query = buildAclFilterQuery(access, request);
    std::ostringstream body;
    body << "{"
         << "\"size\":" << limit << ","
         << "\"track_total_hits\":false,"
         << "\"_source\":" << sourceFieldsJson() << ","
         << "\"query\":{\"bool\":{\"filter\":[" << filter_query << "],"
         << "\"must\":[{\"multi_match\":{\"query\":" << jsonString(request.query)
         << ",\"fields\":[\"title^2\",\"content\"],\"operator\":\"or\"}}]}}"
         << "}";
    return body.str();
}

std::string OpenSearchHttpClient::buildVectorQuery(const SearchRequest& request,
                                                   const AccessContext& access,
                                                   const std::vector<double>& query_embedding,
                                                   std::size_t limit) const {
    const std::string filter_query = buildAclFilterQuery(access, request);
    std::ostringstream body;
    body << "{"
         << "\"size\":" << limit << ","
         << "\"track_total_hits\":false,"
         << "\"_source\":" << sourceFieldsJson() << ","
         << "\"query\":{\"knn\":{\"embedding\":{\"vector\":" << numberArray(query_embedding)
         << ",\"k\":" << limit
         << ",\"filter\":" << filter_query
         << "}}}"
         << "}";
    return body.str();
}

std::vector<SearchHit> OpenSearchHttpClient::parseSearchHits(const std::string& body,
                                                             const std::string& source,
                                                             bool lexical_scores) const {
    std::vector<SearchHit> hits;
    std::size_t pos = 0;
    while (true) {
        const JsonSpan source_span = findJsonValue(body, "_source", pos);
        if (!source_span.found) {
            break;
        }

        double score = 0.0;
        const JsonSpan score_span = findJsonValue(body, "_score", pos);
        if (score_span.found && score_span.start < source_span.start) {
            score = parseDoubleValue(spanValue(body, score_span));
        }

        const std::string source_json = spanValue(body, source_span);
        SearchHit hit;
        hit.tenant_id = extractJsonString(source_json, "tenant_id");
        hit.document_id = extractJsonString(source_json, "document_id");
        hit.chunk_id = extractJsonString(source_json, "chunk_id");
        hit.title = extractJsonString(source_json, "title");
        const std::string content = extractJsonString(source_json, "content");
        hit.snippet = makeSnippet(content, hit.title);
        hit.department = extractJsonString(source_json, "department");
        hit.project_id = extractJsonString(source_json, "project_id");
        hit.allowed_groups = extractJsonStringArray(source_json, "allowed_groups");
        hit.document_type = extractJsonString(source_json, "document_type");
        if (lexical_scores) {
            hit.lexical_score = score;
        } else {
            hit.semantic_score = score;
        }
        hit.source = source;
        hits.push_back(hit);
        pos = source_span.end;
    }
    return hits;
}

std::size_t OpenSearchHttpClient::parseCount(const std::string& body) const {
    return parseNamedSize(body, "count");
}

std::size_t OpenSearchHttpClient::parseAggregationValue(const std::string& body,
                                                        const std::string& aggregation_name) const {
    const auto aggregation = findJsonValue(body, aggregation_name);
    if (!aggregation.found) {
        return 0;
    }
    const auto value = findJsonValue(body, "value", aggregation.start);
    if (!value.found || value.start > aggregation.end) {
        return 0;
    }
    return parseSizeValue(spanValue(body, value));
}

}  // namespace erg
