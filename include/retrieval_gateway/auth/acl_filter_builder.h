#pragma once

#include <string>

#include "retrieval_gateway/auth/access_context.h"
#include "retrieval_gateway/indexing/document_chunk.h"
#include "retrieval_gateway/search/search_request.h"

namespace erg {

class ACLFilterBuilder {
public:
    std::string buildSummary(const AccessContext& access, const SearchRequest& request) const;
    bool isAuthorized(const AccessContext& access, const DocumentChunk& chunk) const;
    bool matchesRequestFilters(const SearchRequest& request, const DocumentChunk& chunk) const;
    bool isSearchable(const AccessContext& access, const SearchRequest& request, const DocumentChunk& chunk) const;
};

}  // namespace erg

