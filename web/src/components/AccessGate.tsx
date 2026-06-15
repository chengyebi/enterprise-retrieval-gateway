import { FormEvent, useState } from 'react';

const ACCESS_CODE = 'erg-demo-local';

interface AccessGateProps {
  onGranted: () => void;
}

export function AccessGate({ onGranted }: AccessGateProps) {
  const [value, setValue] = useState('');
  const [error, setError] = useState('');

  function submit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (value.trim() === ACCESS_CODE) {
      window.localStorage.setItem('erg_demo_access_granted', 'true');
      onGranted();
      return;
    }
    setError('Access code 不正确');
  }

  return (
    <main className="gate-shell">
      <form className="gate-card" onSubmit={submit}>
        <div className="eyebrow">GitHub Pages Static Demo</div>
        <h1>Enterprise Retrieval Gateway Demo</h1>
        <p>C++17 企业知识库检索网关的静态可视化 Demo</p>
        <label htmlFor="access-code">Access code</label>
        <input
          id="access-code"
          type="password"
          value={value}
          onChange={(event) => setValue(event.target.value)}
          autoFocus
          placeholder="erg-demo-local"
        />
        {error && <div className="form-error">{error}</div>}
        <button type="submit" className="primary-button">
          进入 Demo
        </button>
        <p className="small-note">
          默认 access code: <code>{ACCESS_CODE}</code>。这是前端演示门禁，不是生产级安全认证。
        </p>
      </form>
    </main>
  );
}

export { ACCESS_CODE };
