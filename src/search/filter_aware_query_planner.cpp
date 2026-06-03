#include "retrieval_gateway/search/filter_aware_query_planner.h"

#include <algorithm>

namespace erg {

FilterAwareQueryPlanner::FilterAwareQueryPlanner(std::size_t exact_search_threshold,
                                                 std::size_t initial_candidate_limit,
                                                 std::size_t max_candidate_limit)
    : exact_search_threshold_(exact_search_threshold),
      initial_candidate_limit_(initial_candidate_limit),
      max_candidate_limit_(max_candidate_limit) {}

QueryPlan FilterAwareQueryPlanner::buildPlan(const SearchRequest& request,
                                             const AccessContext& access,
                                             const InMemoryOpenSearchClient& backend,
                                             const ACLFilterBuilder& acl_builder) const {
    QueryPlan plan;
    plan.top_k = request.top_k;
    plan.query_text = request.query;
    plan.candidate_limit = std::max(initial_candidate_limit_, request.top_k);
    plan.max_candidate_limit = std::max(max_candidate_limit_, plan.candidate_limit);
    plan.acl_filter = acl_builder.buildSummary(access, request);
    plan.estimated_authorized_candidates = backend.estimateAuthorizedCandidates(request, access);

    if (request.enable_keyword_search && !request.enable_vector_search) {
        plan.mode = RetrievalMode::KeywordOnly;
        plan.candidate_limit = request.top_k;
        return plan;
    }
    if (!request.enable_keyword_search && request.enable_vector_search) {
        plan.mode = shouldUseExactSearch(plan.estimated_authorized_candidates)
                        ? RetrievalMode::FilteredExactVector
                        : RetrievalMode::VectorOnly;
        return plan;
    }
    if (shouldUseExactSearch(plan.estimated_authorized_candidates)) {
        plan.mode = RetrievalMode::FilteredExactVector;
        plan.candidate_limit = std::max(request.top_k, plan.estimated_authorized_candidates);
        return plan;
    }

    plan.mode = RetrievalMode::HybridWithIterativeExpansion;
    return plan;
}

bool FilterAwareQueryPlanner::shouldUseExactSearch(std::size_t estimated_candidates) const {
    return estimated_candidates <= exact_search_threshold_;
}

std::size_t FilterAwareQueryPlanner::nextCandidateLimit(std::size_t current_limit) const {
    if (current_limit >= max_candidate_limit_) {
        return current_limit;
    }
    return std::min(max_candidate_limit_, current_limit * 2);
}

}  // namespace erg
