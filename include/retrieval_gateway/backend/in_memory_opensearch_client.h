#pragma once

#include <map>
#include <string>
#include <vector>

#include "retrieval_gateway/auth/access_context.h"
#include "retrieval_gateway/auth/acl_filter_builder.h"
#include "retrieval_gateway/indexing/document_chunk.h"
#include "retrieval_gateway/search/search_hit.h"
#include "retrieval_gateway/search/search_request.h"

namespace erg {

struct BulkResult {
    bool ok{true};
    std::size_t indexed{0};
    std::vector<std::string> errors;
};

class InMemoryOpenSearchClient {
public:
    explicit InMemoryOpenSearchClient(ACLFilterBuilder acl_builder = ACLFilterBuilder{});

    BulkResult bulkUpsert(const std::vector<DocumentChunk>& chunks);
    bool deleteDocument(const std::string& document_id);
    bool updateAcl(const std::string& document_id,
                   const std::string& department,
                   const std::string& project_id,
                   const std::vector<std::string>& allowed_groups);

    std::vector<SearchHit> keywordSearch(const SearchRequest& request,
                                         const AccessContext& access,
                                         std::size_t limit) const;
    std::vector<SearchHit> vectorSearch(const SearchRequest& request,
                                        const AccessContext& access,
                                        const std::vector<double>& query_embedding,
                                        std::size_t limit,
                                        bool exact) const;

    std::size_t estimateAuthorizedCandidates(const SearchRequest& request, const AccessContext& access) const;
    std::vector<DocumentChunk> authorizedCandidates(const SearchRequest& request, const AccessContext& access) const;
    const DocumentChunk* findChunk(const std::string& chunk_id) const;
    std::size_t chunkCount() const;
    std::size_t documentCount() const;

private:
    SearchHit toHit(const DocumentChunk& chunk, double lexical_score, double semantic_score, const std::string& source) const;
    double keywordScore(const SearchRequest& request, const DocumentChunk& chunk) const;

    std::map<std::string, DocumentChunk> chunks_;
    ACLFilterBuilder acl_builder_;
};

}  // namespace erg

