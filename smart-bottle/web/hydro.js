// Interfața pentru HydroGPT™, abonamente, reclame, clasament și raportul Enterprise.
// Logica deciziilor e în agent.js; aici e doar DOM-ul și legătura cu cloud.js.
import {
  TIERS, TIER_ORDER, ADS, buildContext, decide, speak, reasoningTrace,
  reactToEvent, localReply, kpiReport,
} from './agent.js';
import { cloud } from './cloud.js';
import { dayKey } from './logic.js';

const $ = (id) => document.getElementById(id);
// Iconițe din sprite-ul Lucide din index.html
export function icon(name, cls = 'i') {
  const NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('class', cls);
  svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS(NS, 'use');
  use.setAttribute('href', `#i-${name}`);
  svg.append(use);
  return svg;
}
let app;                       // { cfg, state, saveSettings, toast, todayEvents, onTierChange, notify }
const chat = [];               // { role: 'user'|'agent', text }
let lastNudge = Date.now();
let speech = { key: '', text: '' };
let adIdx = 0;
let cloudNoteShown = false;
let busy = false;

const tier = () => (TIERS[app.cfg.tier] ? app.cfg.tier : 'free');
const T = () => TIERS[tier()];

export function context() {
  const { cfg, state } = app;
  return buildContext({
    now: Date.now(), ml: state.ml, capacity: cfg.capacity, goal: cfg.goal,
    drinks: state.events.filter((e) => e.type === 'drink'),
    connected: state.connected, calibrated: cfg.calibrated, absent: state.absent,
  });
}

/* =========================== Verdict =========================== */
const TITLES = {
  drink: (d) => `Bea ${d.amountMl} mL`,
  wait: (d) => (d.waitMin >= 240 ? 'Dormi. Apa te așteaptă dimineață.' : `Mai așteaptă ~${d.waitMin} min`),
  refill: () => 'Reumple sticla',
  missing: () => 'Pune sticla la loc',
  blind: () => 'Conectează sticla',
};

export function renderVerdict(force = false) {
  const ctx = context();
  const d = decide(ctx);
  const key = `${d.verdict}|${d.amountMl}|${d.waitMin}|${tier()}`;
  if (force || key !== speech.key) speech = { key, text: speak(d, tier()) };
  $('verdict').dataset.verdict = d.verdict;
  $('verdictTitle').textContent = TITLES[d.verdict](d);
  $('verdictSub').textContent = speech.text;

  const list = $('council');
  list.hidden = !T().council || !d.votes.length;
  list.replaceChildren(...d.votes.map((v) => {
    const li = document.createElement('li');
    li.className = v.vote === 'bea' ? 'yes' : 'no';
    li.innerHTML = '<span class="organ"></span><b></b><span class="why muted"></span>';
    li.children[0].append(icon(v.icon), document.createTextNode(v.organ));
    li.children[1].textContent = v.vote;
    li.children[2].textContent = v.reason;
    return li;
  }));
  return { ctx, d };
}

/* =========================== Chat =========================== */
function bubble(role, text, extra = {}) {
  const div = document.createElement('div');
  div.className = `msg ${role}${extra.cls ? ' ' + extra.cls : ''}`;
  div.textContent = text;
  if (extra.tools?.length) {
    const tools = document.createElement('div');
    tools.className = 'tools';
    for (const t of extra.tools) {
      const chip = document.createElement('span');
      chip.append(icon('wrench', 'i xs'), document.createTextNode(t.name));
      tools.append(chip);
    }
    div.prepend(tools);
  }
  const log = $('chatLog');
  log.append(div);
  log.scrollTop = log.scrollHeight;
  return div;
}

export function agentSay(text, extra) {
  chat.push({ role: 'agent', text });
  bubble('agent', text, extra);
}

const quotaKey = () => `sb.quota.${dayKey(Date.now())}`;
function quotaUsed() { try { return Number(localStorage.getItem(quotaKey()) || 0); } catch { return 0; } }
function useQuota() { try { localStorage.setItem(quotaKey(), String(quotaUsed() + 1)); } catch { /* fără stocare */ } }
function renderQuota() {
  const lim = T().chatPerDay;
  $('chatQuota').textContent = Number.isFinite(lim)
    ? `Întrebări rămase azi: ${Math.max(0, lim - quotaUsed())} / ${lim}. Pro le face nelimitate.`
    : T().llm && cloud.ready ? 'AI în cloud, cu unelte: citește senzorul, istoricul și îți poate programa mementouri.' : T().llm ? 'AI-ul din cloud are nevoie de internet; acum răspunde agentul local.' : 'Întrebări nelimitate.';
}

// Pașii de „gândire”, afișați unul câte unul.
async function think(ctx, d) {
  const el = bubble('agent', '', { cls: 'thinking' });
  for (const step of reasoningTrace(ctx, d, tier())) {
    const line = document.createElement('div');
    line.textContent = step;
    el.append(line);
    $('chatLog').scrollTop = $('chatLog').scrollHeight;
    await new Promise((r) => setTimeout(r, T().council ? 380 : 700));
  }
  return el;
}

async function ask(question) {
  if (busy) return;
  const lim = T().chatPerDay;
  bubble('user', question);
  chat.push({ role: 'user', text: question });
  if (Number.isFinite(lim) && quotaUsed() >= lim) {
    agentSay(`Ai folosit cele ${lim} întrebări gratuite de azi. Rinichii tăi au votat pentru Pro (9,99 lei). Vezica s-a abținut.`);
    renderQuota();
    return;
  }
  useQuota();
  renderQuota();
  busy = true;
  $('chatSend').disabled = true;
  try {
    const { ctx, d } = renderVerdict();
    const thinking = await think(ctx, d);
    let res = null;
    if (T().llm && cloud.ready) {
      res = await cloud.askAgent({ question, ctx, decision: d, history: chat.slice(-9, -1) });
    }
    thinking.classList.add('done');
    if (res?.reply) {
      $('agentEngine').textContent = res.model || 'cloud';
      agentSay(res.reply, { tools: res.toolCalls });
      for (const a of res.actions ?? []) if (a.type === 'remind') scheduleReminder(a.minutes, a.message);
    } else {
      if (res?.fallback && !cloudNoteShown) {
        cloudNoteShown = true;
        bubble('agent', `AI-ul din cloud nu e disponibil (${res.reason || res.error || 'fără răspuns'}). Răspunde agentul local.`, { cls: 'note' });
      }
      agentSay(localReply(question, ctx, tier()));
    }
  } finally {
    busy = false;
    $('chatSend').disabled = false;
  }
}

function scheduleReminder(minutes, message) {
  app.toast(`HydroGPT a programat un memento peste ${minutes} min`);
  setTimeout(() => {
    agentSay(message, { cls: 'alarm' });
    app.toast(message);
    app.notify?.(message);
  }, minutes * 60000);
}

// Mesaje proactive: cât de des depinde de abonament.
function nudgeLoop() {
  renderVerdict();
  if (Date.now() - lastNudge < T().nudgeMin * 60000) return;
  const { d } = renderVerdict();
  if (d.verdict !== 'drink' && d.verdict !== 'refill') return;
  lastNudge = Date.now();
  const text = speak(d, tier());
  agentSay(text);
  app.toast(text);
  app.notify?.(text);
}

export function onEvent(ev) {
  cloud.pushEvents([ev]).then(refreshBoard);
  const ctx = context();
  agentSay(reactToEvent(ev, tier(), ctx));
  lastNudge = Date.now();
  renderVerdict(true);
}

/* =========================== Abonamente =========================== */
function applyTier() {
  const t = tier();
  document.documentElement.dataset.tier = t;
  $('btnTier').textContent = T().name;
  $('adBanner').hidden = !T().ads;
  $('btnReport').hidden = !T().report;
  $('agentEngine').textContent = T().llm && cloud.ready ? 'AI în cloud' : 'agent local';
  renderQuota();
  renderVerdict(true);
  app.onTierChange();
}

function renderTiers() {
  const grid = $('tierGrid');
  grid.replaceChildren();
  for (const k of TIER_ORDER) {
    const t = TIERS[k];
    const card = document.createElement('div');
    card.className = 'tier' + (k === tier() ? ' current' : '');
    card.dataset.tier = k;
    card.innerHTML = '<h3></h3><div class="price"></div><div class="blurb muted"></div><ul></ul><button></button>';
    card.querySelector('h3').textContent = t.name;
    card.querySelector('.price').textContent = t.price;
    card.querySelector('.blurb').textContent = t.blurb;
    card.querySelector('ul').append(...t.perks.map((p) => { const li = document.createElement('li'); li.textContent = p; return li; }));
    const b = card.querySelector('button');
    if (k === tier()) { b.textContent = 'Planul tău'; b.disabled = true; b.className = 'ghost'; }
    else {
      b.textContent = k === 'enterprise' ? 'Contactează vânzările' : TIER_ORDER.indexOf(k) < TIER_ORDER.indexOf(tier()) ? 'Downgrade' : 'Upgrade';
      b.className = k === 'free' ? 'ghost' : 'primary';
      b.addEventListener('click', () => changeTier(k, b));
    }
    grid.append(card);
  }
}

async function changeTier(k, btn) {
  if (k === 'free' && !confirm('Sigur? Rinichii tăi vor afla.')) return;
  btn.disabled = true;
  btn.textContent = k === 'enterprise' ? 'Sunăm vânzările…' : 'Se procesează plata…';
  await new Promise((r) => setTimeout(r, 1200));     // suspans
  let invoice = null;
  try {
    if (cloud.ready) invoice = await cloud.checkout(k);
  } catch (e) {
    app.toast('Plata a eșuat: ' + e.message);
    renderTiers();
    return;
  }
  app.cfg.tier = k;
  app.saveSettings();
  applyTier();
  renderTiers();
  const inv = $('invoice');
  inv.hidden = false;
  inv.textContent = '';
  const lines = invoice
    ? [`Factura nr. ${invoice.id}`, `${TIERS[invoice.from_tier]?.name} → ${TIERS[invoice.to_tier]?.name}`, `Preț de listă: ${invoice.list_price}`, `Reducere: ${invoice.discount}`, `Total de plată: ${invoice.total}`]
    : ['Factură locală (offline)', `Plan nou: ${TIERS[k].name}`, 'Total de plată: 0,00 lei'];
  for (const l of lines) { const div = document.createElement('div'); div.textContent = l; inv.append(div); }
  if (k === 'enterprise') agentSay('Am vorbit cu departamentul de vânzări. Eram tot eu. Aprobat. Bun venit în Enterprise.');
  else if (k !== 'free') agentSay(`Bun venit în ${TIERS[k].name}! Rinichii tăi au primit deja notificarea.`);
  else agentSay('Înapoi pe Free. Reclamele ți-au simțit lipsa.');
}

/* =========================== Clasament =========================== */
export async function refreshBoard() {
  const list = $('leaderboard');
  if (!cloud.ready) {
    list.replaceChildren(emptyRow('wifi-off', 'Clasamentul apare când ești online.'));
    return;
  }
  const rows = await cloud.leaderboard();
  list.replaceChildren();
  if (!rows?.length) { list.replaceChildren(emptyRow('droplet', 'Nimeni n-a băut nimic azi. Primul pahar te pune pe locul 1.')); return; }
  for (const r of rows) {
    const li = document.createElement('li');
    if (r.is_me) li.className = 'me';
    li.innerHTML = '<span class="rank"></span><span class="who"></span><span class="tier-chip"></span><b></b>';
    li.children[0].textContent = r.rank;
    li.children[0].dataset.rank = r.rank;
    li.children[1].textContent = r.nickname + (r.is_me ? ' (tu)' : '');
    li.children[2].textContent = TIERS[r.tier]?.name ?? r.tier;
    li.children[2].dataset.tier = r.tier;
    li.children[3].textContent = `${r.total_ml} mL`;
    list.append(li);
  }
}

function emptyRow(name, text) {
  const li = document.createElement('li');
  li.className = 'empty';
  li.append(icon(name, 'i lg'), Object.assign(document.createElement('span'), { textContent: text }));
  return li;
}

/* =========================== Raport Enterprise =========================== */
function openReport() {
  const ctx = context();
  const table = $('reportTable');
  table.replaceChildren();
  for (const { k, v } of kpiReport(ctx, app.todayEvents())) {
    const tr = document.createElement('tr');
    tr.innerHTML = '<th></th><td></td>';
    tr.children[0].textContent = k;
    tr.children[1].textContent = v;
    if (k === 'Status') tr.children[1].dataset.status = v.toLowerCase().replace(/\s+/g, '-');
    table.append(tr);
  }
  const time = new Date().toLocaleTimeString('ro-RO', { hour: '2-digit', minute: '2-digit' });
  $('reportFoot').textContent = `Generat de HydroGPT™ Enterprise la ${time}. Distribuție: board, rinichi, vezică (doar spre informare).`;
  $('reportDialog').showModal();
}

/* =========================== Pornire =========================== */
export async function initHydro(api) {
  app = api;
  $('btnTier').addEventListener('click', () => { $('invoice').hidden = true; renderTiers(); $('tiersDialog').showModal(); });
  $('btnNoAds').addEventListener('click', () => { renderTiers(); $('tiersDialog').showModal(); });
  $('weekLock').addEventListener('click', () => { renderTiers(); $('tiersDialog').showModal(); });
  $('btnReport').addEventListener('click', openReport);
  $('btnAsk').addEventListener('click', () => ask('Când trebuie să beau?'));
  $('chatForm').addEventListener('submit', (e) => {
    e.preventDefault();
    const q = $('chatInput').value.trim();
    if (!q) return;
    $('chatInput').value = '';
    ask(q);
  });
  $('btnBoard').addEventListener('click', refreshBoard);
  $('btnNick').addEventListener('click', async () => {
    if (!cloud.ready) { app.toast('Fără internet nu-ți poți schimba porecla'); return; }
    const n = prompt('Porecla ta în clasament:', cloud.profile.nickname);
    if (!n || !n.trim()) return;
    try { await cloud.setNickname(n.trim().slice(0, 40)); $('nickname').textContent = cloud.profile.nickname; refreshBoard(); }
    catch (e) { app.toast('Nu am putut salva: ' + e.message); }
  });

  setInterval(() => {
    if (!T().ads) return;
    adIdx = (adIdx + 1) % ADS.length;
    $('adText').textContent = ADS[adIdx];
  }, 12000);
  $('adText').textContent = ADS[0];

  applyTier();
  agentSay('Salut! Sunt HydroGPT™. Îți urmăresc sticla și îți spun când să bei. Întreabă-mă orice (despre apă).');

  setInterval(nudgeLoop, 30000);
  setInterval(refreshBoard, 60000);

  // Cloud-ul pornește în fundal; aplicația nu îl așteaptă.
  if (await cloud.init()) {
    app.cfg.tier = cloud.profile.tier;
    app.saveSettings();
    $('nickname').textContent = cloud.profile.nickname;
    applyTier();
    cloud.pushEvents(app.state.events);     // ce s-a strâns offline
    refreshBoard();
  } else {
    $('nickname').textContent = 'offline';
    refreshBoard();
  }
}

export function onConnectionChange() { renderVerdict(true); }
export function syncGoal(goal) { cloud.setGoal(goal); }
export function clearCloud() { return cloud.clearEvents().then(refreshBoard); }
