#pragma once

#include <string>
#include <vector>

#include "retrieval_gateway/backend/search_backend.h"

namespace erg {

struct OpenSearchOptions {
    std::string url{"http://localhost:9200"};
    std::string index{"enterprise_docs"};
};

class OpenSearchHttpClient : public SearchBackend {
public:
    explicit OpenSearchHttpClient(OpenSearchOptions options = OpenSearchOptions{});

    bool isAvailable() const;

    BulkResult bulkUpsert(const std::vector<DocumentChunk>& chunks) override;
    bool deleteDocument(const std::string& document_id) override;
    bool updateAcl(const std::string& document_id,
                   const std::string& department,
                   const std::string& project_id,
                   const std::vector<std::string>& allowed_groups) override;

    std::vector<SearchHit> keywordSearch(const SearchRequest& request,
                                         const AccessContext& access,
                                         std::size_t limit) const override;
    std::vector<SearchHit> vectorSearch(const SearchRequest& request,
                                        const AccessContext& access,
                                        const std::vector<double>& query_embedding,
                                        std::size_t limit,
                                        bool exact) const override;

    std::size_t estimateAuthorizedCandidates(const SearchRequest& request,
                                             const AccessContext& access) const override;
    std::size_t chunkCount() const override;
    std::size_t documentCount() const override;
    std::string backendName() const override;

private:
    struct HttpResponse {
        int status{0};
        std::string body;
    };

    HttpResponse request(const std::string& method,
                         const std::string& path,
                         const std::string& body = "",
                         const std::string& content_type = "application/json") const;

    std::string buildPath(const std::string& path) const;
    std::string buildAclFilterQuery(const AccessContext& access, const SearchRequest& request) const;
    std::string buildKeywordQuery(const SearchRequest& request, const AccessContext& access, std::size_t limit) const;
    std::string buildVectorQuery(const SearchRequest& request,
                                 const AccessContext& access,
                                 const std::vector<double>& query_embedding,
                                 std::size_t limit) const;

    std::vector<SearchHit> parseSearchHits(const std::string& body,
                                           const std::string& source,
                                           bool lexical_scores) const;
    std::size_t parseCount(const std::string& body) const;
    std::size_t parseAggregationValue(const std::string& body, const std::string& aggregation_name) const;

    OpenSearchOptions options_;
    std::string host_;
    std::string port_;
    std::string base_path_;
};

}  // namespace erg
