# EnterpriseRetrievalGateway

EnterpriseRetrievalGateway 是一个 C++17 企业知识库检索网关。它位于 OpenSearch 风格的检索系统之上，负责文档级 ACL 权限过滤、关键词/向量混合检索、RRF 融合、过滤感知查询规划、结果去重、增量索引、指标记录、离线评估和本地基准测试。

## Online Demo

GitHub Pages Demo:

```text
https://chengyebi.github.io/enterprise-retrieval-gateway/
```

默认 access code:

```text
erg-demo-local
```

这个 access code 只是前端演示门禁，会写入浏览器 `localStorage`。它不是生产级认证机制，也不保护真实私有数据。当前 Pages Demo 只使用仓库内的模拟企业语料和模拟 ACL 用户，不包含真实密钥、真实企业数据或外部 API 调用。

Pages 版本是纯静态 B/S Demo：

- 浏览器加载 `web/public/data/demo_data.json`。
- 浏览器内执行轻量关键词/hash-vector 检索、ACL 过滤、planner candidate expansion 模拟和排序。
- 支持用户切换、项目过滤、文档类型过滤、结果卡片、Query Debug、Session Metrics 和 About/Architecture。
- 未知用户按 fail-closed 处理，不返回结果。
- GitHub Pages 不运行 C++ 后端、Docker、OpenSearch、数据库或任何付费云服务。

## Optional Supabase Fullstack Route

仓库也提供一个可选的 Supabase 全栈骨架，用于验证 Auth、Postgres、RLS 和前端登录会话。入口可以直接访问：

```text
https://chengyebi.github.io/enterprise-retrieval-gateway/?mode=supabase
```

本路线包含：

- Supabase Auth：邮箱注册、登录、session 自动恢复和退出。
- Postgres：`tenants`、`users`、`groups`、`user_groups`、`user_projects`、`auth_user_acl_profiles`、`documents`、`chunks`、`document_acl_groups`、`search_events`。
- RLS：默认开启并强制 RLS，按 tenant、department、project、groups 过滤文档。
- 前端：Supabase 登录后进入同一个检索界面，用户身份只读显示为数据库绑定出来的 ACL profile。
- 检索：Supabase RPC 提供 Postgres/RLS 搜索骨架；真实高质量检索服务仍是 C++ 网关。

安全原则：

- 前端只允许放 `VITE_SUPABASE_ANON_KEY`。`service_role` 绝对不要放进仓库、Vite 环境变量或浏览器代码。
- 所有业务表默认开启 RLS。
- 注册/登录只创建 Supabase Auth 用户，不授予任何文档权限。
- 权限 profile 不能由前端自选。管理员必须在数据库中把 `auth.users.id` 绑定到某个 demo ACL 用户。
- 未绑定的已登录用户在 RLS 下默认查不到任何文档。

初始化 Supabase schema：

```sh
# 在 Supabase SQL Editor 或受信任的管理员连接中执行
supabase/schema.sql
```

生成 demo seed SQL：

```sh
python3 scripts/export_supabase_seed.py --output /tmp/erg_supabase_seed.sql
```

然后在 Supabase SQL Editor 或受信任的管理员连接中执行 `/tmp/erg_supabase_seed.sql`。这个 seed 不会创建 Auth 绑定。

管理员绑定 Auth 用户到 demo ACL 用户：

```sql
insert into public.auth_user_acl_profiles (auth_user_id, acl_user_id)
select id, 'backend-user-01'
from auth.users
where email = 'demo@example.com';
```

本地前端启用 Supabase：

```sh
cd web
cp .env.example .env
# 填入 Supabase Project URL 和 anon public key，不要填 service_role
npm run dev
```

GitHub Pages 启用 Supabase：

- 在仓库 `Settings -> Secrets and variables -> Actions -> Variables` 添加 `VITE_SUPABASE_URL` 和 `VITE_SUPABASE_ANON_KEY`。
- 这两个变量会在 Pages workflow 构建时注入。anon key 是浏览器公开 key；不要添加或使用 service_role。

C++ 网关仍然是真实检索服务，可以本地运行：

```sh
make demo
./build/ergateway serve --backend memory --port 8080
```

GitHub Pages 和 Supabase 本身不能托管 C++ 常驻进程。线上如果要跑 C++ 后端，需要部署到能运行进程的环境，例如 VM、容器平台、Fly.io、Render、Railway、Cloud Run 或 Kubernetes，并补齐生产级认证、TLS 和 JWT 校验。

本地前端开发：

```sh
python3 scripts/export_static_demo_data.py
cd web
npm install
npm run dev
```

由于 Vite base 默认为 `/enterprise-retrieval-gateway/`，本地开发页面通常打开：

```text
http://localhost:5173/enterprise-retrieval-gateway/
```

前端构建：

```sh
python3 scripts/export_static_demo_data.py
cd web
npm run build
```

连接本地真实 C++ 网关：

```sh
make demo
./build/ergateway serve --backend memory --port 8080
```

然后在网页里把 Retrieval mode 切换为 `Local C++ Gateway`，Backend URL 保持 `http://localhost:8080`，点击 `Test Connection`。本地网关模式调用 `/health`、`/metrics`、`/v1/search` 和 `/v1/debug/query/{query_id}`。Pages 上仍然只是浏览器前端，真实检索服务必须由你在本机启动。

GitHub Pages 部署：

- workflow: `.github/workflows/deploy-pages.yml`
- 触发方式：push 到 `main` 或在 GitHub Actions 手动 `workflow_dispatch`
- 构建产物：`web/dist`
- 数据导出：workflow 构建前运行 `python scripts/export_static_demo_data.py`

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
