# ACL Model

Every document chunk carries:

- `tenant_id`
- `department`
- `project_id`
- `allowed_groups`
- `document_type`
- `document_id`
- `chunk_id`

Every user access context carries:

- `user_id`
- `tenant_id`
- `department`
- `groups`
- `project_ids`
- `is_admin`

## Authorization Rule

For non-admin users, a chunk is searchable only when all checks pass:

```text
chunk.tenant_id == user.tenant_id
AND chunk.department == user.department
AND chunk.project_id in user.project_ids
AND intersection(chunk.allowed_groups, user.groups) is not empty
```

Admins can access all documents in the same tenant. Admin state is recorded in the ACL summary so debug traces make elevated access explicit.

## Security Properties

- ACL filters are constructed by the server.
- Unknown users fail closed.
- Request filters can narrow projects or document types, but cannot expand user access.
- Debug trace data records ACL summaries, not raw unauthorized content.
- Cache keys must include tenant, department, groups, projects, and admin state before a query cache is added.

## Tested Cases

- Backend employee can see payment/search/security engineering documents.
- Finance employee can see finance-core documents and cannot cross department by group alone.
- Admin can see finance reports.
- Unknown user is denied.
- ACL updates take effect immediately in the indexer path.

