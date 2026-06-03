#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "retrieval_gateway/auth/access_context.h"

namespace erg {

class AccessDenied : public std::runtime_error {
public:
    explicit AccessDenied(const std::string& message) : std::runtime_error(message) {}
};

class AccessPolicyResolver {
public:
    AccessPolicyResolver() = default;
    explicit AccessPolicyResolver(std::vector<AccessContext> contexts);

    static AccessPolicyResolver demo();

    AccessContext resolve(const std::string& user_id) const;
    void upsert(const AccessContext& context);
    void setFailClosed(bool enabled);
    std::size_t userCount() const;

private:
    std::map<std::string, AccessContext> contexts_;
    bool fail_closed_{true};
};

}  // namespace erg
