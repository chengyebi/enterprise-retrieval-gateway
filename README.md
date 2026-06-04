# EnterpriseRetrievalGateway

EnterpriseRetrievalGateway 是一个 C++17 企业知识库检索网关。它位于 OpenSearch 风格的检索系统之上，负责文档级 ACL 权限过滤、关键词/向量混合检索、RRF 融合、过滤感知查询规划、结果去重、增量索引、指标记录、离线评估和本地基准测试。

项目支持两种运行模式：

- `memory`：内存后端，用于快速演示、单元测试和无 Docker 环境。
- `opensearch`：真实 HTTP OpenSearch 后端，用于 Docker/OpenSearch 本地部署验证。

默认运行模式是 `memory`，所以即使没有 Docker，也可以完成核心功能测试。

## 解决的问题

- `E1027`、`payment_timeout`、`API v3` 这类精确关键词应该排在前面。
- “下游支付接口长时间无响应” 这类语义查询也应该找到对应 runbook。
- 用户不能看到租户、部门、项目或用户组之外的文档。
- 严格 ACL 会缩小向量候选集，因此查询规划器会在精确搜索和迭代扩展之间切换。
- 每次查询都会记录 ACL 摘要、检索模式、候选上限、fallback 状态、返回结果数和延迟。

## 核心能力

- C++17 检索核心，无第三方 C++ 运行时依赖。
- 可插拔搜索后端接口 `SearchBackend`。
- 内存后端 `InMemoryOpenSearchClient`，用于稳定单元测试。
- OpenSearch HTTP 后端 `OpenSearchHttpClient`，支持真实 Docker/OpenSearch 查询。
- 服务端 ACL 注入，未知用户 fail-closed。
- BM25 风格关键词打分、向量 cosine 检索、混合 RRF 融合和文档级去重。
- `FilterAwareQueryPlanner` 支持精确向量分支和迭代候选扩展分支。
- `IncrementalIndexer` 支持 upsert、delete、ACL update、content hash 和 embedding 复用。
- HTTP API：`/health`、`/metrics`、`/v1/search`、`/v1/debug/query/{query_id}`。
- 3000 chunk 模拟企业语料、120 条模拟人工标注查询和评估脚本。
- OpenSearch 2.14 兼容的索引 mapping、hybrid normalization pipeline、bootstrap 和 ingest 脚本。

## 快速开始

本机有 `make` 时：

```sh
make test
make demo
```

Windows 上没有 `make`/`cmake` 时，可以用 MinGW `g++` 直接编译，当前已验证通过。

运行一次内存后端查询：

```sh
./build/ergateway search --backend memory --user backend-user-01 --query "E1027 payment_timeout" --top-k 5 --project payment
```

启动本地 HTTP 网关：

```sh
./build/ergateway serve --backend memory --port 8080
tools/request_examples/curl_demo.sh
```

## Docker/OpenSearch 模式

启动 OpenSearch：

```sh
export OPENSEARCH_INITIAL_ADMIN_PASSWORD="<设置一个本地强密码>"
docker compose up -d
```

PowerShell：

```powershell
$env:OPENSEARCH_INITIAL_ADMIN_PASSWORD = "<设置一个本地强密码>"
docker compose up -d
```

初始化索引和 search pipeline：

```sh
python scripts/bootstrap_opensearch.py
```

生成 embedding 并导入文档：

```sh
python scripts/generate_embeddings.py
python scripts/ingest_documents.py
```

确认文档数量：

```sh
curl http://localhost:9200/enterprise_docs/_count
```

运行 OpenSearch 后端查询：

```sh
./build/ergateway search --backend opensearch --user backend-user-01 --query "E1027 payment_timeout" --top-k 5 --project payment
```

启动 OpenSearch 后端 HTTP 服务：

```sh
./build/ergateway serve --backend opensearch --port 8080
```

OpenSearch 2.14 不支持 `score-ranker-processor`，所以 `config/opensearch-pipeline.json` 使用 2.14 可用的 `normalization-processor`。网关自身仍然在 C++ 层执行关键词结果和向量结果的本地 RRF 融合。

## API 示例

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

响应包含：

- `query_id`
- 检索 `mode`
- `fallback_triggered`
- `final_candidate_limit`
- 已授权的 hits 和引用片段

## 评估结果

| strategy | recall@5 | recall@10 | mrr | ndcg@10 | empty_rate | unauthorized | fallback_rate |
|---|---:|---:|---:|---:|---:|---:|---:|
| bm25_only | 0.983 | 0.983 | 0.983 | 0.983 | 0.608 | 0 | 0.000 |
| vector_only | 0.652 | 0.655 | 0.656 | 0.652 | 0.142 | 0 | 0.000 |
| hybrid | 0.763 | 0.900 | 0.839 | 0.847 | 0.000 | 778 | 0.000 |
| hybrid_acl | 0.758 | 0.892 | 0.840 | 0.844 | 0.142 | 0 | 0.000 |
| planner_acl | 0.848 | 0.983 | 0.967 | 0.947 | 0.142 | 0 | 0.000 |

最终推荐策略是 `planner_acl`。它保持 ACL 开启后的 unauthorized 结果为 `0`，同时在模拟数据集上保持较高召回和排序质量。`hybrid` 行故意不加 ACL，用来展示权限泄漏风险。

## 验证命令

本地核心验证：

```sh
make test
python scripts/run_evaluation.py
python scripts/run_benchmark.py --requests 120 --concurrency 1 4 8
```

Docker/OpenSearch 集成验证：

```sh
make test-opensearch
```

如果 Windows 没有 `make`，可以直接运行已编译的测试：

```powershell
.\build\test_core.exe
.\build\test_opensearch_backend.exe
```

本次 Windows 验证结果：

- Docker Desktop 已恢复可用。
- OpenSearch 容器 `enterprise-retrieval-opensearch` 已启动并 healthy。
- `scripts/bootstrap_opensearch.py` 通过。
- `scripts/generate_embeddings.py` 通过。
- `scripts/ingest_documents.py` 导入 3000 条文档。
- `test_core.exe` 通过。
- `test_opensearch_backend.exe` 通过。
- OpenSearch-backed `E1027 payment_timeout` 查询通过。
- `run_evaluation.py` 和小规模 benchmark 通过。

## 项目结构

- `include/`、`src/`：C++ 检索网关实现。
- `include/retrieval_gateway/backend/search_backend.h`：通用搜索后端接口。
- `src/backend/in_memory_opensearch_client.cpp`：内存后端。
- `src/backend/opensearch_http_client.cpp`：OpenSearch HTTP 后端。
- `config/`：网关配置、OpenSearch index mapping、hybrid search pipeline。
- `datasets/`：模拟企业文档、ACL 用户和评估标签。
- `scripts/`：数据集生成、embedding 生成、OpenSearch bootstrap/ingest、评估和 benchmark。
- `tests/unit/`：核心单元、安全和集成风格测试。
- `tests/integration/`：Docker/OpenSearch 后端集成测试。
- `docs/`：架构、ACL、检索策略、增量索引、评估、基准和失败案例说明。

## 注意事项

- `docker-compose.yml` 从环境变量 `OPENSEARCH_INITIAL_ADMIN_PASSWORD` 读取本地 OpenSearch 初始化密码，仓库不保存真实密码。
- Docker 数据被重置会清空本机 Docker 镜像、容器和 volume，但不会影响仓库文件。
- OpenSearch 后端当前只支持本地 HTTP，无 HTTPS/认证集群支持；生产环境需要补充认证、TLS、重试和更完整的 JSON 解析。
