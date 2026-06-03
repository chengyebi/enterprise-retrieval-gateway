#include "retrieval_gateway/search/query_plan.h"

namespace erg {

std::string toString(RetrievalMode mode) {
    switch (mode) {
        case RetrievalMode::KeywordOnly:
            return "keyword_only";
        case RetrievalMode::VectorOnly:
            return "vector_only";
        case RetrievalMode::Hybrid:
            return "hybrid";
        case RetrievalMode::FilteredExactVector:
            return "filtered_exact_vector";
        case RetrievalMode::HybridWithIterativeExpansion:
            return "hybrid_iterative_expansion";
    }
    return "unknown";
}

}  // namespace erg

