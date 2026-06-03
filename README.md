# EnterpriseRetrievalGateway

EnterpriseRetrievalGateway is a C++17 retrieval gateway for enterprise knowledge bases. It sits above OpenSearch-style retrieval and enforces document-level ACL filtering, hybrid keyword/vector search, RRF fusion, filter-aware query planning, result deduplication, incremental indexing, metrics, evaluation, and local benchmark workflows.

The repository is intentionally runnable without Docker: the C++ gateway includes an in-memory OpenSearch-compatible backend for tests and demos. OpenSearch configuration and ingestion scripts are included for a real local deployment when Docker is available.

## What It Solves

- Exact terms such as `E1027`, `payment_timeout`, and `API v3` should rank well.
- Semantic queries such as "downstream payment interface no response" should still find the right runbook.
- Users must never receive documents outside their tenant, department, projects, or groups.
- Strict ACL filters can shrink vector candidates, so the planner switches between exact search and iterative candidate expansion.
- Every query records ACL summary, retrieval mode, candidate limits, fallback state, returned hits, and latency.

## Core Features

- C++17 retrieval core with zero third-party runtime dependency.
- Deterministic local hash embeddings for reproducible demos and tests.
- BM25-style keyword scoring, vector cosine scoring, hybrid RRF fusion, and document-level deduplication.
- Fail-closed `AccessPolicyResolver` and server-side `ACLFilterBuilder`.
- `FilterAwareQueryPlanner` with exact branch and iterative expansion branch.
- `IncrementalIndexer` supporting upsert, delete, ACL update, content hash, and embedding cache reuse.
- Minimal HTTP API for `/health`, `/metrics`, `/v1/search`, and `/v1/debug/query/{query_id}`.
- Generated 3000-chunk evaluation corpus and 120 simulated human-labeled queries.
- OpenSearch index mapping, RRF pipeline config, bootstrap script, and bulk ingest script.

## Quick Start

```sh
make test
make demo
```

Run one query:

```sh
./build/ergateway search --user backend-user-01 --query "E1027 payment_timeout" --top-k 5 --project payment
```

Start the local HTTP gateway:

```sh
./build/ergateway serve --port 8080
tools/request_examples/curl_demo.sh
```

Generate the evaluation corpus and run reports:

```sh
python3 scripts/build_demo_dataset.py --documents 3000 --queries 120
python3 scripts/generate_embeddings.py
python3 scripts/run_evaluation.py --write-doc docs/evaluation.md
python3 scripts/run_benchmark.py --requests 120 --concurrency 1 4 8 --write-doc docs/benchmark.md
```

## OpenSearch Path

When Docker is available:

```sh
docker compose up -d
python3 scripts/bootstrap_opensearch.py
python3 scripts/generate_embeddings.py
python3 scripts/ingest_documents.py
```

The C++ demo uses the in-memory backend so the core behavior remains testable without a running OpenSearch cluster.

## API

`POST /v1/search`

```json
{
  "user_id": "backend-user-01",
  "query": "E1027 payment_timeout",
  "top_k": 5,
  "project_ids": ["payment"],
  "document_types": ["incident_report", "runbook"]
}
```

Response includes `query_id`, retrieval `mode`, `fallback_triggered`, `final_candidate_limit`, and authorized hits with citations.

## Evaluation Snapshot

|strategy|recall@5|recall@10|mrr|ndcg@10|empty_rate|unauthorized|fallback_rate|
|---|---|---|---|---|---|---|---|
|bm25_only|0.983|0.983|0.983|0.983|0.608|0|0.000|
|vector_only|0.652|0.655|0.656|0.652|0.142|0|0.000|
|hybrid|0.763|0.900|0.839|0.847|0.000|778|0.000|
|hybrid_acl|0.758|0.892|0.840|0.844|0.142|0|0.000|
|planner_acl|0.848|0.983|0.967|0.947|0.142|0|0.000|

The unprotected `hybrid` strategy intentionally shows leakage risk. ACL-enabled strategies report `unauthorized = 0`.

## Project Layout

- `include/` and `src/`: C++ retrieval gateway implementation.
- `config/`: gateway example config, OpenSearch index mapping, RRF search pipeline.
- `datasets/`: generated enterprise documents, ACL users, and simulated labels.
- `scripts/`: dataset generation, embedding generation, OpenSearch bootstrap/ingest, evaluation, benchmark.
- `tests/`: unit/security/integration-style coverage for the core local backend.
- `docs/`: architecture, ACL model, retrieval strategy, indexing, evaluation, benchmark, and failure cases.

## Verification

The current local verification target is:

```sh
make test
python3 scripts/run_evaluation.py
python3 scripts/run_benchmark.py --requests 120 --concurrency 1 4 8
```

