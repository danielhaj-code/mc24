// node test_logic.mjs
import assert from 'node:assert/strict';
import {
  volumeFraction, mlFromRaw, relativeHeight, parseMessage, EventDetector,
  dailyTotals, calibrationCommand, levelColor, ABSENT_BELOW,
} from '../web/logic.js';

const near = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) < eps, `${a} != ${b}`);

// forme: capete 0 și 1, monoton crescătoare
for (const s of ['cylinder', 'cone', 'neck']) {
  near(volumeFraction(s, 0), 0);
  near(volumeFraction(s, 1), 1);
  let prev = -1;
  for (let h = 0; h <= 1.0001; h += 0.05) {
    const v = volumeFraction(s, h);
    assert.ok(v >= prev, `${s} nemonoton la ${h}`);
    prev = v;
  }
}
near(volumeFraction('cylinder', 0.5), 0.5);
assert.ok(volumeFraction('cone', 0.5) < 0.5);   // jos e mai îngust
assert.ok(volumeFraction('neck', 0.75) > 0.9);  // corpul ține aproape tot

// conversie
const cal = { empty: 1000, full: 3000, capacity: 750, shape: 'cylinder' };
assert.equal(mlFromRaw(2000, cal), 375);
assert.equal(mlFromRaw(500, cal), 0);
assert.equal(mlFromRaw(9000, cal), 750);
assert.equal(mlFromRaw(2000, { ...cal, empty: 3000, full: 1000 }), 375); // inversat
assert.ok(relativeHeight(800, cal) < ABSENT_BELOW);  // sub gol => absentă

// mesaje
assert.deepEqual(parseMessage('L,2310,64,1\n'), { type: 'level', raw: 2310, pct: 64, stable: true });
assert.deepEqual(parseMessage('L,-8000,0,0'), { type: 'level', raw: -8000, pct: 0, stable: false });
assert.deepEqual(parseMessage('A,C'), { type: 'calAck' });
assert.equal(parseMessage('L,1,2'), null);
assert.equal(parseMessage('L,a,2,1'), null);
assert.equal(parseMessage(''), null);

// detector
const d = new EventDetector({ drinkMin: 30, refillMin: 50 });
assert.equal(d.feed(700, 1), null);                          // linie de bază
assert.equal(d.feed(690, 2), null);                          // zgomot / sorbitură mică
assert.equal(d.feed(675, 3), null, 'sub prag');              // 700 - 675 = 25 < 30 => nimic
const e1 = d.feed(660, 4);                                   // 700 - 660 = 40 => consum
assert.deepEqual(e1, { type: 'drink', ml: 40, ts: 4 });
assert.equal(d.feed(680, 5), null);                          // +20: sub pragul de reumplere
const e2 = d.feed(750, 6);
assert.deepEqual(e2, { type: 'refill', ml: 90, ts: 6 });

// totaluri zilnice
const now = new Date(2026, 9, 13, 18, 0).getTime();
const ev = [
  { type: 'drink', ml: 200, ts: new Date(2026, 9, 13, 9).getTime() },
  { type: 'drink', ml: 300, ts: new Date(2026, 9, 13, 15).getTime() },
  { type: 'refill', ml: 500, ts: new Date(2026, 9, 13, 16).getTime() },
  { type: 'drink', ml: 150, ts: new Date(2026, 9, 11, 10).getTime() },
];
const t = dailyTotals(ev, 7, now);
assert.equal(t.length, 7);
assert.deepEqual(t[6], { day: '2026-10-13', ml: 500 });
assert.deepEqual(t[4], { day: '2026-10-11', ml: 150 });
assert.equal(t[0].day, '2026-10-07');

assert.equal(calibrationCommand({ empty: 120.4, full: 3050 }), 'C,120,3050\n');
assert.equal(levelColor(64), 'ok');
assert.equal(levelColor(30), 'mid');
assert.equal(levelColor(5), 'low');

console.log('Toate testele logicii au trecut');
