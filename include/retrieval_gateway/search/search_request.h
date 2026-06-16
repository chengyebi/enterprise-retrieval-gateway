#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace erg {

inline constexpr std::size_t kDefaultTopK = 10;
inline constexpr std::size_t kMaxTopK = 50;

struct SearchRequest {
    std::string user_id;
    std::string query;
    std::size_t top_k{kDefaultTopK};
    std::vector<std::string> project_ids;
    std::vector<std::string> document_types;
    bool enable_vector_search{true};
    bool enable_keyword_search{true};
};

}  // namespace erg
