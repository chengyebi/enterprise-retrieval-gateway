#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "retrieval_gateway/api/http_server.h"
#include "retrieval_gateway/api/request_mapper.h"
#include "retrieval_gateway/auth/access_policy_resolver.h"
#include "retrieval_gateway/auth/acl_filter_builder.h"
#include "retrieval_gateway/auth/supabase_auth.h"
#include "retrieval_gateway/backend/in_memory_opensearch_client.h"
#include "retrieval_gateway/common/demo_data.h"
#include "retrieval_gateway/common/parse_util.h"
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

void requireInvalidRequest(const std::string& body, const std::string& expected_error) {
    try {
        (void)searchRequestFromJson(body);
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        require(message.find(expected_error) != std::string::npos, "validation error should mention " + expected_error);
        return;
    }
    require(false, "invalid request should be rejected");
}

void requireInvalidSize(const std::string& raw, const std::string& field_name, std::size_t min_value, std::size_t max_value) {
    try {
        (void)parseBoundedSize(raw, field_name, min_value, max_value);
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        require(message.find(field_name) != std::string::npos, "bounded size error should mention field name");
        return;
    }
    require(false, "invalid bounded size should be rejected");
}

void testParseBoundedSize() {
    require(parseBoundedSize("1", "--top-k", 1, kMaxTopK) == 1, "bounded size should accept minimum");
    require(parseBoundedSize("50", "--top-k", 1, kMaxTopK) == 50, "bounded size should accept maximum");
    require(parseBoundedSize("8080", "--port", 1, 65535) == 8080, "bounded size should accept normal port");
    requireInvalidSize("", "--top-k", 1, kMaxTopK);
    requireInvalidSize("0", "--top-k", 1, kMaxTopK);
    requireInvalidSize("-1", "--top-k", 1, kMaxTopK);
    requireInvalidSize("51", "--top-k", 1, kMaxTopK);
    requireInvalidSize("abc", "--top-k", 1, kMaxTopK);
    requireInvalidSize("65536", "--port", 1, 65535);
}

std::string postSearchRequest(const std::string& body, const std::string& authorization = "") {
    std::string headers = "POST /v1/search HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n";
    if (!authorization.empty()) {
        headers += "Authorization: " + authorization + "\r\n";
    }
    headers += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    return headers + body;
}

std::string getRequest(const std::string& path, const std::string& authorization = "") {
    std::string headers = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n";
    if (!authorization.empty()) {
        headers += "Authorization: " + authorization + "\r\n";
    }
    return headers + "\r\n";
}

std::string validSupabaseHsToken() {
    return
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
        "eyJzdWIiOiJhdXRoLXVzZXItMSIsImF1ZCI6ImF1dGhlbnRpY2F0ZWQiLCJlbWFpbCI6ImRlbW9AZXhhbXBsZS5jb20iLCJleHAiOjIwMDAwMDAwMDB9."
        "IDxK49S3f2_KLr8LoP86MVQ71A78vbmMaiuvPxDbHJQ";
}

std::string responseBody(const std::string& response) {
    const auto pos = response.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return "";
    }
    return response.substr(pos + 4);
}

void requireContains(const std::string& value, const std::string& expected, const std::string& message) {
    require(value.find(expected) != std::string::npos, message);
}

void requireNotContains(const std::string& value, const std::string& unexpected, const std::string& message) {
    require(value.find(unexpected) == std::string::npos, message);
}

void testRequestMapperValidation() {
    requireInvalidRequest(R"({"user_id":"backend-user-01","query":"E1027",})", "valid JSON");
    requireInvalidRequest(R"({"user_id":"backend-user-01","top_k":5})", "query");
    requireInvalidRequest(R"({"query":"E1027","top_k":5})", "user_id");
    requireInvalidRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":0})", "top_k");
    requireInvalidRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":-1})", "top_k");
    requireInvalidRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":51})", "top_k");
    requireInvalidRequest(R"({"user_id":"backend-user-01","query":"E1027","project_ids":[1]})", "project_ids");

    const auto request =
        searchRequestFromJson(
            R"({"user_id":"backend-user-01","query":"E1027","top_k":5,"project_ids":["payment"],"document_types":["runbook"],"unknown":true})");
    require(request.user_id == "backend-user-01", "mapper should preserve user_id");
    require(request.query == "E1027", "mapper should preserve query");
    require(request.top_k == 5, "mapper should preserve valid top_k");
    require(request.project_ids.size() == 1 && request.project_ids.front() == "payment", "mapper should preserve project filters");
    require(request.document_types.size() == 1 && request.document_types.front() == "runbook",
            "mapper should preserve document type filters");
}

void testHttpSearchBoundaries() {
    Fixture fixture;
    auto gateway = fixture.gateway();
    HttpServer server(gateway);

    const auto malformed = server.handleRequest(postSearchRequest(R"({"user_id":"backend-user-01","query":"E1027",})"));
    requireContains(malformed, "HTTP/1.1 400 Bad Request", "malformed JSON should return HTTP 400");
    requireContains(responseBody(malformed), "valid JSON", "malformed JSON response should be clear");

    const auto missing_query = server.handleRequest(postSearchRequest(R"({"user_id":"backend-user-01","top_k":5})"));
    requireContains(missing_query, "HTTP/1.1 400 Bad Request", "missing query should return HTTP 400");
    requireContains(responseBody(missing_query), "query", "missing query response should name field");

    const auto missing_user = server.handleRequest(postSearchRequest(R"({"query":"E1027","top_k":5})"));
    requireContains(missing_user, "HTTP/1.1 400 Bad Request", "missing user_id should return HTTP 400");
    requireContains(responseBody(missing_user), "user_id", "missing user_id response should name field");

    const auto zero_top_k =
        server.handleRequest(postSearchRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":0})"));
    requireContains(zero_top_k, "HTTP/1.1 400 Bad Request", "top_k=0 should return HTTP 400");
    requireContains(responseBody(zero_top_k), "top_k", "top_k=0 response should name field");

    const auto unknown_field =
        server.handleRequest(postSearchRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":5,"ignored":true})"));
    requireContains(unknown_field, "HTTP/1.1 200 OK", "unknown fields should be ignored");
    requireContains(responseBody(unknown_field), R"("ok":true)", "valid request with unknown field should search");

    const auto unknown_user =
        server.handleRequest(postSearchRequest(R"({"user_id":"unknown-user","query":"E1027","top_k":5})"));
    requireContains(unknown_user, "HTTP/1.1 403 Forbidden", "unknown user should return HTTP 403");
    requireContains(responseBody(unknown_user), R"("error":"request denied")", "unknown user error should be generic");
    requireContains(responseBody(unknown_user), R"("hits":[])", "unknown user should not receive hits");
    requireNotContains(responseBody(unknown_user), "access policy resolver", "HTTP error must not leak resolver internals");
}

void testHttpAuthErrorsAreGeneric() {
    Fixture fixture;
    auto gateway = fixture.gateway();
    SupabaseAuthSettings settings;
    settings.jwt_secret = "test-secret";
    settings.require_auth = true;
    HttpServer server(gateway, SupabaseAuthManager(settings, SupabaseAuthBindings()));

    const auto missing_auth =
        server.handleRequest(postSearchRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":5})"));
    requireContains(missing_auth, "HTTP/1.1 401 Unauthorized", "missing Bearer token should return HTTP 401");
    requireContains(responseBody(missing_auth), "missing authorization", "missing auth response should be generic");

    const auto invalid_auth = server.handleRequest(
        postSearchRequest(R"({"user_id":"backend-user-01","query":"E1027","top_k":5})", "Bearer invalid-token"));
    requireContains(invalid_auth, "HTTP/1.1 401 Unauthorized", "invalid Bearer token should return HTTP 401");
    requireContains(responseBody(invalid_auth), "unauthorized", "invalid auth response should be generic");
    requireNotContains(responseBody(invalid_auth), "jwt", "invalid auth response should not leak verifier details");
}

void testHttpHealthAuthBoundaries() {
    Fixture fixture;
    auto gateway = fixture.gateway();

    HttpServer local_server(gateway);
    const auto local_health = local_server.handleRequest(getRequest("/health"));
    requireContains(local_health, "HTTP/1.1 200 OK", "local health should be public");
    requireContains(responseBody(local_health), "backend", "local health should include backend details");

    SupabaseAuthBindings bindings = SupabaseAuthBindings::fromObjectMap(R"({"auth-user-1":"backend-user-01"})");
    SupabaseAuthSettings settings;
    settings.jwt_secret = "test-secret";
    settings.expected_audience = "authenticated";
    settings.expected_issuer = "";
    settings.require_auth = true;
    HttpServer protected_server(gateway, SupabaseAuthManager(settings, bindings));

    const auto public_health = protected_server.handleRequest(getRequest("/health"));
    requireContains(public_health, "HTTP/1.1 200 OK", "protected public health should stay available");
    requireContains(responseBody(public_health), R"("status":"ok")", "protected public health should report status");
    requireNotContains(responseBody(public_health), "backend", "protected public health should hide backend details");
    requireNotContains(responseBody(public_health), "chunks", "protected public health should hide corpus size");

    const auto invalid_auth = protected_server.handleRequest(getRequest("/health", "Bearer invalid-token"));
    requireContains(invalid_auth, "HTTP/1.1 401 Unauthorized", "protected health should reject invalid auth");

    const auto valid_auth = protected_server.handleRequest(getRequest("/health", "Bearer " + validSupabaseHsToken()));
    requireContains(valid_auth, "HTTP/1.1 200 OK", "protected health should allow valid auth");
    requireContains(responseBody(valid_auth), "backend", "authenticated health should include backend details");
}

void testHttpMetricsAuthBoundaries() {
    Fixture fixture;
    auto gateway = fixture.gateway();

    HttpServer local_server(gateway);
    const auto local_metrics = local_server.handleRequest(getRequest("/metrics"));
    requireContains(local_metrics, "HTTP/1.1 200 OK", "local metrics should remain available without auth");

    SupabaseAuthBindings bindings = SupabaseAuthBindings::fromObjectMap(R"({"auth-user-1":"backend-user-01"})");
    SupabaseAuthSettings settings;
    settings.jwt_secret = "test-secret";
    settings.expected_audience = "authenticated";
    settings.expected_issuer = "";
    settings.require_auth = true;
    HttpServer protected_server(gateway, SupabaseAuthManager(settings, bindings));

    const auto missing_auth = protected_server.handleRequest(getRequest("/metrics"));
    requireContains(missing_auth, "HTTP/1.1 401 Unauthorized", "protected metrics should require auth");
    requireContains(responseBody(missing_auth), "missing authorization", "protected metrics missing auth should be generic");

    const auto invalid_auth = protected_server.handleRequest(getRequest("/metrics", "Bearer invalid-token"));
    requireContains(invalid_auth, "HTTP/1.1 401 Unauthorized", "protected metrics should reject invalid auth");
    requireContains(responseBody(invalid_auth), "unauthorized", "protected metrics invalid auth should be generic");

    const auto valid_auth = protected_server.handleRequest(getRequest("/metrics", "Bearer " + validSupabaseHsToken()));
    requireContains(valid_auth, "HTTP/1.1 200 OK", "protected metrics should allow valid auth");
    requireContains(responseBody(valid_auth), "total_queries", "protected metrics should return metrics body");
}

void testHttpDebugTraceAuthBoundaries() {
    Fixture fixture;
    auto gateway = fixture.gateway();
    const auto backend_response = gateway.search({"backend-user-01", "E1027", 5, {}, {}, true, true});
    require(backend_response.ok, "debug test search should succeed");

    HttpServer local_server(gateway);
    const auto local_debug = local_server.handleRequest(getRequest("/v1/debug/query/" + backend_response.query_id));
    requireContains(local_debug, "HTTP/1.1 200 OK", "local debug trace should remain available without auth");

    SupabaseAuthBindings bindings = SupabaseAuthBindings::fromObjectMap(R"({"auth-user-1":"backend-user-01"})");
    SupabaseAuthSettings settings;
    settings.jwt_secret = "test-secret";
    settings.expected_audience = "authenticated";
    settings.expected_issuer = "";
    settings.require_auth = true;
    HttpServer protected_server(gateway, SupabaseAuthManager(settings, bindings));

    const auto missing_auth = protected_server.handleRequest(getRequest("/v1/debug/query/" + backend_response.query_id));
    requireContains(missing_auth, "HTTP/1.1 401 Unauthorized", "protected debug trace should require auth");
    requireContains(responseBody(missing_auth), "missing authorization", "protected debug missing auth should be generic");

    const auto invalid_auth =
        protected_server.handleRequest(getRequest("/v1/debug/query/" + backend_response.query_id, "Bearer invalid-token"));
    requireContains(invalid_auth, "HTTP/1.1 401 Unauthorized", "protected debug trace should reject invalid auth");
    requireContains(responseBody(invalid_auth), "unauthorized", "protected debug invalid auth should be generic");

    const auto valid_auth = protected_server.handleRequest(
        getRequest("/v1/debug/query/" + backend_response.query_id, "Bearer " + validSupabaseHsToken()));
    requireContains(valid_auth, "HTTP/1.1 200 OK", "protected debug trace should allow owning ACL user");
    requireContains(responseBody(valid_auth), R"("user_id":"backend-user-01")", "debug trace should belong to ACL user");

    const auto finance_response = gateway.search({"finance-user-01", "confidential financial report", 5, {}, {}, true, true});
    require(finance_response.ok, "finance debug test search should succeed");
    const auto cross_user = protected_server.handleRequest(
        getRequest("/v1/debug/query/" + finance_response.query_id, "Bearer " + validSupabaseHsToken()));
    requireContains(cross_user, "HTTP/1.1 404 Not Found", "debug trace should hide traces for other ACL users");
    requireNotContains(responseBody(cross_user), "finance-user-01", "cross-user debug denial should not leak trace owner");
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

    auto filtered_finance_leak = gateway.search(
        {"backend-user-01", "confidential financial report revenue margin", 5, {"finance-core"}, {"financial_report"}, true, true});
    require(filtered_finance_leak.ok, "request filters should not make authorized user fail");
    require(!containsDocument(filtered_finance_leak, "finance-report-q2"),
            "project/document_type filters must not bypass ACL");

    auto filtered_sre_leak =
        gateway.search({"backend-user-01", "payment SRE on-call", 5, {"payment"}, {"runbook"}, true, true});
    require(filtered_sre_leak.ok, "request filters should keep search successful");
    require(!containsDocument(filtered_sre_leak, "oncall-payment"), "document_type filter must not bypass group ACL");

    auto finance_response = gateway.search({"finance-user-01", "confidential financial report revenue margin", 5, {}, {}, true, true});
    require(containsDocument(finance_response, "finance-report-q2"), "finance user should see finance report");

    auto admin_response = gateway.search({"admin-user", "confidential financial report revenue margin", 5, {}, {}, true, true});
    require(containsDocument(admin_response, "finance-report-q2"), "admin should see finance report");

    auto unknown = gateway.search({"unknown-user", "E1027", 5, {}, {}, true, true});
    require(!unknown.ok, "unknown user should fail closed");
    require(unknown.hits.empty(), "unknown user must not receive hits");
}

void testGatewayBoundaryValidation() {
    Fixture fixture;
    auto gateway = fixture.gateway();

    auto zero_top_k = gateway.search({"backend-user-01", "E1027", 0, {}, {}, true, true});
    require(!zero_top_k.ok, "gateway should reject top_k=0");
    require(zero_top_k.hits.empty(), "top_k=0 should not return hits");
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
    const std::string token = validSupabaseHsToken();

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

void testSupabaseEs256JwksBinding() {
    const std::string token =
        "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6InRlc3QtZXMyNTYta2V5In0."
        "eyJzdWIiOiJhdXRoLXVzZXItZXMyNTYiLCJhdWQiOiJhdXRoZW50aWNhdGVkIiwiaXNzIjoiaHR0cHM6Ly9leGFtcGxlLnN1cGFiYXNlLmNvL2F1dGgvdjEiLCJlbWFpbCI6ImVzMjU2QGV4YW1wbGUuY29tIiwiZXhwIjo0MTAyNDQ0ODAwfQ."
        "LL6fMF3YWofWWJAfrW3ip-2MkpHWng3Z2Dhxo_9tU-HRjHpN72PC7NjnS3bdX42kPuKYPSgmXeQ-UIXvl2EAjA";
    const std::string jwks_json =
        R"({"keys":[{"kty":"EC","x":"MHT4raYf-RIEDUNfe8-mZ83LnyJ6LvKGLo4-t4_zWVs","y":"vIajt8erh4hf1DofGoFC0eUEHY9qDS4hqL_snpPiqeI","crv":"P-256","kid":"test-es256-key","alg":"ES256","use":"sig"}]})";

    SupabaseAuthBindings bindings = SupabaseAuthBindings::fromObjectMap(R"({"auth-user-es256":"sre-user-01"})");
    SupabaseAuthSettings settings;
    settings.jwks_json = jwks_json;
    settings.expected_audience = "authenticated";
    settings.expected_issuer = "https://example.supabase.co/auth/v1";
    SupabaseAuthManager auth(settings, bindings);
    const auto result = auth.resolveBearerToken("Bearer " + token);
    require(result.ok, "valid ES256 supabase jwt should resolve");
    require(result.auth_user_id == "auth-user-es256", "ES256 jwt subject should be preserved");
    require(result.email == "es256@example.com", "ES256 jwt email should be preserved");
    require(result.acl_user_id == "sre-user-01", "ES256 binding should map auth user to ACL user");
}

}  // namespace

int main() {
    testParseBoundedSize();
    testRequestMapperValidation();
    testHttpSearchBoundaries();
    testHttpAuthErrorsAreGeneric();
    testHttpHealthAuthBoundaries();
    testHttpMetricsAuthBoundaries();
    testHttpDebugTraceAuthBoundaries();
    testAclSecurity();
    testGatewayBoundaryValidation();
    testRrfAndDedup();
    testPlannerBranches();
    testIncrementalIndexer();
    testMetricsAndDebugTrace();
    testSupabaseJwtBinding();
    testSupabaseEs256JwksBinding();
    std::cout << "core tests passed\n";
    return 0;
}
