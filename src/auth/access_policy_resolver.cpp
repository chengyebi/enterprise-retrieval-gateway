#include "retrieval_gateway/auth/access_policy_resolver.h"

#include <utility>

#include "retrieval_gateway/common/demo_data.h"

namespace erg {

AccessPolicyResolver::AccessPolicyResolver(std::vector<AccessContext> contexts) {
    for (const auto& context : contexts) {
        upsert(context);
    }
}

AccessPolicyResolver AccessPolicyResolver::demo() {
    return AccessPolicyResolver(buildDemoUsers());
}

AccessContext AccessPolicyResolver::resolve(const std::string& user_id) const {
    const auto it = contexts_.find(user_id);
    if (it == contexts_.end()) {
        if (fail_closed_) {
            throw AccessDenied("access policy resolver has no context for user: " + user_id);
        }
        return AccessContext{user_id, "", "", {}, {}, false};
    }
    return it->second;
}

void AccessPolicyResolver::upsert(const AccessContext& context) {
    contexts_[context.user_id] = context;
}

void AccessPolicyResolver::setFailClosed(bool enabled) {
    fail_closed_ = enabled;
}

std::size_t AccessPolicyResolver::userCount() const {
    return contexts_.size();
}

}  // namespace erg

