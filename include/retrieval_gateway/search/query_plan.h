#pragma once

#include <cstddef>
#include <string>

namespace erg {

enum class RetrievalMode {
    KeywordOnly,
    VectorOnly,
    Hybrid,
    FilteredExactVector,
    HybridWithIterativeExpansion
};

struct QueryPlan {
    RetrievalMode mode{RetrievalMode::Hybrid};
    std::size_t candidate_limit{40};
    std::size_t max_candidate_limit{320};
    std::size_t top_k{10};
    std::size_t estimated_authorized_candidates{0};
    std::string acl_filter;
    std::string query_text;
};

std::string toString(RetrievalMode mode);

}  // namespace erg

