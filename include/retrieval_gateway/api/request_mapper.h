#pragma once

#include <string>

#include "retrieval_gateway/search/search_request.h"
#include "retrieval_gateway/search/search_response.h"

namespace erg {

SearchRequest searchRequestFromJson(const std::string& body);
std::string searchResponseToJson(const SearchResponse& response);

}  // namespace erg

