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
      reason: 'unknown user, fail-closed',
    };
  }

  if (document.tenant_id !== user.tenant_id) {
    return {
      allowed: false,
      reason: `tenant mismatch (${document.tenant_id} != ${user.tenant_id})`,
    };
  }

  if (user.is_admin) {
    return {
      allowed: true,
      reason: `admin in tenant ${user.tenant_id}`,
    };
  }

  if (document.department !== user.department) {
    return {
      allowed: false,
      reason: `department mismatch (${document.department} != ${user.department})`,
    };
  }

  if (!user.project_ids.includes(document.project_id)) {
    return {
      allowed: false,
      reason: `project ${document.project_id} not in user projects`,
    };
  }

  const groupMatch = document.allowed_groups.filter((group) => user.groups.includes(group));
  if (groupMatch.length === 0) {
    return {
      allowed: false,
      reason: 'no allowed_groups intersection',
    };
  }

  return {
    allowed: true,
    reason: `tenant, department, project, and group matched (${groupMatch.join(', ')})`,
  };
}

export function aclSummary(user: DemoUser | undefined): string {
  if (!user) {
    return 'unknown user -> fail-closed';
  }
  if (user.is_admin) {
    return `tenant=${user.tenant_id}; admin=true; same-tenant documents allowed`;
  }
  return [
    `tenant=${user.tenant_id}`,
    `department=${user.department}`,
    `projects=${user.project_ids.join(', ') || 'none'}`,
    `groups=${user.groups.join(', ') || 'none'}`,
    'admin=false',
  ].join('; ');
}
