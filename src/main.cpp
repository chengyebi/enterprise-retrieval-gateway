#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "retrieval_gateway/api/http_server.h"
#include "retrieval_gateway/common/demo_data.h"
#include "retrieval_gateway/common/json_util.h"
#include "retrieval_gateway/indexing/incremental_indexer.h"
#include "retrieval_gateway/metrics/query_metrics_recorder.h"
#include "retrieval_gateway/search/retrieval_gateway.h"

namespace {

using namespace erg;

struct App {
    ACLFilterBuilder acl;
    AccessPolicyResolver resolver;
    InMemoryOpenSearchClient backend;
    EmbeddingProvider embeddings;
    QueryMetricsRecorder metrics;

    App()
        : acl(),
          resolver(AccessPolicyResolver::demo()),
          backend(acl),
          embeddings(64, "local-hash-v1"),
          metrics() {
        IncrementalIndexer indexer(backend, embeddings);
        for (const auto& chunk : buildDemoChunks()) {
            DocumentChange change;
            change.type = ChangeType::Upsert;
            change.chunk = chunk;
            indexer.sync(change);
        }
    }

    RetrievalGateway gateway() {
        return RetrievalGateway(resolver, acl, backend, embeddings, metrics);
    }
};

std::string flagValue(int argc, char** argv, const std::string& name, const std::string& default_value) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return argv[i + 1];
        }
    }
    return default_value;
}

bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 0; i < argc; ++i) {
        if (argv[i] == name) {
            return true;
        }
    }
    return false;
}

void printUsage() {
    std::cout << "EnterpriseRetrievalGateway\n"
              << "Usage:\n"
              << "  ergateway demo\n"
              << "  ergateway search --user backend-user-01 --query E1027 --top-k 5 [--keyword-only|--vector-only]\n"
              << "  ergateway serve --port 8080\n";
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 0;
    }

    App app;
    auto gateway = app.gateway();
    const std::string command = argv[1];

    if (command == "demo") {
        runDemo(gateway);
        return 0;
    }

    if (command == "search") {
        SearchRequest request;
        request.user_id = flagValue(argc, argv, "--user", "backend-user-01");
        request.query = flagValue(argc, argv, "--query", "");
        request.top_k = static_cast<std::size_t>(std::stoull(flagValue(argc, argv, "--top-k", "10")));
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
        const auto port = static_cast<uint16_t>(std::stoul(flagValue(argc, argv, "--port", "8080")));
        HttpServer server(gateway);
        return server.serve(port);
    }

    printUsage();
    return 1;
}

