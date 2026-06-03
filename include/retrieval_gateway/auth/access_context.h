#pragma once

#include <string>
#include <vector>

namespace erg {

struct AccessContext {
    std::string user_id;
    std::string tenant_id;
    std::string department;
    std::vector<std::string> groups;
    std::vector<std::string> project_ids;
    bool is_admin{false};
};

}  // namespace erg

