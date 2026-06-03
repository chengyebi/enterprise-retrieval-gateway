# Retrieval Strategy

The gateway combines keyword and vector retrieval because enterprise queries mix exact identifiers with natural language.

## Keyword Search

Keyword search is strong for:

- error codes such as `E1027`
- internal names such as `payment_timeout`
- API versions such as `API v3`
- ticket ids and contract ids

The local backend implements BM25-style scoring with token occurrence counts and a simple length penalty. OpenSearch should provide the production BM25 implementation.

## Vector Search

Vector search is strong for paraphrases:

- "downstream payment interface no response"
- "dependency did not respond"
- "long latency in transaction gateway"

The local backend uses deterministic hash embeddings so evaluation is reproducible. Production can replace this with a real embedding provider as long as `embedding_model_version` and `content_hash` are preserved.

## RRF Fusion

RRF computes:

```text
score = sum(1 / (rank_constant + rank_i))
```

This avoids comparing raw BM25 scores with cosine similarity scores. A document appearing in both lists receives a natural boost.

## Filter-Aware Planner

ACL filters can cause vector search to return too few authorized results after filtering. The planner handles this with:

- candidate count estimation after ACL and request filters
- exact filtered search for small candidate sets
- hybrid search for larger candidate sets
- iterative candidate expansion when deduplicated results are below `top_k`
- maximum candidate limit to bound latency

The demo intentionally triggers expansion for strict payment queries where several chunks collapse to the same document after deduplication.

