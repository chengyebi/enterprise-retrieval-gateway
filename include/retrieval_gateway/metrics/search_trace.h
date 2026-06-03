#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "retrieval_gateway/search/query_plan.h"

namespace erg {

struct SearchTrace {
    std::string query_id;
    std::string user_id;
    std::string acl_filter_summary;
    RetrievalMode mode{RetrievalMode::Hybrid};
    std::size_t requested_top_k{0};
    std::size_t returned_hits{0};
    std::size_t candidate_limit{0};
    std::size_t estimated_authorized_candidates{0};
    bool fallback_triggered{false};
    int64_t acl_resolve_latency_ms{0};
    int64_t backend_latency_ms{0};
    int64_t fusion_latency_ms{0};
    int64_t total_latency_ms{0};
};

}  // namespace erg

