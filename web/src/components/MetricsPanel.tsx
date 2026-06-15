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
    ['total_queries', String(view.total_queries)],
    ['success_queries', String(view.success_queries)],
    ['failed_queries', String(view.failed_queries)],
    ['avg_latency_ms', compactNumber(view.avg_latency_ms, 1)],
    ['avg_returned_hits', compactNumber(view.avg_returned_hits, 1)],
    ['acl_filtered_total', String(view.acl_filtered_total)],
    ['static_mode_queries', String(view.static_mode_queries)],
    ['local_gateway_queries', String(view.local_gateway_queries)],
    ['fallback_rate', `${compactNumber(view.fallback_rate * 100, 1)}%`],
    ['last_query_time', view.last_query_time ? new Date(view.last_query_time).toLocaleString() : 'none'],
  ];

  return (
    <section className="metrics-panel">
      <div className="panel-toolbar">
        <h2>Session Metrics</h2>
        <button type="button" className="secondary-button" onClick={onReset}>
          Reset Metrics
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
