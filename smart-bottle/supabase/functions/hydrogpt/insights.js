// Obiceiurile de hidratare, calculate din evenimente (înghițituri și reumpleri).
// Logică pură, fără DOM: aceeași copie rulează în aplicație și în funcția hydrogpt din Supabase
// (supabase/functions/hydrogpt/insights.js e o copie identică; testul verifică asta).
//
// events: [{ ts: ms | ISO, type|kind: 'drink'|'refill', ml }]
// Orele și zilele sunt în fusul orar al utilizatorului (implicit Europe/Bucharest).

export const WEEKDAYS_RO = ['luni', 'marți', 'miercuri', 'joi', 'vineri', 'sâmbătă', 'duminică'];

function clock(timeZone) {
  const fmt = new Intl.DateTimeFormat('en-CA', {
    timeZone, year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', hour12: false, weekday: 'short',
  });
  const WD = { Mon: 0, Tue: 1, Wed: 2, Thu: 3, Fri: 4, Sat: 5, Sun: 6 };
  return (ts) => {
    const p = Object.fromEntries(fmt.formatToParts(new Date(ts)).map((x) => [x.type, x.value]));
    return { day: `${p.year}-${p.month}-${p.day}`, hour: Number(p.hour) % 24, weekday: WD[p.weekday] };
  };
}

const median = (xs) => {
  if (!xs.length) return null;
  const s = [...xs].sort((a, b) => a - b);
  const m = Math.floor(s.length / 2);
  return s.length % 2 ? s[m] : Math.round((s[m - 1] + s[m]) / 2);
};
const round = (x) => Math.round(x);
const addDays = (dayKey, n) => {
  const d = new Date(`${dayKey}T12:00:00Z`);
  d.setUTCDate(d.getUTCDate() + n);
  return d.toISOString().slice(0, 10);
};
const hourLabel = (h) => `${String(h).padStart(2, '0')}:00–${String((h + 1) % 24).padStart(2, '0')}:00`;

export function computeInsights(events, { goal = 2000, days = 30, now = Date.now(), timeZone = 'Europe/Bucharest' } = {}) {
  const at = clock(timeZone);
  const today = at(now).day;
  const firstDay = addDays(today, -(days - 1));
  const norm = events
    .map((e) => ({ ts: typeof e.ts === 'number' ? e.ts : Date.parse(e.ts), type: e.type ?? e.kind, ml: Number(e.ml) }))
    .filter((e) => Number.isFinite(e.ts) && e.ml > 0 && at(e.ts).day >= firstDay && e.ts <= now);
  const drinks = norm.filter((e) => e.type === 'drink');
  const refills = norm.filter((e) => e.type === 'refill');

  // ---- pe zile (inclusiv zilele fără nimic) ----
  const perDay = new Map();
  for (let i = 0; i < days; i++) perDay.set(addDays(firstDay, i), { ml: 0, sips: 0, first: null, last: null });
  for (const e of drinks) {
    const { day, hour } = at(e.ts);
    const d = perDay.get(day);
    if (!d) continue;
    d.ml += e.ml; d.sips++;
    d.first = d.first === null ? hour : Math.min(d.first, hour);
    d.last = d.last === null ? hour : Math.max(d.last, hour);
  }
  const daily = [...perDay].map(([day, d]) => ({ day, ...d }));
  // zilele „cu date”: de la prima înghițitură încolo, ca o lună fără aplicație să nu strice mediile
  const firstActive = daily.findIndex((d) => d.sips > 0);
  const active = firstActive < 0 ? [] : daily.slice(firstActive);
  const complete = active.filter((d) => d.day !== today);          // azi încă nu s-a terminat

  // ---- pe ore ----
  const byHour = Array.from({ length: 24 }, (_, h) => ({ hour: h, ml: 0, sips: 0 }));
  for (const e of drinks) { const { hour } = at(e.ts); byHour[hour].ml += e.ml; byHour[hour].sips++; }
  const totalMl = drinks.reduce((s, e) => s + e.ml, 0);
  // media pe zi activă (mai ușor de înțeles decât totalul pe 30 de zile) și procentul din toată apa
  const nActive = Math.max(1, active.length);
  for (const h of byHour) { h.avgMl = round(h.ml / nActive); h.share = totalMl ? round((h.ml / totalMl) * 100) : 0; }
  const peakHours = [...byHour].filter((h) => h.ml > 0).sort((a, b) => b.ml - a.ml).slice(0, 3)
    .map((h) => ({ hour: h.hour, label: hourLabel(h.hour), avgMlPerDay: h.avgMl, share: h.share }));

  // cel mai lung interval din zi (8–22) în care aproape nu bei: < 3% din total pe oră
  let gap = null;
  if (totalMl) {
    let start = null;
    for (let h = 8; h <= 22; h++) {
      const quiet = h < 22 && byHour[h].ml / totalMl < 0.03;
      if (quiet && start === null) start = h;
      if ((!quiet || h === 22) && start !== null) {
        const len = h - start;
        if (len >= 2 && (!gap || len > gap.hours)) gap = { from: start, to: h, hours: len, label: `${String(start).padStart(2, '0')}:00–${String(h).padStart(2, '0')}:00` };
        start = null;
      }
    }
  }

  // ---- pe zile ale săptămânii (media zilelor complete) ----
  const wd = Array.from({ length: 7 }, (_, i) => ({ weekday: i, name: WEEKDAYS_RO[i], total: 0, days: 0 }));
  for (const d of complete) { const w = wd[at(Date.parse(`${d.day}T12:00:00Z`)).weekday]; w.total += d.ml; w.days++; }
  const byWeekday = wd.map((w) => ({ weekday: w.weekday, name: w.name, avgMl: w.days ? round(w.total / w.days) : null, days: w.days }));
  const avgOf = (ds) => (ds.length ? round(ds.reduce((s, d) => s + d.ml, 0) / ds.length) : null);
  const weekdayDays = complete.filter((d) => at(Date.parse(`${d.day}T12:00:00Z`)).weekday < 5);
  const weekendDays = complete.filter((d) => at(Date.parse(`${d.day}T12:00:00Z`)).weekday >= 5);

  // ---- serii (zile consecutive cu obiectivul atins) ----
  let longest = 0, run = 0;
  for (const d of complete) { run = d.ml >= goal ? run + 1 : 0; longest = Math.max(longest, run); }
  let current = 0;
  const todayRow = daily.at(-1);
  for (let i = complete.length - 1; i >= 0 && complete[i].ml >= goal; i--) current++;
  if (todayRow?.ml >= goal) current++;                              // azi contează doar dacă e deja atins

  // ---- tendință: ultimele 7 zile complete față de cele 7 dinainte ----
  const last7 = complete.slice(-7), prev7 = complete.slice(-14, -7);
  const lastAvg = avgOf(last7), prevAvg = prev7.length >= 3 ? avgOf(prev7) : null;
  const trend = lastAvg === null || prevAvg === null || prevAvg === 0 ? null
    : { last7Avg: lastAvg, prev7Avg: prevAvg, changePct: round(((lastAvg - prevAvg) / prevAvg) * 100) };

  const sipSizes = drinks.map((e) => e.ml);
  const best = complete.length ? complete.reduce((a, b) => (b.ml > a.ml ? b : a)) : null;
  const worst = complete.length ? complete.reduce((a, b) => (b.ml < a.ml ? b : a)) : null;

  return {
    period: { days, from: firstDay, to: today, activeDays: active.length, timeZone },
    goal,
    totals: { ml: totalMl, sips: drinks.length, refills: refills.length },
    averages: {
      perDay: avgOf(complete),
      weekday: avgOf(weekdayDays),
      weekend: avgOf(weekendDays),
      sipMl: sipSizes.length ? round(totalMl / sipSizes.length) : null,
      sipMedianMl: median(sipSizes),
      sipsPerDay: complete.length ? Math.round((complete.reduce((s, d) => s + d.sips, 0) / complete.length) * 10) / 10 : null,
      refillsPerDay: complete.length ? Math.round((refills.length / Math.max(active.length, 1)) * 10) / 10 : null,
      firstSipHour: median(complete.filter((d) => d.first !== null).map((d) => d.first)),
      lastSipHour: median(complete.filter((d) => d.last !== null).map((d) => d.last)),
    },
    goalDays: { hit: complete.filter((d) => d.ml >= goal).length, of: complete.length },
    streak: { current, longest },
    trend,
    best: best && best.ml > 0 ? { day: best.day, ml: best.ml } : null,
    worst: worst ? { day: worst.day, ml: worst.ml } : null,
    peakHours,
    quietWindow: gap,
    byHour,
    byWeekday,
    daily: daily.map(({ day, ml, sips }) => ({ day, ml, sips })),
  };
}
