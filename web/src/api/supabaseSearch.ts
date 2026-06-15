import type { SupabaseAclProfile, SupabaseSearchResponse, SearchHit, SearchRequest } from '../types';
import { requireSupabaseClient } from './supabaseClient';

interface SupabaseProfileRow {
  auth_user_id: string;
  acl_user_id: string;
  tenant_id: string;
  department: string;
  groups: string[] | null;
  project_ids: string[] | null;
  is_admin: boolean;
  display_name: string | null;
}

interface SupabaseSearchRow {
  tenant_id: string;
  document_id: string;
  chunk_id: string;
  title: string;
  content: string;
  department: string;
  project_id: string;
  document_type: string;
  allowed_groups: string[] | null;
  score: number | null;
  lexical_score: number | null;
  semantic_score: number | null;
  total_candidates: number | null;
}

function mapProfile(row: SupabaseProfileRow): SupabaseAclProfile {
  return {
    auth_user_id: row.auth_user_id,
    acl_user_id: row.acl_user_id,
    tenant_id: row.tenant_id,
    department: row.department,
    groups: row.groups ?? [],
    project_ids: row.project_ids ?? [],
    is_admin: row.is_admin,
    display_name: row.display_name,
  };
}

export async function fetchCurrentSupabaseProfile(): Promise<SupabaseAclProfile | null> {
  const supabase = requireSupabaseClient();
  const { data, error } = await supabase
    .from('current_user_acl_profile')
    .select('*')
    .maybeSingle<SupabaseProfileRow>();

  if (error) {
    throw new Error(error.message);
  }

  return data ? mapProfile(data) : null;
}

function toHit(row: SupabaseSearchRow, profile: SupabaseAclProfile | null): SearchHit {
  const score = Number(row.score ?? row.lexical_score ?? 0);
  const groups = row.allowed_groups ?? [];
  return {
    tenant_id: row.tenant_id,
    document_id: row.document_id,
    chunk_id: row.chunk_id,
    title: row.title,
    snippet: row.content.slice(0, 260),
    department: row.department,
    project_id: row.project_id,
    allowed_groups: groups,
    document_type: row.document_type,
    score,
    lexical_score: Number(row.lexical_score ?? score),
    semantic_score: Number(row.semantic_score ?? 0),
    fusion_score: score,
    source: 'supabase-postgres-rls',
    visibility_reason: profile
      ? `Supabase RLS 已按 ${profile.tenant_id}/${profile.department}/${profile.project_ids.join(', ') || '无项目'} 过滤`
      : '未绑定 ACL 用户，Supabase RLS 默认不返回文档',
  };
}

function buildQueryId(): string {
  const random = Math.random().toString(36).slice(2, 8);
  return `supabase-${Date.now().toString(36)}-${random}`;
}

export async function searchSupabaseDocuments(
  request: SearchRequest,
  profile: SupabaseAclProfile | null,
  totalChunks: number,
): Promise<SupabaseSearchResponse> {
  const started = performance.now();
  const supabase = requireSupabaseClient();
  const { data, error } = await supabase.rpc('search_visible_chunks', {
    p_document_types: request.document_types,
    p_project_ids: request.project_ids,
    p_query: request.query,
    p_top_k: request.top_k,
  });

  if (error) {
    throw new Error(error.message);
  }

  const rows = (data ?? []) as SupabaseSearchRow[];
  const hits = rows.map((row) => toHit(row, profile));
  const totalCandidates = Number(rows[0]?.total_candidates ?? hits.length);
  const latencyMs = Math.max(1, Math.round(performance.now() - started));
  const queryId = buildQueryId();

  return {
    ok: true,
    query_id: queryId,
    mode: 'supabase_postgres_rls',
    hits,
    debug: {
      query_id: queryId,
      current_user: profile?.acl_user_id ?? '未绑定 Supabase auth 用户',
      query: request.query,
      mode: 'supabase_postgres_rls',
      top_k: request.top_k,
      project_filters: request.project_ids,
      document_type_filters: request.document_types,
      total_chunks: totalChunks,
      candidates_before_acl: totalCandidates,
      filtered_by_acl: 0,
      candidates_after_acl: totalCandidates,
      returned_hits: hits.length,
      latency_ms: latencyMs,
      fallback_triggered: false,
      final_candidate_limit: request.top_k,
      retrieval_explanation:
        'Supabase RPC 在当前 Auth session 下查询 Postgres；documents/chunks 表 RLS 先执行 tenant、department、project、groups 过滤。未由管理员绑定 auth.users.id 的账号默认没有 ACL profile，因此返回 0 条文档。',
    },
    fallback_triggered: false,
    final_candidate_limit: request.top_k,
  };
}
