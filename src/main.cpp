#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

#include "retrieval_gateway/api/http_server.h"
#include "retrieval_gateway/auth/supabase_auth.h"
#include "retrieval_gateway/backend/in_memory_opensearch_client.h"
#include "retrieval_gateway/backend/opensearch_http_client.h"
#include "retrieval_gateway/common/demo_data.h"
#include "retrieval_gateway/common/json_util.h"
#include "retrieval_gateway/common/parse_util.h"
#include "retrieval_gateway/indexing/incremental_indexer.h"
#include "retrieval_gateway/metrics/query_metrics_recorder.h"
#include "retrieval_gateway/search/retrieval_gateway.h"

namespace {

using namespace erg;

struct BackendSettings {
    std::string backend{"memory"};
    std::string opensearch_url{"http://localhost:9200"};
    std::string opensearch_index{"enterprise_docs"};
    bool seed_demo{false};
};

struct SupabaseAuthSettingsInput {
    SupabaseAuthSettings settings;
    std::string bindings_file;
};

struct App {
    ACLFilterBuilder acl;
    AccessPolicyResolver resolver;
    std::unique_ptr<SearchBackend> backend;
    EmbeddingProvider embeddings;
    QueryMetricsRecorder metrics;

    explicit App(const BackendSettings& settings)
        : acl(),
          resolver(AccessPolicyResolver::demo()),
          backend(),
          embeddings(64, "local-hash-v1"),
          metrics() {
        if (settings.backend == "memory") {
            backend = std::make_unique<InMemoryOpenSearchClient>(acl);
        } else if (settings.backend == "opensearch") {
            backend = std::make_unique<OpenSearchHttpClient>(
                OpenSearchOptions{settings.opensearch_url, settings.opensearch_index});
        } else {
            throw std::runtime_error("unknown backend: " + settings.backend);
        }

        if (settings.backend == "memory" || settings.seed_demo) {
            IncrementalIndexer indexer(*backend, embeddings);
            for (const auto& chunk : buildDemoChunks()) {
                DocumentChange change;
                change.type = ChangeType::Upsert;
                change.chunk = chunk;
                indexer.sync(change);
            }
        }
    }

    RetrievalGateway gateway() {
        return RetrievalGateway(resolver, acl, *backend, embeddings, metrics);
    }
};

std::string flagValue(int argc, char** argv, const std::string& name, const std::string& default_value) {
    for (int i = 0; i < argc; ++i) {
        if (argv[i] == name) {
            if (i + 1 >= argc || std::string(argv[i + 1]).find("--") == 0) {
                throw std::invalid_argument(name + " requires a value");
            }
            return argv[i + 1];
        }
    }
    return default_value;
}

std::string envValue(const char* name, const std::string& default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return default_value;
    }
    return std::string(value);
}

std::string readTextFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 0; i < argc; ++i) {
        if (argv[i] == name) {
            return true;
        }
    }
    return false;
}

std::string commandName(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string value = argv[i];
        if (value == "demo" || value == "search" || value == "serve") {
            return value;
        }
    }
    return "";
}

BackendSettings backendSettings(int argc, char** argv) {
    BackendSettings settings;
    settings.backend = flagValue(argc, argv, "--backend", "memory");
    settings.opensearch_url = flagValue(argc, argv, "--opensearch-url", "http://localhost:9200");
    settings.opensearch_index = flagValue(argc, argv, "--opensearch-index", "enterprise_docs");
    settings.seed_demo = hasFlag(argc, argv, "--seed-demo");
    return settings;
}

SupabaseAuthSettingsInput supabaseAuthSettings(int argc, char** argv) {
    SupabaseAuthSettingsInput input;
    input.settings.jwt_secret = flagValue(argc, argv, "--supabase-jwt-secret", envValue("SUPABASE_JWT_SECRET", ""));
    input.settings.jwks_json = flagValue(argc, argv, "--supabase-jwks-json", envValue("SUPABASE_JWKS_JSON", ""));
    const std::string jwks_file = flagValue(argc, argv, "--supabase-jwks-file", envValue("SUPABASE_JWKS_FILE", ""));
    if (input.settings.jwks_json.empty() && !jwks_file.empty()) {
        input.settings.jwks_json = readTextFile(jwks_file);
    }
    input.settings.expected_audience = flagValue(
        argc, argv, "--supabase-jwt-audience", envValue("SUPABASE_JWT_AUDIENCE", "authenticated"));
    input.settings.expected_issuer = flagValue(argc, argv, "--supabase-jwt-issuer", envValue("SUPABASE_JWT_ISSUER", ""));
    input.settings.require_auth = hasFlag(argc, argv, "--require-supabase-auth") ||
                                  envValue("SUPABASE_REQUIRE_AUTH", "false") == "true";
    input.bindings_file = flagValue(argc, argv, "--supabase-bindings-file", envValue("SUPABASE_BINDINGS_FILE", ""));
    return input;
}

void printUsage() {
    std::cout << "EnterpriseRetrievalGateway\n"
              << "Usage:\n"
              << "  ergateway demo [--backend memory|opensearch]\n"
              << "  ergateway search --user backend-user-01 --query E1027 --top-k 5 [--keyword-only|--vector-only]\n"
              << "  ergateway serve --port 8080 [--backend memory|opensearch]\n"
              << "Options:\n"
              << "  --backend memory|opensearch\n"
              << "  --opensearch-url http://localhost:9200\n"
              << "  --opensearch-index enterprise_docs\n"
              << "  --seed-demo  upsert the small built-in demo corpus into the selected backend\n";
    std::cout << "  --supabase-jwt-secret <secret>\n"
              << "  --supabase-jwks-file jwks.json\n"
              << "  --supabase-jwks-json '{...}'\n"
              << "  --supabase-jwt-audience authenticated\n"
              << "  --supabase-jwt-issuer https://<project>.supabase.co/auth/v1\n"
              << "  --supabase-bindings-file auth_bindings.json\n"
              << "  --require-supabase-auth\n";
}

void runDemo(RetrievalGateway& gateway) {
    const std::vector<SearchRequest> requests = {
        {"backend-user-01", "E1027 payment_timeout", 5, {"payment"}, {}, true, true},
        {"backend-user-01", "downstream payment interface no response for a long time", 5, {"payment"}, {}, true, true},
        {"backend-user-01", "confidential financial report revenue margin", 5, {}, {}, true, true},
        {"finance-user-01", "confidential financial report revenue margin", 5, {}, {}, true, true},
        {"no-access-user", "E1027 payment", 5, {}, {}, true, true},
    };

    for (const auto& request : requests) {
        const auto response = gateway.search(request);
        std::cout << responseToJson(response) << "\n";
    }
    std::cout << metricsToJson(gateway.metrics()) << "\n";
}

int run(int argc, char** argv) {
    const std::string command = commandName(argc, argv);
    if (command.empty()) {
        printUsage();
        return 0;
    }

    App app(backendSettings(argc, argv));
    auto gateway = app.gateway();

    if (command == "demo") {
        runDemo(gateway);
        return 0;
    }

    if (command == "search") {
        SearchRequest request;
        request.user_id = flagValue(argc, argv, "--user", "backend-user-01");
        request.query = flagValue(argc, argv, "--query", "");
        request.top_k = parseBoundedSize(flagValue(argc, argv, "--top-k", std::to_string(kDefaultTopK)),
                                         "--top-k",
                                         1,
                                         kMaxTopK);
        const std::string project = flagValue(argc, argv, "--project", "");
        const std::string type = flagValue(argc, argv, "--type", "");
        if (!project.empty()) {
            request.project_ids.push_back(project);
        }
        if (!type.empty()) {
            request.document_types.push_back(type);
        }
        if (hasFlag(argc, argv, "--keyword-only")) {
            request.enable_vector_search = false;
        }
        if (hasFlag(argc, argv, "--vector-only")) {
            request.enable_keyword_search = false;
        }
        std::cout << responseToJson(gateway.search(request)) << "\n";
        return 0;
    }

    if (command == "serve") {
        const auto port_value = parseBoundedSize(flagValue(argc, argv, "--port", "8080"), "--port", 1, 65535);
        const auto port = static_cast<uint16_t>(port_value);
        const SupabaseAuthSettingsInput auth_input = supabaseAuthSettings(argc, argv);
        SupabaseAuthBindings bindings;
        if (!auth_input.bindings_file.empty()) {
            bindings = SupabaseAuthBindings::fromFile(auth_input.bindings_file);
        }
        if (auth_input.settings.require_auth) {
            if (auth_input.settings.jwt_secret.empty() && auth_input.settings.jwks_json.empty()) {
                throw std::runtime_error(
                    "SUPABASE_JWT_SECRET or SUPABASE_JWKS_FILE is required when --require-supabase-auth is set");
            }
            if (bindings.empty()) {
                throw std::runtime_error("Supabase auth bindings are required when --require-supabase-auth is set");
            }
        }
        HttpServer server(gateway, SupabaseAuthManager(auth_input.settings, std::move(bindings)));
        return server.serve(port);
    }

    printUsage();
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
