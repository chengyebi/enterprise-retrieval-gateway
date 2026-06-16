#include "retrieval_gateway/search/retrieval_gateway.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "retrieval_gateway/common/clock.h"

namespace erg {

namespace {

std::size_t clampTopK(std::size_t top_k) {
    return std::min<std::size_t>(top_k, kMaxTopK);
}

bool isBlank(const std::string& value) {
    for (const unsigned char c : value) {
        if (!std::isspace(c)) {
            return false;
        }
    }
    return true;
}

}  // namespace

RetrievalGateway::RetrievalGateway(AccessPolicyResolver resolver,
                                   ACLFilterBuilder acl_builder,
                                   SearchBackend& backend,
                                   EmbeddingProvider& embedding_provider,
                                   QueryMetricsRecorder& metrics)
    : resolver_(std::move(resolver)),
      acl_builder_(std::move(acl_builder)),
      backend_(backend),
      embedding_provider_(embedding_provider),
      metrics_(metrics),
      planner_(),
      fusion_(),
      deduplicator_(1) {}

SearchResponse RetrievalGateway::search(const SearchRequest& original_request) {
    Stopwatch total_watch;

    SearchResponse response;
    response.query_id = nextQueryId();

    SearchTrace trace;
    trace.query_id = response.query_id;
    trace.user_id = original_request.user_id;
    trace.requested_top_k = original_request.top_k;

    if (original_request.top_k == 0) {
        response.ok = false;
        response.error = "top_k must be greater than 0";
        trace.total_latency_ms = total_watch.elapsedMs();
        metrics_.record(trace);
        return response;
    }

    SearchRequest request = original_request;
    request.top_k = clampTopK(request.top_k);
    response.final_candidate_limit = request.top_k;

    // API 层会先校验，这里保留核心调用的兜底防线。
    if (isBlank(request.query)) {
        response.ok = false;
        response.error = "query must not be empty";
        trace.total_latency_ms = total_watch.elapsedMs();
        metrics_.record(trace);
        return response;
    }

    try {
        Stopwatch acl_watch;
        const AccessContext access = resolver_.resolve(request.user_id);
        trace.acl_resolve_latency_ms = acl_watch.elapsedMs();

        const QueryPlan plan = planner_.buildPlan(request, access, backend_, acl_builder_);
        response.mode = plan.mode;
        trace.mode = plan.mode;
        trace.acl_filter_summary = plan.acl_filter;
        trace.candidate_limit = plan.candidate_limit;
        trace.estimated_authorized_candidates = plan.estimated_authorized_candidates;

        Stopwatch backend_watch;
        const auto query_embedding = embedding_provider_.embed(request.query);
        std::vector<SearchHit> lexical_hits;
        std::vector<SearchHit> semantic_hits;
        std::vector<SearchHit> fused;
        std::size_t candidate_limit = plan.candidate_limit;
        bool fallback = false;

        while (true) {
            lexical_hits.clear();
            semantic_hits.clear();

            if (request.enable_keyword_search && plan.mode != RetrievalMode::VectorOnly) {
                lexical_hits = backend_.keywordSearch(request, access, candidate_limit);
            }
            if (request.enable_vector_search && plan.mode != RetrievalMode::KeywordOnly) {
                const bool exact = plan.mode == RetrievalMode::FilteredExactVector;
                semantic_hits = backend_.vectorSearch(request, access, query_embedding, candidate_limit, exact);
            }

            Stopwatch fusion_watch;
            if (request.enable_keyword_search && request.enable_vector_search) {
                fused = fusion_.fuse(lexical_hits, semantic_hits, candidate_limit);
            } else if (request.enable_keyword_search) {
                fused = lexical_hits;
                for (auto& hit : fused) {
                    hit.fusion_score = hit.lexical_score;
                }
            } else {
                fused = semantic_hits;
                for (auto& hit : fused) {
                    hit.fusion_score = hit.semantic_score;
                }
            }

            response.hits = deduplicator_.deduplicate(fused, request.top_k);
            trace.fusion_latency_ms += fusion_watch.elapsedMs();

            const bool can_expand = plan.mode == RetrievalMode::HybridWithIterativeExpansion &&
                                    response.hits.size() < request.top_k &&
                                    candidate_limit < plan.max_candidate_limit;
            if (!can_expand) {
                break;
            }

            const std::size_t next_limit = planner_.nextCandidateLimit(candidate_limit);
            if (next_limit == candidate_limit) {
                break;
            }
            candidate_limit = next_limit;
            fallback = true;
        }

        trace.backend_latency_ms = backend_watch.elapsedMs();
        trace.candidate_limit = candidate_limit;
        trace.returned_hits = response.hits.size();
        trace.fallback_triggered = fallback;
        trace.total_latency_ms = total_watch.elapsedMs();

        response.fallback_triggered = fallback;
        response.final_candidate_limit = candidate_limit;
        metrics_.record(trace);
        return response;
    } catch (const AccessDenied& error) {
        response.ok = false;
        response.error = error.what();
        trace.total_latency_ms = total_watch.elapsedMs();
        metrics_.record(trace);
        return response;
    } catch (const std::exception& error) {
        response.ok = false;
        response.error = std::string("search failed: ") + error.what();
        trace.total_latency_ms = total_watch.elapsedMs();
        metrics_.record(trace);
        return response;
    }
}

std::string RetrievalGateway::health() const {
    std::ostringstream out;
    out << "{\"status\":\"ok\",\"backend\":\"" << backend_.backendName() << "\",\"chunks\":" << backend_.chunkCount()
        << ",\"documents\":" << backend_.documentCount() << "}";
    return out.str();
}

MetricsSnapshot RetrievalGateway::metrics() const {
    return metrics_.snapshot();
}

const SearchTrace* RetrievalGateway::debugTrace(const std::string& query_id) const {
    return metrics_.findTrace(query_id);
}

std::string RetrievalGateway::nextQueryId() {
    std::ostringstream out;
    out << "q-" << ++query_sequence_;
    return out.str();
}

}  // namespace erg
