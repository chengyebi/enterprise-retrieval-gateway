import type { DemoDocument, DemoUser } from '../types';

export interface AclDecision {
  allowed: boolean;
  reason: string;
}

export function findUser(users: DemoUser[], userId: string): DemoUser | undefined {
  return users.find((user) => user.user_id === userId);
}

export function canAccessDocument(user: DemoUser | undefined, document: DemoDocument): AclDecision {
  if (!user) {
    return {
      allowed: false,
      reason: '未知用户，按失败关闭策略拒绝返回',
    };
  }

  if (document.tenant_id !== user.tenant_id) {
    return {
      allowed: false,
      reason: `租户不匹配 (${document.tenant_id} != ${user.tenant_id})`,
    };
  }

  if (user.is_admin) {
    return {
      allowed: true,
      reason: `同租户管理员 (${user.tenant_id})`,
    };
  }

  if (document.department !== user.department) {
    return {
      allowed: false,
      reason: `部门不匹配 (${document.department} != ${user.department})`,
    };
  }

  if (!user.project_ids.includes(document.project_id)) {
    return {
      allowed: false,
      reason: `用户无项目 ${document.project_id} 权限`,
    };
  }

  const groupMatch = document.allowed_groups.filter((group) => user.groups.includes(group));
  if (groupMatch.length === 0) {
    return {
      allowed: false,
      reason: '用户组与文档可见用户组无交集',
    };
  }

  return {
    allowed: true,
    reason: `租户、部门、项目和用户组均匹配 (${groupMatch.join(', ')})`,
  };
}

export function aclSummary(user: DemoUser | undefined): string {
  if (!user) {
    return '未知用户 -> 失败关闭，拒绝返回';
  }
  if (user.is_admin) {
    return `租户=${user.tenant_id}; 管理员=是; 允许访问同租户文档`;
  }
  return [
    `租户=${user.tenant_id}`,
    `部门=${user.department}`,
    `项目=${user.project_ids.join(', ') || '无'}`,
    `用户组=${user.groups.join(', ') || '无'}`,
    '管理员=否',
  ].join('; ');
}
