#pragma once

#include <cstddef>
#include <vector>

#include "retrieval_gateway/search/search_hit.h"

namespace erg {

class RRFFusion {
public:
    explicit RRFFusion(double rank_constant = 60.0);

    std::vector<SearchHit> fuse(const std::vector<SearchHit>& lexical_hits,
                                const std::vector<SearchHit>& semantic_hits,
                                std::size_t top_k) const;

private:
    double rank_constant_;
};

}  // namespace erg

