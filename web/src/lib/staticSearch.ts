import type {
  DemoData,
  DemoDocument,
  DemoUser,
  QueryDebug,
  SearchHit,
  SearchRequest,
  StaticSearchResponse,
} from '../types';
import { aclSummary, canAccessDocument, findUser } from './acl';
import { containsExactPhrase, isCodeLike, normalizeText, tokenCounts, tokenize } from './tokenize';

interface ScoredDocument {
  document: DemoDocument;
  score: number;
  lexical_score: number;
  semantic_score: number;
}

const VECTOR_SIZE = 64;

function sumCounts(counts: Map<string, number>): number {
  let total = 0;
  for (const count of counts.values()) {
    total += count;
  }
  return total || 1;
}

function weightedTokenScore(tokens: string[], value: string, weight: number): number {
  const counts = tokenCounts(value);
  const length = sumCounts(counts);
  let score = 0;
  for (const token of tokens) {
    const count = counts.get(token) ?? 0;
    if (count > 0) {
      score += weight * (1 + Math.log(count)) * (1.4 / Math.sqrt(length));
      if (isCodeLike(token)) {
        score += weight * 0.8;
      }
    }
  }
  return score;
}

function matchedTokenCount(tokens: string[], document: DemoDocument): number {
  const searchable = normalizeText(
    [
      document.title,
      document.text,
      document.document_id,
      document.chunk_id,
      document.document_type,
      document.project_id,
    ].join(' '),
  );
  return tokens.filter((token) => searchable.includes(token)).length;
}

function lexicalScore(document: DemoDocument, query: string, tokens: string[]): number {
  if (tokens.length === 0) {
    return 0;
  }

  let score = 0;
  score += weightedTokenScore(tokens, document.title, 7);
  score += weightedTokenScore(tokens, document.text, 3);
  score += weightedTokenScore(tokens, document.document_id, 5);
  score += weightedTokenScore(tokens, document.chunk_id, 3);
  score += weightedTokenScore(tokens, document.document_type, 2);
  score += weightedTokenScore(tokens, document.project_id, 2);

  if (containsExactPhrase(document.title, query)) {
    score += 16;
  }
  if (containsExactPhrase(document.text, query)) {
    score += 10;
  }
  if (containsExactPhrase(document.document_id, query) || containsExactPhrase(document.chunk_id, query)) {
    score += 8;
  }

  const coverage = matchedTokenCount(tokens, document) / tokens.length;
  score += coverage * coverage * 12;
  if (coverage === 1) {
    score += 6;
  }
  return score;
}

function stableHash(value: string): number {
  let hash = 2166136261;
  for (let index = 0; index < value.length; index += 1) {
    hash ^= value.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
}

function hashVector(value: string): number[] {
  const vector = new Array<number>(VECTOR_SIZE).fill(0);
  const counts = tokenCounts(value);
  for (const [token, count] of counts.entries()) {
    const hash = stableHash(token);
    const slot = hash % VECTOR_SIZE;
    const sign = hash & 1 ? 1 : -1;
    vector[slot] += sign * (1 + Math.log(count));
  }
  const norm = Math.sqrt(vector.reduce((sum, item) => sum + item * item, 0));
  if (norm === 0) {
    return vector;
  }
  return vector.map((item) => item / norm);
}

function cosine(left: number[], right: number[]): number {
  let score = 0;
  for (let index = 0; index < Math.min(left.length, right.length); index += 1) {
    score += left[index] * right[index];
  }
  return score;
}

function semanticScore(document: DemoDocument, queryVector: number[]): number {
  const docVector = hashVector(
    [
      document.title,
      document.text,
      document.document_id,
      document.document_type,
      document.project_id,
    ].join(' '),
  );
  return Math.max(0, cosine(queryVector, docVector));
}

function passesRequestFilters(document: DemoDocument, request: SearchRequest): boolean {
  if (request.project_ids.length > 0 && !request.project_ids.includes(document.project_id)) {
    return false;
  }
  if (request.document_types.length > 0 && !request.document_types.includes(document.document_type)) {
    return false;
  }
  return true;
}

function scoreDocuments(data: DemoData, request: SearchRequest): ScoredDocument[] {
  const tokens = tokenize(request.query);
  const queryVector = hashVector(request.query);
  return data.documents
    .filter((document) => passesRequestFilters(document, request))
    .map((document) => {
      const lexical_score = request.enable_keyword_search === false ? 0 : lexicalScore(document, request.query, tokens);
      const semantic_score = request.enable_vector_search === false ? 0 : semanticScore(document, queryVector);
      const score = lexical_score + semantic_score * 9;
      return { document, lexical_score, semantic_score, score };
    })
    .filter((item) => item.lexical_score > 0 || item.semantic_score >= 0.18)
    .sort((left, right) => {
      if (right.score !== left.score) {
        return right.score - left.score;
      }
      return left.document.chunk_id.localeCompare(right.document.chunk_id);
    });
}

function makeSnippet(document: DemoDocument, tokens: string[]): string {
  const text = document.text || document.snippet || '';
  const normalized = normalizeText(text);
  const firstMatch = tokens
    .map((token) => normalized.indexOf(token))
    .filter((index) => index >= 0)
    .sort((left, right) => left - right)[0];
  if (firstMatch === undefined) {
    return text.slice(0, 220);
  }
  const start = Math.max(0, firstMatch - 80);
  const end = Math.min(text.length, firstMatch + 220);
  const prefix = start > 0 ? '...' : '';
  const suffix = end < text.length ? '...' : '';
  return `${prefix}${text.slice(start, end)}${suffix}`;
}

function toHit(item: ScoredDocument, user: DemoUser | undefined, queryTokens: string[]): SearchHit {
  const decision = canAccessDocument(user, item.document);
  return {
    tenant_id: item.document.tenant_id,
    document_id: item.document.document_id,
    chunk_id: item.document.chunk_id,
    title: item.document.title,
    snippet: makeSnippet(item.document, queryTokens),
    department: item.document.department,
    project_id: item.document.project_id,
    allowed_groups: item.document.allowed_groups,
    document_type: item.document.document_type,
    score: item.score,
    lexical_score: item.lexical_score,
    semantic_score: item.semantic_score,
    fusion_score: item.score,
    source: 'static-browser',
    visibility_reason: decision.allowed ? decision.reason : 'filtered',
  };
}

function buildQueryId(): string {
  const random = Math.random().toString(36).slice(2, 8);
  return `static-${Date.now().toString(36)}-${random}`;
}

export function staticSearch(data: DemoData, request: SearchRequest): StaticSearchResponse {
  const started = performance.now();
  const query_id = buildQueryId();
  const user = findUser(data.users, request.user_id);
  const tokens = tokenize(request.query);
  const scored = scoreDocuments(data, request);
  const initialLimit = Math.min(scored.length, Math.max(request.top_k * 8, 24));

  let finalLimit = initialLimit;
  let candidates = scored.slice(0, finalLimit);
  let authorized = candidates.filter((item) => canAccessDocument(user, item.document).allowed);
  let fallbackTriggered = false;

  if (authorized.length < request.top_k && finalLimit < scored.length) {
    fallbackTriggered = true;
    finalLimit = Math.min(scored.length, Math.max(finalLimit * 3, request.top_k * 24));
    candidates = scored.slice(0, finalLimit);
    authorized = candidates.filter((item) => canAccessDocument(user, item.document).allowed);
  }

  const filteredByAcl = candidates.length - authorized.length;
  const hits = authorized.slice(0, request.top_k).map((item) => toHit(item, user, tokens));
  const latencyMs = Math.max(1, Math.round(performance.now() - started));
  const failClosed = !user;
  const debug: QueryDebug = {
    query_id,
    current_user: request.user_id,
    query: request.query,
    mode: 'static_browser_planner_acl',
    top_k: request.top_k,
    project_filters: request.project_ids,
    document_type_filters: request.document_types,
    total_chunks: data.documents.length,
    candidates_before_acl: candidates.length,
    filtered_by_acl: filteredByAcl,
    candidates_after_acl: authorized.length,
    returned_hits: hits.length,
    latency_ms: latencyMs,
    fallback_triggered: fallbackTriggered,
    final_candidate_limit: finalLimit,
    retrieval_explanation: failClosed
      ? `Unknown user failed closed. ACL summary: ${aclSummary(user)}.`
      : `Browser static planner scored keyword and hash-vector candidates, applied request filters, then enforced ACL (${aclSummary(
          user,
        )}). This visualizes the C++ gateway product shape and is not a replacement for the real backend.`,
  };

  return {
    ok: true,
    query_id,
    mode: 'static_browser_planner_acl',
    hits,
    debug,
    fallback_triggered: fallbackTriggered,
    final_candidate_limit: finalLimit,
  };
}
