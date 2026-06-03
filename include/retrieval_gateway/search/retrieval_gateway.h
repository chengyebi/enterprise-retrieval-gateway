#pragma once

#include <string>

#include "retrieval_gateway/auth/access_policy_resolver.h"
#include "retrieval_gateway/auth/acl_filter_builder.h"
#include "retrieval_gateway/backend/in_memory_opensearch_client.h"
#include "retrieval_gateway/indexing/embedding_provider.h"
#include "retrieval_gateway/metrics/query_metrics_recorder.h"
#include "retrieval_gateway/search/filter_aware_query_planner.h"
#include "retrieval_gateway/search/result_deduplicator.h"
#include "retrieval_gateway/search/rrf_fusion.h"
#include "retrieval_gateway/search/search_request.h"
#include "retrieval_gateway/search/search_response.h"

namespace erg {

class RetrievalGateway {
public:
    RetrievalGateway(AccessPolicyResolver resolver,
                     ACLFilterBuilder acl_builder,
                     InMemoryOpenSearchClient& backend,
                     EmbeddingProvider& embedding_provider,
                     QueryMetricsRecorder& metrics);

    SearchResponse search(const SearchRequest& request);
    std::string health() const;
    MetricsSnapshot metrics() const;
    const SearchTrace* debugTrace(const std::string& query_id) const;

private:
    std::string nextQueryId();

    AccessPolicyResolver resolver_;
    ACLFilterBuilder acl_builder_;
    InMemoryOpenSearchClient& backend_;
    EmbeddingProvider& embedding_provider_;
    QueryMetricsRecorder& metrics_;
    FilterAwareQueryPlanner planner_;
    RRFFusion fusion_;
    ResultDeduplicator deduplicator_;
    std::size_t query_sequence_{0};
};

}  // namespace erg

