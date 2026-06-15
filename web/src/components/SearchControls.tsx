import type { ConnectionState, DemoUser, SearchMode } from '../types';

interface SearchControlsProps {
  query: string;
  users: DemoUser[];
  selectedUserId: string;
  topK: number;
  projects: string[];
  selectedProjects: string[];
  documentTypes: string[];
  selectedDocumentTypes: string[];
  mode: SearchMode;
  backendUrl: string;
  examples: string[];
  isLoading: boolean;
  connection: ConnectionState;
  onQueryChange: (value: string) => void;
  onUserChange: (value: string) => void;
  onTopKChange: (value: number) => void;
  onProjectToggle: (value: string) => void;
  onDocumentTypeToggle: (value: string) => void;
  onModeChange: (value: SearchMode) => void;
  onBackendUrlChange: (value: string) => void;
  onSearch: () => void;
  onTestConnection: () => void;
}

function CheckboxGroup({
  label,
  values,
  selected,
  onToggle,
}: {
  label: string;
  values: string[];
  selected: string[];
  onToggle: (value: string) => void;
}) {
  return (
    <section className="control-block">
      <div className="control-label">{label}</div>
      <div className="chip-grid">
        {values.map((value) => (
          <label key={value} className={`chip ${selected.includes(value) ? 'selected' : ''}`}>
            <input
              type="checkbox"
              checked={selected.includes(value)}
              onChange={() => onToggle(value)}
            />
            <span>{value}</span>
          </label>
        ))}
      </div>
    </section>
  );
}

export function SearchControls(props: SearchControlsProps) {
  const {
    query,
    users,
    selectedUserId,
    topK,
    projects,
    selectedProjects,
    documentTypes,
    selectedDocumentTypes,
    mode,
    backendUrl,
    examples,
    isLoading,
    connection,
    onQueryChange,
    onUserChange,
    onTopKChange,
    onProjectToggle,
    onDocumentTypeToggle,
    onModeChange,
    onBackendUrlChange,
    onSearch,
    onTestConnection,
  } = props;

  return (
    <aside className="control-panel">
      <section className="control-block">
        <label htmlFor="query" className="control-label">
          Query
        </label>
        <textarea
          id="query"
          value={query}
          onChange={(event) => onQueryChange(event.target.value)}
          rows={3}
          placeholder="E1027 payment_timeout"
        />
        <div className="example-row">
          {examples.map((example) => (
            <button key={example} type="button" className="ghost-button" onClick={() => onQueryChange(example)}>
              {example}
            </button>
          ))}
        </div>
      </section>

      <div className="control-grid">
        <section className="control-block">
          <label htmlFor="user" className="control-label">
            User
          </label>
          <select id="user" value={selectedUserId} onChange={(event) => onUserChange(event.target.value)}>
            {users.map((user) => (
              <option key={user.user_id} value={user.user_id}>
                {user.user_id}
              </option>
            ))}
            <option value="unknown-user">unknown-user</option>
          </select>
        </section>

        <section className="control-block">
          <label htmlFor="top-k" className="control-label">
            top_k
          </label>
          <select id="top-k" value={topK} onChange={(event) => onTopKChange(Number(event.target.value))}>
            {[3, 5, 10, 20].map((value) => (
              <option key={value} value={value}>
                {value}
              </option>
            ))}
          </select>
        </section>
      </div>

      <section className="control-block">
        <div className="control-label">Retrieval mode</div>
        <div className="segmented-control">
          <button
            type="button"
            className={mode === 'static' ? 'selected' : ''}
            onClick={() => onModeChange('static')}
          >
            Static Browser Demo
          </button>
          <button
            type="button"
            className={mode === 'local' ? 'selected' : ''}
            onClick={() => onModeChange('local')}
          >
            Local C++ Gateway
          </button>
        </div>
      </section>

      {mode === 'local' && (
        <section className="control-block local-box">
          <label htmlFor="backend-url" className="control-label">
            Backend URL
          </label>
          <input
            id="backend-url"
            value={backendUrl}
            onChange={(event) => onBackendUrlChange(event.target.value)}
          />
          <div className="connection-row">
            <button type="button" className="secondary-button" onClick={onTestConnection}>
              Test Connection
            </button>
            <span className={`status-pill ${connection.status}`}>{connection.message || connection.status}</span>
          </div>
        </section>
      )}

      <CheckboxGroup
        label="Project filters"
        values={projects}
        selected={selectedProjects}
        onToggle={onProjectToggle}
      />
      <CheckboxGroup
        label="Document type filters"
        values={documentTypes}
        selected={selectedDocumentTypes}
        onToggle={onDocumentTypeToggle}
      />

      <button type="button" className="primary-button search-button" disabled={isLoading} onClick={onSearch}>
        {isLoading ? 'Searching...' : 'Search'}
      </button>
    </aside>
  );
}
