export function AboutPanel() {
  return (
    <section className="about-panel">
      <div className="about-grid">
        <article>
          <h2>Project Scope</h2>
          <p>
            Enterprise Retrieval Gateway separates enterprise retrieval policy from the search backend. It keeps ACL
            enforcement, query planning, hybrid retrieval, debug traces, and metrics in the gateway layer.
          </p>
        </article>
        <article>
          <h2>Why ACL Matters</h2>
          <p>
            Enterprise knowledge bases mix engineering incidents, finance reports, contracts, HR policy, and security
            notes. Search must enforce tenant, department, project, and group rules before returning citations.
          </p>
        </article>
        <article>
          <h2>Why Planner Matters</h2>
          <p>
            Strict filters can shrink vector candidate sets. The gateway planner estimates filtered candidates, chooses
            keyword/vector/hybrid paths, and expands limits when authorized results are too sparse.
          </p>
        </article>
        <article>
          <h2>Backends</h2>
          <p>
            The memory backend is deterministic and runs without Docker. The OpenSearch backend validates the same
            gateway behavior against a local OpenSearch index and hybrid pipeline.
          </p>
        </article>
      </div>

      <div className="architecture-card">
        <h2>Architecture</h2>
        <div className="architecture-flow" aria-label="Demo architecture diagram">
          <div>Browser UI</div>
          <span>-&gt;</span>
          <div>Static Demo Engine / Local C++ Gateway</div>
          <span>-&gt;</span>
          <div>ACL Filter</div>
          <span>-&gt;</span>
          <div>Keyword / Vector / Hybrid Planner</div>
          <span>-&gt;</span>
          <div>Ranked Results / Debug / Metrics</div>
        </div>
      </div>

      <div className="runbook-card">
        <h2>Local C++ Gateway</h2>
        <pre>
          <code>{'make demo\n./build/ergateway serve --backend memory --port 8080'}</code>
        </pre>
        <p>
          Switch retrieval mode to Local C++ Gateway, keep the backend URL as <code>http://localhost:8080</code>, then
          test the connection. GitHub Pages mode remains static and free; it does not run a backend process.
        </p>
      </div>
    </section>
  );
}
