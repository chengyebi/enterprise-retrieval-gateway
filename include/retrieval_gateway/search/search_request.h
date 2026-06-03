#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace erg {

struct SearchRequest {
    std::string user_id;
    std::string query;
    std::size_t top_k{10};
    std::vector<std::string> project_ids;
    std::vector<std::string> document_types;
    bool enable_vector_search{true};
    bool enable_keyword_search{true};
};

}  // namespace erg

