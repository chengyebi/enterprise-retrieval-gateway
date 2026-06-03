# Windows Runbook: Continue EnterpriseRetrievalGateway

This file records what was completed in this session and what remains if you want to finish the project with a real Docker/OpenSearch backend on a Windows machine.

## 1. Repository

Public GitHub repository:

```text
https://github.com/chengyebi/enterprise-retrieval-gateway
```

Local Mac path used during development:

```text
/Users/cy/enterprise-retrieval-gateway
```

Recommended Windows path after cloning:

```powershell
git clone https://github.com/chengyebi/enterprise-retrieval-gateway.git
cd enterprise-retrieval-gateway
```

## 2. What Has Been Completed

The project is already a runnable local demo and evaluation project.

Completed core implementation:

- C++17 retrieval gateway.
- Fail-closed `AccessPolicyResolver`.
- Server-side `ACLFilterBuilder`.
- In-memory OpenSearch-compatible backend for local testing.
- BM25-style keyword scoring.
- Deterministic local hash embeddings.
- Vector cosine retrieval.
- Hybrid retrieval with RRF fusion.
- Filter-aware query planner.
- Iterative candidate expansion.
- Result deduplication by document.
- Incremental indexing: upsert, delete, ACL update.
- Content hash and embedding model version fields.
- Query metrics and debug trace recording.
- Minimal HTTP API:
  - `GET /health`
  - `GET /metrics`
  - `POST /v1/search`
  - `GET /v1/debug/query/{query_id}`

Completed project artifacts:

- `README.md`
- `docker-compose.yml`
- OpenSearch index mapping: `config/opensearch-index.json`
- OpenSearch RRF pipeline config: `config/opensearch-pipeline.json`
- Dataset generator: `scripts/build_demo_dataset.py`
- Embedding generator: `scripts/generate_embeddings.py`
- OpenSearch bootstrap script: `scripts/bootstrap_opensearch.py`
- OpenSearch bulk ingest script: `scripts/ingest_documents.py`
- Evaluation script: `scripts/run_evaluation.py`
- Benchmark script: `scripts/run_benchmark.py`
- Docs under `docs/`
- Unit/security/integration-style C++ tests in `tests/unit/test_core.cpp`

Generated local dataset:

- 3000 simulated enterprise document chunks.
- 120 simulated manually labeled evaluation queries.
- 366 relevance judgments.
- ACL users for engineering, finance, sales, HR, admin, and no-access cases.

## 3. Verification Already Run

These commands were run successfully on the Mac:

```sh
make test
python3 scripts/run_evaluation.py
python3 scripts/run_benchmark.py --requests 30 --concurrency 1
```

HTTP smoke test was also run successfully:

```sh
./build/ergateway serve --port 8090
curl http://localhost:8090/health
curl -X POST http://localhost:8090/v1/search \
  -H "Content-Type: application/json" \
  --data @tools/request_examples/search_backend_user.json
```

Important note:

```text
Docker and CMake were not installed on the Mac, so Docker/OpenSearch and CMake verification were not run there.
```

## 4. Evaluation Result

Current offline evaluation result:

| strategy | Recall@5 | Recall@10 | MRR | NDCG@10 | Empty Rate | Unauthorized |
|---|---:|---:|---:|---:|---:|---:|
| bm25_only | 0.983 | 0.983 | 0.983 | 0.983 | 0.608 | 0 |
| vector_only | 0.652 | 0.655 | 0.656 | 0.652 | 0.142 | 0 |
| hybrid | 0.763 | 0.900 | 0.839 | 0.847 | 0.000 | 778 |
| hybrid_acl | 0.758 | 0.892 | 0.840 | 0.844 | 0.142 | 0 |
| planner_acl | 0.848 | 0.983 | 0.967 | 0.947 | 0.142 | 0 |

Interpretation:

- Final recommended strategy is `planner_acl`.
- It keeps unauthorized results at `0`.
- It has strong retrieval quality on the simulated dataset.
- The unsafe `hybrid` row intentionally shows why ACL is mandatory: it leaked 778 unauthorized hits.

## 5. Windows Environment Needed

Minimum Windows environment:

- Docker Desktop with WSL2 backend.
- Git.
- Python 3.
- C++17 compiler.
- Make or CMake.

Recommended:

- Visual Studio 2022 Build Tools or full Visual Studio with C++ workload.
- CMake 3.16 or newer.
- PowerShell 7, optional.

If you only want to run the OpenSearch/data scripts first, C++ is not required immediately. Docker + Python are enough.

## 6. First Windows Run: OpenSearch

From the repository root:

```powershell
docker compose up -d
```

Check OpenSearch:

```powershell
curl http://localhost:9200
```

Bootstrap index and RRF pipeline:

```powershell
python scripts/bootstrap_opensearch.py
```

Generate or refresh the dataset:

```powershell
python scripts/build_demo_dataset.py --documents 3000 --queries 120
python scripts/generate_embeddings.py
```

Ingest documents into OpenSearch:

```powershell
python scripts/ingest_documents.py
```

At this point OpenSearch should contain the generated enterprise document chunks.

## 7. Re-run Evaluation and Benchmark on Windows

These evaluation scripts use the deterministic local retrieval model, not OpenSearch:

```powershell
python scripts/run_evaluation.py --write-doc docs/evaluation.md
python scripts/run_benchmark.py --requests 120 --concurrency 1 4 8 --write-doc docs/benchmark.md
```

Use them to confirm the repo behaves the same on Windows.

## 8. Current Limitation

The C++ gateway currently queries the local in-memory backend:

```text
InMemoryOpenSearchClient
```

The repository already contains OpenSearch config and ingestion scripts, but the C++ `RetrievalGateway` does not yet call OpenSearch over HTTP.

This was deliberate so the project could be fully built and tested on a machine without Docker.

## 9. What Remains to Truly Finish the OpenSearch Version

To make the project fully production-like, implement a real OpenSearch backend:

```text
OpenSearchHttpClient
```

Recommended implementation tasks:

1. Add a real backend interface.

Current code directly uses `InMemoryOpenSearchClient`. Refactor to an interface such as:

```cpp
class SearchBackend {
public:
    virtual ~SearchBackend() = default;
    virtual BulkResult bulkUpsert(const std::vector<DocumentChunk>& chunks) = 0;
    virtual bool deleteDocument(const std::string& document_id) = 0;
    virtual bool updateAcl(...) = 0;
    virtual std::vector<SearchHit> keywordSearch(...) const = 0;
    virtual std::vector<SearchHit> vectorSearch(...) const = 0;
    virtual std::size_t estimateAuthorizedCandidates(...) const = 0;
};
```

2. Keep `InMemoryOpenSearchClient` for unit tests.

3. Add `OpenSearchHttpClient` for real Docker/OpenSearch execution.

4. Use `libcurl` or `cpp-httplib` for HTTP.

5. Use `nlohmann/json` for JSON construction/parsing.

6. Build OpenSearch query bodies with server-side ACL filter injection.

The OpenSearch filter must include:

```text
tenant_id == user.tenant_id
department == user.department
project_id in user.project_ids
allowed_groups intersects user.groups
```

For admin users, keep tenant filtering and record admin state in trace logs.

7. Implement real OpenSearch operations:

- keyword search with `match` / `multi_match`
- vector search with `knn`
- hybrid query or two separate queries followed by local RRF
- count API for candidate estimation
- bulk upsert
- delete by `document_id`
- update ACL by `document_id`

8. Add integration tests that require Docker/OpenSearch.

Suggested integration test flow:

```text
docker compose up -d
python scripts/bootstrap_opensearch.py
python scripts/generate_embeddings.py
python scripts/ingest_documents.py
run C++ integration test against http://localhost:9200
verify unauthorized result count is 0
verify E1027 query returns payment docs
verify ACL update hides a document
verify delete removes old chunks
```

9. Add config switch:

```json
{
  "backend": "memory"
}
```

or:

```json
{
  "backend": "opensearch",
  "opensearch": {
    "url": "http://localhost:9200",
    "index": "enterprise_docs"
  }
}
```

10. Update README with two modes:

- local deterministic mode
- Docker/OpenSearch mode

## 10. Suggested Windows Completion Order

Do this in order:

1. Clone the repo.
2. Run Docker/OpenSearch.
3. Bootstrap index and pipeline.
4. Generate embeddings.
5. Ingest documents.
6. Confirm documents exist in OpenSearch.
7. Re-run local evaluation and benchmark.
8. Implement `OpenSearchHttpClient`.
9. Add backend interface.
10. Add config switch between memory and OpenSearch.
11. Add Docker/OpenSearch integration tests.
12. Re-run evaluation against real OpenSearch-backed gateway.
13. Commit and push.

## 11. Quick OpenSearch Checks

Check cluster:

```powershell
curl http://localhost:9200
```

Check index exists:

```powershell
curl http://localhost:9200/enterprise_docs
```

Count documents:

```powershell
curl http://localhost:9200/enterprise_docs/_count
```

Simple keyword query:

```powershell
curl -X POST http://localhost:9200/enterprise_docs/_search `
  -H "Content-Type: application/json" `
  -d "{\"query\":{\"match\":{\"content\":\"E1027\"}},\"size\":5}"
```

## 12. Success Definition for the Windows Follow-up

The OpenSearch-backed version is complete when all are true:

- Docker OpenSearch starts cleanly.
- Index and RRF pipeline are created.
- Generated documents are ingested.
- C++ gateway can query OpenSearch, not just memory backend.
- ACL filters are injected into every OpenSearch query path.
- Unknown users fail closed.
- `E1027` query returns payment incident/runbook/API docs for authorized engineering users.
- Finance report does not appear for backend users.
- Finance report appears for finance user or admin.
- ACL update changes search visibility.
- Delete removes old chunks.
- Evaluation still reports unauthorized result count as `0` for ACL-enabled strategies.
- README documents both local and OpenSearch modes.

