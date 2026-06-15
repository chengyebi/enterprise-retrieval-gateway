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
    setError('访问码不正确');
  }

  return (
    <main className="gate-shell">
      <form className="gate-card" onSubmit={submit}>
        <div className="eyebrow">GitHub Pages 静态演示</div>
        <h1>企业知识库检索网关演示</h1>
        <p>C++17 企业知识库检索网关的静态可视化演示</p>
        <label htmlFor="access-code">访问码</label>
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
          进入演示
        </button>
        <p className="small-note">
          默认访问码：<code>{ACCESS_CODE}</code>。这是前端演示门禁，不是生产级安全认证。
        </p>
      </form>
    </main>
  );
}

export { ACCESS_CODE };
