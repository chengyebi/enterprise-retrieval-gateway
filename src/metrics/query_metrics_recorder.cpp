#include "retrieval_gateway/metrics/query_metrics_recorder.h"

#include <algorithm>
#include <sstream>

namespace erg {

void QueryMetricsRecorder::record(const SearchTrace& trace) {
    traces_.push_back(trace);
    by_id_[trace.query_id] = trace;
}

MetricsSnapshot QueryMetricsRecorder::snapshot() const {
    MetricsSnapshot snapshot;
    snapshot.total_queries = traces_.size();
    if (traces_.empty()) {
        return snapshot;
    }

    std::vector<int64_t> latencies;
    latencies.reserve(traces_.size());
    int64_t total = 0;
    for (const auto& trace : traces_) {
        total += trace.total_latency_ms;
        latencies.push_back(trace.total_latency_ms);
        if (trace.fallback_triggered) {
            ++snapshot.fallback_queries;
        }
    }
    std::sort(latencies.begin(), latencies.end());
    snapshot.avg_latency_ms = static_cast<double>(total) / static_cast<double>(latencies.size());
    const std::size_t p95_index = static_cast<std::size_t>(0.95 * static_cast<double>(latencies.size() - 1));
    snapshot.p95_latency_ms = latencies[p95_index];
    snapshot.max_latency_ms = latencies.back();
    return snapshot;
}

const SearchTrace* QueryMetricsRecorder::findTrace(const std::string& query_id) const {
    const auto it = by_id_.find(query_id);
    if (it == by_id_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<SearchTrace> QueryMetricsRecorder::traces() const {
    return traces_;
}

std::string metricsToJson(const MetricsSnapshot& snapshot) {
    std::ostringstream out;
    out << "{"
        << "\"total_queries\":" << snapshot.total_queries << ","
        << "\"error_queries\":" << snapshot.error_queries << ","
        << "\"fallback_queries\":" << snapshot.fallback_queries << ","
        << "\"avg_latency_ms\":" << snapshot.avg_latency_ms << ","
        << "\"p95_latency_ms\":" << snapshot.p95_latency_ms << ","
        << "\"max_latency_ms\":" << snapshot.max_latency_ms
        << "}";
    return out.str();
}

}  // namespace erg

