import type { LocalSearchResponse, QueryDebug, SearchRequest } from '../types';

export interface LocalGatewayMetrics {
  total_queries?: number;
  error_queries?: number;
  fallback_queries?: number;
  avg_latency_ms?: number;
  p95_latency_ms?: number;
  max_latency_ms?: number;
}

function endpoint(baseUrl: string, path: string): string {
  return `${baseUrl.replace(/\/+$/, '')}${path}`;
}

async function parseJson<T>(response: Response): Promise<T> {
  const text = await response.text();
  const parsed = text ? JSON.parse(text) : {};
  if (!response.ok) {
    const message = typeof parsed?.error === 'string' ? parsed.error : `${response.status} ${response.statusText}`;
    throw new Error(message);
  }
  return parsed as T;
}

export async function healthCheck(baseUrl: string): Promise<Record<string, unknown>> {
  const response = await fetch(endpoint(baseUrl, '/health'), {
    method: 'GET',
  });
  return parseJson<Record<string, unknown>>(response);
}

export async function fetchGatewayMetrics(baseUrl: string): Promise<LocalGatewayMetrics> {
  const response = await fetch(endpoint(baseUrl, '/metrics'), {
    method: 'GET',
  });
  return parseJson<LocalGatewayMetrics>(response);
}

export async function searchLocalGateway(baseUrl: string, request: SearchRequest): Promise<LocalSearchResponse> {
  const response = await fetch(endpoint(baseUrl, '/v1/search'), {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({
      user_id: request.user_id,
      query: request.query,
      top_k: request.top_k,
      project_ids: request.project_ids,
      document_types: request.document_types,
      enable_vector_search: request.enable_vector_search ?? true,
      enable_keyword_search: request.enable_keyword_search ?? true,
    }),
  });
  const text = await response.text();
  const parsed = text ? (JSON.parse(text) as LocalSearchResponse) : ({} as LocalSearchResponse);
  if (!response.ok && parsed.ok !== false) {
    const message = typeof parsed.error === 'string' ? parsed.error : `${response.status} ${response.statusText}`;
    throw new Error(message);
  }
  return parsed;
}

export async function fetchQueryDebug(baseUrl: string, queryId: string): Promise<Partial<QueryDebug>> {
  const response = await fetch(endpoint(baseUrl, `/v1/debug/query/${encodeURIComponent(queryId)}`), {
    method: 'GET',
  });
  return parseJson<Partial<QueryDebug>>(response);
}
