#include "retrieval_gateway/search/rrf_fusion.h"

#include <algorithm>
#include <map>

namespace erg {

RRFFusion::RRFFusion(double rank_constant) : rank_constant_(rank_constant) {}

std::vector<SearchHit> RRFFusion::fuse(const std::vector<SearchHit>& lexical_hits,
                                       const std::vector<SearchHit>& semantic_hits,
                                       std::size_t top_k) const {
    std::map<std::string, SearchHit> by_chunk;

    for (std::size_t i = 0; i < lexical_hits.size(); ++i) {
        SearchHit hit = lexical_hits[i];
        hit.source = "keyword";
        hit.fusion_score += 1.0 / (rank_constant_ + static_cast<double>(i + 1));
        by_chunk[hit.chunk_id] = hit;
    }

    for (std::size_t i = 0; i < semantic_hits.size(); ++i) {
        const auto it = by_chunk.find(semantic_hits[i].chunk_id);
        if (it == by_chunk.end()) {
            SearchHit hit = semantic_hits[i];
            hit.source = "vector";
            hit.fusion_score += 1.0 / (rank_constant_ + static_cast<double>(i + 1));
            by_chunk[hit.chunk_id] = hit;
        } else {
            it->second.semantic_score = semantic_hits[i].semantic_score;
            it->second.fusion_score += 1.0 / (rank_constant_ + static_cast<double>(i + 1));
            it->second.source = "hybrid";
        }
    }

    std::vector<SearchHit> fused;
    fused.reserve(by_chunk.size());
    for (auto& item : by_chunk) {
        fused.push_back(item.second);
    }
    std::sort(fused.begin(), fused.end(), [](const SearchHit& left, const SearchHit& right) {
        if (left.fusion_score == right.fusion_score) {
            return left.chunk_id < right.chunk_id;
        }
        return left.fusion_score > right.fusion_score;
    });
    if (fused.size() > top_k) {
        fused.resize(top_k);
    }
    return fused;
}

}  // namespace erg

