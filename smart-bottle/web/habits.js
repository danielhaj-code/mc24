// Cardul „Obiceiurile tale”: când bei, în ce zile, serii și tendință. Disponibil din Ultra;
// sub Ultra se vede o previzualizare estompată cu date de exemplu.
import { computeInsights, WEEKDAYS_RO } from './insights.js';

const $ = (id) => document.getElementById(id);
const NS = 'http://www.w3.org/2000/svg';
let opts;                 // { enabled(), loadEvents(days) -> Promise<events>, goal(), ask(question) }
let days = 30;
let timer = null;
let last = null;          // ultimul calcul, pentru redesenare la redimensionare

function el(name, attrs = {}, text) {
  const e = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (text !== undefined) e.textContent = text;
  return e;
}
const fmtMl = (ml) => (ml === null || ml === undefined ? '–' : ml >= 1000 ? `${(ml / 1000).toFixed(1).replace('.', ',')} L` : `${ml} mL`);
const fmtHour = (h) => (h === null ? '–' : `${String(h).padStart(2, '0')}:00`);

// Date de exemplu pentru previzualizarea blocată (nu ale utilizatorului).
function sampleEvents() {
  const out = [];
  const now = Date.now();
  const pattern = [[8, 250], [10, 200], [13, 300], [15, 220], [16, 260], [19, 180]];
  for (let d = 0; d < 30; d++) {
    for (const [h, ml] of pattern) {
      const t = new Date(now - d * 86400000); t.setHours(h, (d * 7) % 60, 0, 0);
      if ((d + h) % 5 !== 0) out.push({ ts: t.getTime(), type: 'drink', ml: ml + ((d * h) % 60) });
    }
  }
  return out;
}

/* =========================== Desen =========================== */
function renderHours(ins) {
  const svg = $('habitsHours');
  svg.replaceChildren();
  const W = Math.max(280, Math.round(svg.clientWidth || 560)), H = 150, top = 22, bottom = 124;
  svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
  const max = Math.max(1, ...ins.byHour.map((h) => h.avgMl));
  const slot = W / 24, bw = Math.max(3, slot * 0.62);
  const peaks = new Set(ins.peakHours.map((p) => p.hour));
  if (ins.quietWindow) {
    const q = ins.quietWindow;
    svg.append(el('rect', { x: q.from * slot, y: top - 6, width: (q.to - q.from) * slot, height: bottom - top + 6, rx: 8, class: 'quiet' }));
    svg.append(el('text', { x: ((q.from + q.to) / 2) * slot, y: top + 8, 'text-anchor': 'middle', class: 'quiet-label' }, 'aici uiți de apă'));
  }
  ins.byHour.forEach((h) => {
    const bh = (h.avgMl / max) * (bottom - top);
    const r = el('rect', { x: h.hour * slot + (slot - bw) / 2, y: bottom - bh, width: bw, height: Math.max(bh, 1.5), rx: Math.min(4, bw / 2), class: peaks.has(h.hour) ? 'bar peak' : 'bar' });
    r.append(el('title', {}, `${fmtHour(h.hour)}: în medie ${h.avgMl} mL/zi (${h.share}% din apă)`));
    svg.append(r);
  });
  for (const h of [0, 6, 12, 18, 23]) svg.append(el('text', { x: h * slot + slot / 2, y: H - 8, 'text-anchor': 'middle' }, fmtHour(h).slice(0, 2)));
  const p = ins.peakHours[0];
  if (p) {
    const x = p.hour * slot + slot / 2;
    svg.append(el('text', { x: Math.min(Math.max(x, 40), W - 40), y: 12, 'text-anchor': 'middle', class: 'peak-label' }, `vârf ${fmtHour(p.hour)}`));
  }
}

function renderWeekdays(ins, goal) {
  const svg = $('habitsWeek');
  svg.replaceChildren();
  const W = Math.max(240, Math.round(svg.clientWidth || 320)), H = 150, top = 16, bottom = 122;
  svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
  const vals = ins.byWeekday.map((w) => w.avgMl ?? 0);
  const max = Math.max(goal * 1.1, ...vals, 1);
  const slot = W / 7, bw = slot * 0.5;
  const gy = bottom - (goal / max) * (bottom - top);
  svg.append(el('line', { x1: 0, x2: W, y1: gy, y2: gy, class: 'goal-line' }));
  ins.byWeekday.forEach((w, i) => {
    const v = w.avgMl ?? 0;
    const bh = (v / max) * (bottom - top);
    const r = el('rect', { x: i * slot + (slot - bw) / 2, y: bottom - bh, width: bw, height: Math.max(bh, 1.5), rx: 5, class: v >= goal ? 'bar met' : i >= 5 ? 'bar weekend' : 'bar' });
    r.append(el('title', {}, w.avgMl === null ? `${w.name}: fără date` : `${w.name}: în medie ${w.avgMl} mL`));
    svg.append(r);
    svg.append(el('text', { x: i * slot + slot / 2, y: H - 8, 'text-anchor': 'middle' }, WEEKDAYS_RO[i].slice(0, 2)));
  });
}

function stat(label, value, note, tone) {
  const d = document.createElement('div');
  d.className = 'habit-stat' + (tone ? ` ${tone}` : '');
  d.innerHTML = '<span class="v"></span><span class="l"></span><span class="n"></span>';
  d.children[0].textContent = value;
  d.children[1].textContent = label;
  d.children[2].textContent = note || '';
  return d;
}

function render(ins, goal) {
  const a = ins.averages;
  const t = ins.trend;
  const stats = $('habitsStats');
  stats.replaceChildren(
    stat('pe zi, în medie', fmtMl(a.perDay), `obiectiv ${fmtMl(goal)}`),
    stat('zile cu obiectivul', `${ins.goalDays.hit} din ${ins.goalDays.of}`, ins.goalDays.of ? `${Math.round((ins.goalDays.hit / ins.goalDays.of) * 100)}%` : ''),
    stat('serie curentă', `${ins.streak.current} ${ins.streak.current === 1 ? 'zi' : 'zile'}`, `record ${ins.streak.longest}`),
    stat('înghițitura tipică', fmtMl(a.sipMedianMl), a.sipsPerDay ? `${String(a.sipsPerDay).replace('.', ',')} pe zi` : ''),
    stat('ultimele 7 zile', t ? `${t.changePct > 0 ? '+' : ''}${t.changePct}%` : '–', t ? `față de cele 7 dinainte` : 'prea puține date', t ? (t.changePct >= 0 ? 'up' : 'down') : ''),
  );

  const p = ins.peakHours[0];
  const lines = [];
  if (p) lines.push(`Bei cel mai mult între ${p.label}: ${p.share}% din toată apa.`);
  if (ins.quietWindow) lines.push(`Între ${ins.quietWindow.label} aproape că nu bei.`);
  if (a.weekday !== null && a.weekend !== null && a.weekday > 0) {
    const diff = Math.round(((a.weekend - a.weekday) / a.weekday) * 100);
    lines.push(diff <= -10 ? `În weekend bei cu ${-diff}% mai puțin decât în timpul săptămânii.` : diff >= 10 ? `În weekend bei cu ${diff}% mai mult decât în timpul săptămânii.` : 'Bei cam la fel în weekend și în timpul săptămânii.');
  }
  if (a.firstSipHour !== null && a.lastSipHour !== null) lines.push(`De obicei începi pe la ${fmtHour(a.firstSipHour)} și te oprești pe la ${fmtHour(a.lastSipHour)}.`);
  if (ins.best) lines.push(`Cea mai bună zi: ${new Date(`${ins.best.day}T12:00`).toLocaleDateString('ro-RO', { weekday: 'long', day: 'numeric', month: 'long' })}, ${fmtMl(ins.best.ml)}.`);
  const list = $('habitsFindings');
  list.replaceChildren(...lines.map((l) => Object.assign(document.createElement('li'), { textContent: l })));

  renderHours(ins);
  renderWeekdays(ins, goal);
}

/* =========================== Date =========================== */
export async function refreshHabits() {
  if (!opts) return;
  const on = opts.enabled();
  const card = $('habitsCard');
  card.classList.toggle('locked', !on);
  $('habitsLock').hidden = on;
  $('habitsAsk').hidden = !on;
  const goal = opts.goal();
  let events = on ? await opts.loadEvents(days) : sampleEvents();
  const ins = computeInsights(events, { goal, days, timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone || 'Europe/Bucharest' });
  const empty = on && ins.totals.sips === 0;
  $('habitsEmpty').hidden = !empty;
  $('habitsBody').hidden = empty;
  if (!empty) render(ins, goal);
  last = empty ? null : { ins, goal };
  $('habitsSource').textContent = on ? `${ins.period.activeDays} ${ins.period.activeDays === 1 ? 'zi' : 'zile'} cu date, ${ins.totals.sips} înghițituri` : 'date de exemplu';
}

// Recalculează după un eveniment nou, dar fără să interogheze la fiecare înghițitură.
export function habitsChanged() {
  clearTimeout(timer);
  timer = setTimeout(refreshHabits, 4000);
}

export function initHabits(o) {
  opts = o;
  for (const b of document.querySelectorAll('#habitsRange button')) {
    b.addEventListener('click', () => {
      days = Number(b.dataset.days);
      for (const x of document.querySelectorAll('#habitsRange button')) x.setAttribute('aria-pressed', String(x === b));
      refreshHabits();
    });
  }
  $('habitsAsk').addEventListener('click', () => o.ask('Ce obiceiuri ai observat la mine? La ce ore beau cel mai mult și unde pot să mă îmbunătățesc?'));
  $('habitsUnlock').addEventListener('click', () => o.openTiers());
  let rz;
  new ResizeObserver(() => {
    clearTimeout(rz);
    rz = setTimeout(() => { if (last) { renderHours(last.ins); renderWeekdays(last.ins, last.goal); } }, 150);
  }).observe($('habitsCard'));
  refreshHabits();
}
