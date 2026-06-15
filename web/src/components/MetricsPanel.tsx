import type { SessionMetrics } from '../types';
import { compactNumber } from '../lib/tokenize';
import { metricsView } from '../lib/metrics';

interface MetricsPanelProps {
  metrics: SessionMetrics;
  onReset: () => void;
}

export function MetricsPanel({ metrics, onReset }: MetricsPanelProps) {
  const view = metricsView(metrics);
  const cards = [
    ['总查询数', String(view.total_queries)],
    ['成功查询', String(view.success_queries)],
    ['失败查询', String(view.failed_queries)],
    ['平均延迟（毫秒）', compactNumber(view.avg_latency_ms, 1)],
    ['平均返回数', compactNumber(view.avg_returned_hits, 1)],
    ['ACL 累计过滤', String(view.acl_filtered_total)],
    ['静态模式查询', String(view.static_mode_queries)],
    ['本地网关查询', String(view.local_gateway_queries)],
    ['Supabase 查询', String(view.supabase_fullstack_queries)],
    ['候选扩展比例', `${compactNumber(view.fallback_rate * 100, 1)}%`],
    ['最后查询时间', view.last_query_time ? new Date(view.last_query_time).toLocaleString('zh-CN') : '无'],
  ];

  return (
    <section className="metrics-panel">
      <div className="panel-toolbar">
        <h2>会话指标</h2>
        <button type="button" className="secondary-button" onClick={onReset}>
          重置指标
        </button>
      </div>
      <div className="metric-grid">
        {cards.map(([label, value]) => (
          <div key={label} className="metric-card">
            <span>{label}</span>
            <strong>{value}</strong>
          </div>
        ))}
      </div>
    </section>
  );
}
