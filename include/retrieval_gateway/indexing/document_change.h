#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "retrieval_gateway/indexing/document_chunk.h"

namespace erg {

enum class ChangeType {
    Upsert,
    DeleteDocument,
    UpdateAcl
};

struct DocumentChange {
    ChangeType type{ChangeType::Upsert};
    DocumentChunk chunk;
    std::string document_id;
    std::string department;
    std::string project_id;
    std::vector<std::string> allowed_groups;
};

struct SyncResult {
    bool ok{true};
    std::string message;
    std::size_t changed_chunks{0};
};

struct SyncSummary {
    std::size_t total{0};
    std::size_t succeeded{0};
    std::size_t failed{0};
    std::vector<std::string> errors;
};

}  // namespace erg

