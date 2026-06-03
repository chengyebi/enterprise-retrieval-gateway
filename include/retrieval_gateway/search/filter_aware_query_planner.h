#pragma once

#include <cstddef>

#include "retrieval_gateway/auth/access_context.h"
#include "retrieval_gateway/auth/acl_filter_builder.h"
#include "retrieval_gateway/backend/in_memory_opensearch_client.h"
#include "retrieval_gateway/search/query_plan.h"
#include "retrieval_gateway/search/search_request.h"

namespace erg {

class FilterAwareQueryPlanner {
public:
    FilterAwareQueryPlanner(std::size_t exact_search_threshold = 5,
                            std::size_t initial_candidate_limit = 5,
                            std::size_t max_candidate_limit = 320);

    QueryPlan buildPlan(const SearchRequest& request,
                        const AccessContext& access,
                        const InMemoryOpenSearchClient& backend,
                        const ACLFilterBuilder& acl_builder) const;

    bool shouldUseExactSearch(std::size_t estimated_candidates) const;
    std::size_t nextCandidateLimit(std::size_t current_limit) const;

private:
    std::size_t exact_search_threshold_;
    std::size_t initial_candidate_limit_;
    std::size_t max_candidate_limit_;
};

}  // namespace erg
