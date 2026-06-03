#include "retrieval_gateway/api/request_mapper.h"

#include "retrieval_gateway/common/json_util.h"

namespace erg {

SearchRequest searchRequestFromJson(const std::string& body) {
    SearchRequest request;
    request.user_id = extractJsonString(body, "user_id");
    request.query = extractJsonString(body, "query");
    request.top_k = extractJsonSize(body, "top_k", 10);
    request.project_ids = extractJsonStringArray(body, "project_ids");
    request.document_types = extractJsonStringArray(body, "document_types");
    request.enable_vector_search = extractJsonBool(body, "enable_vector_search", true);
    request.enable_keyword_search = extractJsonBool(body, "enable_keyword_search", true);
    return request;
}

std::string searchResponseToJson(const SearchResponse& response) {
    return responseToJson(response);
}

}  // namespace erg

