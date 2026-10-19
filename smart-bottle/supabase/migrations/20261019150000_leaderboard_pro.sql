-- Clasamentul se vede doar din Pro în sus. Cei de pe Free apar în continuare în clasament
-- (pentru ceilalți), dar funcția nu le întoarce nimic.
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
    and coalesce((select me.tier from public.profiles me where me.id = auth.uid()), 'free') <> 'free'
  order by t.total_ml desc
  limit 10;
$$;
