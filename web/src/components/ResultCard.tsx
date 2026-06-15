import type { SearchHit } from '../types';
import { compactNumber } from '../lib/tokenize';

interface ResultCardProps {
  hit: SearchHit;
  index: number;
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
          <span>score</span>
          <strong>{compactNumber(hit.score ?? hit.fusion_score ?? 0)}</strong>
        </div>
      </div>
      <p className="snippet">{hit.snippet}</p>
      <div className="metadata-grid">
        <div>
          <span>document_id</span>
          <strong>{hit.document_id}</strong>
        </div>
        <div>
          <span>chunk_id</span>
          <strong>{hit.chunk_id}</strong>
        </div>
        <div>
          <span>document_type</span>
          <strong>{hit.document_type}</strong>
        </div>
        <div>
          <span>project_id</span>
          <strong>{hit.project_id}</strong>
        </div>
        <div>
          <span>department</span>
          <strong>{hit.department}</strong>
        </div>
        <div>
          <span>allowed_groups</span>
          <strong>{hit.allowed_groups.length ? hit.allowed_groups.join(', ') : 'ACL enforced by gateway'}</strong>
        </div>
      </div>
      <div className="reason-line">
        <span>Visible because</span>
        <strong>{hit.visibility_reason}</strong>
      </div>
      <div className="score-line">
        <span>lexical {compactNumber(hit.lexical_score ?? 0)}</span>
        <span>semantic {compactNumber(hit.semantic_score ?? 0)}</span>
        <span>source {hit.source || 'gateway'}</span>
      </div>
    </article>
  );
}
