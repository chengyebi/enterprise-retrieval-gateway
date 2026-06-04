#pragma once

#include <vector>

#include "retrieval_gateway/backend/search_backend.h"
#include "retrieval_gateway/indexing/document_change.h"
#include "retrieval_gateway/indexing/embedding_provider.h"

namespace erg {

class IncrementalIndexer {
public:
    IncrementalIndexer(SearchBackend& backend, EmbeddingProvider& embedding_provider);

    SyncResult sync(const DocumentChange& change);
    SyncSummary bulkSync(const std::vector<DocumentChange>& changes);

private:
    SearchBackend& backend_;
    EmbeddingProvider& embedding_provider_;
};

}  // namespace erg
