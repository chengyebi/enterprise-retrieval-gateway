#pragma once

#include <vector>

#include "retrieval_gateway/auth/access_context.h"
#include "retrieval_gateway/indexing/document_chunk.h"

namespace erg {

std::vector<DocumentChunk> buildDemoChunks();
std::vector<AccessContext> buildDemoUsers();

}  // namespace erg

