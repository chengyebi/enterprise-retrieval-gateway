#include "retrieval_gateway/indexing/incremental_indexer.h"

#include <sstream>

#include "retrieval_gateway/common/text.h"

namespace erg {

IncrementalIndexer::IncrementalIndexer(SearchBackend& backend, EmbeddingProvider& embedding_provider)
    : backend_(backend), embedding_provider_(embedding_provider) {}

SyncResult IncrementalIndexer::sync(const DocumentChange& change) {
    if (change.type == ChangeType::DeleteDocument) {
        const bool removed = backend_.deleteDocument(change.document_id);
        return SyncResult{removed, removed ? "deleted" : "document not found", removed ? 1u : 0u};
    }

    if (change.type == ChangeType::UpdateAcl) {
        const bool updated = backend_.updateAcl(change.document_id, change.department, change.project_id, change.allowed_groups);
        return SyncResult{updated, updated ? "acl updated" : "document not found", updated ? 1u : 0u};
    }

    DocumentChunk chunk = change.chunk;
    if (chunk.content_hash.empty()) {
        chunk.content_hash = stableHashHex(chunk.title + "\n" + chunk.content);
    }
    chunk.embedding_model_version = embedding_provider_.modelVersion();
    chunk.embedding = embedding_provider_.embed(chunk.title + "\n" + chunk.content);
    const auto result = backend_.bulkUpsert({chunk});
    if (result.ok) {
        return SyncResult{true, "upserted", result.indexed};
    }
    std::ostringstream message;
    message << "upsert failed";
    for (const auto& error : result.errors) {
        message << ": " << error;
    }
    return SyncResult{false, message.str(), result.indexed};
}

SyncSummary IncrementalIndexer::bulkSync(const std::vector<DocumentChange>& changes) {
    SyncSummary summary;
    summary.total = changes.size();
    for (const auto& change : changes) {
        const auto result = sync(change);
        if (result.ok) {
            ++summary.succeeded;
        } else {
            ++summary.failed;
            summary.errors.push_back(result.message);
        }
    }
    return summary;
}

}  // namespace erg
