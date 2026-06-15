-- Optional Supabase fullstack skeleton for EnterpriseRetrievalGateway.
-- Safe public-browser config uses only the Supabase anon key. Never commit service_role.

create extension if not exists pgcrypto;

create table if not exists public.tenants (
  id text primary key,
  name text not null,
  created_at timestamptz not null default now()
);

create table if not exists public.users (
  user_id text primary key,
  tenant_id text not null references public.tenants(id) on delete restrict,
  display_name text,
  department text not null,
  is_admin boolean not null default false,
  created_at timestamptz not null default now()
);

create table if not exists public.groups (
  id uuid primary key default gen_random_uuid(),
  tenant_id text not null references public.tenants(id) on delete cascade,
  group_key text not null,
  name text not null,
  created_at timestamptz not null default now(),
  unique (tenant_id, group_key)
);

create table if not exists public.user_groups (
  user_id text not null references public.users(user_id) on delete cascade,
  group_id uuid not null references public.groups(id) on delete cascade,
  created_at timestamptz not null default now(),
  primary key (user_id, group_id)
);

create table if not exists public.user_projects (
  user_id text not null references public.users(user_id) on delete cascade,
  tenant_id text not null references public.tenants(id) on delete cascade,
  project_id text not null,
  created_at timestamptz not null default now(),
  primary key (user_id, project_id)
);

create table if not exists public.auth_user_acl_profiles (
  auth_user_id uuid primary key references auth.users(id) on delete cascade,
  acl_user_id text not null references public.users(user_id) on delete restrict,
  created_at timestamptz not null default now(),
  created_by uuid default auth.uid()
);

create table if not exists public.documents (
  document_id text primary key,
  tenant_id text not null references public.tenants(id) on delete restrict,
  title text not null,
  department text not null,
  project_id text not null,
  document_type text not null,
  document_version integer,
  updated_at timestamptz,
  created_at timestamptz not null default now()
);

create table if not exists public.chunks (
  chunk_id text primary key,
  document_id text not null references public.documents(document_id) on delete cascade,
  content text not null,
  content_hash text,
  embedding_model_version text,
  updated_at timestamptz,
  content_tsv tsvector generated always as (
    to_tsvector('english', coalesce(content, '') || ' ' || coalesce(chunk_id, ''))
  ) stored,
  created_at timestamptz not null default now()
);

create table if not exists public.document_acl_groups (
  document_id text not null references public.documents(document_id) on delete cascade,
  group_id uuid not null references public.groups(id) on delete cascade,
  created_at timestamptz not null default now(),
  primary key (document_id, group_id)
);

create table if not exists public.search_events (
  id uuid primary key default gen_random_uuid(),
  auth_user_id uuid not null default auth.uid(),
  acl_user_id text references public.users(user_id) on delete set null,
  tenant_id text references public.tenants(id) on delete set null,
  query text not null,
  top_k integer not null check (top_k between 1 and 50),
  project_ids text[] not null default '{}'::text[],
  document_types text[] not null default '{}'::text[],
  result_count integer not null default 0 check (result_count >= 0),
  mode text not null default 'supabase_postgres_rls',
  created_at timestamptz not null default now()
);

create index if not exists groups_tenant_group_key_idx on public.groups (tenant_id, group_key);
create index if not exists users_tenant_department_idx on public.users (tenant_id, department);
create index if not exists user_projects_user_project_idx on public.user_projects (user_id, project_id);
create index if not exists documents_acl_idx on public.documents (tenant_id, department, project_id, document_type);
create index if not exists chunks_document_idx on public.chunks (document_id);
create index if not exists chunks_content_tsv_idx on public.chunks using gin (content_tsv);
create index if not exists search_events_auth_created_idx on public.search_events (auth_user_id, created_at desc);

create or replace function public.current_acl_user_id()
returns text
language sql
stable
security definer
set search_path = public
as $$
  select p.acl_user_id
  from public.auth_user_acl_profiles p
  where p.auth_user_id = auth.uid()
  limit 1
$$;

create or replace function public.current_tenant_id()
returns text
language sql
stable
security definer
set search_path = public
as $$
  select u.tenant_id
  from public.users u
  where u.user_id = public.current_acl_user_id()
  limit 1
$$;

create or replace function public.current_department()
returns text
language sql
stable
security definer
set search_path = public
as $$
  select u.department
  from public.users u
  where u.user_id = public.current_acl_user_id()
  limit 1
$$;

create or replace function public.current_user_is_admin()
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select coalesce((
    select u.is_admin
    from public.users u
    where u.user_id = public.current_acl_user_id()
    limit 1
  ), false)
$$;

create or replace function public.current_project_ids()
returns text[]
language sql
stable
security definer
set search_path = public
as $$
  select coalesce(array_agg(up.project_id order by up.project_id), '{}'::text[])
  from public.user_projects up
  where up.user_id = public.current_acl_user_id()
$$;

create or replace function public.current_group_ids()
returns uuid[]
language sql
stable
security definer
set search_path = public
as $$
  select coalesce(array_agg(ug.group_id order by ug.group_id), '{}'::uuid[])
  from public.user_groups ug
  where ug.user_id = public.current_acl_user_id()
$$;

create or replace function public.can_read_document(p_document_id text)
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select exists (
    select 1
    from public.documents d
    join public.users u on u.user_id = public.current_acl_user_id()
    where d.document_id = p_document_id
      and d.tenant_id = u.tenant_id
      and (
        u.is_admin
        or (
          d.department = u.department
          and exists (
            select 1
            from public.user_projects up
            where up.user_id = u.user_id
              and up.tenant_id = d.tenant_id
              and up.project_id = d.project_id
          )
          and exists (
            select 1
            from public.document_acl_groups dag
            join public.user_groups ug on ug.group_id = dag.group_id
            where dag.document_id = d.document_id
              and ug.user_id = u.user_id
          )
        )
      )
  )
$$;

create or replace view public.current_user_acl_profile
with (security_invoker = true)
as
select
  p.auth_user_id,
  u.user_id as acl_user_id,
  u.tenant_id,
  u.department,
  u.is_admin,
  u.display_name,
  coalesce((
    select array_agg(g.group_key order by g.group_key)
    from public.user_groups ug
    join public.groups g on g.id = ug.group_id
    where ug.user_id = u.user_id
  ), '{}'::text[]) as groups,
  coalesce((
    select array_agg(up.project_id order by up.project_id)
    from public.user_projects up
    where up.user_id = u.user_id
  ), '{}'::text[]) as project_ids
from public.auth_user_acl_profiles p
join public.users u on u.user_id = p.acl_user_id
where p.auth_user_id = auth.uid();

create or replace function public.search_visible_chunks(
  p_query text,
  p_top_k integer default 5,
  p_project_ids text[] default '{}'::text[],
  p_document_types text[] default '{}'::text[]
)
returns table (
  tenant_id text,
  document_id text,
  chunk_id text,
  title text,
  content text,
  department text,
  project_id text,
  document_type text,
  allowed_groups text[],
  score double precision,
  lexical_score double precision,
  semantic_score double precision,
  total_candidates integer
)
language plpgsql
security invoker
set search_path = public
as $$
declare
  v_query text := nullif(trim(coalesce(p_query, '')), '');
  v_top_k integer := least(greatest(coalesce(p_top_k, 5), 1), 50);
  v_project_ids text[] := coalesce(p_project_ids, '{}'::text[]);
  v_document_types text[] := coalesce(p_document_types, '{}'::text[]);
  v_result_count integer := 0;
begin
  if v_query is null then
    return;
  end if;

  return query
  with q as (
    select websearch_to_tsquery('english', v_query) as tsq
  ),
  ranked as (
    select
      d.tenant_id,
      d.document_id,
      c.chunk_id,
      d.title,
      c.content,
      d.department,
      d.project_id,
      d.document_type,
      coalesce(array_agg(distinct g.group_key) filter (where g.group_key is not null), '{}'::text[]) as allowed_groups,
      (
        greatest(ts_rank_cd(c.content_tsv, q.tsq), 0) * 100
        + case when d.title ilike '%' || v_query || '%' then 20 else 0 end
        + case when c.content ilike '%' || v_query || '%' then 10 else 0 end
      )::double precision as score,
      (
        greatest(ts_rank_cd(c.content_tsv, q.tsq), 0) * 100
        + case when d.title ilike '%' || v_query || '%' then 20 else 0 end
        + case when c.content ilike '%' || v_query || '%' then 10 else 0 end
      )::double precision as lexical_score,
      0::double precision as semantic_score,
      (count(*) over())::integer as total_candidates
    from public.chunks c
    join public.documents d on d.document_id = c.document_id
    cross join q
    left join public.document_acl_groups dag on dag.document_id = d.document_id
    left join public.groups g on g.id = dag.group_id
    where (cardinality(v_project_ids) = 0 or d.project_id = any(v_project_ids))
      and (cardinality(v_document_types) = 0 or d.document_type = any(v_document_types))
      and (
        c.content_tsv @@ q.tsq
        or d.title ilike '%' || v_query || '%'
        or c.content ilike '%' || v_query || '%'
        or d.document_id ilike '%' || v_query || '%'
        or c.chunk_id ilike '%' || v_query || '%'
      )
    group by
      d.tenant_id,
      d.document_id,
      c.chunk_id,
      d.title,
      c.content,
      d.department,
      d.project_id,
      d.document_type,
      c.content_tsv,
      q.tsq
    order by score desc, c.chunk_id
    limit v_top_k
  )
  select * from ranked;

  get diagnostics v_result_count = row_count;

  insert into public.search_events (
    auth_user_id,
    acl_user_id,
    tenant_id,
    query,
    top_k,
    project_ids,
    document_types,
    result_count,
    mode
  )
  values (
    auth.uid(),
    public.current_acl_user_id(),
    public.current_tenant_id(),
    v_query,
    v_top_k,
    v_project_ids,
    v_document_types,
    v_result_count,
    'supabase_postgres_rls'
  );
end;
$$;

alter table public.tenants enable row level security;
alter table public.users enable row level security;
alter table public.groups enable row level security;
alter table public.user_groups enable row level security;
alter table public.user_projects enable row level security;
alter table public.auth_user_acl_profiles enable row level security;
alter table public.documents enable row level security;
alter table public.chunks enable row level security;
alter table public.document_acl_groups enable row level security;
alter table public.search_events enable row level security;

alter table public.tenants force row level security;
alter table public.users force row level security;
alter table public.groups force row level security;
alter table public.user_groups force row level security;
alter table public.user_projects force row level security;
alter table public.auth_user_acl_profiles force row level security;
alter table public.documents force row level security;
alter table public.chunks force row level security;
alter table public.document_acl_groups force row level security;
alter table public.search_events force row level security;

drop policy if exists tenants_select_current on public.tenants;
create policy tenants_select_current
on public.tenants
for select
to authenticated
using (id = public.current_tenant_id());

drop policy if exists users_select_current_or_tenant_admin on public.users;
create policy users_select_current_or_tenant_admin
on public.users
for select
to authenticated
using (
  user_id = public.current_acl_user_id()
  or (tenant_id = public.current_tenant_id() and public.current_user_is_admin())
);

drop policy if exists groups_select_current_tenant on public.groups;
create policy groups_select_current_tenant
on public.groups
for select
to authenticated
using (tenant_id = public.current_tenant_id());

drop policy if exists user_groups_select_current_or_admin on public.user_groups;
create policy user_groups_select_current_or_admin
on public.user_groups
for select
to authenticated
using (
  user_id = public.current_acl_user_id()
  or (
    public.current_user_is_admin()
    and exists (
      select 1 from public.users u
      where u.user_id = public.user_groups.user_id
        and u.tenant_id = public.current_tenant_id()
    )
  )
);

drop policy if exists user_projects_select_current_or_admin on public.user_projects;
create policy user_projects_select_current_or_admin
on public.user_projects
for select
to authenticated
using (
  user_id = public.current_acl_user_id()
  or (tenant_id = public.current_tenant_id() and public.current_user_is_admin())
);

drop policy if exists auth_profiles_select_own on public.auth_user_acl_profiles;
create policy auth_profiles_select_own
on public.auth_user_acl_profiles
for select
to authenticated
using (auth_user_id = auth.uid());

drop policy if exists documents_select_acl on public.documents;
create policy documents_select_acl
on public.documents
for select
to authenticated
using (public.can_read_document(document_id));

drop policy if exists chunks_select_document_acl on public.chunks;
create policy chunks_select_document_acl
on public.chunks
for select
to authenticated
using (public.can_read_document(document_id));

drop policy if exists document_acl_groups_select_visible_document on public.document_acl_groups;
create policy document_acl_groups_select_visible_document
on public.document_acl_groups
for select
to authenticated
using (public.can_read_document(document_id));

drop policy if exists search_events_insert_own on public.search_events;
create policy search_events_insert_own
on public.search_events
for insert
to authenticated
with check (auth_user_id = auth.uid());

drop policy if exists search_events_select_own_or_tenant_admin on public.search_events;
create policy search_events_select_own_or_tenant_admin
on public.search_events
for select
to authenticated
using (
  auth_user_id = auth.uid()
  or (tenant_id = public.current_tenant_id() and public.current_user_is_admin())
);

revoke all on function public.current_acl_user_id() from public;
revoke all on function public.current_tenant_id() from public;
revoke all on function public.current_department() from public;
revoke all on function public.current_user_is_admin() from public;
revoke all on function public.current_project_ids() from public;
revoke all on function public.current_group_ids() from public;
revoke all on function public.can_read_document(text) from public;
revoke all on function public.search_visible_chunks(text, integer, text[], text[]) from public;

grant usage on schema public to authenticated;
grant select on
  public.tenants,
  public.users,
  public.groups,
  public.user_groups,
  public.user_projects,
  public.auth_user_acl_profiles,
  public.documents,
  public.chunks,
  public.document_acl_groups,
  public.search_events,
  public.current_user_acl_profile
to authenticated;
grant insert on public.search_events to authenticated;
grant execute on function public.current_acl_user_id() to authenticated;
grant execute on function public.current_tenant_id() to authenticated;
grant execute on function public.current_department() to authenticated;
grant execute on function public.current_user_is_admin() to authenticated;
grant execute on function public.current_project_ids() to authenticated;
grant execute on function public.current_group_ids() to authenticated;
grant execute on function public.can_read_document(text) to authenticated;
grant execute on function public.search_visible_chunks(text, integer, text[], text[]) to authenticated;
