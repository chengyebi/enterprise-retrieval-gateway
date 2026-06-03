#include "retrieval_gateway/common/text.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace erg {

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;

    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    std::unordered_set<std::string> seen;
    std::vector<std::string> unique;
    for (const auto& token : tokens) {
        if (seen.insert(token).second) {
            unique.push_back(token);
        }
    }
    return unique;
}

std::vector<std::string> byteNgrams(const std::string& text, std::size_t n) {
    std::vector<std::string> grams;
    const std::string lower = toLowerAscii(text);
    if (lower.size() <= n) {
        if (!lower.empty()) {
            grams.push_back(lower);
        }
        return grams;
    }
    for (std::size_t i = 0; i + n <= lower.size(); ++i) {
        grams.push_back(lower.substr(i, n));
    }
    return grams;
}

std::string makeSnippet(const std::string& content, const std::string& query, std::size_t max_len) {
    if (content.size() <= max_len) {
        return content;
    }

    std::size_t pos = std::string::npos;
    for (const auto& token : tokenize(query)) {
        pos = toLowerAscii(content).find(token);
        if (pos != std::string::npos) {
            break;
        }
    }

    if (pos == std::string::npos) {
        return content.substr(0, max_len) + "...";
    }

    const std::size_t start = pos > max_len / 3 ? pos - max_len / 3 : 0;
    std::string snippet = content.substr(start, max_len);
    if (start > 0) {
        snippet = "..." + snippet;
    }
    if (start + max_len < content.size()) {
        snippet += "...";
    }
    return snippet;
}

std::string stableHashHex(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

bool containsAny(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    for (const auto& value : left) {
        if (containsValue(right, value)) {
            return true;
        }
    }
    return false;
}

bool containsValue(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace erg

