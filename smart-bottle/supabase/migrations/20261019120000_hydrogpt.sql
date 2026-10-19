-- Smart Bottle / HydroGPT™ — schema cloud
-- Utilizatori anonimi (Supabase Auth, „Allow anonymous sign-ins” activat), istoric sincronizat,
-- abonamente (false, dar cu facturi) și clasament zilnic.

-- ============================ Profiluri ============================
create table if not exists public.profiles (
  id          uuid primary key references auth.users (id) on delete cascade,
  nickname    text not null,
  tier        text not null default 'free' check (tier in ('free', 'pro', 'ultra', 'enterprise')),
  goal_ml     int  not null default 2000 check (goal_ml between 250 and 6000),
  created_at  timestamptz not null default now()
);

-- Porecle generate automat: „Cămila Însetată #4821”
create or replace function public.random_nickname()
returns text
language sql
volatile
set search_path = ''
as $$
  select (array['Cămila','Ficusul','Peștele','Cactusul','Delfinul','Burete','Hipopotamul','Ursul Polar'])[1 + floor(random() * 8)::int]
      || ' '
      || (array['Însetat','Hidratat','Deshidratat','Curajos','Leneș','Sceptic','Fluid','Premium'])[1 + floor(random() * 8)::int]
      || ' #' || lpad((floor(random() * 10000))::int::text, 4, '0');
$$;

create or replace function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  insert into public.profiles (id, nickname) values (new.id, public.random_nickname())
  on conflict (id) do nothing;
  return new;
end;
$$;

drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created
  after insert on auth.users
  for each row execute function public.handle_new_user();

alter table public.profiles enable row level security;

drop policy if exists "profil propriu: citire" on public.profiles;
create policy "profil propriu: citire" on public.profiles
  for select to authenticated using ((select auth.uid()) = id);

drop policy if exists "profil propriu: modificare" on public.profiles;
create policy "profil propriu: modificare" on public.profiles
  for update to authenticated using ((select auth.uid()) = id) with check ((select auth.uid()) = id);

-- Abonamentul se schimbă DOAR prin checkout(); direct se pot edita porecla și obiectivul.
revoke update on public.profiles from authenticated, anon;
grant select on public.profiles to authenticated;
grant update (nickname, goal_ml) on public.profiles to authenticated;

-- ============================ Evenimente ============================
create table if not exists public.drink_events (
  id         bigint generated always as identity primary key,
  user_id    uuid not null default auth.uid() references auth.users (id) on delete cascade,
  ts         timestamptz not null,
  kind       text not null check (kind in ('drink', 'refill')),
  ml         int  not null check (ml between 1 and 5000),
  client_id  text not null,                       -- deduplicare la sincronizare
  unique (user_id, client_id)
);
create index if not exists drink_events_user_ts on public.drink_events (user_id, ts desc);
create index if not exists drink_events_ts on public.drink_events (ts);

alter table public.drink_events enable row level security;

drop policy if exists "evenimente proprii: citire" on public.drink_events;
create policy "evenimente proprii: citire" on public.drink_events
  for select to authenticated using ((select auth.uid()) = user_id);

drop policy if exists "evenimente proprii: adăugare" on public.drink_events;
create policy "evenimente proprii: adăugare" on public.drink_events
  for insert to authenticated with check ((select auth.uid()) = user_id);

drop policy if exists "evenimente proprii: ștergere" on public.drink_events;
create policy "evenimente proprii: ștergere" on public.drink_events
  for delete to authenticated using ((select auth.uid()) = user_id);

grant select, insert, delete on public.drink_events to authenticated;

-- ============================ Facturi (false) ============================
create table if not exists public.invoices (
  id          bigint generated always as identity primary key,
  user_id     uuid not null references auth.users (id) on delete cascade,
  created_at  timestamptz not null default now(),
  from_tier   text not null,
  to_tier     text not null,
  list_price  text not null,
  discount    text not null,
  total       text not null
);
create index if not exists invoices_user on public.invoices (user_id, created_at desc);

alter table public.invoices enable row level security;
drop policy if exists "facturi proprii: citire" on public.invoices;
create policy "facturi proprii: citire" on public.invoices
  for select to authenticated using ((select auth.uid()) = user_id);
grant select on public.invoices to authenticated;

-- Checkout: plata reușește mereu. E un proiect de facultate.
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
  if uid is null then raise exception 'neautentificat'; end if;
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

revoke execute on function public.checkout(text) from public, anon;
grant execute on function public.checkout(text) to authenticated;

-- ============================ Clasament ============================
-- Top 10 al zilei (ora României). Expune doar porecla și totalul, nu id-uri.
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
  order by t.total_ml desc
  limit 10;
$$;

revoke execute on function public.leaderboard_today() from public, anon;
grant execute on function public.leaderboard_today() to authenticated;

revoke execute on function public.random_nickname() from public, anon, authenticated;
revoke execute on function public.handle_new_user() from public, anon, authenticated;
