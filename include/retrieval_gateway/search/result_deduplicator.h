#pragma once

#include <cstddef>
#include <vector>

#include "retrieval_gateway/search/search_hit.h"

namespace erg {

class ResultDeduplicator {
public:
    explicit ResultDeduplicator(std::size_t max_chunks_per_document = 1);

    std::vector<SearchHit> deduplicate(const std::vector<SearchHit>& hits, std::size_t top_k) const;

private:
    std::size_t max_chunks_per_document_;
};

}  // namespace erg

