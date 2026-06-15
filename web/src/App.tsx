import { useEffect, useMemo, useState } from 'react';
import type { Session } from '@supabase/supabase-js';
import { searchLocalGateway, fetchQueryDebug, healthCheck } from './api/localGateway';
import { getSupabaseClient, supabaseConfig } from './api/supabaseClient';
import { fetchCurrentSupabaseProfile, searchSupabaseDocuments } from './api/supabaseSearch';
import { AboutPanel } from './components/AboutPanel';
import { AccessGate } from './components/AccessGate';
import { DebugPanel } from './components/DebugPanel';
import { MetricsPanel } from './components/MetricsPanel';
import { ResultCard } from './components/ResultCard';
import { SearchControls } from './components/SearchControls';
import { SupabaseAuthPanel } from './components/SupabaseAuthPanel';
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
  SupabaseAclProfile,
} from './types';

const DEFAULT_BACKEND_URL = 'http://localhost:8080';
const ACCESS_STORAGE_KEY = 'erg_demo_access_granted';

function initialSearchMode(): SearchMode {
  const params = new URLSearchParams(window.location.search);
  return params.get('mode') === 'supabase' ? 'supabase' : 'static';
}

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

function makeSupabaseFailureDebug(
  request: SearchRequest,
  latencyMs: number,
  message: string,
  totalChunks: number,
): QueryDebug {
  return {
    query_id: `supabase-failed-${Date.now().toString(36)}`,
    current_user: request.user_id,
    query: request.query,
    mode: 'supabase_postgres_rls',
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
    retrieval_explanation: 'Supabase 全栈模式需要可用的 Auth session、Postgres schema 和 RLS 函数。',
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
  const decision = document ? canAccessDocument(user, document) : { allowed: true, reason: '本地网关已完成授权过滤' };
  const score = raw.fusion_score ?? raw.score ?? raw.lexical_score ?? raw.semantic_score ?? 0;
  return {
    tenant_id: document?.tenant_id ?? raw.tenant_id,
    document_id: raw.document_id ?? document?.document_id ?? '未知文档',
    chunk_id: raw.chunk_id ?? document?.chunk_id ?? '未知分块',
    title: raw.title ?? document?.title ?? raw.document_id ?? '未命名文档',
    snippet: raw.snippet ?? document?.snippet ?? document?.text?.slice(0, 220) ?? '',
    department: raw.department ?? document?.department ?? '无可用信息',
    project_id: raw.project_id ?? document?.project_id ?? '无可用信息',
    allowed_groups: raw.allowed_groups ?? document?.allowed_groups ?? [],
    document_type: raw.document_type ?? document?.document_type ?? '无可用信息',
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
    retrieval_explanation: `本地 C++ 网关已返回结果。ACL 摘要：${
      String(trace.acl_filter_summary ?? '由网关报告')
    }。`,
  };
}

function toggleValue(values: string[], value: string): string[] {
  return values.includes(value) ? values.filter((item) => item !== value) : [...values, value];
}

function selectedUserSummary(data: DemoData | null, userId: string): string {
  if (!data) {
    return '正在加载用户';
  }
  const user = findUser(data.users, userId);
  if (!user) {
    return '未知用户 -> 失败关闭，拒绝返回';
  }
  return `部门=${user.department}；用户组=${user.groups.join(', ')}；项目=${user.project_ids.join(', ')}${
    user.is_admin ? '；管理员=true' : ''
  }`;
}

function selectedSupabaseSummary(profile: SupabaseAclProfile | null, session: Session | null): string {
  if (!session) {
    return 'Supabase 未登录';
  }
  if (!profile) {
    return '已登录但未绑定 ACL 用户 -> RLS 默认不返回文档';
  }
  return `ACL 用户=${profile.acl_user_id}；租户=${profile.tenant_id}；部门=${profile.department}；用户组=${profile.groups.join(
    ', ',
  )}；项目=${profile.project_ids.join(', ')}${profile.is_admin ? '；管理员=true' : ''}`;
}

export default function App() {
  const defaultMode = initialSearchMode();
  const [accessGranted, setAccessGranted] = useState(
    () => defaultMode === 'supabase' || window.localStorage.getItem(ACCESS_STORAGE_KEY) === 'true',
  );
  const [data, setData] = useState<DemoData | null>(null);
  const [dataError, setDataError] = useState('');
  const [query, setQuery] = useState('E1027 payment_timeout');
  const [selectedUserId, setSelectedUserId] = useState('backend-user-01');
  const [topK, setTopK] = useState(5);
  const [selectedProjects, setSelectedProjects] = useState<string[]>([]);
  const [selectedDocumentTypes, setSelectedDocumentTypes] = useState<string[]>([]);
  const [mode, setMode] = useState<SearchMode>(defaultMode);
  const [backendUrl, setBackendUrl] = useState(
    () => window.localStorage.getItem('erg_demo_backend_url') || DEFAULT_BACKEND_URL,
  );
  const [connection, setConnection] = useState<ConnectionState>({ status: 'idle', message: '未测试' });
  const [isLoading, setIsLoading] = useState(false);
  const [activeTab, setActiveTab] = useState<AppTab>('search');
  const [results, setResults] = useState<SearchHit[]>([]);
  const [debug, setDebug] = useState<QueryDebug | null>(null);
  const [searchError, setSearchError] = useState('');
  const [metrics, setMetrics] = useState<SessionMetrics>(() => loadMetrics());
  const [supabaseSession, setSupabaseSession] = useState<Session | null>(null);
  const [supabaseProfile, setSupabaseProfile] = useState<SupabaseAclProfile | null>(null);
  const [supabaseAuthLoading, setSupabaseAuthLoading] = useState(false);
  const [supabaseAuthError, setSupabaseAuthError] = useState('');
  const [supabaseAuthMessage, setSupabaseAuthMessage] = useState('');

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

  useEffect(() => {
    const supabase = getSupabaseClient();
    if (!supabase) {
      return undefined;
    }

    let mounted = true;
    supabase.auth.getSession().then(({ data: sessionData, error }) => {
      if (!mounted) {
        return;
      }
      if (error) {
        setSupabaseAuthError(error.message);
      }
      setSupabaseSession(sessionData.session);
    });

    const {
      data: { subscription },
    } = supabase.auth.onAuthStateChange((_event, session) => {
      if (!mounted) {
        return;
      }
      setSupabaseSession(session);
      if (!session) {
        setSupabaseProfile(null);
      }
    });

    return () => {
      mounted = false;
      subscription.unsubscribe();
    };
  }, []);

  useEffect(() => {
    if (!supabaseSession) {
      return;
    }
    void refreshSupabaseProfile();
  }, [supabaseSession?.user.id]);

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

  async function refreshSupabaseProfile() {
    if (!supabaseSession) {
      setSupabaseProfile(null);
      return;
    }

    setSupabaseAuthLoading(true);
    setSupabaseAuthError('');
    try {
      const profile = await fetchCurrentSupabaseProfile();
      setSupabaseProfile(profile);
      if (profile) {
        setSelectedUserId(profile.acl_user_id);
        setSupabaseAuthMessage('权限 profile 已加载。');
      } else {
        setSupabaseAuthMessage('已登录，但管理员尚未绑定 ACL 用户。');
      }
    } catch (error) {
      setSupabaseProfile(null);
      setSupabaseAuthError(error instanceof Error ? error.message : String(error));
    } finally {
      setSupabaseAuthLoading(false);
    }
  }

  async function signInSupabase(email: string, password: string) {
    const supabase = getSupabaseClient();
    if (!supabase) {
      setSupabaseAuthError('Supabase 未配置。');
      return;
    }

    setSupabaseAuthLoading(true);
    setSupabaseAuthError('');
    setSupabaseAuthMessage('');
    try {
      const { error } = await supabase.auth.signInWithPassword({ email, password });
      if (error) {
        throw error;
      }
      setSupabaseAuthMessage('登录成功，正在读取 ACL profile。');
    } catch (error) {
      setSupabaseAuthError(error instanceof Error ? error.message : String(error));
    } finally {
      setSupabaseAuthLoading(false);
    }
  }

  async function signUpSupabase(email: string, password: string) {
    const supabase = getSupabaseClient();
    if (!supabase) {
      setSupabaseAuthError('Supabase 未配置。');
      return;
    }

    setSupabaseAuthLoading(true);
    setSupabaseAuthError('');
    setSupabaseAuthMessage('');
    try {
      const { data: signupData, error } = await supabase.auth.signUp({ email, password });
      if (error) {
        throw error;
      }
      setSupabaseAuthMessage(
        signupData.session
          ? '注册成功。管理员仍需在数据库中绑定 ACL 用户后才会返回文档。'
          : '注册已提交。如果项目开启邮箱确认，请先完成确认；管理员仍需绑定 ACL 用户。',
      );
    } catch (error) {
      setSupabaseAuthError(error instanceof Error ? error.message : String(error));
    } finally {
      setSupabaseAuthLoading(false);
    }
  }

  async function signOutSupabase() {
    const supabase = getSupabaseClient();
    if (!supabase) {
      return;
    }
    setSupabaseAuthLoading(true);
    setSupabaseAuthError('');
    try {
      const { error } = await supabase.auth.signOut();
      if (error) {
        throw error;
      }
      setSupabaseSession(null);
      setSupabaseProfile(null);
      setSupabaseAuthMessage('已退出 Supabase。');
    } catch (error) {
      setSupabaseAuthError(error instanceof Error ? error.message : String(error));
    } finally {
      setSupabaseAuthLoading(false);
    }
  }

  async function testConnection() {
    setConnection({ status: 'checking', message: '检测中' });
    try {
      await healthCheck(backendUrl);
      setConnection({ status: 'healthy', message: '连接正常' });
    } catch (error) {
      setConnection({
        status: 'failed',
        message: `连接失败：${error instanceof Error ? error.message : String(error)}`,
      });
    }
  }

  async function runSearch() {
    if (!data) {
      return;
    }
    const trimmedQuery = query.trim();
    if (!trimmedQuery) {
      setSearchError('查询不能为空。');
      setResults([]);
      return;
    }

    const request: SearchRequest = {
      user_id:
        mode === 'supabase'
          ? supabaseProfile?.acl_user_id ?? `auth:${supabaseSession?.user.id ?? 'anonymous'}`
          : selectedUserId,
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

    if (mode === 'supabase') {
      if (!supabaseConfig.isConfigured || !supabaseSession) {
        const message = !supabaseConfig.isConfigured ? 'Supabase 未配置。' : '请先登录 Supabase。';
        const latency = Math.round(performance.now() - started);
        setResults([]);
        setDebug(makeSupabaseFailureDebug(request, latency, message, data.documents.length));
        setSearchError(message);
        setActiveTab('debug');
        setIsLoading(false);
        return;
      }

      try {
        const response = await searchSupabaseDocuments(request, supabaseProfile, data.documents.length);
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
      } catch (error) {
        const latency = Math.round(performance.now() - started);
        const message = `Supabase 查询失败：${error instanceof Error ? error.message : String(error)}`;
        setResults([]);
        setDebug(makeSupabaseFailureDebug(request, latency, message, data.documents.length));
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
          retrieval_explanation: '本地 C++ 网关搜索成功，但调试记录拉取失败。',
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

  const supabaseUserLabel = !supabaseConfig.isConfigured
    ? 'Supabase 未配置'
    : supabaseProfile?.acl_user_id ?? (supabaseSession ? '未绑定 ACL 用户' : '未登录');
  const canSearch = mode !== 'supabase' || (supabaseConfig.isConfigured && Boolean(supabaseSession));

  return (
    <main className="app-shell">
      <header className="app-header">
        <div>
          <div className="eyebrow">C++17 企业搜索网关</div>
          <h1>企业知识库检索网关演示</h1>
          <p>C++17 企业知识库检索网关的静态可视化演示</p>
        </div>
        <div className="header-actions">
          <span className="small-note">前端演示门禁，不是生产级安全认证。</span>
          <button type="button" className="secondary-button" onClick={logout}>
            退出访问
          </button>
        </div>
      </header>

      <nav className="tabs" aria-label="演示导航">
        {(['search', 'debug', 'metrics', 'about'] as AppTab[]).map((tab) => (
          <button
            key={tab}
            type="button"
            className={activeTab === tab ? 'selected' : ''}
            onClick={() => setActiveTab(tab)}
          >
            {tab === 'search' ? '搜索' : tab === 'debug' ? '调试' : tab === 'metrics' ? '指标' : '架构'}
          </button>
        ))}
      </nav>

      {dataError && <div className="error-banner">{dataError}</div>}

      {activeTab === 'search' && (
        <div className="search-layout">
          <div className="control-stack">
            {mode === 'supabase' && (
              <SupabaseAuthPanel
                configured={supabaseConfig.isConfigured}
                session={supabaseSession}
                profile={supabaseProfile}
                isLoading={supabaseAuthLoading}
                error={supabaseAuthError}
                message={supabaseAuthMessage}
                onSignIn={signInSupabase}
                onSignUp={signUpSupabase}
                onSignOut={signOutSupabase}
                onRefreshProfile={refreshSupabaseProfile}
              />
            )}
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
              userSelectDisabled={mode === 'supabase'}
              userLabel={supabaseUserLabel}
              canSearch={canSearch}
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
          </div>

          <section className="results-panel">
            <div className="panel-toolbar">
              <div>
                <h2>检索结果</h2>
                <p>
                  {mode === 'supabase'
                    ? selectedSupabaseSummary(supabaseProfile, supabaseSession)
                    : selectedUserSummary(data, selectedUserId)}
                </p>
              </div>
              <span className="status-pill healthy">{results.length} 条结果</span>
            </div>
            {searchError && <div className="error-banner">{searchError}</div>}
            {!data && !dataError && <div className="empty-state">正在加载演示数据...</div>}
            {data && results.length === 0 && !searchError && (
              <div className="empty-state">
                {mode === 'supabase'
                  ? '当前 Supabase 用户没有可见结果。未绑定 ACL 用户时这是 RLS 的默认安全结果；已绑定用户可以调整查询或过滤条件。'
                  : '当前用户没有可见结果。可以切换用户，或打开“调试”查看 ACL 过滤数量。'}
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
