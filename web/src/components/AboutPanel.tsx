export function AboutPanel() {
  return (
    <section className="about-panel">
      <div className="about-grid">
        <article>
          <h2>项目定位</h2>
          <p>
            企业知识库检索网关把企业检索策略从搜索后端中抽离出来，在网关层统一处理 ACL、查询规划、混合检索、调试链路和指标。
          </p>
        </article>
        <article>
          <h2>为什么需要 ACL</h2>
          <p>
            企业知识库会混合工程事故、财务报告、合同、人事政策和安全资料。检索返回引用前必须先执行租户、部门、项目和用户组权限过滤。
          </p>
        </article>
        <article>
          <h2>为什么需要规划器</h2>
          <p>
            严格过滤会压缩向量候选集。网关规划器会估算过滤后的候选规模，选择关键词、向量或混合路径，并在授权结果过少时扩大候选上限。
          </p>
        </article>
        <article>
          <h2>后端模式</h2>
          <p>
            memory 后端无需 Docker，适合本地演示和测试；OpenSearch 后端用于验证真实索引、BM25、向量检索和混合检索流水线。
            Supabase 全栈模式用于验证 Auth、Postgres 和 RLS 权限边界。
          </p>
        </article>
      </div>

      <div className="architecture-card">
        <h2>架构图</h2>
        <div className="architecture-flow" aria-label="演示架构图">
          <div>浏览器界面 / Supabase Auth</div>
          <span>-&gt;</span>
          <div>静态演示引擎 / 本地 C++ 网关 / Postgres RPC</div>
          <span>-&gt;</span>
          <div>C++ ACL / Supabase RLS</div>
          <span>-&gt;</span>
          <div>关键词 / 向量 / 混合规划器</div>
          <span>-&gt;</span>
          <div>排序结果 / 调试 / 指标</div>
        </div>
      </div>

      <div className="runbook-card">
        <h2>本地 C++ 网关</h2>
        <pre>
          <code>{'make demo\n./build/ergateway serve --backend memory --port 8080'}</code>
        </pre>
        <p>
          将检索模式切换为“本地 C++ 网关”，后端地址保持 <code>http://localhost:8080</code>，然后点击“测试连接”。
          GitHub Pages 和 Supabase 都不能托管这个 C++ 常驻服务；线上运行 C++ 网关需要另找可运行进程的环境。
        </p>
      </div>
    </section>
  );
}
