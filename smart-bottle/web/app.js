import {
  SHAPES, volumeFraction, relativeHeight, mlFromRaw, levelColor, parseMessage,
  EventDetector, dailyTotals, dayKey, calibrationCommand, ABSENT_BELOW, LinkMonitor,
  volumeLine, todayLine, sinceLine, weekLine,
} from './logic.js';
import { displayMl } from './agent.js';
import { authGate } from './auth.js';
import { playUnskippable } from './ads.js';
import { initMobile, keepAwake, haptic, refreshWakeLock, wakeLockSupported } from './mobile.js';
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
  drinkMin: 30, remindHours: 2, notify: false, wakeLock: true,
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
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Operațiile GATT pot rămâne agățate la nesfârșit când legătura se blochează; le limităm în timp.
const withTimeout = (p, ms, what) => Promise.race([
  p, new Promise((_, rej) => setTimeout(() => rej(new Error(`${what}: fără răspuns în ${ms / 1000} s`)), ms)),
]);
const RECONNECT_DELAYS = [500, 1500, 3000, 6000, 10000];   // ~21 s în total, apoi cerem un gest

class BleDevice {
  constructor() {
    this.device = null; this.rx = null; this.tx = null; this.buf = '';
    this.queue = Promise.resolve();
    this.manual = false; this.reconnecting = false;
    this.onValue = (e) => this.receive(e.target.value);
    this.onDrop = () => this.handleDrop();
  }

  async connect(onLine, onState) {
    this.onLine = onLine; this.onState = onState; this.manual = false;
    await this.pick();
    this.onState('busy');
    await this.attach();
    this.onState('on');
  }

  // Alegerea dispozitivului (cere un gest al utilizatorului).
  async pick() {
    if (this.device) this.device.removeEventListener('gattserverdisconnected', this.onDrop);
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [NUS_SERVICE] }, { name: 'SmartBottle' }],
      optionalServices: [NUS_SERVICE],
    });
    this.device.addEventListener('gattserverdisconnected', this.onDrop);
  }

  async attach() {
    const server = await withTimeout(this.device.gatt.connect(), 10000, 'conectarea GATT');
    const svc = await withTimeout(server.getPrimaryService(NUS_SERVICE), 5000, 'serviciul NUS');
    this.rx = await withTimeout(svc.getCharacteristic(NUS_RX), 5000, 'caracteristica RX');
    const tx = await withTimeout(svc.getCharacteristic(NUS_TX), 5000, 'caracteristica TX');
    this.tx?.removeEventListener('characteristicvaluechanged', this.onValue);
    this.tx = tx;
    tx.addEventListener('characteristicvaluechanged', this.onValue);
    await withTimeout(tx.startNotifications(), 5000, 'notificările');
    this.buf = '';
    this.queue = Promise.resolve();   // o scriere agățată de pe vechea legătură nu mai blochează coada
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
        const w = this.rx.writeValueWithoutResponse ? this.rx.writeValueWithoutResponse(chunk) : this.rx.writeValue(chunk);
        await withTimeout(w, 3000, 'scrierea BLE');
      }
    }).catch((e) => console.warn('BLE write', e.message));
    return this.queue;
  }

  // Cerută de heartbeat când legătura pare blocată deși browserul o crede deschisă.
  forceReconnect() {
    if (!this.manual && !this.reconnecting) this.reconnect();
  }

  handleDrop() {
    this.rx = null;
    if (this.manual) { this.onState('off'); return; }
    if (!this.reconnecting) this.reconnect();
  }

  async reconnect() {
    this.reconnecting = true;
    this.rx = null;
    this.onState('reconnecting');
    try { if (this.device.gatt.connected) this.device.gatt.disconnect(); } catch { /* deja închisă */ }
    for (const delay of RECONNECT_DELAYS) {
      await sleep(delay);
      if (this.manual) break;
      try {
        await this.attach();
        this.reconnecting = false;
        this.onState('reconnected');
        return;
      } catch (e) {
        console.warn('reconectare', e.message);
        try { this.device.gatt.disconnect(); } catch { /* nimic de închis */ }
      }
    }
    this.reconnecting = false;
    this.onState(this.manual ? 'off' : 'lost');
  }

  // Din butonul „Reconectează” (gest al utilizatorului): întâi același dispozitiv,
  // apoi, dacă nu se poate, o asociere nouă din lista Bluetooth.
  async repair() {
    this.manual = false;
    this.onState('busy');
    try {
      await this.attach();
    } catch {
      await this.pick();
      await this.attach();
    }
    this.onState('reconnected');
  }

  // „B” (la revedere) întâi: Chrome pe Windows poate ține legătura BLE deschisă și după
  // gatt.disconnect(), deci ESP32 nu ar afla. La „B” închide el conexiunea.
  async disconnect() {
    this.manual = true;
    if (this.device?.gatt?.connected) {
      await Promise.race([this.send('B\n'), sleep(300)]);
      try { this.device.gatt.disconnect(); } catch { /* deja închisă */ }
    } else this.onState?.('off');
  }

  // La închiderea paginii nu avem timp de așteptat: trimitem „B” și atât.
  bye() {
    if (this.rx && this.device?.gatt?.connected) this.send('B\n');
  }
}

/* =========================== Transport: simulator =========================== */
// Imită STM32 + ESP32: emite "L,raw,pct,stable" la 1 s, cu înghițituri și reumpleri,
// răspunde la ping ("P") și poate simula o legătură blocată sau o placă STM32 tăcută.
class DemoDevice {
  constructor() { this.timer = null; this.cal = { empty: 800, full: 3000 }; this.started = Date.now(); this.stallUntil = 0; this.freezeUntil = 0; }
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

  get stalled() { return Date.now() < this.stallUntil; }
  get frozen() { return Date.now() < this.freezeUntil; }

  tick() {
    if (this.stalled || this.frozen) return;
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
    if (this.stalled) return Promise.resolve();          // legătură blocată: nu răspunde nimic
    for (const line of text.split('\n')) {
      const p = line.trim().split(',');
      if (p[0] === 'P' && p.length === 1) {
        const up = Math.floor((Date.now() - this.started) / 1000);
        setTimeout(() => this.onLine(`P,${up},${this.frozen ? 0 : 1}`), 40);
      } else if (this.frozen) {
        continue;                                          // „STM32” tace, ESP32 răspunde doar la ping
      } else if (p[0] === 'C' && p.length === 3) {
        const [e, f] = [Number(p[1]), Number(p[2])];
        if (e === f) { this.onLine('E,C'); } else { this.cal = { empty: e, full: f }; this.onLine('A,C'); }
      } else if (p[0] === 'R') {
        this.tick();
      }
    }
    return Promise.resolve();
  }

  // Simulări pentru demonstrații (din consolă: smartBottleDebug.stall(15) / .freeze(20)).
  simulateStall(sec) { this.stallUntil = Date.now() + sec * 1000; }
  simulateFreeze(sec) { this.freezeUntil = Date.now() + sec * 1000; }

  forceReconnect() {
    this.onState('reconnecting');
    setTimeout(() => {               // o legătură nouă scapă de blocaj
      this.stallUntil = 0;
      this.onState('reconnected');
    }, 1500);
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
  monitor.onMessage(msg, Date.now());   // orice mesaj dovedește că legătura trăiește
  if (!msg) return;
  if (msg.type === 'pong') { renderLinkHealth(); return; }
  if (msg.type === 'calAck') {
    if (lcd.quietAck) { lcd.quietAck = false; return; }   // confirmarea calibrării venite chiar de la sticlă
    toast('Calibrare trimisă dispozitivului'); return;
  }
  if (msg.type === 'calFromDevice') { applyDeviceCalibration(msg); return; }
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
  pushVolume(state.ml, now);
  const ev = detector.feed(state.ml, now);
  if (ev) {
    db.add('events', ev);
    state.events.push(ev);
    if (ev.type === 'drink') { state.lastDrinkTs = ev.ts; state.reminded = false; hideReminder(); }
    toast(ev.type === 'drink' ? `Ai băut ${ev.ml} mL` : `Reumplere: +${ev.ml} mL`);
    haptic(ev.type === 'drink' ? [18, 60, 18] : 30);
    renderToday(); renderEvents(); renderWeekChart();
    pushTodayStats();
    onEvent(ev);
  }
}

function onState(s) {
  const pill = $('connState');
  if (s === 'busy' || s === 'reconnecting') {
    pill.className = 'pill busy';
    pill.textContent = s === 'busy' ? 'Se conectează…' : 'Se reconectează…';
    state.reconnecting = s === 'reconnecting';
    return;
  }
  state.reconnecting = false;
  const on = s === 'on' || s === 'reconnected';
  state.connected = on;
  $('btnConnect').textContent = on ? 'Deconectează' : 'Conectează';
  $('btnConnect').className = on ? 'ghost' : 'primary';   // „Deconectează” nu mai e acțiunea principală
  $('btnConnect').disabled = false;
  $('btnDemo').hidden = on;
  $('linkLost').hidden = s !== 'lost';
  if (on) {
    state.lostLink = null;
    state.connectedSince = state.connectedSince ?? Date.now();
    detector.reset(null);                            // linie de bază nouă după (re)conectare
    state.link.send(calibrationCommand(cfg));        // STM32 ține calibrarea doar în RAM
    state.link.send('R\n');
    lcd.lastMl = null;
    pushTodayStats({ week: true });                  // statistici pentru meniul de pe LCD
    startLcdTimer();
    startHeartbeat();
    keepAwake(true);
    if (s === 'reconnected') toast('Reconectat');
  } else {
    stopHeartbeat();
    stopLcdTimer();
    keepAwake(false);
    if (s === 'lost') state.lostLink = state.link;   // păstrat pentru butonul „Reconectează”
    state.connectedSince = null;
    state.link = null;
    hideReminder();
  }
  renderLinkHealth();
  onConnectionChange();
}

/* =========================== Statistici pentru LCD-ul STM32 =========================== */
// STM32 nu ține istoric: aplicația îi trimite ce să afișeze în meniu (vezi logic.js: volumeLine etc.).
// Scrierile trec prin coada lui BleDevice.send, deci liniile pleacă pe rând, fără să se piardă.
const lcd = {
  lastMl: null, lastMlAt: 0,   // ultimul „M” trimis
  todayMl: null,               // ultimul total de azi trimis ca „W,6”
  quietAck: false,             // nu anunța „A,C” pentru o calibrare venită de la sticlă
  timer: null,
};

function sendToDevice(line) {
  if (state.connected && state.link && !state.reconnecting) state.link.send(line);
}

function minutesSinceDrink(now = Date.now()) {
  if (!state.lastDrinkTs || dayKey(state.lastDrinkTs) !== dayKey(now)) return -1;
  return Math.max(0, Math.floor((now - state.lastDrinkTs) / 60000));
}

// Volumul curent: doar la schimbare și cel mult o dată pe secundă.
function pushVolume(ml, now = Date.now()) {
  if (!state.connected || ml === lcd.lastMl || now - lcd.lastMlAt < 1000) return;
  lcd.lastMl = ml; lcd.lastMlAt = now;
  sendToDevice(volumeLine(ml, cfg.capacity));
}

// „T” + „U”, apoi „W,6” dacă s-a schimbat totalul de azi; cu week = true, toate cele 7 zile.
function pushTodayStats({ week = false } = {}) {
  if (!state.connected) return;
  const drinks = todayEvents().filter((e) => e.type === 'drink');
  const total = drinks.reduce((s, e) => s + e.ml, 0);
  sendToDevice(todayLine(total, cfg.goal, drinks.length));
  sendToDevice(sinceLine(minutesSinceDrink()));
  if (week) dailyTotals(state.events, 7).forEach((d, i) => sendToDevice(weekLine(i, d.ml)));
  else if (total !== lcd.todayMl) sendToDevice(weekLine(6, total));
  lcd.todayMl = total;
}

function startLcdTimer() {
  clearInterval(lcd.timer);
  lcd.timer = setInterval(() => sendToDevice(sinceLine(minutesSinceDrink())), 60000);
}
function stopLcdTimer() { clearInterval(lcd.timer); lcd.timer = null; }

// „K,gol,plin”: utilizatorul a calibrat pe ecranul tactil. O tratăm exact ca butoanele
// „Setează gol/plin” (salvare, UI, linie de bază nouă) și trimitem înapoi „C,gol,plin”.
function applyDeviceCalibration({ empty, full }) {
  if (empty === full) return;
  cfg.empty = empty; cfg.full = full;
  cfg.calibratedEmpty = cfg.calibratedFull = true;
  lcd.quietAck = state.connected;
  lcd.lastMl = null;                 // mL se recalculează cu noua calibrare
  applyCalibration();                // salvează, resetează detectorul, trimite „C,...” dacă e conectat
  if (state.raw !== null) {
    state.absent = relativeHeight(state.raw, cfg) < ABSENT_BELOW;
    state.h = Math.min(1, Math.max(0, relativeHeight(state.raw, cfg)));
    state.ml = mlFromRaw(state.raw, cfg);
  }
  renderReadout();
  toast('Calibrare primită de la sticlă');
}

/* =========================== Heartbeat: sănătatea legăturii =========================== */
const monitor = new LinkMonitor();
let hbTimer = null;

function startHeartbeat() {
  monitor.reset(Date.now());
  clearInterval(hbTimer);
  hbTimer = setInterval(heartbeat, 1000);
}
function stopHeartbeat() { clearInterval(hbTimer); hbTimer = null; }

function heartbeat() {
  if (!state.connected || !state.link || state.reconnecting) return;
  const now = Date.now();
  const action = monitor.tick(now);
  if (action === 'ping') state.link.send('P\n');
  if (action === 'reconnect') {
    toast('Sticla nu mai răspunde. Mă reconectez…');
    monitor.reset(now);
    state.link.forceReconnect();
  }
  renderLinkHealth(now);
}

function renderLinkHealth(now = Date.now()) {
  const pill = $('connState');
  const h = state.connected && !state.reconnecting ? monitor.health(now) : null;
  $('sourceStall').hidden = h !== 'source';
  if (!state.connected || state.reconnecting) {
    if (!state.reconnecting && !pill.classList.contains('busy')) { pill.className = 'pill off'; pill.textContent = 'Deconectat'; }
    if (!state.reconnecting) pill.removeAttribute('title');
    return;
  }
  const demo = state.link instanceof DemoDevice;
  pill.className = 'pill ' + ({ ok: 'on', quiet: 'weak', stalled: 'weak', source: 'warn' }[h]);
  pill.textContent = h === 'ok' ? (demo ? 'Demo' : 'Conectat') : h === 'source' ? 'Fără date' : 'Semnal slab';
  const ago = Math.round((now - monitor.lastRx) / 1000);
  pill.title = `Ultimul mesaj: acum ${ago} s` + (monitor.rtt !== null ? `, latență ${monitor.rtt} ms` : '');
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
  $('calLive').textContent = state.raw === null ? 'fără date' : `${state.raw} (${state.stable ? 'stabil' : 'se mișcă'})`;
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
  // viewBox la lățimea reală: textul rămâne la 10 px, nu se întinde odată cu cardul
  const W = Math.max(240, Math.round(svg.clientWidth || 320)), H = 140, top = 8, bottom = 128;
  svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
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
  const W = Math.max(240, Math.round(svg.clientWidth || 320)), top = 14, bottom = 128;
  svg.setAttribute('viewBox', `0 0 ${W} 150`);
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
    pushTodayStats({ week: true });                // zilele s-au decalat cu una
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
  $('wakeLock').checked = cfg.wakeLock;
  $('wakeLockRow').hidden = !wakeLockSupported();
}

function applyCalibration() {
  cfg.calibrated = cfg.empty !== cfg.full && cfg.calibratedEmpty && cfg.calibratedFull;
  saveSettings();
  renderSettings();
  detector.reset(null);
  if (state.connected && cfg.empty !== cfg.full) state.link.send(calibrationCommand(cfg));
}

async function captureCal(which) {
  if (state.raw === null) { toast('Nicio citire încă — conectează dispozitivul'); return; }
  await playUnskippable('calibrate');   // doar pe Free
  if (state.raw === null) return;
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
    if (key === 'capacity') { detector.reset(null); lcd.lastMl = null; }
    if (key === 'goal') { syncGoal(v); pushTodayStats(); }
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

  $('wakeLock').addEventListener('change', (e) => { cfg.wakeLock = e.target.checked; saveSettings(); refreshWakeLock(); });

  const openSettings = () => { renderSettings(); renderReadout(); $('settingsDialog').showModal(); };
  $('btnSettings').addEventListener('click', openSettings);
  $('tabSettings').addEventListener('click', openSettings);

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
    pushTodayStats({ week: true });
    toast('Istoric șters');
  });
}

async function exportCsv() {
  await playUnskippable('export');      // doar pe Free
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

// Evenimente din cloud care lipsesc local (alt dispozitiv sau altă adresă a site-ului):
// le adăugăm, ca istoricul, graficele și meniul de pe LCD să arate aceleași date peste tot.
// Cheia e aceeași ca la urcare (client_id = `${ts}-${type}`), deci nimic nu se dublează.
async function mergeCloudEvents(rows) {
  const have = new Set(state.events.map((e) => `${e.ts}-${e.type}`));
  const fresh = rows.filter((e) => !have.has(`${e.ts}-${e.type}`));
  if (!fresh.length) return 0;
  for (const ev of fresh) { await db.add('events', ev); state.events.push(ev); }
  state.events.sort((a, b) => a.ts - b.ts);
  const drinks = state.events.filter((e) => e.type === 'drink');
  state.lastDrinkTs = drinks.length ? drinks.at(-1).ts : null;
  renderToday(); renderEvents(); renderWeekChart();
  pushTodayStats({ week: true });
  return fresh.length;
}

/* =========================== Conectare =========================== */

// Pagina se închide: anunțăm sticla, ca LCD-ul să arate imediat „Deconectat”.
addEventListener('pagehide', () => state.link?.bye?.());
async function toggleConnect() {
  if (state.link) { state.link.disconnect(); return; }
  state.lostLink = null;
  $('linkLost').hidden = true;
  if (!navigator.bluetooth) { toast('Web Bluetooth nu e disponibil aici — încearcă Demo'); return; }
  $('btnConnect').disabled = true;
  state.link = new BleDevice();
  try {
    await state.link.connect(onLine, onState);
    playUnskippable('connect');           // conexiunea rămâne activă în spatele reclamei
  } catch (e) {
    state.link = null;
    onState('off');
    if (e.name !== 'NotFoundError') toast('Conectarea a eșuat: ' + e.message);   // NotFoundError = utilizatorul a închis dialogul
  }
}

async function repairLink() {
  const link = state.lostLink;
  if (!link) return;
  $('btnRepair').disabled = true;
  state.link = link;
  try {
    await link.repair();
  } catch (e) {
    onState('lost');                    // păstrează legătura pentru o nouă încercare
    if (e.name !== 'NotFoundError') toast('Nu am putut reconecta: ' + e.message);
  } finally {
    $('btnRepair').disabled = false;
  }
}

async function startDemo() {
  if (state.link) return;
  await playUnskippable('demo');
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

/* =========================== Ferestre: închidere la click în afară =========================== */
// Toate ferestrele (setări, abonamente, raport, acces) se închid la click/tap pe fundalul din jur.
// Reclama video nu e „.sheet”, deci rămâne imposibil de închis. closedby="any" e varianta nativă
// din browserele noi; codul de mai jos acoperă și browserele care nu o au.
function closeSheetsOnOutsideClick() {
  for (const d of document.querySelectorAll('dialog.sheet')) {
    d.setAttribute('closedby', 'any');
    let downOutside = false;
    const outside = (e) => {
      if (e.target !== d) return false;           // clicul a nimerit conținutul ferestrei
      const r = d.getBoundingClientRect();
      return e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom;
    };
    // apăsarea ȘI ridicarea trebuie să fie în afară: o selecție de text care iese din fereastră nu o închide
    d.addEventListener('pointerdown', (e) => { downOutside = outside(e); });
    d.addEventListener('click', (e) => { if (downOutside && outside(e)) d.close(); downOutside = false; });
  }
}

/* =========================== Pornire =========================== */
async function init() {
  let unsupportedHidden = false;
  try { unsupportedHidden = localStorage.getItem('sb.hideUnsupported') === '1'; } catch { /* fără stocare */ }
  if (!navigator.bluetooth && !unsupportedHidden) $('unsupported').hidden = false;
  $('btnUnsupportedClose').addEventListener('click', () => {
    $('unsupported').hidden = true;
    try { localStorage.setItem('sb.hideUnsupported', '1'); } catch { /* fără stocare */ }
  });
  bindSettings();
  renderSettings();
  setBottleShape();
  renderReadout();
  requestAnimationFrame(frame);

  $('btnConnect').addEventListener('click', toggleConnect);
  $('btnDemo').addEventListener('click', startDemo);
  $('btnRepair').addEventListener('click', repairLink);
  $('btnLostDismiss').addEventListener('click', () => { $('linkLost').hidden = true; state.lostLink = null; });

  // Pentru demonstrații: simulează o legătură blocată sau o placă STM32 care nu mai trimite.
  window.smartBottleDebug = {
    stall: (sec = 15) => state.link?.simulateStall?.(sec) ?? 'merge doar în modul Demo',
    freeze: (sec = 20) => state.link?.simulateFreeze?.(sec) ?? 'merge doar în modul Demo',
    health: () => ({ health: monitor.health(Date.now()), lastRxMsAgo: Date.now() - monitor.lastRx, rtt: monitor.rtt, stalls: monitor.stalls, reboots: monitor.reboots }),
  };

  await db.open();
  await db.deleteBefore('readings', Date.now() - 30 * DAY_MS);   // istoric detaliat: 30 de zile
  const t0 = startOfToday();
  state.todayReadings = (await db.since('readings', t0)).map((r) => ({ ts: r.ts, ml: r.ml }));
  state.events = await db.since('events', t0 - 7 * DAY_MS);
  const drinks = state.events.filter((e) => e.type === 'drink');
  state.lastDrinkTs = drinks.length ? drinks.at(-1).ts : null;

  renderToday(); renderEvents(); renderWeekChart(); renderDayChart();
  setInterval(checkReminder, 30000);
  // graficele se redesenează când se schimbă lățimea cardului
  let resizeTimer;
  new ResizeObserver(() => { clearTimeout(resizeTimer); resizeTimer = setTimeout(() => { renderDayChart(); renderWeekChart(); }, 120); })
    .observe($('chartWeek').parentElement);

  await authGate();
  initHydro({
    cfg, state, saveSettings, toast, todayEvents, notify: systemNotify,
    loadLocalEvents: (since) => db.since('events', since),
    mergeCloudEvents,
    onTierChange: () => { renderReadout(); renderWeekChart(); },
  });

  // scurtătura „Demo” din aplicația instalată
  if (new URLSearchParams(location.search).has('demo')) startDemo();

  closeSheetsOnOutsideClick();

  initMobile({
    wakeLockEnabled: () => cfg.wakeLock,
    // La revenirea în prim-plan, timer-ele au fost încetinite: pornim curat și verificăm imediat.
    onResume: () => {
      checkReminder();
      if (state.connected && state.link && !state.reconnecting) {
        monitor.reset(Date.now());
        state.link.send('P\n');
      }
    },
  });

  if ('serviceWorker' in navigator && window.isSecureContext) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
}

init();
