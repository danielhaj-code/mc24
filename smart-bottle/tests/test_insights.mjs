// node test_insights.mjs — testează obiceiurile (web/insights.js)
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { computeInsights } from '../web/insights.js';

let n = 0;
const t = (name, fn) => { fn(); n++; console.log('ok', name); };
// ora României în octombrie = UTC+3
const ro = (day, h, m = 0) => Date.parse(`2026-10-${String(day).padStart(2, '0')}T${String(h - 3).padStart(2, '0')}:${String(m).padStart(2, '0')}:00Z`);
const NOW = ro(19, 15);

// 10 zile: dimineața 300 mL la 9, după-amiaza 250 la 14; în weekend (17–18 oct) doar 400 la 11
const ev = [];
for (let d = 9; d <= 18; d++) {
  const weekend = [10, 11, 17, 18].includes(d);   // 10–11 și 17–18 oct 2026 sunt sâmbătă–duminică
  if (weekend) ev.push({ ts: ro(d, 11), type: 'drink', ml: 400 });
  else { ev.push({ ts: ro(d, 9), type: 'drink', ml: 300 }, { ts: ro(d, 14), type: 'drink', ml: 250 }, { ts: ro(d, 9, 30), type: 'refill', ml: 500 }); }
}
ev.push({ ts: ro(19, 9), kind: 'drink', ml: 2100 });   // azi: obiectiv atins (format din baza de date: kind)

const ins = computeInsights(ev, { goal: 2000, days: 30, now: NOW });

t('ore de vârf, în ora României', () => {
  assert.equal(ins.peakHours[0].hour, 9);
  assert.equal(ins.peakHours[0].label, '09:00–10:00');
  assert.deepEqual(ins.peakHours.map((h) => h.hour), [9, 11, 14]);
  assert.equal(ins.byHour.length, 24);
});

t('fereastră liniștită în timpul zilei', () => {
  assert.ok(ins.quietWindow && ins.quietWindow.hours >= 2, JSON.stringify(ins.quietWindow));
  assert.ok(ins.quietWindow.from >= 15);   // după 15:00 nu bea nimic
});

t('zile lucrătoare față de weekend', () => {
  assert.equal(ins.averages.weekday, 550);
  assert.equal(ins.averages.weekend, 400);
  assert.equal(ins.byWeekday.find((w) => w.name === 'sâmbătă').avgMl, 400);
  assert.equal(ins.byWeekday.find((w) => w.name === 'luni').avgMl, 550);
});

t('mărimea înghițiturilor, reumpleri, prima/ultima oră', () => {
  assert.equal(ins.totals.sips, 17);
  assert.equal(ins.averages.sipMedianMl, 300);
  assert.equal(ins.totals.refills, 6);
  assert.equal(ins.averages.firstSipHour, 9);
});

t('obiectiv, serii, zile active', () => {
  assert.equal(ins.goalDays.hit, 0);
  assert.equal(ins.goalDays.of, 10);              // azi nu intră
  assert.equal(ins.streak.current, 1);            // azi e atins
  assert.equal(ins.streak.longest, 0);
  assert.equal(ins.period.activeDays, 11);
  assert.equal(ins.daily.length, 30);
});

t('tendință pe 7 zile', () => {
  const more = [...ev];
  for (let d = 1; d <= 8; d++) more.push({ ts: ro(d, 10), type: 'drink', ml: 1000 });
  const r = computeInsights(more, { goal: 2000, days: 30, now: NOW });
  assert.ok(r.trend && r.trend.changePct < 0, JSON.stringify(r.trend));
});

t('fără date: nu crapă', () => {
  const r = computeInsights([], { now: NOW });
  assert.equal(r.peakHours.length, 0);
  assert.equal(r.averages.perDay, null);
  assert.equal(r.trend, null);
});

t('copia din funcția Supabase e identică', () => {
  const a = readFileSync(new URL('../web/insights.js', import.meta.url), 'utf8');
  const b = readFileSync(new URL('../supabase/functions/hydrogpt/insights.js', import.meta.url), 'utf8');
  assert.equal(a, b, 'rulează supabase/sync-shared.sh');
});

console.log(`\n${n} teste trecute`);
