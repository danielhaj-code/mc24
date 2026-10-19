// Logică pură (fără DOM / Bluetooth) — testabilă în Node: node ../tests/test_logic.mjs

export const SHAPES = {
  cylinder: 'Cilindru',
  cone: 'Pahar conic',
  neck: 'Sticlă cu gât îngust',
};

// Fracția de volum (0..1) ocupată când lichidul are înălțimea relativă h (0..1).
export function volumeFraction(shape, h) {
  h = Math.min(1, Math.max(0, h));
  switch (shape) {
    case 'cone': {
      // rază 0.6 la bază, 1.0 sus => aria ∝ (a + b·y)²
      const a = 0.6, b = 0.4;
      const integ = (y) => ((a + b * y) ** 3 - a ** 3) / (3 * b);
      return integ(h) / integ(1);
    }
    case 'neck': {
      // corp cilindric până la 75% din înălțime, gât cu rază 0.45 deasupra
      const split = 0.75, neckArea = 0.45 ** 2;
      const total = split + (1 - split) * neckArea;
      const v = h <= split ? h : split + (h - split) * neckArea;
      return v / total;
    }
    default:
      return h;
  }
}

// Înălțimea relativă din valoarea brută, NEtăiată la [0,1] (pentru detectarea sticlei absente).
export function relativeHeight(raw, cal) {
  const span = cal.full - cal.empty;
  if (span === 0) return 0;
  return (raw - cal.empty) / span;
}

// Sub această înălțime relativă considerăm că sticla lipsește (ridicată de pe cântar)
// sau că citirea e aberantă; citirea nu intră în detecția de consum.
export const ABSENT_BELOW = -0.05;

export function mlFromRaw(raw, cal) {
  const h = relativeHeight(raw, cal);
  return Math.round(cal.capacity * volumeFraction(cal.shape, h));
}

export function levelColor(pct) {
  if (pct >= 50) return 'ok';
  if (pct >= 20) return 'mid';
  return 'low';
}

// Parsează un mesaj de la dispozitiv (vezi PROTOCOL.md).
export function parseMessage(line) {
  const t = line.trim();
  if (!t) return null;
  const p = t.split(',');
  if (p[0] === 'L' && p.length === 4) {
    const [raw, pct, stable] = p.slice(1).map(Number);
    if ([raw, pct, stable].some(Number.isNaN)) return null;
    return { type: 'level', raw, pct, stable: stable === 1 };
  }
  if (p[0] === 'P' && p.length === 3) {
    const [uptime, ok] = p.slice(1).map(Number);
    if ([uptime, ok].some(Number.isNaN)) return null;
    return { type: 'pong', uptime, sourceOk: ok === 1 };
  }
  if (t === 'A,C') return { type: 'calAck' };
  if (t === 'E,C') return { type: 'calErr' };
  return null;
}

// Detector de evenimente de consum / reumplere.
// Linia de bază se mută DOAR la evenimente, deci înghițiturile mici se adună
// până trec pragul, iar zgomotul mic nu produce nimic.
export class EventDetector {
  constructor({ drinkMin = 30, refillMin = 50 } = {}) {
    this.drinkMin = drinkMin;
    this.refillMin = refillMin;
    this.baseline = null;
  }

  reset(ml = null) { this.baseline = ml; }

  // Primește doar citiri stabile cu sticla prezentă. Întoarce un eveniment sau null.
  feed(ml, ts) {
    if (this.baseline === null) { this.baseline = ml; return null; }
    const delta = ml - this.baseline;
    if (delta <= -this.drinkMin) {
      this.baseline = ml;
      return { type: 'drink', ml: -delta, ts };
    }
    if (delta >= this.refillMin) {
      this.baseline = ml;
      return { type: 'refill', ml: delta, ts };
    }
    return null;
  }
}

// Cheie de zi locală "YYYY-MM-DD".
export function dayKey(ts) {
  const d = new Date(ts);
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${d.getFullYear()}-${m}-${day}`;
}

// Totalul băut pe zi pentru ultimele `days` zile (cea mai veche prima).
export function dailyTotals(events, days, now = Date.now()) {
  const out = [];
  const byDay = new Map();
  for (const e of events) {
    if (e.type !== 'drink') continue;
    const k = dayKey(e.ts);
    byDay.set(k, (byDay.get(k) || 0) + e.ml);
  }
  const base = new Date(now);
  base.setHours(12, 0, 0, 0); // la prânz: evită problemele de la schimbarea orei
  for (let i = days - 1; i >= 0; i--) {
    const d = new Date(base);
    d.setDate(base.getDate() - i);
    const k = dayKey(d.getTime());
    out.push({ day: k, ml: byDay.get(k) || 0 });
  }
  return out;
}

// Calibrarea trimisă dispozitivului ca "C,gol,plin".
export function calibrationCommand(cal) {
  return `C,${Math.round(cal.empty)},${Math.round(cal.full)}\n`;
}

// Supravegherea legăturii BLE (heartbeat). Logică pură: primește timpul, întoarce ce e de făcut.
//  - trimite „P” periodic și imediat ce se face liniște;
//  - liniște prea lungă => conexiunea e blocată => reconectare;
//  - pong cu sursa_ok = 0 => Bluetooth-ul merge, dar STM32 nu mai trimite (nu reconectăm degeaba).
export const HEARTBEAT = {
  pingEvery: 5000,    // ping de rutină
  quietAfter: 3000,   // citirile vin la 1 s; după 3 s de liniște întrebăm
  pingGap: 2000,      // nu trimitem ping-uri mai des de atât
  stallAfter: 10000,  // nimic primit atâta timp => conexiune blocată
  sourceStale: 12000, // cât timp rămâne valabilă informația „sursa nu trimite”
};

export class LinkMonitor {
  constructor(opts = {}, now = 0) {
    this.o = { ...HEARTBEAT, ...opts };
    this.reboots = 0;            // contoare pe toată sesiunea, nu se resetează la reconectare
    this.stalls = 0;
    this.reset(now);
  }

  reset(now) {
    this.lastRx = now;          // orice linie primită
    this.lastData = now;        // ultima citire L (date de la STM32)
    this.lastPing = -Infinity;
    this.pingSentAt = null;
    this.rtt = null;
    this.pong = null;           // { at, sourceOk, uptime }
  }

  onMessage(msg, now) {
    this.lastRx = now;
    if (!msg) return;
    if (msg.type === 'level') this.lastData = now;
    if (msg.type === 'pong') {
      if (this.pingSentAt !== null) { this.rtt = now - this.pingSentAt; this.pingSentAt = null; }
      if (this.pong && msg.uptime < this.pong.uptime) this.reboots++;   // ESP32 a repornit între timp
      this.pong = { at: now, sourceOk: msg.sourceOk, uptime: msg.uptime };
    }
  }

  // Starea legăturii: ok | quiet | stalled | source
  health(now) {
    const silence = now - this.lastRx;
    if (silence >= this.o.stallAfter) return 'stalled';
    if (silence >= this.o.quietAfter) return 'quiet';
    if (this.pong && !this.pong.sourceOk && now - this.pong.at < this.o.sourceStale
        && now - this.lastData >= this.o.quietAfter) return 'source';
    return 'ok';
  }

  // Apelat periodic (o dată pe secundă). Întoarce 'ping', 'reconnect' sau null.
  tick(now) {
    const h = this.health(now);
    if (h === 'stalled') { this.stalls++; return 'reconnect'; }
    const sincePing = now - this.lastPing;
    if ((h === 'quiet' && sincePing >= this.o.pingGap) || sincePing >= this.o.pingEvery) {
      this.lastPing = now;
      this.pingSentAt = now;
      return 'ping';
    }
    return null;
  }
}
