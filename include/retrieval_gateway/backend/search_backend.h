#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "retrieval_gateway/auth/access_context.h"
#include "retrieval_gateway/indexing/document_chunk.h"
#include "retrieval_gateway/search/search_hit.h"
#include "retrieval_gateway/search/search_request.h"

namespace erg {

struct BulkResult {
    bool ok{true};
    std::size_t indexed{0};
    std::vector<std::string> errors;
};

class SearchBackend {
public:
    virtual ~SearchBackend() = default;

    virtual BulkResult bulkUpsert(const std::vector<DocumentChunk>& chunks) = 0;
    virtual bool deleteDocument(const std::string& document_id) = 0;
    virtual bool updateAcl(const std::string& document_id,
                           const std::string& department,
                           const std::string& project_id,
                           const std::vector<std::string>& allowed_groups) = 0;

    virtual std::vector<SearchHit> keywordSearch(const SearchRequest& request,
                                                 const AccessContext& access,
                                                 std::size_t limit) const = 0;
    virtual std::vector<SearchHit> vectorSearch(const SearchRequest& request,
                                                const AccessContext& access,
                                                const std::vector<double>& query_embedding,
                                                std::size_t limit,
                                                bool exact) const = 0;

    virtual std::size_t estimateAuthorizedCandidates(const SearchRequest& request,
                                                     const AccessContext& access) const = 0;
    virtual std::size_t chunkCount() const = 0;
    virtual std::size_t documentCount() const = 0;
    virtual std::string backendName() const = 0;
};

}  // namespace erg
