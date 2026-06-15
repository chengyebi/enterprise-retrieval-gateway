import { FormEvent, useState } from 'react';
import type { Session } from '@supabase/supabase-js';
import type { SupabaseAclProfile } from '../types';

interface SupabaseAuthPanelProps {
  configured: boolean;
  session: Session | null;
  profile: SupabaseAclProfile | null;
  isLoading: boolean;
  error: string;
  message: string;
  onSignIn: (email: string, password: string) => Promise<void>;
  onSignUp: (email: string, password: string) => Promise<void>;
  onSignOut: () => Promise<void>;
  onRefreshProfile: () => Promise<void>;
}

export function SupabaseAuthPanel({
  configured,
  session,
  profile,
  isLoading,
  error,
  message,
  onSignIn,
  onSignUp,
  onSignOut,
  onRefreshProfile,
}: SupabaseAuthPanelProps) {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [authMode, setAuthMode] = useState<'login' | 'signup'>('login');

  async function submit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (authMode === 'login') {
      await onSignIn(email.trim(), password);
      return;
    }
    await onSignUp(email.trim(), password);
  }

  if (!configured) {
    return (
      <section className="supabase-box">
        <div className="control-label">Supabase Fullstack</div>
        <p className="small-note">
          未配置 <code>VITE_SUPABASE_URL</code> 和 <code>VITE_SUPABASE_ANON_KEY</code>，当前仍可使用静态演示和本地 C++ 网关。
        </p>
      </section>
    );
  }

  if (session) {
    return (
      <section className="supabase-box">
        <div className="connection-row">
          <span className="status-pill healthy">已登录</span>
          <button type="button" className="secondary-button" onClick={onRefreshProfile} disabled={isLoading}>
            刷新权限
          </button>
          <button type="button" className="secondary-button" onClick={onSignOut} disabled={isLoading}>
            退出 Supabase
          </button>
        </div>
        <p className="small-note">{session.user.email ?? session.user.id}</p>
        {profile ? (
          <div className="profile-summary">
            <span>ACL 用户</span>
            <strong>{profile.acl_user_id}</strong>
            <span>租户 / 部门</span>
            <strong>
              {profile.tenant_id} / {profile.department}
            </strong>
            <span>项目</span>
            <strong>{profile.project_ids.join(', ') || '无'}</strong>
            <span>用户组</span>
            <strong>{profile.groups.join(', ') || '无'}</strong>
          </div>
        ) : (
          <div className="empty-state">
            当前 Auth 用户尚未由管理员绑定到 ACL 用户，RLS 下默认查不到任何文档。
          </div>
        )}
        {message && <div className="info-banner">{message}</div>}
        {error && <div className="error-banner">{error}</div>}
      </section>
    );
  }

  return (
    <section className="supabase-box">
      <div className="control-label">Supabase 登录</div>
      <div className="segmented-control">
        <button
          type="button"
          className={authMode === 'login' ? 'selected' : ''}
          onClick={() => setAuthMode('login')}
        >
          登录
        </button>
        <button
          type="button"
          className={authMode === 'signup' ? 'selected' : ''}
          onClick={() => setAuthMode('signup')}
        >
          注册
        </button>
      </div>
      <form className="auth-form" onSubmit={submit}>
        <label htmlFor="supabase-email" className="control-label">
          邮箱
        </label>
        <input
          id="supabase-email"
          type="email"
          value={email}
          onChange={(event) => setEmail(event.target.value)}
          autoComplete="email"
          required
        />
        <label htmlFor="supabase-password" className="control-label">
          密码
        </label>
        <input
          id="supabase-password"
          type="password"
          value={password}
          onChange={(event) => setPassword(event.target.value)}
          autoComplete={authMode === 'login' ? 'current-password' : 'new-password'}
          minLength={6}
          required
        />
        <button type="submit" className="primary-button" disabled={isLoading}>
          {isLoading ? '处理中...' : authMode === 'login' ? '登录' : '注册'}
        </button>
      </form>
      {message && <div className="info-banner">{message}</div>}
      {error && <div className="error-banner">{error}</div>}
    </section>
  );
}
