#include "retrieval_gateway/api/http_server.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "retrieval_gateway/api/request_mapper.h"
#include "retrieval_gateway/common/json_util.h"
#include "retrieval_gateway/metrics/query_metrics_recorder.h"

namespace erg {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLength = int;
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
using SocketLength = socklen_t;
constexpr SocketHandle invalid_socket_handle = -1;

void closeSocket(SocketHandle socket) {
    close(socket);
}

void ensureSocketRuntime() {}
#endif

std::string httpResponse(const std::string& status, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

std::string errorBody(const std::string& message) {
    return "{\"error\":\"" + jsonEscape(message) + "\"}";
}

std::string minimalHealthBody() {
    return "{\"status\":\"ok\"}";
}

std::string requestBody(const std::string& request_text) {
    const std::string sep = "\r\n\r\n";
    const auto pos = request_text.find(sep);
    if (pos == std::string::npos) {
        return "";
    }
    return request_text.substr(pos + sep.size());
}

std::string requestLine(const std::string& request_text) {
    const auto pos = request_text.find("\r\n");
    if (pos == std::string::npos) {
        return request_text;
    }
    return request_text.substr(0, pos);
}

std::map<std::string, std::string> requestHeaders(const std::string& request_text) {
    std::map<std::string, std::string> headers;
    std::istringstream stream(request_text);
    std::string line;
    bool first = true;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (first) {
            first = false;
            continue;
        }
        if (line.empty()) {
            break;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        headers[key] = value;
    }
    return headers;
}

std::string headerValue(const std::map<std::string, std::string>& headers, const std::string& name) {
    const auto it = headers.find(name);
    if (it == headers.end()) {
        return "";
    }
    return it->second;
}

struct AuthGateResult {
    bool ok{true};
    std::string acl_user_id;
    std::string response;
};

AuthGateResult requireBearerAuth(const SupabaseAuthManager& auth_manager,
                                 const std::map<std::string, std::string>& headers) {
    AuthGateResult result;
    if (!auth_manager.enabled() || !auth_manager.settings().require_auth) {
        return result;
    }
    const std::string authorization = headerValue(headers, "authorization");
    if (authorization.empty()) {
        result.ok = false;
        result.response = httpResponse("401 Unauthorized", errorBody("missing authorization"));
        return result;
    }
    const SupabaseAuthResult auth = auth_manager.resolveBearerToken(authorization);
    if (!auth.ok) {
        result.ok = false;
        result.response = httpResponse("401 Unauthorized", errorBody("unauthorized"));
        return result;
    }
    result.acl_user_id = auth.acl_user_id;
    return result;
}

}  // namespace

HttpServer::HttpServer(RetrievalGateway& gateway, SupabaseAuthManager supabase_auth)
    : gateway_(gateway), supabase_auth_(std::move(supabase_auth)) {}

int HttpServer::serve(uint16_t port) {
    ensureSocketRuntime();

    const SocketHandle server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == invalid_socket_handle) {
        std::cerr << "failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "failed to bind port " << port << "\n";
        closeSocket(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "failed to listen\n";
        closeSocket(server_fd);
        return 1;
    }

    std::cout << "EnterpriseRetrievalGateway listening on http://localhost:" << port << "\n";

    while (true) {
        sockaddr_in client_address{};
        SocketLength client_len = sizeof(client_address);
        const SocketHandle client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_address), &client_len);
        if (client_fd == invalid_socket_handle) {
            continue;
        }
        char buffer[65536];
        std::memset(buffer, 0, sizeof(buffer));
        const int read_size = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (read_size > 0) {
            const std::string request_text(buffer, static_cast<std::size_t>(read_size));
            const std::string response = handleRequest(request_text);
            send(client_fd, response.data(), static_cast<int>(response.size()), 0);
        }
        closeSocket(client_fd);
    }
}

std::string HttpServer::handleRequest(const std::string& request_text) {
    const std::string line = requestLine(request_text);
    const auto headers = requestHeaders(request_text);
    if (line.find("OPTIONS ") == 0) {
        return httpResponse("204 No Content", "");
    }
    if (line.find("GET /health ") == 0) {
        if (supabase_auth_.enabled() && supabase_auth_.settings().require_auth) {
            const std::string authorization = headerValue(headers, "authorization");
            if (authorization.empty()) {
                return httpResponse("200 OK", minimalHealthBody());
            }
            const SupabaseAuthResult auth = supabase_auth_.resolveBearerToken(authorization);
            if (!auth.ok) {
                return httpResponse("401 Unauthorized", errorBody("unauthorized"));
            }
        }
        return httpResponse("200 OK", gateway_.health());
    }
    if (line.find("GET /metrics ") == 0) {
        const auto auth_gate = requireBearerAuth(supabase_auth_, headers);
        if (!auth_gate.ok) {
            return auth_gate.response;
        }
        return httpResponse("200 OK", metricsToJson(gateway_.metrics()));
    }
    if (line.find("POST /v1/search ") == 0) {
        SearchRequest request;
        try {
            request = searchRequestFromJson(requestBody(request_text));
        } catch (const std::invalid_argument& error) {
            return httpResponse("400 Bad Request", errorBody(error.what()));
        }
        const std::string authorization = headerValue(headers, "authorization");
        if (supabase_auth_.enabled()) {
            if (!authorization.empty()) {
                const SupabaseAuthResult auth = supabase_auth_.resolveBearerToken(authorization);
                if (!auth.ok) {
                    return httpResponse("401 Unauthorized", errorBody("unauthorized"));
                }
                request.user_id = auth.acl_user_id;
            } else if (supabase_auth_.settings().require_auth) {
                return httpResponse("401 Unauthorized", errorBody("missing authorization"));
            }
        }
        auto response = gateway_.search(request);
        if (!response.ok) {
            response.error = "request denied";
        }
        return httpResponse(response.ok ? "200 OK" : "403 Forbidden", searchResponseToJson(response));
    }
    if (line.find("GET /v1/debug/query/") == 0) {
        const auto auth_gate = requireBearerAuth(supabase_auth_, headers);
        if (!auth_gate.ok) {
            return auth_gate.response;
        }
        const std::string prefix = "GET /v1/debug/query/";
        const auto start = prefix.size();
        const auto end = line.find(' ', start);
        const std::string query_id = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto* trace = gateway_.debugTrace(query_id);
        if (trace == nullptr) {
            return httpResponse("404 Not Found", errorBody("trace not found"));
        }
        if (!auth_gate.acl_user_id.empty() && trace->user_id != auth_gate.acl_user_id) {
            return httpResponse("404 Not Found", errorBody("trace not found"));
        }
        return httpResponse("200 OK", traceToJson(*trace));
    }
    return httpResponse("404 Not Found", errorBody("not found"));
}

}  // namespace erg
