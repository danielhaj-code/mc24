-- Conturi demo pentru clasament: emailuri *@demo.smartbottle.invalid (nu primesc niciodată email),
-- pe lista de acces cu nota „cont demo”. Istoricul lor de 30 de zile e generat o singură dată
-- (scriptul de seed nu e în repo: datele sunt deja în baza de date).
-- Ca să nu stea pe zero în fiecare dimineață, pg_cron le adaugă înghițituri pe parcursul zilei,
-- mai des în orele lor preferate.

create table if not exists public.demo_patterns (
  email      text primary key references public.allowed_emails (email) on delete cascade,
  peak_hours int[] not null,      -- ore (ora României) în care beau cel mai des
  sip_ml     int not null         -- înghițitura tipică
);
alter table public.demo_patterns enable row level security;   -- fără politici: doar serverul le citește
revoke all on public.demo_patterns from anon, authenticated;

insert into public.demo_patterns (email, peak_hours, sip_ml) values
  ('ana.morning@demo.smartbottle.invalid',      '{7,8,9,10}',        230),
  ('mihai.aia@demo.smartbottle.invalid',        '{14,15,16}',        180),
  ('ioana.sommelier@demo.smartbottle.invalid',  '{9,11,13,15,17,19}', 200),
  ('vlad.sprint@demo.smartbottle.invalid',      '{17,18,19,20}',     320),
  ('bianca.noapte@demo.smartbottle.invalid',    '{20,21,22,23}',     160),
  ('prof.ionescu@demo.smartbottle.invalid',     '{8,10,11,12}',      250),
  ('cristi.camila@demo.smartbottle.invalid',    '{13,18}',           140),
  ('raluca.streak@demo.smartbottle.invalid',    '{7,9,11,13,15,17,19}', 210)
on conflict (email) do update set peak_hours = excluded.peak_hours, sip_ml = excluded.sip_ml;

-- Rulează la 20 de minute: între 7 și 23, fiecare cont demo bea cu probabilitate mai mare în orele lui.
create or replace function public.demo_tick()
returns int
language plpgsql
security definer
set search_path = ''
as $$
declare
  h int := extract(hour from now() at time zone 'Europe/Bucharest');
  r record;
  n int := 0;
  ml int;
begin
  if h < 7 then return 0; end if;
  for r in
    select u.id, d.peak_hours, d.sip_ml
    from public.demo_patterns d join auth.users u on lower(u.email) = d.email
  loop
    if random() < (case when h = any (r.peak_hours) then 0.7 else 0.12 end) then
      ml := greatest(40, least(500, round(r.sip_ml * (0.6 + random() * 0.8))))::int;
      insert into public.drink_events (user_id, ts, kind, ml, client_id)
      values (r.id, now(), 'drink', ml, 'demo-' || md5(random()::text));
      n := n + 1;
    end if;
  end loop;
  return n;
end;
$$;
revoke execute on function public.demo_tick() from public, anon, authenticated;

create extension if not exists pg_cron;
select cron.unschedule('demo-sips') where exists (select 1 from cron.job where jobname = 'demo-sips');
select cron.schedule('demo-sips', '*/20 * * * *', 'select public.demo_tick()');
