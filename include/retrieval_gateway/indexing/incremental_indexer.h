#pragma once

#include <vector>

#include "retrieval_gateway/backend/in_memory_opensearch_client.h"
#include "retrieval_gateway/indexing/document_change.h"
#include "retrieval_gateway/indexing/embedding_provider.h"

namespace erg {

class IncrementalIndexer {
public:
    IncrementalIndexer(InMemoryOpenSearchClient& backend, EmbeddingProvider& embedding_provider);

    SyncResult sync(const DocumentChange& change);
    SyncSummary bulkSync(const std::vector<DocumentChange>& changes);

private:
    InMemoryOpenSearchClient& backend_;
    EmbeddingProvider& embedding_provider_;
};

}  // namespace erg

