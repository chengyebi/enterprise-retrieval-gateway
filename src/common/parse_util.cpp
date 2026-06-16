#include "retrieval_gateway/common/parse_util.h"

#include <cctype>
#include <limits>
#include <stdexcept>

namespace erg {

namespace {

std::string rangeError(const std::string& field_name, std::size_t min_value, std::size_t max_value) {
    return field_name + " must be an integer between " + std::to_string(min_value) + " and " +
           std::to_string(max_value);
}

}  // namespace

std::size_t parseBoundedSize(const std::string& raw,
                             const std::string& field_name,
                             std::size_t min_value,
                             std::size_t max_value) {
    if (raw.empty()) {
        throw std::invalid_argument(rangeError(field_name, min_value, max_value));
    }
    for (const unsigned char c : raw) {
        if (!std::isdigit(c)) {
            throw std::invalid_argument(rangeError(field_name, min_value, max_value));
        }
    }

    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(raw);
    } catch (...) {
        throw std::invalid_argument(rangeError(field_name, min_value, max_value));
    }
    if (parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(rangeError(field_name, min_value, max_value));
    }

    const auto value = static_cast<std::size_t>(parsed);
    if (value < min_value || value > max_value) {
        throw std::invalid_argument(rangeError(field_name, min_value, max_value));
    }
    return value;
}

}  // namespace erg
