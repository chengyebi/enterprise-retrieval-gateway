#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace erg {

std::string toLowerAscii(std::string value);
std::vector<std::string> tokenize(const std::string& text);
std::vector<std::string> byteNgrams(const std::string& text, std::size_t n);
std::string makeSnippet(const std::string& content, const std::string& query, std::size_t max_len = 180);
std::string stableHashHex(const std::string& value);
bool containsAny(const std::vector<std::string>& left, const std::vector<std::string>& right);
bool containsValue(const std::vector<std::string>& values, const std::string& value);

}  // namespace erg

