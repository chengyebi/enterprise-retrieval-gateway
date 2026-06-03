#pragma once

#include <string>
#include <vector>

namespace erg {

struct DocumentChunk {
    std::string tenant_id{"tenant-acme"};
    std::string document_id;
    std::string chunk_id;
    std::string title;
    std::string content;
    std::string department;
    std::string project_id;
    std::vector<std::string> allowed_groups;
    std::string document_type;
    int document_version{1};
    std::string content_hash;
    std::string updated_at;
    std::string embedding_model_version{"local-hash-v1"};
    std::vector<double> embedding;
};

}  // namespace erg

