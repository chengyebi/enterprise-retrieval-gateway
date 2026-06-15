import type { QueryDebug } from '../types';

interface DebugPanelProps {
  debug: QueryDebug | null;
}

function formatList(values: string[]): string {
  return values.length > 0 ? values.join(', ') : '无';
}

function formatBool(value: boolean): string {
  return value ? '是' : '否';
}

function formatMode(mode: string): string {
  const labels: Record<string, string> = {
    static_browser_planner_acl: '静态浏览器规划器 + 权限过滤',
    local_cpp_gateway: '本地 C++ 网关',
    supabase_postgres_rls: 'Supabase Postgres + RLS',
    hybrid_iterative_expansion: '混合检索 + 候选扩展',
    filtered_exact_vector: '过滤后精确向量检索',
    hybrid: '混合检索',
    keyword: '关键词检索',
    vector: '向量检索',
  };
  return labels[mode] ?? mode;
}

export function DebugPanel({ debug }: DebugPanelProps) {
  if (!debug) {
    return <div className="empty-state">执行一次查询后会生成调试信息。</div>;
  }

  const rows = [
    ['查询 ID', debug.query_id],
    ['当前用户', debug.current_user],
    ['查询语句', debug.query],
    ['检索模式', formatMode(debug.mode)],
    ['返回数量 top_k', String(debug.top_k)],
    ['项目过滤', formatList(debug.project_filters)],
    ['文档类型过滤', formatList(debug.document_type_filters)],
    ['总分块数', String(debug.total_chunks)],
    ['ACL 前候选数', String(debug.candidates_before_acl)],
    ['ACL 过滤数量', String(debug.filtered_by_acl)],
    ['ACL 后候选数', String(debug.candidates_after_acl)],
    ['返回结果数', String(debug.returned_hits)],
    ['延迟（毫秒）', String(debug.latency_ms)],
    ['是否触发候选扩展', formatBool(debug.fallback_triggered)],
    ['最终候选上限', String(debug.final_candidate_limit)],
  ];

  return (
    <section className="debug-panel">
      <div className="kv-grid">
        {rows.map(([label, value]) => (
          <div key={label} className="kv-row">
            <span>{label}</span>
            <strong>{value}</strong>
          </div>
        ))}
      </div>
      {debug.error && <div className="error-banner">{debug.error}</div>}
      <div className="explanation-box">
        <span>检索解释</span>
        <p>{debug.retrieval_explanation}</p>
      </div>
    </section>
  );
}
