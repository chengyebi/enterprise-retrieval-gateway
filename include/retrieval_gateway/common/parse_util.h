#pragma once

#include <cstddef>
#include <string>

namespace erg {

std::size_t parseBoundedSize(const std::string& raw,
                             const std::string& field_name,
                             std::size_t min_value,
                             std::size_t max_value);

}  // namespace erg
