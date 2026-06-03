#include "retrieval_gateway/search/result_deduplicator.h"

#include <map>

namespace erg {

ResultDeduplicator::ResultDeduplicator(std::size_t max_chunks_per_document)
    : max_chunks_per_document_(max_chunks_per_document) {}

std::vector<SearchHit> ResultDeduplicator::deduplicate(const std::vector<SearchHit>& hits, std::size_t top_k) const {
    std::vector<SearchHit> deduped;
    std::map<std::string, std::size_t> counts;
    for (const auto& hit : hits) {
        const auto current = counts[hit.document_id];
        if (current >= max_chunks_per_document_) {
            continue;
        }
        counts[hit.document_id] = current + 1;
        deduped.push_back(hit);
        if (deduped.size() >= top_k) {
            break;
        }
    }
    return deduped;
}

}  // namespace erg

