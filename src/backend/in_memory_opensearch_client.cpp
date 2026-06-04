#include "retrieval_gateway/backend/in_memory_opensearch_client.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "retrieval_gateway/common/text.h"
#include "retrieval_gateway/indexing/embedding_provider.h"

namespace erg {

InMemoryOpenSearchClient::InMemoryOpenSearchClient(ACLFilterBuilder acl_builder)
    : acl_builder_(std::move(acl_builder)) {}

BulkResult InMemoryOpenSearchClient::bulkUpsert(const std::vector<DocumentChunk>& chunks) {
    BulkResult result;
    for (const auto& chunk : chunks) {
        if (chunk.document_id.empty() || chunk.chunk_id.empty()) {
            result.ok = false;
            result.errors.push_back("document_id and chunk_id are required");
            continue;
        }
        chunks_[chunk.chunk_id] = chunk;
        ++result.indexed;
    }
    return result;
}

bool InMemoryOpenSearchClient::deleteDocument(const std::string& document_id) {
    bool removed = false;
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        if (it->second.document_id == document_id) {
            it = chunks_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

bool InMemoryOpenSearchClient::updateAcl(const std::string& document_id,
                                         const std::string& department,
                                         const std::string& project_id,
                                         const std::vector<std::string>& allowed_groups) {
    bool updated = false;
    for (auto& item : chunks_) {
        if (item.second.document_id == document_id) {
            item.second.department = department;
            item.second.project_id = project_id;
            item.second.allowed_groups = allowed_groups;
            updated = true;
        }
    }
    return updated;
}

std::vector<SearchHit> InMemoryOpenSearchClient::keywordSearch(const SearchRequest& request,
                                                              const AccessContext& access,
                                                              std::size_t limit) const {
    std::vector<SearchHit> hits;
    for (const auto& item : chunks_) {
        const auto& chunk = item.second;
        if (!acl_builder_.isSearchable(access, request, chunk)) {
            continue;
        }
        const double score = keywordScore(request, chunk);
        if (score > 0.0) {
            hits.push_back(toHit(chunk, score, 0.0, "keyword"));
        }
    }
    std::sort(hits.begin(), hits.end(), [](const SearchHit& left, const SearchHit& right) {
        if (left.lexical_score == right.lexical_score) {
            return left.chunk_id < right.chunk_id;
        }
        return left.lexical_score > right.lexical_score;
    });
    if (hits.size() > limit) {
        hits.resize(limit);
    }
    return hits;
}

std::vector<SearchHit> InMemoryOpenSearchClient::vectorSearch(const SearchRequest& request,
                                                             const AccessContext& access,
                                                             const std::vector<double>& query_embedding,
                                                             std::size_t limit,
                                                             bool exact) const {
    std::vector<SearchHit> hits;
    const double threshold = exact ? 0.0 : 0.02;
    for (const auto& item : chunks_) {
        const auto& chunk = item.second;
        if (!acl_builder_.isSearchable(access, request, chunk)) {
            continue;
        }
        const double score = cosineSimilarity(query_embedding, chunk.embedding);
        if (score > threshold) {
            hits.push_back(toHit(chunk, 0.0, score, "vector"));
        }
    }
    std::sort(hits.begin(), hits.end(), [](const SearchHit& left, const SearchHit& right) {
        if (left.semantic_score == right.semantic_score) {
            return left.chunk_id < right.chunk_id;
        }
        return left.semantic_score > right.semantic_score;
    });
    if (hits.size() > limit) {
        hits.resize(limit);
    }
    return hits;
}

std::size_t InMemoryOpenSearchClient::estimateAuthorizedCandidates(const SearchRequest& request,
                                                                   const AccessContext& access) const {
    std::size_t count = 0;
    for (const auto& item : chunks_) {
        if (acl_builder_.isSearchable(access, request, item.second)) {
            ++count;
        }
    }
    return count;
}

std::vector<DocumentChunk> InMemoryOpenSearchClient::authorizedCandidates(const SearchRequest& request,
                                                                         const AccessContext& access) const {
    std::vector<DocumentChunk> values;
    for (const auto& item : chunks_) {
        if (acl_builder_.isSearchable(access, request, item.second)) {
            values.push_back(item.second);
        }
    }
    return values;
}

const DocumentChunk* InMemoryOpenSearchClient::findChunk(const std::string& chunk_id) const {
    const auto it = chunks_.find(chunk_id);
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::size_t InMemoryOpenSearchClient::chunkCount() const {
    return chunks_.size();
}

std::size_t InMemoryOpenSearchClient::documentCount() const {
    std::set<std::string> ids;
    for (const auto& item : chunks_) {
        ids.insert(item.second.document_id);
    }
    return ids.size();
}

std::string InMemoryOpenSearchClient::backendName() const {
    return "in_memory_opensearch";
}

SearchHit InMemoryOpenSearchClient::toHit(const DocumentChunk& chunk,
                                          double lexical_score,
                                          double semantic_score,
                                          const std::string& source) const {
    SearchHit hit;
    hit.tenant_id = chunk.tenant_id;
    hit.document_id = chunk.document_id;
    hit.chunk_id = chunk.chunk_id;
    hit.title = chunk.title;
    hit.snippet = makeSnippet(chunk.content, chunk.title);
    hit.department = chunk.department;
    hit.project_id = chunk.project_id;
    hit.allowed_groups = chunk.allowed_groups;
    hit.document_type = chunk.document_type;
    hit.lexical_score = lexical_score;
    hit.semantic_score = semantic_score;
    hit.source = source;
    return hit;
}

double InMemoryOpenSearchClient::keywordScore(const SearchRequest& request, const DocumentChunk& chunk) const {
    const auto query_tokens = tokenize(request.query);
    if (query_tokens.empty()) {
        return 0.0;
    }

    const std::string haystack = toLowerAscii(chunk.title + " " + chunk.content);
    double score = 0.0;
    for (const auto& token : query_tokens) {
        std::size_t occurrences = 0;
        std::size_t pos = haystack.find(token);
        while (pos != std::string::npos) {
            ++occurrences;
            pos = haystack.find(token, pos + token.size());
        }
        if (occurrences > 0) {
            score += 2.0 + std::log(1.0 + static_cast<double>(occurrences));
            if (chunk.title.find(token) != std::string::npos || toLowerAscii(chunk.title).find(token) != std::string::npos) {
                score += 1.5;
            }
        }
    }

    const double length_penalty = 1.0 + static_cast<double>(tokenize(chunk.content).size()) / 300.0;
    return score / length_penalty;
}

}  // namespace erg
