# Architecture

EnterpriseRetrievalGateway separates enterprise retrieval policy from the search engine kernel.

```text
user request
-> RetrievalGateway
-> AccessPolicyResolver
-> ACLFilterBuilder
-> FilterAwareQueryPlanner
-> keyword search + vector search
-> RRFFusion
-> ResultDeduplicator
-> authorized hits with citations
-> QueryMetricsRecorder
```

## Modules

`AccessPolicyResolver` resolves user identity into tenant, department, groups, projects, and admin state. Unknown users fail closed.

`ACLFilterBuilder` creates a server-side ACL summary and decides whether a document is searchable. Client requests cannot disable this check.

`InMemoryOpenSearchClient` is the local deterministic backend. It mirrors the shape of the OpenSearch operations used by the gateway: bulk upsert, document delete, ACL update, keyword search, vector search, and candidate counting.

`EmbeddingProvider` generates deterministic local hash vectors and caches by content hash/model version.

`FilterAwareQueryPlanner` estimates authorized candidates. Small candidate sets use exact filtered search. Larger sets use hybrid retrieval and can expand candidate limits when deduplicated results are insufficient.

`RRFFusion` fuses keyword and vector result lists by rank, avoiding score-scale normalization.

`QueryMetricsRecorder` stores per-query trace data and exposes aggregate metrics.

## Backend Strategy

The production target is OpenSearch, configured in `config/opensearch-index.json` and `config/opensearch-pipeline.json`. The local in-memory backend keeps the repository buildable and testable without Docker, while preserving the gateway behavior that matters most: ACL injection, query planning, fusion, deduplication, and metrics.

