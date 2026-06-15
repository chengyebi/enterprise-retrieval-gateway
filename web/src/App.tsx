import { useEffect, useMemo, useState } from 'react';
import { searchLocalGateway, fetchQueryDebug, healthCheck } from './api/localGateway';
import { AboutPanel } from './components/AboutPanel';
import { AccessGate } from './components/AccessGate';
import { DebugPanel } from './components/DebugPanel';
import { MetricsPanel } from './components/MetricsPanel';
import { ResultCard } from './components/ResultCard';
import { SearchControls } from './components/SearchControls';
import { canAccessDocument, findUser } from './lib/acl';
import { loadDemoData, documentIndexByChunk } from './lib/data';
import { clearMetrics, loadMetrics, recordQuery } from './lib/metrics';
import { staticSearch } from './lib/staticSearch';
import type {
  AppTab,
  ConnectionState,
  DemoData,
  DemoDocument,
  QueryDebug,
  SearchHit,
  SearchMode,
  SearchRequest,
  SessionMetrics,
} from './types';

const DEFAULT_BACKEND_URL = 'http://localhost:8080';
const ACCESS_STORAGE_KEY = 'erg_demo_access_granted';

function makeLocalFailureDebug(
  request: SearchRequest,
  latencyMs: number,
  message: string,
  totalChunks: number,
): QueryDebug {
  return {
    query_id: `local-failed-${Date.now().toString(36)}`,
    current_user: request.user_id,
    query: request.query,
    mode: 'local_cpp_gateway',
    top_k: request.top_k,
    project_filters: request.project_ids,
    document_type_filters: request.document_types,
    total_chunks: totalChunks,
    candidates_before_acl: 0,
    filtered_by_acl: 0,
    candidates_after_acl: 0,
    returned_hits: 0,
    latency_ms: latencyMs,
    fallback_triggered: false,
    final_candidate_limit: 0,
    retrieval_explanation:
      '本地 C++ 网关未启动，请运行 ./build/ergateway serve --backend memory --port 8080。',
    error: message,
  };
}

function enrichLocalHit(
  raw: Partial<SearchHit>,
  documentsByChunk: Map<string, DemoDocument>,
  userId: string,
  data: DemoData,
): SearchHit {
  const document = raw.chunk_id ? documentsByChunk.get(raw.chunk_id) : undefined;
  const user = findUser(data.users, userId);
  const decision = document ? canAccessDocument(user, document) : { allowed: true, reason: 'authorized by local gateway' };
  const score = raw.fusion_score ?? raw.score ?? raw.lexical_score ?? raw.semantic_score ?? 0;
  return {
    tenant_id: document?.tenant_id ?? raw.tenant_id,
    document_id: raw.document_id ?? document?.document_id ?? 'unknown',
    chunk_id: raw.chunk_id ?? document?.chunk_id ?? 'unknown',
    title: raw.title ?? document?.title ?? raw.document_id ?? 'Untitled',
    snippet: raw.snippet ?? document?.snippet ?? document?.text?.slice(0, 220) ?? '',
    department: raw.department ?? document?.department ?? 'unavailable',
    project_id: raw.project_id ?? document?.project_id ?? 'unavailable',
    allowed_groups: raw.allowed_groups ?? document?.allowed_groups ?? [],
    document_type: raw.document_type ?? document?.document_type ?? 'unavailable',
    score,
    lexical_score: raw.lexical_score,
    semantic_score: raw.semantic_score,
    fusion_score: raw.fusion_score ?? score,
    source: raw.source ?? 'local-cpp-gateway',
    visibility_reason: decision.reason,
  };
}

function mapLocalDebug(
  request: SearchRequest,
  responseQueryId: string,
  responseMode: string,
  fallbackTriggered: boolean,
  finalCandidateLimit: number,
  returnedHits: number,
  totalChunks: number,
  measuredLatency: number,
  trace: Partial<QueryDebug> & Record<string, unknown>,
): QueryDebug {
  const candidateLimit = Number(trace.candidate_limit ?? finalCandidateLimit ?? 0);
  const estimatedAuthorized = Number(trace.estimated_authorized_candidates ?? returnedHits);
  return {
    query_id: responseQueryId,
    current_user: request.user_id,
    query: request.query,
    mode: responseMode || String(trace.mode ?? 'local_cpp_gateway'),
    top_k: request.top_k,
    project_filters: request.project_ids,
    document_type_filters: request.document_types,
    total_chunks: totalChunks,
    candidates_before_acl: candidateLimit,
    filtered_by_acl: Math.max(0, candidateLimit - estimatedAuthorized),
    candidates_after_acl: estimatedAuthorized,
    returned_hits: returnedHits,
    latency_ms: Number(trace.total_latency_ms ?? measuredLatency),
    fallback_triggered: Boolean(trace.fallback_triggered ?? fallbackTriggered),
    final_candidate_limit: Number(trace.candidate_limit ?? finalCandidateLimit),
    retrieval_explanation: `Local C++ Gateway response. ACL summary: ${
      String(trace.acl_filter_summary ?? 'reported by gateway')
    }.`,
  };
}

function toggleValue(values: string[], value: string): string[] {
  return values.includes(value) ? values.filter((item) => item !== value) : [...values, value];
}

function selectedUserSummary(data: DemoData | null, userId: string): string {
  if (!data) {
    return 'loading users';
  }
  const user = findUser(data.users, userId);
  if (!user) {
    return 'unknown user -> fail-closed';
  }
  return `${user.department}; groups=${user.groups.join(', ')}; projects=${user.project_ids.join(', ')}${
    user.is_admin ? '; admin=true' : ''
  }`;
}

export default function App() {
  const [accessGranted, setAccessGranted] = useState(
    () => window.localStorage.getItem(ACCESS_STORAGE_KEY) === 'true',
  );
  const [data, setData] = useState<DemoData | null>(null);
  const [dataError, setDataError] = useState('');
  const [query, setQuery] = useState('E1027 payment_timeout');
  const [selectedUserId, setSelectedUserId] = useState('backend-user-01');
  const [topK, setTopK] = useState(5);
  const [selectedProjects, setSelectedProjects] = useState<string[]>([]);
  const [selectedDocumentTypes, setSelectedDocumentTypes] = useState<string[]>([]);
  const [mode, setMode] = useState<SearchMode>('static');
  const [backendUrl, setBackendUrl] = useState(
    () => window.localStorage.getItem('erg_demo_backend_url') || DEFAULT_BACKEND_URL,
  );
  const [connection, setConnection] = useState<ConnectionState>({ status: 'idle', message: 'not tested' });
  const [isLoading, setIsLoading] = useState(false);
  const [activeTab, setActiveTab] = useState<AppTab>('search');
  const [results, setResults] = useState<SearchHit[]>([]);
  const [debug, setDebug] = useState<QueryDebug | null>(null);
  const [searchError, setSearchError] = useState('');
  const [metrics, setMetrics] = useState<SessionMetrics>(() => loadMetrics());

  useEffect(() => {
    loadDemoData()
      .then((loaded) => {
        setData(loaded);
        if (!loaded.users.some((user) => user.user_id === 'backend-user-01') && loaded.users[0]) {
          setSelectedUserId(loaded.users[0].user_id);
        }
      })
      .catch((error) => setDataError(String(error)));
  }, []);

  useEffect(() => {
    window.localStorage.setItem('erg_demo_backend_url', backendUrl);
  }, [backendUrl]);

  const documentsByChunk = useMemo(() => documentIndexByChunk(data?.documents ?? []), [data]);
  const examples = data?.example_queries?.length
    ? data.example_queries
    : [
        'E1027 payment_timeout',
        'checkout API v3 dependency timeout',
        'downstream payment interface no response',
        'OpenSearch bulk 429',
        'ANN ACL filtering too few hits',
      ];

  function logout() {
    window.localStorage.removeItem(ACCESS_STORAGE_KEY);
    setAccessGranted(false);
  }

  async function testConnection() {
    setConnection({ status: 'checking', message: 'checking' });
    try {
      await healthCheck(backendUrl);
      setConnection({ status: 'healthy', message: 'healthy' });
    } catch (error) {
      setConnection({
        status: 'failed',
        message: `failed: ${error instanceof Error ? error.message : String(error)}`,
      });
    }
  }

  async function runSearch() {
    if (!data) {
      return;
    }
    const trimmedQuery = query.trim();
    if (!trimmedQuery) {
      setSearchError('Query cannot be empty.');
      setResults([]);
      return;
    }

    const request: SearchRequest = {
      user_id: selectedUserId,
      query: trimmedQuery,
      top_k: topK,
      project_ids: selectedProjects,
      document_types: selectedDocumentTypes,
      enable_keyword_search: true,
      enable_vector_search: true,
    };

    setIsLoading(true);
    setSearchError('');
    const started = performance.now();

    if (mode === 'static') {
      const response = staticSearch(data, request);
      setResults(response.hits);
      setDebug(response.debug);
      setActiveTab('search');
      setMetrics((current) =>
        recordQuery(current, {
          ok: response.ok,
          mode,
          latency_ms: response.debug.latency_ms,
          returned_hits: response.hits.length,
          acl_filtered: response.debug.filtered_by_acl,
          fallback_triggered: response.fallback_triggered,
        }),
      );
      setIsLoading(false);
      return;
    }

    try {
      const response = await searchLocalGateway(backendUrl, request);
      const hits = (response.hits ?? []).map((hit) => enrichLocalHit(hit, documentsByChunk, selectedUserId, data));
      let localDebug: QueryDebug;
      try {
        const trace = await fetchQueryDebug(backendUrl, response.query_id);
        localDebug = mapLocalDebug(
          request,
          response.query_id,
          response.mode,
          response.fallback_triggered,
          response.final_candidate_limit,
          hits.length,
          data.documents.length,
          Math.round(performance.now() - started),
          trace as Partial<QueryDebug> & Record<string, unknown>,
        );
      } catch (error) {
        localDebug = {
          query_id: response.query_id,
          current_user: request.user_id,
          query: request.query,
          mode: response.mode || 'local_cpp_gateway',
          top_k: request.top_k,
          project_filters: request.project_ids,
          document_type_filters: request.document_types,
          total_chunks: data.documents.length,
          candidates_before_acl: response.final_candidate_limit,
          filtered_by_acl: 0,
          candidates_after_acl: hits.length,
          returned_hits: hits.length,
          latency_ms: Math.round(performance.now() - started),
          fallback_triggered: response.fallback_triggered,
          final_candidate_limit: response.final_candidate_limit,
          retrieval_explanation: 'Local C++ Gateway search succeeded, but debug trace fetch failed.',
          error: error instanceof Error ? error.message : String(error),
        };
      }
      setResults(hits);
      setDebug(localDebug);
      setActiveTab('search');
      setMetrics((current) =>
        recordQuery(current, {
          ok: response.ok,
          mode,
          latency_ms: localDebug.latency_ms,
          returned_hits: hits.length,
          acl_filtered: localDebug.filtered_by_acl,
          fallback_triggered: localDebug.fallback_triggered,
        }),
      );
    } catch (error) {
      const latency = Math.round(performance.now() - started);
      const message = `本地 C++ 网关未启动，请运行 ./build/ergateway serve --backend memory --port 8080。${
        error instanceof Error ? ` (${error.message})` : ''
      }`;
      const failureDebug = makeLocalFailureDebug(request, latency, message, data.documents.length);
      setResults([]);
      setDebug(failureDebug);
      setSearchError(message);
      setActiveTab('debug');
      setMetrics((current) =>
        recordQuery(current, {
          ok: false,
          mode,
          latency_ms: latency,
          returned_hits: 0,
          acl_filtered: 0,
          fallback_triggered: false,
        }),
      );
    } finally {
      setIsLoading(false);
    }
  }

  if (!accessGranted) {
    return <AccessGate onGranted={() => setAccessGranted(true)} />;
  }

  return (
    <main className="app-shell">
      <header className="app-header">
        <div>
          <div className="eyebrow">C++17 Enterprise Search Gateway</div>
          <h1>Enterprise Retrieval Gateway Demo</h1>
          <p>C++17 企业知识库检索网关的静态可视化 Demo</p>
        </div>
        <div className="header-actions">
          <span className="small-note">前端演示门禁，不是生产级安全认证。</span>
          <button type="button" className="secondary-button" onClick={logout}>
            退出访问
          </button>
        </div>
      </header>

      <nav className="tabs" aria-label="Demo tabs">
        {(['search', 'debug', 'metrics', 'about'] as AppTab[]).map((tab) => (
          <button
            key={tab}
            type="button"
            className={activeTab === tab ? 'selected' : ''}
            onClick={() => setActiveTab(tab)}
          >
            {tab === 'search' ? 'Search' : tab === 'debug' ? 'Debug' : tab === 'metrics' ? 'Metrics' : 'About'}
          </button>
        ))}
      </nav>

      {dataError && <div className="error-banner">{dataError}</div>}

      {activeTab === 'search' && (
        <div className="search-layout">
          <SearchControls
            query={query}
            users={data?.users ?? []}
            selectedUserId={selectedUserId}
            topK={topK}
            projects={data?.projects ?? []}
            selectedProjects={selectedProjects}
            documentTypes={data?.document_types ?? []}
            selectedDocumentTypes={selectedDocumentTypes}
            mode={mode}
            backendUrl={backendUrl}
            examples={examples}
            isLoading={isLoading}
            connection={connection}
            onQueryChange={setQuery}
            onUserChange={setSelectedUserId}
            onTopKChange={setTopK}
            onProjectToggle={(value) => setSelectedProjects((current) => toggleValue(current, value))}
            onDocumentTypeToggle={(value) => setSelectedDocumentTypes((current) => toggleValue(current, value))}
            onModeChange={setMode}
            onBackendUrlChange={setBackendUrl}
            onSearch={runSearch}
            onTestConnection={testConnection}
          />

          <section className="results-panel">
            <div className="panel-toolbar">
              <div>
                <h2>Results</h2>
                <p>{selectedUserSummary(data, selectedUserId)}</p>
              </div>
              <span className="status-pill healthy">{results.length} hits</span>
            </div>
            {searchError && <div className="error-banner">{searchError}</div>}
            {!data && !dataError && <div className="empty-state">Loading demo data...</div>}
            {data && results.length === 0 && !searchError && (
              <div className="empty-state">
                No visible hits. Try another user or open Debug to inspect ACL filtering counts.
              </div>
            )}
            <div className="result-list">
              {results.map((hit, index) => (
                <ResultCard key={`${hit.chunk_id}-${index}`} hit={hit} index={index} />
              ))}
            </div>
          </section>
        </div>
      )}

      {activeTab === 'debug' && <DebugPanel debug={debug} />}
      {activeTab === 'metrics' && (
        <MetricsPanel
          metrics={metrics}
          onReset={() => {
            setMetrics(clearMetrics());
          }}
        />
      )}
      {activeTab === 'about' && <AboutPanel />}
    </main>
  );
}
