import type { MetricsView, SearchMode, SessionMetrics } from '../types';

const STORAGE_KEY = 'erg_demo_metrics';

export function emptyMetrics(): SessionMetrics {
  return {
    total_queries: 0,
    success_queries: 0,
    failed_queries: 0,
    latency_sum_ms: 0,
    returned_hits_sum: 0,
    acl_filtered_total: 0,
    static_mode_queries: 0,
    local_gateway_queries: 0,
    supabase_fullstack_queries: 0,
    fallback_queries: 0,
    last_query_time: null,
  };
}

export function loadMetrics(): SessionMetrics {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    if (!raw) {
      return emptyMetrics();
    }
    return { ...emptyMetrics(), ...JSON.parse(raw) };
  } catch {
    return emptyMetrics();
  }
}

export function saveMetrics(metrics: SessionMetrics): void {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(metrics));
}

export function clearMetrics(): SessionMetrics {
  const metrics = emptyMetrics();
  saveMetrics(metrics);
  return metrics;
}

export function recordQuery(
  metrics: SessionMetrics,
  input: {
    ok: boolean;
    mode: SearchMode;
    latency_ms: number;
    returned_hits: number;
    acl_filtered: number;
    fallback_triggered: boolean;
  },
): SessionMetrics {
  const next: SessionMetrics = {
    ...metrics,
    total_queries: metrics.total_queries + 1,
    success_queries: metrics.success_queries + (input.ok ? 1 : 0),
    failed_queries: metrics.failed_queries + (input.ok ? 0 : 1),
    latency_sum_ms: metrics.latency_sum_ms + Math.max(0, input.latency_ms),
    returned_hits_sum: metrics.returned_hits_sum + Math.max(0, input.returned_hits),
    acl_filtered_total: metrics.acl_filtered_total + Math.max(0, input.acl_filtered),
    static_mode_queries: metrics.static_mode_queries + (input.mode === 'static' ? 1 : 0),
    local_gateway_queries: metrics.local_gateway_queries + (input.mode === 'local' ? 1 : 0),
    supabase_fullstack_queries: metrics.supabase_fullstack_queries + (input.mode === 'supabase' ? 1 : 0),
    fallback_queries: metrics.fallback_queries + (input.fallback_triggered ? 1 : 0),
    last_query_time: new Date().toISOString(),
  };
  saveMetrics(next);
  return next;
}

export function metricsView(metrics: SessionMetrics): MetricsView {
  const total = metrics.total_queries || 0;
  return {
    total_queries: metrics.total_queries,
    success_queries: metrics.success_queries,
    failed_queries: metrics.failed_queries,
    avg_latency_ms: total > 0 ? metrics.latency_sum_ms / total : 0,
    avg_returned_hits: total > 0 ? metrics.returned_hits_sum / total : 0,
    acl_filtered_total: metrics.acl_filtered_total,
    static_mode_queries: metrics.static_mode_queries,
    local_gateway_queries: metrics.local_gateway_queries,
    supabase_fullstack_queries: metrics.supabase_fullstack_queries,
    last_query_time: metrics.last_query_time,
    fallback_rate: total > 0 ? metrics.fallback_queries / total : 0,
  };
}
