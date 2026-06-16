#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "retrieval_gateway/metrics/search_trace.h"
#include "retrieval_gateway/search/search_hit.h"
#include "retrieval_gateway/search/search_response.h"

namespace erg {

std::string jsonEscape(const std::string& value);
std::string jsonArray(const std::vector<std::string>& values);
std::string hitToJson(const SearchHit& hit);
std::string responseToJson(const SearchResponse& response);
std::string traceToJson(const SearchTrace& trace);
std::string stringMapToJson(const std::map<std::string, std::string>& values);

bool isValidJsonObject(const std::string& body);
bool hasJsonKey(const std::string& body, const std::string& key);
std::string extractJsonRawValue(const std::string& body, const std::string& key);
std::string extractJsonString(const std::string& body, const std::string& key, const std::string& default_value = "");
std::size_t extractJsonSize(const std::string& body, const std::string& key, std::size_t default_value);
bool extractJsonBool(const std::string& body, const std::string& key, bool default_value);
std::vector<std::string> extractJsonStringArray(const std::string& body, const std::string& key);

}  // namespace erg
