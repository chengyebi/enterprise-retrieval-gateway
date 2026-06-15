export type SearchMode = 'static' | 'local';

export type AppTab = 'search' | 'debug' | 'metrics' | 'about';

export interface DemoUser {
  user_id: string;
  tenant_id: string;
  tenant?: string;
  department: string;
  groups: string[];
  project_ids: string[];
  projects?: string[];
  is_admin: boolean;
}

export interface DemoDocument {
  tenant_id: string;
  tenant?: string;
  document_id: string;
  chunk_id: string;
  title: string;
  text: string;
  snippet?: string;
  department: string;
  project_id: string;
  document_type: string;
  allowed_groups: string[];
  document_version?: number;
  updated_at?: string;
  embedding_model_version?: string;
}

export interface EvaluationQuery {
  query_id: string;
  query: string;
  user_id: string;
  top_k: number;
  project_ids: string[];
  document_types: string[];
}

export interface DemoData {
  schema_version: number;
  source_files: string[];
  acl_model: {
    non_admin_rule: string;
    admin_rule: string;
    unknown_user: string;
  };
  users: DemoUser[];
  documents: DemoDocument[];
  projects: string[];
  document_types: string[];
  departments: string[];
  example_queries: string[];
  evaluation_queries: EvaluationQuery[];
}

export interface SearchFilters {
  project_ids: string[];
  document_types: string[];
}

export interface SearchRequest {
  user_id: string;
  query: string;
  top_k: number;
  project_ids: string[];
  document_types: string[];
  enable_vector_search?: boolean;
  enable_keyword_search?: boolean;
}

export interface SearchHit {
  tenant_id?: string;
  document_id: string;
  chunk_id: string;
  title: string;
  snippet: string;
  department: string;
  project_id: string;
  allowed_groups: string[];
  document_type: string;
  score: number;
  lexical_score?: number;
  semantic_score?: number;
  fusion_score?: number;
  source?: string;
  visibility_reason: string;
}

export interface QueryDebug {
  query_id: string;
  current_user: string;
  query: string;
  mode: string;
  top_k: number;
  project_filters: string[];
  document_type_filters: string[];
  total_chunks: number;
  candidates_before_acl: number;
  filtered_by_acl: number;
  candidates_after_acl: number;
  returned_hits: number;
  latency_ms: number;
  fallback_triggered: boolean;
  final_candidate_limit: number;
  retrieval_explanation: string;
  error?: string;
}

export interface StaticSearchResponse {
  ok: boolean;
  query_id: string;
  mode: 'static_browser_planner_acl';
  hits: SearchHit[];
  debug: QueryDebug;
  fallback_triggered: boolean;
  final_candidate_limit: number;
}

export interface LocalSearchResponse {
  ok: boolean;
  query_id: string;
  mode: string;
  hits: Array<Partial<SearchHit> & {
    lexical_score?: number;
    semantic_score?: number;
    fusion_score?: number;
  }>;
  fallback_triggered: boolean;
  final_candidate_limit: number;
  error?: string;
}

export interface SessionMetrics {
  total_queries: number;
  success_queries: number;
  failed_queries: number;
  latency_sum_ms: number;
  returned_hits_sum: number;
  acl_filtered_total: number;
  static_mode_queries: number;
  local_gateway_queries: number;
  fallback_queries: number;
  last_query_time: string | null;
}

export interface MetricsView {
  total_queries: number;
  success_queries: number;
  failed_queries: number;
  avg_latency_ms: number;
  avg_returned_hits: number;
  acl_filtered_total: number;
  static_mode_queries: number;
  local_gateway_queries: number;
  last_query_time: string | null;
  fallback_rate: number;
}

export interface ConnectionState {
  status: 'idle' | 'checking' | 'healthy' | 'failed';
  message: string;
}
