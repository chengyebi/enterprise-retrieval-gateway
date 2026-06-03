#include "retrieval_gateway/auth/acl_filter_builder.h"

#include <sstream>

#include "retrieval_gateway/common/json_util.h"
#include "retrieval_gateway/common/text.h"

namespace erg {

std::string ACLFilterBuilder::buildSummary(const AccessContext& access, const SearchRequest& request) const {
    std::ostringstream out;
    out << "tenant=" << access.tenant_id
        << ";department=" << access.department
        << ";groups=" << jsonArray(access.groups)
        << ";projects=" << jsonArray(access.project_ids)
        << ";admin=" << (access.is_admin ? "true" : "false");
    if (!request.project_ids.empty()) {
        out << ";request_projects=" << jsonArray(request.project_ids);
    }
    if (!request.document_types.empty()) {
        out << ";request_types=" << jsonArray(request.document_types);
    }
    return out.str();
}

bool ACLFilterBuilder::isAuthorized(const AccessContext& access, const DocumentChunk& chunk) const {
    if (access.tenant_id.empty() || chunk.tenant_id != access.tenant_id) {
        return false;
    }

    if (access.is_admin) {
        return true;
    }

    if (chunk.department != access.department) {
        return false;
    }
    if (!containsValue(access.project_ids, chunk.project_id)) {
        return false;
    }
    if (!containsAny(access.groups, chunk.allowed_groups)) {
        return false;
    }
    return true;
}

bool ACLFilterBuilder::matchesRequestFilters(const SearchRequest& request, const DocumentChunk& chunk) const {
    if (!request.project_ids.empty() && !containsValue(request.project_ids, chunk.project_id)) {
        return false;
    }
    if (!request.document_types.empty() && !containsValue(request.document_types, chunk.document_type)) {
        return false;
    }
    return true;
}

bool ACLFilterBuilder::isSearchable(const AccessContext& access,
                                    const SearchRequest& request,
                                    const DocumentChunk& chunk) const {
    return isAuthorized(access, chunk) && matchesRequestFilters(request, chunk);
}

}  // namespace erg

