import type { QueryDebug } from '../types';

interface DebugPanelProps {
  debug: QueryDebug | null;
}

function formatList(values: string[]): string {
  return values.length > 0 ? values.join(', ') : 'none';
}

export function DebugPanel({ debug }: DebugPanelProps) {
  if (!debug) {
    return <div className="empty-state">Run a query to generate query debug details.</div>;
  }

  const rows = [
    ['query_id', debug.query_id],
    ['current_user', debug.current_user],
    ['query', debug.query],
    ['mode', debug.mode],
    ['top_k', String(debug.top_k)],
    ['project filters', formatList(debug.project_filters)],
    ['document_type filters', formatList(debug.document_type_filters)],
    ['total_chunks', String(debug.total_chunks)],
    ['candidates_before_acl', String(debug.candidates_before_acl)],
    ['filtered_by_acl', String(debug.filtered_by_acl)],
    ['candidates_after_acl', String(debug.candidates_after_acl)],
    ['returned_hits', String(debug.returned_hits)],
    ['latency_ms', String(debug.latency_ms)],
    ['fallback_triggered', String(debug.fallback_triggered)],
    ['final_candidate_limit', String(debug.final_candidate_limit)],
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
        <span>retrieval explanation</span>
        <p>{debug.retrieval_explanation}</p>
      </div>
    </section>
  );
}
