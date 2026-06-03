#pragma once

#include <string>
#include <vector>

namespace erg {

struct SearchHit {
    std::string tenant_id;
    std::string document_id;
    std::string chunk_id;
    std::string title;
    std::string snippet;
    std::string department;
    std::string project_id;
    std::vector<std::string> allowed_groups;
    std::string document_type;
    double lexical_score{0.0};
    double semantic_score{0.0};
    double fusion_score{0.0};
    std::string source;
};

}  // namespace erg

