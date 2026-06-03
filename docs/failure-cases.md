# Failure Cases

## Unknown User

The resolver throws `AccessDenied`, and the gateway returns an error without searching.

## Empty Query

The gateway rejects empty queries before resolving the backend path.

## ACL Resolver Failure

The default posture is deny. A production resolver should use the same fail-closed behavior for timeouts and partial responses.

## Strict ACL Filter

When authorized candidates are small, the planner uses exact filtered search. When candidates are larger but deduplication reduces returned results, the planner expands candidate limits until it either reaches `top_k` or the maximum candidate limit.

## Backend Unavailable

The local backend does not simulate network failure. In the OpenSearch path, bootstrap and ingest scripts fail fast on HTTP errors. A production gateway should return a clear backend error or a deliberately scoped degraded result.

## Duplicate Chunks

`ResultDeduplicator` limits each document to one chunk by default, preventing adjacent chunks from dominating top results.

## Unauthorized Hybrid Results

The evaluation includes an intentionally unsafe `hybrid` strategy without ACL. It produces unauthorized hits, demonstrating why ACL injection must happen in the gateway and not be left to clients.

