// node test_agent.mjs — testează agentul HydroGPT (web/agent.js)
import assert from 'node:assert/strict';
import {
  TIERS, TIER_ORDER, displayMl, expectedByNow, buildContext, council, decide,
  speak, reasoningTrace, reactToEvent, localReply, kpiReport, norm,
} from '../web/agent.js';

let n = 0;
const t = (name, fn) => { fn(); n++; console.log('ok', name); };
const at = (h, m = 0) => new Date(2026, 9, 19, h, m).getTime();
const rng = () => 0;   // determinist: prima variantă

t('abonamente complete', () => {
  assert.deepEqual(TIER_ORDER, Object.keys(TIERS));
  for (const k of TIER_ORDER) assert.ok(TIERS[k].perks.length >= 3);
});

t('HydroGPT doar din Ultra', () => {
  assert.deepEqual(TIER_ORDER.filter((k) => TIERS[k].agent), ['ultra', 'enterprise']);
});

t('precizia depinde de abonament', () => {
  assert.equal(displayMl(437, 'free'), 450);
  assert.equal(displayMl(437, 'pro'), 440);
  assert.equal(displayMl(437, 'ultra'), 437);
});

t('curba ideală', () => {
  assert.equal(expectedByNow(2000, new Date(at(7))), 0);
  assert.equal(expectedByNow(2000, new Date(at(15))), 1000);
  assert.equal(expectedByNow(2000, new Date(at(23))), 2000);
});

const base = { capacity: 750, goal: 2000, connected: true, calibrated: true, ml: 500 };

t('context din evenimente', () => {
  const now = at(15);
  const ctx = buildContext({ ...base, now, drinks: [
    { ts: at(9), ml: 200 }, { ts: at(14, 50), ml: 150 }, { ts: at(14, 40), ml: 200 },
    { ts: now - 30 * 3600000, ml: 999 },   // ieri: nu contează la azi
  ] });
  assert.equal(ctx.todayMl, 550);
  assert.equal(ctx.sips, 3);
  assert.equal(ctx.minutesSinceDrink, 10);
  assert.equal(ctx.last30Ml, 350);
  assert.equal(ctx.deficit, 1000 - 550);
});

t('fără senzor: blind; sticla ridicată: missing', () => {
  assert.equal(decide(buildContext({ ...base, connected: false, now: at(12) })).verdict, 'blind');
  assert.equal(decide(buildContext({ ...base, absent: true, now: at(12) })).verdict, 'missing');
});

t('în urmă și fără apă de mult => bea, cantitate rotunjită și limitată', () => {
  const d = decide(buildContext({ ...base, now: at(16), drinks: [{ ts: at(9), ml: 200 }] }));
  assert.equal(d.verdict, 'drink');
  assert.equal(d.votes.length, 3);
  assert.equal(d.amountMl, 400);                     // deficit 942 => 471 => plafon 400
  assert.equal(d.amountMl % 50, 0);
});

t('vezica și creierul blochează după mult băut recent', () => {
  const now = at(16);
  const d = decide(buildContext({ ...base, now, drinks: [{ ts: now - 10 * 60000, ml: 400 }] }));
  assert.equal(d.verdict, 'wait');
  assert.ok(d.waitMin >= 10 && d.waitMin <= 90);
  assert.equal(d.votes.find((v) => v.organ === 'Vezica').vote, 'așteaptă');
});

t('noaptea se doarme', () => {
  const d = decide(buildContext({ ...base, now: at(2), drinks: [] }));
  assert.equal(d.verdict, 'wait');
});

t('sticla aproape goală => reumplere', () => {
  const d = decide(buildContext({ ...base, ml: 40, now: at(16), drinks: [] }));
  assert.equal(d.verdict, 'refill');
});

t('fiecare abonament are replici pentru fiecare verdict', () => {
  for (const tier of TIER_ORDER) {
    for (const verdict of ['drink', 'wait', 'refill', 'missing', 'blind']) {
      const s = speak({ verdict, amountMl: 250, waitMin: 20, votes: [] }, tier, rng);
      assert.ok(s.length > 10, `${tier}/${verdict}`);
      if (verdict === 'drink') assert.ok(s.includes('250'), `${tier} menționează cantitatea`);
    }
  }
  assert.ok(speak({ verdict: 'wait', waitMin: 5 }, 'free', rng).includes('sponsorizat'));
});

t('raționament: blocat pe Free, complet pe Ultra cu voturile', () => {
  const ctx = buildContext({ ...base, now: at(16), drinks: [] });
  const d = decide(ctx);
  assert.ok(reasoningTrace(ctx, d, 'free').some((s) => s.includes('disponibil în Ultra')));
  const ultra = reasoningTrace(ctx, d, 'ultra');
  assert.ok(ultra.some((s) => s.includes('Rinichiul')));
  assert.ok(reasoningTrace(ctx, d, 'enterprise').some((s) => s.includes('Guvernanță')));
});

t('reacții la evenimente', () => {
  const ctx = buildContext({ ...base, now: at(16), drinks: [{ ts: at(16), ml: 2100 }] });
  assert.ok(reactToEvent({ type: 'drink', ml: 2100 }, 'pro', ctx, rng).includes('OBIECTIV'));
  assert.ok(reactToEvent({ type: 'refill', ml: 300 }, 'enterprise', ctx, rng).includes('300'));
});

t('chat local: intenții, cu și fără diacritice', () => {
  const ctx = buildContext({ ...base, now: at(15), drinks: [{ ts: at(10), ml: 300 }] });
  assert.equal(norm('Cât am BĂUT?'), 'cat am baut?');
  assert.ok(localReply('cât am băut azi?', ctx, 'pro', rng).includes('300 / 2000'));
  assert.ok(localReply('cat am baut', ctx, 'pro', rng).includes('300 / 2000'));
  assert.ok(localReply('pot bea o bere?', ctx, 'free', rng).includes('ficatului'));
  assert.ok(localReply('cât costă upgrade?', ctx, 'free', rng).includes('Pro'));
  assert.ok(localReply('cât costă upgrade?', ctx, 'enterprise', rng).includes('marea'));
  assert.ok(localReply('când trebuie să beau?', ctx, 'ultra', rng).length > 10);
});

t('raport KPI', () => {
  const now = at(15);
  const evs = [{ type: 'drink', ts: at(9), ml: 300 }, { type: 'drink', ts: at(12), ml: 200 }, { type: 'refill', ts: at(13), ml: 500 }];
  const ctx = buildContext({ ...base, now, drinks: evs.filter((e) => e.type === 'drink') });
  const r = Object.fromEntries(kpiReport(ctx, evs).map((x) => [x.k, x.v]));
  assert.equal(r['Sip count'], '2');
  assert.equal(r['Dimensiune medie sip'], '250 mL');
  assert.equal(r['Sip velocity'], '83 mL/h');        // 500 mL în 6 h
  assert.equal(r['Water churn (reumpleri)'], '1');
  assert.ok(r.Status === 'Needs attention');          // deficit exact 500 mL: încă nu e AT RISK
});

console.log(`\n${n} teste trecute`);
