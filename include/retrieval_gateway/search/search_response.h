#pragma once

#include <string>
#include <vector>

#include "retrieval_gateway/search/query_plan.h"
#include "retrieval_gateway/search/search_hit.h"

namespace erg {

struct SearchResponse {
    std::string query_id;
    RetrievalMode mode{RetrievalMode::Hybrid};
    std::vector<SearchHit> hits;
    bool ok{true};
    std::string error;
    bool fallback_triggered{false};
    std::size_t final_candidate_limit{0};
};

}  // namespace erg

