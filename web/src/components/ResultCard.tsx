import type { SearchHit } from '../types';
import { compactNumber } from '../lib/tokenize';

interface ResultCardProps {
  hit: SearchHit;
  index: number;
}

function sourceLabel(source?: string): string {
  if (!source) {
    return '网关';
  }
  const labels: Record<string, string> = {
    'static-browser': '静态浏览器',
    'local-cpp-gateway': '本地 C++ 网关',
    'supabase-postgres-rls': 'Supabase RLS',
    hybrid: '混合检索',
    keyword: '关键词检索',
    vector: '向量检索',
    gateway: '网关',
  };
  return labels[source] ?? source;
}

export function ResultCard({ hit, index }: ResultCardProps) {
  return (
    <article className="result-card">
      <div className="result-header">
        <div>
          <div className="result-rank">#{index + 1}</div>
          <h3>{hit.title || hit.document_id}</h3>
        </div>
        <div className="score-box">
          <span>分数</span>
          <strong>{compactNumber(hit.score ?? hit.fusion_score ?? 0)}</strong>
        </div>
      </div>
      <p className="snippet">{hit.snippet}</p>
      <div className="metadata-grid">
        <div>
          <span>文档 ID</span>
          <strong>{hit.document_id}</strong>
        </div>
        <div>
          <span>分块 ID</span>
          <strong>{hit.chunk_id}</strong>
        </div>
        <div>
          <span>文档类型</span>
          <strong>{hit.document_type}</strong>
        </div>
        <div>
          <span>项目</span>
          <strong>{hit.project_id}</strong>
        </div>
        <div>
          <span>部门</span>
          <strong>{hit.department}</strong>
        </div>
        <div>
          <span>可见用户组</span>
          <strong>{hit.allowed_groups.length ? hit.allowed_groups.join(', ') : '由网关执行 ACL'}</strong>
        </div>
      </div>
      <div className="reason-line">
        <span>当前用户可见原因</span>
        <strong>{hit.visibility_reason}</strong>
      </div>
      <div className="score-line">
        <span>关键词分 {compactNumber(hit.lexical_score ?? 0)}</span>
        <span>语义分 {compactNumber(hit.semantic_score ?? 0)}</span>
        <span>来源 {sourceLabel(hit.source)}</span>
      </div>
    </article>
  );
}
