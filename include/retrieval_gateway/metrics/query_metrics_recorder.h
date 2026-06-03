#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "retrieval_gateway/metrics/search_trace.h"

namespace erg {

struct MetricsSnapshot {
    std::size_t total_queries{0};
    std::size_t error_queries{0};
    std::size_t fallback_queries{0};
    double avg_latency_ms{0.0};
    int64_t p95_latency_ms{0};
    int64_t max_latency_ms{0};
};

class QueryMetricsRecorder {
public:
    void record(const SearchTrace& trace);
    MetricsSnapshot snapshot() const;
    const SearchTrace* findTrace(const std::string& query_id) const;
    std::vector<SearchTrace> traces() const;

private:
    std::vector<SearchTrace> traces_;
    std::map<std::string, SearchTrace> by_id_;
};

std::string metricsToJson(const MetricsSnapshot& snapshot);

}  // namespace erg

