import {
  SHAPES, volumeFraction, relativeHeight, mlFromRaw, levelColor, parseMessage,
  EventDetector, dailyTotals, dayKey, calibrationCommand, ABSENT_BELOW,
} from './logic.js';
import { displayMl } from './agent.js';
import { initHydro, onEvent, onConnectionChange, syncGoal, clearCloud, icon } from './hydro.js';

const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';   // scriem aici
const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';   // notificări de aici
const DAY_MS = 24 * 3600 * 1000;
const $ = (id) => document.getElementById(id);

/* =========================== Setări =========================== */
const DEFAULTS = {
  empty: 0, full: 3000, calibrated: false, calibratedEmpty: false, calibratedFull: false,
  capacity: 750, shape: 'cylinder', goal: 2000,
  drinkMin: 30, remindHours: 2, notify: false,
  tier: 'free',
};

function loadSettings() {
  try { return { ...DEFAULTS, ...JSON.parse(localStorage.getItem('sb.settings') || '{}') }; }
  catch { return { ...DEFAULTS }; }
}
function saveSettings() {
  try { localStorage.setItem('sb.settings', JSON.stringify(cfg)); } catch { /* fără stocare: doar în memorie */ }
}
const cfg = loadSettings();

/* =========================== IndexedDB =========================== */
const db = {
  handle: null,
  open() {
    return new Promise((resolve) => {
      if (!('indexedDB' in window)) return resolve(null);
      const req = indexedDB.open('smartbottle', 1);
      req.onupgradeneeded = () => {
        const d = req.result;
        for (const name of ['readings', 'events']) {
          const s = d.createObjectStore(name, { keyPath: 'id', autoIncrement: true });
          s.createIndex('ts', 'ts');
        }
      };
      req.onsuccess = () => { this.handle = req.result; resolve(this.handle); };
      req.onerror = () => resolve(null);   // istoricul rămâne doar în memorie
    });
  },
  add(store, obj) {
    if (!this.handle) return Promise.resolve();
    return new Promise((res) => {
      const tx = this.handle.transaction(store, 'readwrite');
      tx.objectStore(store).add(obj);
      tx.oncomplete = res; tx.onerror = res;
    });
  },
  since(store, ts) {
    if (!this.handle) return Promise.resolve([]);
    return new Promise((res) => {
      const out = [];
      const tx = this.handle.transaction(store, 'readonly');
      const cur = tx.objectStore(store).index('ts').openCursor(IDBKeyRange.lowerBound(ts));
      cur.onsuccess = () => { const c = cur.result; if (c) { out.push(c.value); c.continue(); } };
      tx.oncomplete = () => res(out); tx.onerror = () => res(out);
    });
  },
  deleteBefore(store, ts) {
    if (!this.handle) return Promise.resolve();
    return new Promise((res) => {
      const tx = this.handle.transaction(store, 'readwrite');
      const cur = tx.objectStore(store).index('ts').openCursor(IDBKeyRange.upperBound(ts, true));
      cur.onsuccess = () => { const c = cur.result; if (c) { c.delete(); c.continue(); } };
      tx.oncomplete = res; tx.onerror = res;
    });
  },
  clear() {
    if (!this.handle) return Promise.resolve();
    return new Promise((res) => {
      const tx = this.handle.transaction(['readings', 'events'], 'readwrite');
      tx.objectStore('readings').clear(); tx.objectStore('events').clear();
      tx.oncomplete = res; tx.onerror = res;
    });
  },
};

/* =========================== Transport: Web Bluetooth =========================== */
class BleDevice {
  constructor() { this.device = null; this.rx = null; this.queue = Promise.resolve(); this.buf = ''; this.manual = false; }

  async connect(onLine, onState) {
    this.onLine = onLine; this.onState = onState; this.manual = false;
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [NUS_SERVICE] }, { name: 'SmartBottle' }],
      optionalServices: [NUS_SERVICE],
    });
    this.device.addEventListener('gattserverdisconnected', () => this.handleDrop());
    await this.attach();
  }

  async attach() {
    this.onState('busy');
    const server = await this.device.gatt.connect();
    const svc = await server.getPrimaryService(NUS_SERVICE);
    this.rx = await svc.getCharacteristic(NUS_RX);
    const tx = await svc.getCharacteristic(NUS_TX);
    tx.addEventListener('characteristicvaluechanged', (e) => this.receive(e.target.value));
    await tx.startNotifications();
    this.buf = '';
    this.onState('on');
  }

  receive(dataView) {
    this.buf += new TextDecoder().decode(dataView);
    let i;
    while ((i = this.buf.indexOf('\n')) >= 0) {
      const line = this.buf.slice(0, i);
      this.buf = this.buf.slice(i + 1);
      this.onLine(line);
    }
    if (this.buf.length > 64) this.buf = '';   // gunoi fără terminator
  }

  // Scrierile GATT trebuie serializate; bucăți de max. 20 octeți (MTU implicit).
  send(text) {
    const bytes = new TextEncoder().encode(text);
    this.queue = this.queue.then(async () => {
      if (!this.rx) return;
      for (let i = 0; i < bytes.length; i += 20) {
        const chunk = bytes.slice(i, i + 20);
        if (this.rx.writeValueWithoutResponse) await this.rx.writeValueWithoutResponse(chunk);
        else await this.rx.writeValue(chunk);
      }
    }).catch((e) => console.warn('BLE write', e));
    return this.queue;
  }

  async handleDrop() {
    this.rx = null;
    if (this.manual) { this.onState('off'); return; }
    // reconectare automată: 3 încercări cu pauză crescătoare
    for (let attempt = 1; attempt <= 3; attempt++) {
      this.onState('busy');
      await new Promise((r) => setTimeout(r, attempt * 1500));
      if (this.manual) break;
      try { await this.attach(); this.onState('reconnected'); return; } catch { /* încercăm din nou */ }
    }
    this.onState('off');
  }

  disconnect() {
    this.manual = true;
    if (this.device?.gatt?.connected) this.device.gatt.disconnect();
    else this.onState?.('off');
  }
}

/* =========================== Transport: simulator =========================== */
// Imită STM32 + ESP32: emite "L,raw,pct,stable" la 1 s, cu înghițituri și reumpleri.
class DemoDevice {
  constructor() { this.timer = null; this.cal = { empty: 800, full: 3000 }; }
  static RAW_EMPTY = 800;
  static RAW_FULL = 3000;

  connect(onLine, onState) {
    this.onLine = onLine; this.onState = onState;
    this.level = 0.8;           // înălțime relativă reală
    this.history = [];
    this.sipLeft = 0;           // secunde de „sticlă înclinată”
    this.nextSip = 6;
    this.timer = setInterval(() => this.tick(), 1000);
    onState('on');
    return Promise.resolve();
  }

  tick() {
    this.nextSip--;
    if (this.nextSip <= 0 && this.sipLeft === 0) {
      this.sipLeft = 3;
      this.nextSip = 12 + Math.floor(Math.random() * 10);
    }
    let noise = (Math.random() - 0.5) * 6;
    if (this.sipLeft > 0) {
      this.sipLeft--;
      noise = (Math.random() - 0.5) * 400;    // în timpul băutului citirile sunt haotice
      if (this.sipLeft === 0) {
        this.level -= 0.06 + Math.random() * 0.08;
        if (this.level < 0.12) this.level = 0.95;   // reumplere
      }
    }
    const raw = Math.round(DemoDevice.RAW_EMPTY + (DemoDevice.RAW_FULL - DemoDevice.RAW_EMPTY) * this.level + noise);
    this.history.push(raw);
    if (this.history.length > 5) this.history.shift();
    const stable = this.history.length === 5 && Math.max(...this.history) - Math.min(...this.history) <= 15;
    const span = this.cal.full - this.cal.empty;
    const pct = span ? Math.max(0, Math.min(100, Math.trunc((raw - this.cal.empty) * 100 / span))) : 0;
    this.onLine(`L,${raw},${pct},${stable ? 1 : 0}`);
  }

  send(text) {
    for (const line of text.split('\n')) {
      const p = line.trim().split(',');
      if (p[0] === 'C' && p.length === 3) {
        const [e, f] = [Number(p[1]), Number(p[2])];
        if (e === f) { this.onLine('E,C'); } else { this.cal = { empty: e, full: f }; this.onLine('A,C'); }
      } else if (p[0] === 'R') {
        this.tick();
      }
    }
    return Promise.resolve();
  }

  disconnect() { clearInterval(this.timer); this.timer = null; this.onState('off'); }
}

/* =========================== Stare aplicație =========================== */
const state = {
  link: null,            // BleDevice | DemoDevice
  connected: false,
  raw: null, ml: null, h: 0, stable: false, absent: false,
  lastLogged: null,      // { ts, ml }
  todayReadings: [],     // { ts, ml }
  events: [],            // ultimele 8 zile
  lastDrinkTs: null,
  connectedSince: null,
  reminded: false,
};
const detector = new EventDetector({ drinkMin: cfg.drinkMin });

/* =========================== Mesaje de la dispozitiv =========================== */
function onLine(line) {
  const msg = parseMessage(line);
  if (!msg) return;
  if (msg.type === 'calAck') { toast('Calibrare trimisă dispozitivului'); return; }
  if (msg.type === 'calErr') { toast('Dispozitivul a respins calibrarea (gol = plin)'); return; }

  const now = Date.now();
  state.raw = msg.raw;
  state.stable = msg.stable;
  const h = relativeHeight(msg.raw, cfg);
  state.absent = cfg.calibrated && h < ABSENT_BELOW;
  state.h = Math.min(1, Math.max(0, h));
  state.ml = mlFromRaw(msg.raw, cfg);
  renderReadout();

  if (!msg.stable || state.absent) return;

  // log: doar citiri stabile, la schimbare de >= 5 mL sau o dată pe minut
  const last = state.lastLogged;
  if (!last || Math.abs(last.ml - state.ml) >= 5 || now - last.ts >= 60000) {
    const rec = { ts: now, raw: msg.raw, ml: state.ml };
    state.lastLogged = rec;
    db.add('readings', rec);
    state.todayReadings.push({ ts: now, ml: state.ml });
    renderDayChart();
  }

  if (!cfg.calibrated) return;   // fără calibrare, mL nu înseamnă nimic
  const ev = detector.feed(state.ml, now);
  if (ev) {
    db.add('events', ev);
    state.events.push(ev);
    if (ev.type === 'drink') { state.lastDrinkTs = ev.ts; state.reminded = false; hideReminder(); }
    toast(ev.type === 'drink' ? `Ai băut ${ev.ml} mL` : `Reumplere: +${ev.ml} mL`);
    renderToday(); renderEvents(); renderWeekChart();
    onEvent(ev);
  }
}

function onState(s) {
  const pill = $('connState');
  pill.className = 'pill ' + (s === 'reconnected' ? 'on' : s);
  if (s === 'busy') { pill.textContent = 'Se conectează…'; return; }
  const on = s === 'on' || s === 'reconnected';
  state.connected = on;
  pill.textContent = on ? (state.link instanceof DemoDevice ? 'Demo' : 'Conectat') : 'Deconectat';
  $('btnConnect').textContent = on ? 'Deconectează' : 'Conectează';
  $('btnConnect').disabled = false;
  $('btnDemo').hidden = on;
  if (on) {
    state.connectedSince = state.connectedSince ?? Date.now();
    detector.reset(null);                            // linie de bază nouă după (re)conectare
    state.link.send(calibrationCommand(cfg));        // STM32 ține calibrarea doar în RAM
    state.link.send('R\n');
  } else {
    state.connectedSince = null;
    state.link = null;
    hideReminder();
  }
  onConnectionChange();
}

/* =========================== Desen: sticla =========================== */
const BOTTLE = { top: 40, bottom: 305 };
const OUTLINES = {
  cylinder: 'M40,52 Q40,40 52,40 H148 Q160,40 160,52 V293 Q160,305 148,305 H52 Q40,305 40,293 Z',
  cone: 'M28,40 H172 L150,298 Q149,305 142,305 H58 Q51,305 50,298 Z',
  neck: 'M73,40 H127 V92 Q160,98 160,122 V293 Q160,305 148,305 H52 Q40,305 40,293 V122 Q40,98 73,92 Z',
};
const anim = { shown: 0, phase: 0 };

function setBottleShape() {
  const d = OUTLINES[cfg.shape] || OUTLINES.cylinder;
  $('outline').setAttribute('d', d);
  $('clipPathShape').setAttribute('d', d);
}

function wavePath(levelY, amp, phase, freq) {
  let d = `M0,320 L0,${levelY}`;
  for (let x = 0; x <= 200; x += 5) {
    d += ` L${x},${(levelY + amp * Math.sin(x * freq + phase)).toFixed(1)}`;
  }
  return d + ' L200,320 Z';
}

function frame() {
  const target = state.raw === null ? 0 : state.h;
  anim.shown += (target - anim.shown) * 0.06;     // nivelul alunecă spre valoarea nouă
  anim.phase += 0.05;
  const y = BOTTLE.bottom - anim.shown * (BOTTLE.bottom - BOTTLE.top);
  const amp = anim.shown > 0.01 ? 4 : 0;
  $('wave1').setAttribute('d', wavePath(y, amp, anim.phase, 0.06));
  $('wave2').setAttribute('d', wavePath(y - 2, amp * 0.8, -anim.phase * 0.8 + 1.5, 0.045));
  $('bottleWell').style.setProperty('--fill', anim.shown.toFixed(3));
  requestAnimationFrame(frame);
}

function renderReadout() {
  const pctVol = state.ml === null ? 0 : Math.round((state.ml / cfg.capacity) * 100);
  const svg = $('bottle');
  svg.dataset.level = levelColor(pctVol);
  svg.dataset.absent = state.absent ? '1' : '0';
  $('mlNow').textContent = !cfg.calibrated ? '–' : state.absent ? '—' : displayMl(state.ml, cfg.tier);
  $('pctNow').textContent = state.raw === null ? '–' : pctVol;
  $('rawNow').textContent = state.raw ?? '–';
  const st = state.raw === null ? 'idle' : state.absent ? 'absent' : state.stable ? 'stable' : 'moving';
  $('stableNow').dataset.state = st;
  $('stableNow').textContent = { idle: 'fără date', absent: 'sticla lipsește', stable: 'stabil', moving: 'se mișcă' }[st];
}

/* =========================== Desen: grafice =========================== */
const SVGNS = 'http://www.w3.org/2000/svg';
function el(name, attrs, text) {
  const e = document.createElementNS(SVGNS, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (text !== undefined) e.textContent = text;
  return e;
}

function startOfToday() { const d = new Date(); d.setHours(0, 0, 0, 0); return d.getTime(); }

function renderDayChart() {
  const svg = $('chartDay');
  svg.replaceChildren();
  const W = 320, H = 140, top = 8, bottom = 128;
  for (const f of [0, 0.5, 1]) {
    const y = bottom - f * (bottom - top);
    svg.append(el('line', { x1: 0, x2: W, y1: y, y2: y, class: 'grid-line' }));
  }
  const pts = state.todayReadings;
  if (pts.length < 2) {
    svg.append(el('text', { x: W / 2, y: H / 2, 'text-anchor': 'middle', class: 'empty-msg' }, 'Încă nu sunt destule citiri azi'));
    return;
  }
  // axa X: de la prima citire de azi până acum (minim o oră), ca să nu fie strivită pe 24 h
  const tEnd = Date.now();
  const tStart = Math.min(pts[0].ts, tEnd - 3600 * 1000);
  const xy = pts.map((p) => [
    ((p.ts - tStart) / (tEnd - tStart)) * W,
    bottom - Math.min(1, p.ml / cfg.capacity) * (bottom - top),
  ]);
  const line = xy.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(1)},${y.toFixed(1)}`).join(' ');
  svg.append(el('path', { d: `${line} L${xy.at(-1)[0].toFixed(1)},${bottom} L${xy[0][0].toFixed(1)},${bottom} Z`, class: 'area' }));
  svg.append(el('path', { d: line, class: 'series' }));
  svg.append(el('text', { x: 2, y: 139 }, fmtTime(tStart)));
  svg.append(el('text', { x: W - 2, y: 139, 'text-anchor': 'end' }, 'acum'));
}

const WEEKDAYS = ['D', 'L', 'Ma', 'Mi', 'J', 'V', 'S'];
function renderWeekChart() {
  const svg = $('chartWeek');
  svg.replaceChildren();
  const locked = cfg.tier === 'free';
  $('weekLock').hidden = !locked;
  $('weekWrap').classList.toggle('locked', locked);
  const data = dailyTotals(state.events, 7);
  const W = 320, top = 14, bottom = 128;
  const max = Math.max(cfg.goal * 1.15, ...data.map((d) => d.ml), 1);
  const slot = W / 7, bw = slot * 0.56;
  const gy = bottom - (cfg.goal / max) * (bottom - top);
  svg.append(el('line', { x1: 0, x2: W, y1: gy, y2: gy, class: 'goal-line' }));
  svg.append(el('text', { x: W - 2, y: gy - 4, 'text-anchor': 'end' }, `obiectiv ${cfg.goal}`));
  data.forEach((d, i) => {
    const h = (d.ml / max) * (bottom - top);
    const x = i * slot + (slot - bw) / 2;
    const r = el('rect', { x, y: bottom - h, width: bw, height: Math.max(h, 0.5), rx: 3, class: 'bar' + (d.ml >= cfg.goal ? ' met' : '') });
    r.append(el('title', {}, `${d.day}: ${d.ml} mL`));
    svg.append(r);
    const wd = WEEKDAYS[new Date(d.day + 'T12:00').getDay()];
    svg.append(el('text', { x: i * slot + slot / 2, y: 143, 'text-anchor': 'middle' }, wd));
    if (d.ml > 0) svg.append(el('text', { x: i * slot + slot / 2, y: bottom - h - 3, 'text-anchor': 'middle' }, d.ml >= 1000 ? (d.ml / 1000).toFixed(1) + 'L' : d.ml));
  });
}

/* =========================== Azi + evenimente =========================== */
function todayEvents() {
  const k = dayKey(Date.now());
  return state.events.filter((e) => dayKey(e.ts) === k);
}

function renderToday() {
  const total = todayEvents().filter((e) => e.type === 'drink').reduce((s, e) => s + e.ml, 0);
  $('todayMl').textContent = total;
  $('goalMl').textContent = cfg.goal;
  const bar = $('goalBar');
  bar.style.width = Math.min(100, (total / cfg.goal) * 100) + '%';
  bar.classList.toggle('done', total >= cfg.goal);
  $('lastDrink').textContent = state.lastDrinkTs && dayKey(state.lastDrinkTs) === dayKey(Date.now())
    ? `Ultima înghițitură: ${fmtTime(state.lastDrinkTs)} (${ago(state.lastDrinkTs)})`
    : 'Niciun consum înregistrat azi';
}

function renderEvents() {
  const list = $('events');
  const evs = todayEvents().slice().reverse();
  list.replaceChildren();
  if (!evs.length) {
    const li = document.createElement('li');
    li.className = 'empty';
    li.append(icon('droplet', 'i lg'), Object.assign(document.createElement('span'), {
      textContent: 'Nicio înghițitură azi. Ia sticla și bea: apare aici în câteva secunde.',
    }));
    list.append(li); return;
  }
  for (const e of evs) {
    const li = document.createElement('li');
    li.className = e.type;
    const left = document.createElement('span');
    const time = document.createElement('span');
    time.className = 'mono time';
    time.textContent = fmtTime(e.ts);
    left.append(time, document.createTextNode(e.type === 'drink' ? 'Înghițitură' : 'Reumplere'));
    const right = document.createElement('b');
    right.textContent = `${e.type === 'drink' ? '−' : '+'}${e.ml} mL`;
    li.append(left, right);
    list.append(li);
  }
}

const fmtTime = (ts) => new Date(ts).toLocaleTimeString('ro-RO', { hour: '2-digit', minute: '2-digit' });
function ago(ts) {
  const m = Math.round((Date.now() - ts) / 60000);
  if (m < 1) return 'acum';
  if (m < 60) return `acum ${m} min`;
  const h = Math.floor(m / 60);
  return `acum ${h} h ${m % 60} min`;
}

/* =========================== Memento (doar cu pagina deschisă) =========================== */
let currentDay = dayKey(Date.now());
function checkReminder() {
  if (dayKey(Date.now()) !== currentDay) {          // a trecut miezul nopții
    currentDay = dayKey(Date.now());
    state.todayReadings = [];
    renderDayChart(); renderEvents(); renderWeekChart();
  }
  renderToday();   // actualizează „acum X min”
  if (!state.connected || state.reminded || !cfg.calibrated) return;
  const since = Math.max(state.lastDrinkTs ?? 0, state.connectedSince ?? 0);
  if (!since || Date.now() - since < cfg.remindHours * 3600 * 1000) return;
  state.reminded = true;
  const text = `N-ai mai băut apă de peste ${cfg.remindHours} h.`;
  $('reminderText').textContent = text;
  $('reminder').hidden = false;
  if (cfg.notify && 'Notification' in window && Notification.permission === 'granted') {
    try { new Notification('Smart Bottle', { body: text, icon: 'icon.svg', tag: 'sb-remind' }); } catch { /* Android: doar prin SW */ }
    navigator.serviceWorker?.ready.then((r) => r.showNotification('Smart Bottle', { body: text, icon: 'icon.svg', tag: 'sb-remind' })).catch(() => {});
  }
}
function hideReminder() { $('reminder').hidden = true; }

function systemNotify(text) {
  if (!cfg.notify || !('Notification' in window) || Notification.permission !== 'granted') return;
  try { new Notification('HydroGPT™', { body: text, icon: 'icon.svg', tag: 'sb-agent' }); } catch { /* Android: doar prin SW */ }
  navigator.serviceWorker?.ready.then((r) => r.showNotification('HydroGPT™', { body: text, icon: 'icon.svg', tag: 'sb-agent' })).catch(() => {});
}

/* =========================== Setări UI =========================== */
function renderSettings() {
  $('calEmpty').textContent = cfg.calibratedEmpty ? cfg.empty : '–';
  $('calFull').textContent = cfg.calibratedFull ? cfg.full : '–';
  $('shape').value = cfg.shape;
  $('capacity').value = cfg.capacity;
  $('goal').value = cfg.goal;
  $('drinkMin').value = cfg.drinkMin;
  $('remindHours').value = cfg.remindHours;
  $('notify').checked = cfg.notify;
}

function applyCalibration() {
  cfg.calibrated = cfg.empty !== cfg.full && cfg.calibratedEmpty && cfg.calibratedFull;
  saveSettings();
  renderSettings();
  detector.reset(null);
  if (state.connected && cfg.empty !== cfg.full) state.link.send(calibrationCommand(cfg));
}

function captureCal(which) {
  if (state.raw === null) { toast('Nicio citire încă — conectează dispozitivul'); return; }
  if (!state.stable) toast('Atenție: citirea nu e stabilă încă');
  cfg[which] = state.raw;
  cfg[which === 'empty' ? 'calibratedEmpty' : 'calibratedFull'] = true;
  if (cfg.calibratedEmpty && cfg.calibratedFull && cfg.empty === cfg.full) {
    toast('GOL și PLIN au aceeași valoare — refă calibrarea');
  }
  applyCalibration();
  renderReadout();
}

function bindSettings() {
  const sel = $('shape');
  for (const [k, v] of Object.entries(SHAPES)) sel.append(new Option(v, k));
  sel.addEventListener('change', () => { cfg.shape = sel.value; saveSettings(); setBottleShape(); detector.reset(null); });

  const num = (id, key, min, max) => $(id).addEventListener('change', (e) => {
    const v = Number(e.target.value);
    if (!Number.isFinite(v) || v < min || v > max) { e.target.value = cfg[key]; return; }
    cfg[key] = v; saveSettings();
    if (key === 'drinkMin') detector.drinkMin = v;
    if (key === 'capacity') detector.reset(null);
    if (key === 'goal') syncGoal(v);
    renderToday(); renderWeekChart(); renderDayChart(); renderReadout();
  });
  num('capacity', 'capacity', 50, 5000);
  num('goal', 'goal', 250, 6000);
  num('drinkMin', 'drinkMin', 10, 300);
  num('remindHours', 'remindHours', 0.5, 12);

  $('notify').addEventListener('change', async (e) => {
    if (e.target.checked && 'Notification' in window && Notification.permission !== 'granted') {
      const p = await Notification.requestPermission();
      if (p !== 'granted') { e.target.checked = false; toast('Notificările nu au fost permise'); }
    }
    cfg.notify = e.target.checked; saveSettings();
  });

  $('btnEmpty').addEventListener('click', () => captureCal('empty'));
  $('btnFull').addEventListener('click', () => captureCal('full'));
  $('btnDismiss').addEventListener('click', hideReminder);

  $('btnExport').addEventListener('click', exportCsv);
  $('btnClear').addEventListener('click', async () => {
    if (!confirm('Ștergi tot istoricul de citiri și consum? Acțiunea nu poate fi anulată.')) return;
    await db.clear();
    await clearCloud();
    state.events = []; state.todayReadings = []; state.lastDrinkTs = null; state.lastLogged = null;
    renderToday(); renderEvents(); renderWeekChart(); renderDayChart();
    toast('Istoric șters');
  });
}

async function exportCsv() {
  const [readings, events] = await Promise.all([db.since('readings', 0), db.since('events', 0)]);
  const rows = [['tip', 'data', 'ora', 'raw', 'ml']];
  for (const r of readings) rows.push(['citire', dayKey(r.ts), fmtTime(r.ts), r.raw, r.ml]);
  for (const e of events) rows.push([e.type === 'drink' ? 'consum' : 'reumplere', dayKey(e.ts), fmtTime(e.ts), '', e.ml]);
  const blob = new Blob([rows.map((r) => r.join(',')).join('\n')], { type: 'text/csv' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `smart-bottle-${dayKey(Date.now())}.csv`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

/* =========================== Conectare =========================== */
async function toggleConnect() {
  if (state.link) { state.link.disconnect(); return; }
  if (!navigator.bluetooth) { toast('Web Bluetooth nu e disponibil aici — încearcă Demo'); return; }
  $('btnConnect').disabled = true;
  state.link = new BleDevice();
  try {
    await state.link.connect(onLine, onState);
  } catch (e) {
    state.link = null;
    onState('off');
    if (e.name !== 'NotFoundError') toast('Conectarea a eșuat: ' + e.message);   // NotFoundError = utilizatorul a închis dialogul
  }
}

function startDemo() {
  if (state.link) return;
  if (!cfg.calibrated) {
    cfg.empty = DemoDevice.RAW_EMPTY; cfg.full = DemoDevice.RAW_FULL;
    cfg.calibratedEmpty = cfg.calibratedFull = true;
    applyCalibration();
    toast('Demo: am aplicat o calibrare de test');
  }
  state.link = new DemoDevice();
  state.link.connect(onLine, onState);
}

/* =========================== UI comun =========================== */
let toastTimer;
function toast(text) {
  const t = $('toast');
  t.textContent = text;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
}

/* =========================== Pornire =========================== */
async function init() {
  if (!navigator.bluetooth) $('unsupported').hidden = false;
  bindSettings();
  renderSettings();
  setBottleShape();
  renderReadout();
  requestAnimationFrame(frame);

  $('btnConnect').addEventListener('click', toggleConnect);
  $('btnDemo').addEventListener('click', startDemo);

  await db.open();
  await db.deleteBefore('readings', Date.now() - 30 * DAY_MS);   // istoric detaliat: 30 de zile
  const t0 = startOfToday();
  state.todayReadings = (await db.since('readings', t0)).map((r) => ({ ts: r.ts, ml: r.ml }));
  state.events = await db.since('events', t0 - 7 * DAY_MS);
  const drinks = state.events.filter((e) => e.type === 'drink');
  state.lastDrinkTs = drinks.length ? drinks.at(-1).ts : null;

  renderToday(); renderEvents(); renderWeekChart(); renderDayChart();
  setInterval(checkReminder, 30000);

  initHydro({
    cfg, state, saveSettings, toast, todayEvents, notify: systemNotify,
    onTierChange: () => { renderReadout(); renderWeekChart(); },
  });

  if ('serviceWorker' in navigator && window.isSecureContext) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
}

init();
