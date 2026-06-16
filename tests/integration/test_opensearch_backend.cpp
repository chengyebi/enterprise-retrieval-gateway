#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "retrieval_gateway/auth/access_policy_resolver.h"
#include "retrieval_gateway/backend/opensearch_http_client.h"
#include "retrieval_gateway/indexing/incremental_indexer.h"
#include "retrieval_gateway/metrics/query_metrics_recorder.h"
#include "retrieval_gateway/search/retrieval_gateway.h"

namespace {

using namespace erg;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string envOr(const char* name, const std::string& default_value) {
    const char* value = std::getenv(name);
    return value == nullptr || std::string(value).empty() ? default_value : std::string(value);
}

bool containsDocument(const SearchResponse& response, const std::string& document_id) {
    for (const auto& hit : response.hits) {
        if (hit.document_id == document_id) {
            return true;
        }
    }
    return false;
}

DocumentChunk chunk(const std::string& document_id,
                    const std::string& chunk_id,
                    const std::string& title,
                    const std::string& content,
                    const std::string& department,
                    const std::string& project_id,
                    const std::vector<std::string>& groups,
                    const std::string& document_type) {
    DocumentChunk value;
    value.document_id = document_id;
    value.chunk_id = chunk_id;
    value.title = title;
    value.content = content;
    value.department = department;
    value.project_id = project_id;
    value.allowed_groups = groups;
    value.document_type = document_type;
    value.updated_at = "2026-06-04T00:00:00Z";
    return value;
}

SyncResult upsert(IncrementalIndexer& indexer, const DocumentChunk& value) {
    DocumentChange change;
    change.type = ChangeType::Upsert;
    change.chunk = value;
    return indexer.sync(change);
}

void requireSyncOk(const SyncResult& result, const std::string& message) {
    if (!result.ok) {
        std::cerr << "FAIL: " << message << "\n" << result.message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::string url = envOr("OPENSEARCH_URL", "http://localhost:9200");
    const std::string index = envOr("OPENSEARCH_INDEX", "enterprise_docs");

    ACLFilterBuilder acl;
    AccessPolicyResolver resolver = AccessPolicyResolver::demo();
    OpenSearchHttpClient backend(OpenSearchOptions{url, index});
    if (!backend.isAvailable()) {
        std::cout << "SKIP: OpenSearch is not available at " << url << "\n";
        return 0;
    }
    EmbeddingProvider embeddings(64, "local-hash-v1");
    QueryMetricsRecorder metrics;
    IncrementalIndexer indexer(backend, embeddings);

    backend.deleteDocument("integration-opensearch-payment");
    backend.deleteDocument("integration-opensearch-finance");

    requireSyncOk(upsert(indexer,
                         chunk("integration-opensearch-payment",
                               "integration-opensearch-payment#1",
                               "Integration E2999 payment runbook",
                               "Integration E2999 payment outage runbook for backend responders.",
                               "engineering",
                               "payment",
                               {"backend"},
                               "runbook")),
                  "payment test document should upsert");
    requireSyncOk(upsert(indexer,
                         chunk("integration-opensearch-finance",
                               "integration-opensearch-finance#1",
                               "Integration confidential finance report",
                               "Integration confidential finance revenue margin report for finance users.",
                               "finance",
                               "finance-core",
                               {"finance"},
                               "finance_report")),
                  "finance test document should upsert");

    RetrievalGateway gateway(resolver, acl, backend, embeddings, metrics);
    auto backend_payment = gateway.search({"backend-user-01", "Integration E2999 payment", 5, {"payment"}, {}, true, true});
    require(backend_payment.ok, "backend payment query should succeed");
    require(containsDocument(backend_payment, "integration-opensearch-payment"),
            "backend user should see payment integration document");

    auto backend_finance = gateway.search({"backend-user-01", "Integration confidential finance revenue", 5, {}, {}, true, true});
    require(backend_finance.ok, "backend finance query should succeed");
    require(!containsDocument(backend_finance, "integration-opensearch-finance"),
            "backend user must not see finance integration document");

    auto finance_response = gateway.search({"finance-user-01", "Integration confidential finance revenue", 5, {}, {}, true, true});
    require(finance_response.ok, "finance query should succeed");
    require(containsDocument(finance_response, "integration-opensearch-finance"),
            "finance user should see finance integration document");

    auto admin_response = gateway.search({"admin-user", "Integration confidential finance revenue", 5, {}, {}, true, true});
    require(admin_response.ok, "admin query should succeed");
    require(containsDocument(admin_response, "integration-opensearch-finance"),
            "admin should see finance integration document");

    require(backend.updateAcl("integration-opensearch-payment", "finance", "finance-core", {"finance"}),
            "ACL update should report updated documents");
    auto hidden_after_acl = gateway.search({"backend-user-01", "Integration E2999 payment", 5, {}, {}, true, true});
    require(hidden_after_acl.ok, "query after ACL update should succeed");
    require(!containsDocument(hidden_after_acl, "integration-opensearch-payment"),
            "ACL update should hide payment document from backend user");

    require(backend.deleteDocument("integration-opensearch-payment"), "payment integration document should delete");
    require(backend.deleteDocument("integration-opensearch-finance"), "finance integration document should delete");

    std::cout << "opensearch integration tests passed\n";
    return 0;
}
