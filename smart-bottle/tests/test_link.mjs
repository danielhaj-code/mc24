// node test_link.mjs — testează heartbeat-ul (LinkMonitor, mesajul P) din web/logic.js
import assert from 'node:assert/strict';
import { LinkMonitor, parseMessage } from '../web/logic.js';

let n = 0;
const t = (name, fn) => { fn(); n++; console.log('ok', name); };
const L = parseMessage('L,2000,50,1');

// rulează monitorul secundă cu secundă; feed(sec) întoarce mesajul primit în acea secundă sau null
function run(m, from, to, feed) {
  const actions = [];
  for (let s = from; s <= to; s++) {
    const now = s * 1000;
    const msg = feed(s);
    if (msg) m.onMessage(msg, now);
    const a = m.tick(now);
    if (a) actions.push([s, a]);
  }
  return actions;
}

t('parsează pong-ul', () => {
  assert.deepEqual(parseMessage('P,123,1'), { type: 'pong', uptime: 123, sourceOk: true });
  assert.deepEqual(parseMessage('P,5,0\n'), { type: 'pong', uptime: 5, sourceOk: false });
  assert.equal(parseMessage('P,x,1'), null);
  assert.equal(parseMessage('P'), null);
});

t('legătură sănătoasă: doar ping de rutină la 5 s, niciodată reconectare', () => {
  const m = new LinkMonitor({}, 0);
  const acts = run(m, 1, 30, (s) => (s % 5 === 1 ? parseMessage(`P,${s},1`) : L));
  assert.ok(acts.every(([, a]) => a === 'ping'));
  assert.ok(acts.length >= 5 && acts.length <= 7, `ping-uri: ${acts.length}`);
  assert.equal(m.health(30000), 'ok');
});

t('liniște: ping la 3 s, apoi reconectare la 10 s', () => {
  const m = new LinkMonitor({}, 0);
  run(m, 1, 5, () => L);                       // ultima citire la 5 s
  const acts = run(m, 6, 16, () => null);
  const pings = acts.filter(([, a]) => a === 'ping').map(([s]) => s);
  assert.ok(pings.includes(8), `ping-uri la ${pings}`);   // 3 s de liniște
  assert.ok(pings.every((s, i) => i === 0 || s - pings[i - 1] >= 2));
  const rec = acts.find(([, a]) => a === 'reconnect');
  assert.equal(rec[0], 15);                    // 10 s de liniște
  assert.equal(m.health(15000), 'stalled');
});

t('STM32 tace dar ESP32 răspunde: stare „source”, fără reconectare', () => {
  const m = new LinkMonitor({}, 0);
  run(m, 1, 3, () => L);
  const acts = run(m, 4, 40, (s) => (s % 2 === 0 ? parseMessage(`P,${s},0`) : null));
  assert.ok(!acts.some(([, a]) => a === 'reconnect'));
  assert.equal(m.health(40000), 'source');
});

t('revenire după reconectare: reset curăță starea', () => {
  const m = new LinkMonitor({}, 0);
  run(m, 1, 20, () => null);
  assert.equal(m.health(20000), 'stalled');
  m.reset(20000);
  assert.equal(m.health(20500), 'ok');
});

t('detectează repornirea ESP32 și măsoară latența', () => {
  const m = new LinkMonitor({}, 0);
  m.tick(5000);                                // ping la 5 s
  m.onMessage(parseMessage('P,500,1'), 5040);
  assert.equal(m.rtt, 40);
  m.onMessage(parseMessage('P,3,1'), 9000);    // uptime a scăzut
  assert.equal(m.reboots, 1);
});

console.log(`\n${n} teste trecute`);
