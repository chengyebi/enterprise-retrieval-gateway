#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "retrieval_gateway/auth/access_policy_resolver.h"
#include "retrieval_gateway/auth/acl_filter_builder.h"
#include "retrieval_gateway/auth/supabase_auth.h"
#include "retrieval_gateway/backend/in_memory_opensearch_client.h"
#include "retrieval_gateway/common/demo_data.h"
#include "retrieval_gateway/indexing/incremental_indexer.h"
#include "retrieval_gateway/search/filter_aware_query_planner.h"
#include "retrieval_gateway/search/retrieval_gateway.h"
#include "retrieval_gateway/search/result_deduplicator.h"
#include "retrieval_gateway/search/rrf_fusion.h"

namespace {

using namespace erg;

struct Fixture {
    ACLFilterBuilder acl;
    AccessPolicyResolver resolver;
    InMemoryOpenSearchClient backend;
    EmbeddingProvider embeddings;
    QueryMetricsRecorder metrics;

    Fixture()
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

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool containsDocument(const SearchResponse& response, const std::string& document_id) {
    for (const auto& hit : response.hits) {
        if (hit.document_id == document_id) {
            return true;
        }
    }
    return false;
}

void testAclSecurity() {
    Fixture fixture;
    auto gateway = fixture.gateway();

    auto backend_response = gateway.search({"backend-user-01", "E1027 payment_timeout", 5, {"payment"}, {}, true, true});
    require(backend_response.ok, "backend user search should succeed");
    require(containsDocument(backend_response, "incident-2026-041"), "backend user should see payment incident");
    for (const auto& hit : backend_response.hits) {
        require(hit.department == "engineering", "backend user must not receive non-engineering documents");
    }

    auto finance_leak = gateway.search({"backend-user-01", "confidential financial report revenue margin", 5, {}, {}, true, true});
    for (const auto& hit : finance_leak.hits) {
        require(hit.document_id != "finance-report-q2", "backend user must not see finance report");
    }

    auto finance_response = gateway.search({"finance-user-01", "confidential financial report revenue margin", 5, {}, {}, true, true});
    require(containsDocument(finance_response, "finance-report-q2"), "finance user should see finance report");

    auto admin_response = gateway.search({"admin-user", "confidential financial report revenue margin", 5, {}, {}, true, true});
    require(containsDocument(admin_response, "finance-report-q2"), "admin should see finance report");

    auto unknown = gateway.search({"unknown-user", "E1027", 5, {}, {}, true, true});
    require(!unknown.ok, "unknown user should fail closed");
}

void testRrfAndDedup() {
    SearchHit a;
    a.chunk_id = "a#1";
    a.document_id = "a";
    a.lexical_score = 10.0;
    SearchHit b;
    b.chunk_id = "b#1";
    b.document_id = "b";
    b.lexical_score = 5.0;
    SearchHit c = a;
    c.semantic_score = 8.0;

    RRFFusion fusion;
    const auto fused = fusion.fuse({a, b}, {c}, 10);
    require(!fused.empty(), "RRF should produce hits");
    require(fused.front().chunk_id == "a#1", "RRF should boost document present in both result lists");
    require(fused.front().source == "hybrid", "RRF should mark combined source as hybrid");

    SearchHit a2 = a;
    a2.chunk_id = "a#2";
    ResultDeduplicator dedup(1);
    const auto deduped = dedup.deduplicate({a, a2, b}, 10);
    require(deduped.size() == 2, "dedup should keep at most one chunk per document");
}

void testPlannerBranches() {
    Fixture fixture;
    const auto access = fixture.resolver.resolve("backend-user-01");
    FilterAwareQueryPlanner planner(3, 4, 16);

    SearchRequest broad{"backend-user-01", "payment search", 10, {}, {}, true, true};
    const auto broad_plan = planner.buildPlan(broad, access, fixture.backend, fixture.acl);
    require(broad_plan.mode == RetrievalMode::HybridWithIterativeExpansion, "broad ACL should use iterative hybrid plan");
    require(planner.nextCandidateLimit(4) == 8, "planner should double candidate limit");

    SearchRequest strict{"backend-user-01", "payment", 5, {"security"}, {}, true, true};
    const auto strict_plan = planner.buildPlan(strict, access, fixture.backend, fixture.acl);
    require(strict_plan.mode == RetrievalMode::FilteredExactVector, "small candidate set should use exact branch");
}

void testIncrementalIndexer() {
    Fixture fixture;
    IncrementalIndexer indexer(fixture.backend, fixture.embeddings);

    DocumentChunk chunk;
    chunk.document_id = "runbook-new";
    chunk.chunk_id = "runbook-new#1";
    chunk.title = "New payment runbook";
    chunk.content = "New E2000 payment alert runbook for backend users.";
    chunk.department = "engineering";
    chunk.project_id = "payment";
    chunk.allowed_groups = {"backend"};
    chunk.document_type = "runbook";

    DocumentChange upsert;
    upsert.type = ChangeType::Upsert;
    upsert.chunk = chunk;
    require(indexer.sync(upsert).ok, "upsert should succeed");

    auto gateway = fixture.gateway();
    auto visible = gateway.search({"backend-user-01", "E2000", 5, {"payment"}, {}, true, true});
    require(containsDocument(visible, "runbook-new"), "new document should be searchable");

    DocumentChange acl_update;
    acl_update.type = ChangeType::UpdateAcl;
    acl_update.document_id = "runbook-new";
    acl_update.department = "finance";
    acl_update.project_id = "finance-core";
    acl_update.allowed_groups = {"finance"};
    require(indexer.sync(acl_update).ok, "acl update should succeed");

    auto hidden = gateway.search({"backend-user-01", "E2000", 5, {}, {}, true, true});
    require(!containsDocument(hidden, "runbook-new"), "ACL update should hide document immediately");

    DocumentChange delete_change;
    delete_change.type = ChangeType::DeleteDocument;
    delete_change.document_id = "runbook-new";
    require(indexer.sync(delete_change).ok, "delete should succeed");
    auto deleted = gateway.search({"finance-user-01", "E2000", 5, {}, {}, true, true});
    require(!containsDocument(deleted, "runbook-new"), "delete should remove document from search");
}

void testMetricsAndDebugTrace() {
    Fixture fixture;
    auto gateway = fixture.gateway();
    const auto response = gateway.search({"backend-user-01", "downstream payment timeout", 5, {}, {}, true, true});
    require(response.ok, "search should succeed");
    const auto metrics = gateway.metrics();
    require(metrics.total_queries == 1, "metrics should count query");
    const auto* trace = gateway.debugTrace(response.query_id);
    require(trace != nullptr, "debug trace should exist");
    require(trace->acl_filter_summary.find("tenant=tenant-acme") != std::string::npos, "trace should record ACL summary");
}

void testSupabaseJwtBinding() {
    const std::string token =
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
        "eyJzdWIiOiJhdXRoLXVzZXItMSIsImF1ZCI6ImF1dGhlbnRpY2F0ZWQiLCJlbWFpbCI6ImRlbW9AZXhhbXBsZS5jb20iLCJleHAiOjIwMDAwMDAwMDB9."
        "IDxK49S3f2_KLr8LoP86MVQ71A78vbmMaiuvPxDbHJQ";

    SupabaseAuthBindings bindings = SupabaseAuthBindings::fromObjectMap(R"({"auth-user-1":"backend-user-01"})");
    SupabaseAuthSettings settings;
    settings.jwt_secret = "test-secret";
    settings.expected_audience = "authenticated";
    settings.expected_issuer = "";
    SupabaseAuthManager auth(settings, bindings);
    const auto result = auth.resolveBearerToken("Bearer " + token);
    require(result.ok, "valid supabase jwt should resolve");
    require(result.auth_user_id == "auth-user-1", "jwt subject should be preserved");
    require(result.acl_user_id == "backend-user-01", "binding should map auth user to ACL user");

    AccessPolicyResolver resolver = AccessPolicyResolver::demo();
    const auto access = auth.accessContextForBearerToken("Bearer " + token, resolver);
    require(access.user_id == "backend-user-01", "resolved access context should use bound ACL user");
    require(access.department == "engineering", "resolved ACL user should come from demo resolver");
}

}  // namespace

int main() {
    testAclSecurity();
    testRrfAndDedup();
    testPlannerBranches();
    testIncrementalIndexer();
    testMetricsAndDebugTrace();
    testSupabaseJwtBinding();
    std::cout << "core tests passed\n";
    return 0;
}
