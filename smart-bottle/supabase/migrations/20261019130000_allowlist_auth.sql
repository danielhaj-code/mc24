-- Autentificare pe bază de listă (allowlist): doar adresele aprobate își pot face cont și pot folosi datele.
-- Pe lângă migrare, în Auth: hook-ul „Before User Created” → public.hook_before_user_created,
-- conturile anonime dezactivate. Vezi README, secțiunea Cloud.

-- ============================ Lista de acces ============================
create table if not exists public.allowed_emails (
  email      text primary key check (email = lower(email) and email like '%_@_%._%'),
  note       text,
  added_by   text,
  added_at   timestamptz not null default now()
);

create table if not exists public.app_admins (
  email text primary key check (email = lower(email))
);

insert into public.app_admins (email) values ('hajdaniel123@gmail.com') on conflict do nothing;
insert into public.allowed_emails (email, note, added_by)
values ('hajdaniel123@gmail.com', 'administrator', 'setup') on conflict do nothing;

alter table public.allowed_emails enable row level security;
alter table public.app_admins enable row level security;
-- Fără politici directe: totul trece prin funcțiile de mai jos.
revoke all on public.allowed_emails from anon, authenticated;
revoke all on public.app_admins from anon, authenticated;

-- Emailul din tokenul curent (doar conturi cu email confirmat contează)
create or replace function public.current_email()
returns text
language sql
stable
set search_path = ''
as $$
  select lower(nullif(auth.jwt() ->> 'email', ''));
$$;

create or replace function public.is_allowed()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (select 1 from public.allowed_emails where email = public.current_email());
$$;

create or replace function public.is_admin()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (select 1 from public.app_admins where email = public.current_email());
$$;

grant execute on function public.current_email() to authenticated;
grant execute on function public.is_allowed() to authenticated;
grant execute on function public.is_admin() to authenticated;
revoke execute on function public.is_allowed() from anon, public;
revoke execute on function public.is_admin() from anon, public;

-- ============================ Hook: blochează conturile din afara listei ============================
create or replace function public.hook_before_user_created(event jsonb)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  em text := lower(event -> 'user' ->> 'email');
begin
  if em is null or not exists (select 1 from public.allowed_emails where email = em) then
    return jsonb_build_object('error', jsonb_build_object(
      'http_code', 403,
      'message', 'Adresa nu are acces. Cere-i administratorului să te adauge.'));
  end if;
  return '{}'::jsonb;
end;
$$;

revoke execute on function public.hook_before_user_created(jsonb) from public, anon, authenticated;
grant execute on function public.hook_before_user_created(jsonb) to supabase_auth_admin;
grant usage on schema public to supabase_auth_admin;

-- ============================ RLS: datele cer și apartenența la listă ============================
drop policy if exists "profil propriu: citire" on public.profiles;
create policy "profil propriu: citire" on public.profiles
  for select to authenticated using ((select auth.uid()) = id and (select public.is_allowed()));

drop policy if exists "profil propriu: modificare" on public.profiles;
create policy "profil propriu: modificare" on public.profiles
  for update to authenticated using ((select auth.uid()) = id and (select public.is_allowed()))
  with check ((select auth.uid()) = id);

drop policy if exists "evenimente proprii: citire" on public.drink_events;
create policy "evenimente proprii: citire" on public.drink_events
  for select to authenticated using ((select auth.uid()) = user_id and (select public.is_allowed()));

drop policy if exists "evenimente proprii: adăugare" on public.drink_events;
create policy "evenimente proprii: adăugare" on public.drink_events
  for insert to authenticated with check ((select auth.uid()) = user_id and (select public.is_allowed()));

drop policy if exists "evenimente proprii: ștergere" on public.drink_events;
create policy "evenimente proprii: ștergere" on public.drink_events
  for delete to authenticated using ((select auth.uid()) = user_id and (select public.is_allowed()));

drop policy if exists "facturi proprii: citire" on public.invoices;
create policy "facturi proprii: citire" on public.invoices
  for select to authenticated using ((select auth.uid()) = user_id and (select public.is_allowed()));

-- checkout și clasamentul: doar pentru cei de pe listă
create or replace function public.checkout(new_tier text)
returns public.invoices
language plpgsql
security definer
set search_path = ''
as $$
declare
  uid  uuid := auth.uid();
  old  text;
  inv  public.invoices;
  price text;
begin
  if uid is null or not public.is_allowed() then raise exception 'fără acces'; end if;
  if new_tier not in ('free', 'pro', 'ultra', 'enterprise') then raise exception 'abonament necunoscut'; end if;

  select tier into old from public.profiles where id = uid for update;
  if old is null then
    insert into public.profiles (id, nickname) values (uid, public.random_nickname());
    old := 'free';
  end if;

  price := case new_tier when 'free' then '0,00 lei' when 'pro' then '9,99 lei' when 'ultra' then '49,99 lei' else '∞ lei' end;
  update public.profiles set tier = new_tier where id = uid;
  insert into public.invoices (user_id, from_tier, to_tier, list_price, discount, total)
  values (uid, old, new_tier, price, '100% (reducere de proiect de facultate)', '0,00 lei')
  returning * into inv;
  return inv;
end;
$$;

create or replace function public.leaderboard_today()
returns table (rank int, nickname text, tier text, total_ml int, is_me boolean)
language sql
stable
security definer
set search_path = ''
as $$
  with day as (
    select (date_trunc('day', now() at time zone 'Europe/Bucharest') at time zone 'Europe/Bucharest') as start
  ), totals as (
    select e.user_id, sum(e.ml)::int as total_ml
    from public.drink_events e, day
    where e.kind = 'drink' and e.ts >= day.start
    group by e.user_id
  )
  select (row_number() over (order by t.total_ml desc))::int, p.nickname, p.tier, t.total_ml, t.user_id = auth.uid()
  from totals t join public.profiles p on p.id = t.user_id
  where public.is_allowed()
  order by t.total_ml desc
  limit 10;
$$;

-- ============================ Funcții de administrare ============================
create or replace function public.admin_list_allowed()
returns table (email text, note text, added_by text, added_at timestamptz, registered boolean, last_sign_in timestamptz, is_admin boolean)
language plpgsql
stable
security definer
set search_path = ''
as $$
begin
  if not public.is_admin() then raise exception 'doar administratorul'; end if;
  return query
    select a.email, a.note, a.added_by, a.added_at,
           u.id is not null, u.last_sign_in_at,
           exists (select 1 from public.app_admins ad where ad.email = a.email)
    from public.allowed_emails a
    left join auth.users u on lower(u.email) = a.email
    order by a.added_at desc;
end;
$$;

create or replace function public.admin_add_email(new_email text, new_note text default null)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare em text := lower(trim(new_email));
begin
  if not public.is_admin() then raise exception 'doar administratorul'; end if;
  if em !~ '^[^@\s]+@[^@\s]+\.[^@\s]+$' then raise exception 'adresă invalidă'; end if;
  insert into public.allowed_emails (email, note, added_by)
  values (em, nullif(trim(new_note), ''), public.current_email())
  on conflict (email) do update set note = coalesce(excluded.note, public.allowed_emails.note);
end;
$$;

create or replace function public.admin_remove_email(old_email text)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare em text := lower(trim(old_email));
begin
  if not public.is_admin() then raise exception 'doar administratorul'; end if;
  if exists (select 1 from public.app_admins where email = em) then
    raise exception 'un administrator nu poate fi scos de pe listă';
  end if;
  delete from public.allowed_emails where email = em;
end;
$$;

revoke execute on function public.admin_list_allowed() from public, anon;
revoke execute on function public.admin_add_email(text, text) from public, anon;
revoke execute on function public.admin_remove_email(text) from public, anon;
grant execute on function public.admin_list_allowed() to authenticated;
grant execute on function public.admin_add_email(text, text) to authenticated;
grant execute on function public.admin_remove_email(text) to authenticated;
