// HydroGPT™ — agentul de hidratare. Logică pură (fără DOM), testabilă în Node.
// Decizia e luată de „Consiliul Organelor”: Rinichiul, Vezica și Creierul votează,
// majoritatea câștigă. Abonamentul decide doar CUM ți se spune, nu CE.

/* =========================== Abonamente =========================== */
export const TIERS = {
  free: {
    name: 'Free', price: '0 lei', blurb: 'Apă, dar cu reclame.',
    nudgeMin: 60, chatPerDay: 3, ads: true, precision: 50, council: false,
    weekChart: false, llm: false, report: false,
    perks: ['Afișaj rotunjit la ±50 mL', '3 întrebări pe zi pentru HydroGPT', 'Reclame la apă (de la sponsorul nostru, Apa)', 'Memento o dată pe oră'],
  },
  pro: {
    name: 'Pro', price: '9,99 lei / lună', blurb: 'Pentru cei care beau serios.',
    nudgeMin: 20, chatPerDay: Infinity, ads: false, precision: 10, council: false,
    weekChart: true, llm: false, report: false,
    perks: ['Precizie ±10 mL', 'HydroGPT nelimitat, ton de antrenor', 'Graficul pe 7 zile deblocat', 'Fără reclame', 'Memento la 20 min'],
  },
  ultra: {
    name: 'Ultra', price: '49,99 lei / lună', blurb: 'Somelier de apă personal.',
    nudgeMin: 10, chatPerDay: Infinity, ads: false, precision: 1, council: true,
    weekChart: true, llm: true, report: false,
    perks: ['Precizie la mililitru', 'Consiliul Organelor: vezi cum votează rinichii', 'AI adevărat (Gemini) care îți citește sticla și istoricul', 'Temă aurie', 'Memento la 10 min'],
  },
  enterprise: {
    name: 'Enterprise', price: 'Contactează vânzările', blurb: 'Hidratare la scară, cu guvernanță.',
    nudgeMin: 5, chatPerDay: Infinity, ads: false, precision: 1, council: true,
    weekChart: true, llm: true, report: true,
    perks: ['Tot din Ultra', 'Raport KPI de hidratare pentru board', 'Monitorizare în timp real (5 min)', 'Conformitate ISO H₂O:2026*', 'Account manager dedicat (tot HydroGPT)'],
  },
};
export const TIER_ORDER = ['free', 'pro', 'ultra', 'enterprise'];

// Rotunjirea afișajului în funcție de abonament (da, ±50 mL pe Free e intenționat).
export function displayMl(ml, tier) {
  const step = TIERS[tier]?.precision ?? 1;
  return Math.round(ml / step) * step;
}

export const ADS = [
  'Ți-e sete? Încearcă APA™ — 0 calorii, 0 zahăr, 100% apă.',
  'APA™ Premium: acum și udă.',
  'Rinichii tăi merită APA™. Și Pro.',
  'Studiile arată că 9 din 10 pești preferă APA™.',
  'Nou! APA™ Light — cu 0% mai puțină apă.',
  'Upgrade la Pro și reclama asta dispare. Ca apa din sticla ta.',
];

/* =========================== Context =========================== */
export const WAKE_HOUR = 8;
export const SLEEP_HOUR = 22;

// Câtă apă ar fi trebuit băută până acum, pe o curbă liniară între trezire și culcare.
export function expectedByNow(goal, date) {
  const h = date.getHours() + date.getMinutes() / 60;
  const f = Math.min(1, Math.max(0, (h - WAKE_HOUR) / (SLEEP_HOUR - WAKE_HOUR)));
  return Math.round(goal * f);
}

// Rezumatul situației, din datele reale ale aplicației.
// drinks = evenimente de consum (cel puțin cele din ultima oră și de azi).
export function buildContext({ now = Date.now(), ml = null, capacity = 750, goal = 2000,
  drinks = [], connected = false, calibrated = false, absent = false }) {
  const date = new Date(now);
  const startDay = new Date(date); startDay.setHours(0, 0, 0, 0);
  const today = drinks.filter((d) => d.ts >= startDay.getTime());
  const todayMl = today.reduce((s, d) => s + d.ml, 0);
  const last = drinks.length ? Math.max(...drinks.map((d) => d.ts)) : null;
  const last30Ml = drinks.filter((d) => now - d.ts <= 30 * 60000).reduce((s, d) => s + d.ml, 0);
  const expected = expectedByNow(goal, date);
  return {
    now, hour: date.getHours(),
    sensing: connected && calibrated,
    absent,
    bottleMl: ml, capacity,
    bottlePct: ml === null ? null : Math.round((ml / capacity) * 100),
    todayMl, goal,
    goalPct: Math.round((todayMl / goal) * 100),
    sips: today.length,
    expected,
    deficit: expected - todayMl,
    minutesSinceDrink: last === null ? null : Math.round((now - last) / 60000),
    last30Ml,
  };
}

/* =========================== Consiliul Organelor =========================== */
const isNight = (h) => h >= 23 || h < 7;

export function council(ctx) {
  const since = ctx.minutesSinceDrink ?? 999;
  const votes = [];

  // Rinichiul: vrea apă dacă ești în urmă sau n-ai băut de mult.
  if (ctx.deficit > 150) votes.push({ organ: 'Rinichiul', icon: 'bean', vote: 'bea', reason: `Suntem cu ${ctx.deficit} mL în urma planului. Inacceptabil.` });
  else if (since > 90) votes.push({ organ: 'Rinichiul', icon: 'bean', vote: 'bea', reason: `${since === 999 ? 'Azi încă nimic' : `Au trecut ${since} min`}. Filtrez praf.` });
  else votes.push({ organ: 'Rinichiul', icon: 'bean', vote: 'așteaptă', reason: 'Sunt mulțumit. Deocamdată.' });

  // Vezica: mereu sceptică, mai ales dacă ai băut mult recent.
  if (ctx.last30Ml >= 300) votes.push({ organ: 'Vezica', icon: 'droplet', vote: 'așteaptă', reason: `${ctx.last30Ml} mL în ultima jumătate de oră. Am și eu o capacitate.` });
  else if (isNight(ctx.hour)) votes.push({ organ: 'Vezica', icon: 'droplet', vote: 'așteaptă', reason: 'E noapte. Nu mă trezi la 4.' });
  else votes.push({ organ: 'Vezica', icon: 'droplet', vote: 'bea', reason: 'Am loc. Fie.' });

  // Creierul: ține cont de oră și de obiectiv.
  if (isNight(ctx.hour)) votes.push({ organ: 'Creierul', icon: 'brain', vote: 'așteaptă', reason: 'Somnul e și el hidratare. Pentru creier.' });
  else if (ctx.todayMl >= ctx.goal) votes.push({ organ: 'Creierul', icon: 'brain', vote: 'așteaptă', reason: 'Obiectivul e atins. Putem bea din plăcere, nu din datorie.' });
  else if (since >= 45) votes.push({ organ: 'Creierul', icon: 'brain', vote: 'bea', reason: 'Concentrarea scade cu 2% la fiecare procent de deshidratare. Am citit asta undeva.' });
  else votes.push({ organ: 'Creierul', icon: 'brain', vote: 'așteaptă', reason: 'Ai băut de curând. Lasă-mă să lucrez.' });

  return votes;
}

const roundTo = (v, step) => Math.round(v / step) * step;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Verdictul: drink | wait | refill | missing | blind
export function decide(ctx) {
  if (!ctx.sensing) return { verdict: 'blind', votes: [], amountMl: 0, waitMin: 0 };
  if (ctx.absent) return { verdict: 'missing', votes: [], amountMl: 0, waitMin: 0 };
  const votes = council(ctx);
  const yes = votes.filter((v) => v.vote === 'bea').length;
  if (yes >= 2) {
    const amountMl = clamp(roundTo(Math.max(ctx.deficit, 0) * 0.5, 50), 150, 400);
    if (ctx.bottleMl !== null && ctx.bottleMl < Math.min(amountMl, ctx.capacity * 0.15)) {
      return { verdict: 'refill', votes, amountMl, waitMin: 0 };
    }
    return { verdict: 'drink', votes, amountMl, waitMin: 0 };
  }
  const since = ctx.minutesSinceDrink ?? 0;
  const waitMin = isNight(ctx.hour) ? 8 * 60 - 0 : ctx.todayMl >= ctx.goal ? 60 : clamp(45 - since, 10, 90);
  return { verdict: 'wait', votes, amountMl: 0, waitMin };
}

/* =========================== Personalități =========================== */
const pick = (arr, rng) => arr[Math.floor(rng() * arr.length) % arr.length];

const LINES = {
  free: {
    drink: (d) => [`Bea ${d.amountMl} mL. Sau nu. Noi doar zicem.`, `Apă. ${d.amountMl} mL. Acum.`],
    wait: (d) => [`Mai aștepți ${d.waitMin} min. Sau dai upgrade și aștepți cu stil.`],
    refill: () => ['Sticla e aproape goală. Ca planul tău Free.'],
    missing: () => ['Unde e sticla? Nici pe Free nu putem urmări o sticlă invizibilă.'],
    blind: () => ['Nu văd nimic. Conectează sticla. (Pe Pro tot nu vezi, dar măcar fără reclame.)'],
  },
  pro: {
    drink: (d) => [`HAI CĂ POȚI! ${d.amountMl} mL, ACUM! Fiecare înghițitură contează!`, `Campionii beau apă. Tu ești campion. ${d.amountMl} mL. GO!`],
    wait: (d) => [`Pauză tactică de ${d.waitMin} min. Și sportivii se odihnesc.`, `Respiră. Peste ${d.waitMin} min revenim în teren.`],
    refill: () => ['Reumple sticla! Un campion nu luptă cu rezervorul gol!'],
    missing: () => ['Unde ți-e sticla, campionule? Echipamentul e jumătate din victorie!'],
    blind: () => ['Conectează sticla și hai să facem istorie!'],
  },
  ultra: {
    drink: (d) => [`Vă recomand ${d.amountMl} mL dintr-o recoltă de robinet 2026, cu note subtile de clor și un final lung, mineral.`, `Momentul e ideal pentru ${d.amountMl} mL. Lăsați apa să respire o secundă în pahar, apoi savurați.`],
    wait: (d) => [`Rinichii dumneavoastră se odihnesc. Revin cu o recomandare în ${d.waitMin} minute.`, `Răbdare. O apă bună se bea la momentul potrivit: peste ${d.waitMin} min.`],
    refill: () => ['Sticla și-a încheiat serviciul. Vă rog, o nouă turnare, din aceeași sursă distinsă.'],
    missing: () => ['Sticla a părăsit masa. Sper că nu a plecat la concurență.'],
    blind: () => ['Fără conexiune nu pot degusta datele. Vă rog, conectați sticla.'],
  },
  enterprise: {
    drink: (d) => [`Per sync-ul de azi, propun să aliniem un deliverable de ${d.amountMl} mL în sprint-ul curent. Action item: tu. Deadline: EOD, adică acum.`, `Hidratarea e un KPI critic pentru Q4. Recomand execuția imediată a unui sip de ${d.amountMl} mL, în linie cu OKR-urile.`],
    wait: (d) => [`Pipeline-ul de hidratare e on track. Reluăm discuția într-un follow-up peste ${d.waitMin} min.`, `Nu e nevoie de action item acum. Am pus un placeholder în calendar peste ${d.waitMin} min.`],
    refill: () => ['Avem un risc de capacitate în rezervorul principal. Recomand un refill pentru a asigura business continuity.'],
    missing: () => ['Asset-ul principal (sticla) nu mai e în scope. Am deschis un ticket P1.'],
    blind: () => ['Nu avem vizibilitate asupra datelor. Recomand reconectarea ca prioritate strategică.'],
  },
};

export function speak(decision, tier, rng = Math.random) {
  const t = LINES[tier] || LINES.free;
  const text = pick(t[decision.verdict](decision), rng);
  return tier === 'free' ? `${text} (Mesaj sponsorizat de APA™)` : text;
}

// Pași de „gândire” afișați înaintea răspunsului. Folosesc cifrele reale.
export function reasoningTrace(ctx, decision, tier) {
  if (!TIERS[tier]?.council) {
    return ['Analizez...', 'Raționamentul complet e disponibil în Ultra.'];
  }
  if (!ctx.sensing) return ['Caut sticla prin Bluetooth...', 'Nicio sticlă. Nicio dată. Nicio speranță.'];
  const steps = [
    `Interoghez senzorul: ${ctx.bottleMl ?? '?'} mL în sticlă (${ctx.bottlePct ?? '?'}%).`,
    `Ai băut azi ${ctx.todayMl} mL din ${ctx.goal} mL (${ctx.goalPct}%).`,
    `Curba ideală cere ${ctx.expected} mL până acum: ${ctx.deficit > 0 ? `deficit ${ctx.deficit} mL` : `surplus ${-ctx.deficit} mL`}.`,
    ctx.minutesSinceDrink === null ? 'Ultima înghițitură: niciuna. Îngrijorător.' : `Ultima înghițitură: acum ${ctx.minutesSinceDrink} min.`,
    'Convoc Consiliul Organelor...',
  ];
  for (const v of decision.votes) steps.push(`${v.organ} votează „${v.vote}”: ${v.reason}`);
  if (tier === 'enterprise') steps.push('Escaladez decizia către Comitetul de Guvernanță Hidrică... aprobat.');
  return steps;
}

// Reacție la un eveniment detectat de senzor.
export function reactToEvent(ev, tier, ctx, rng = Math.random) {
  if (ev.type === 'refill') {
    return pick({
      free: [`+${ev.ml} mL în sticlă. Upgrade-ul e tot gol.`],
      pro: [`Rezervor plin! +${ev.ml} mL de energie pură!`],
      ultra: [`O nouă turnare: ${ev.ml} mL. Excelentă alegere.`],
      enterprise: [`Capacitatea a fost restaurată (+${ev.ml} mL). Risc închis.`],
    }[tier] || [''], rng);
  }
  const done = ctx.todayMl >= ctx.goal;
  const lines = {
    free: [`Ai băut ${ev.ml} mL. Pe Pro am fi aplaudat.`],
    pro: [done ? `OBIECTIV ATINS! ${ctx.todayMl} mL! LEGENDĂ!` : `BOOM! ${ev.ml} mL! Încă ${ctx.goal - ctx.todayMl} mL până la glorie!`],
    ultra: [done ? `Obiectivul zilei, onorat. ${ctx.todayMl} mL. Rinichii dumneavoastră vă salută.` : `${ev.ml} mL, băuți cu eleganță. Rinichii dumneavoastră vă transmit salutări.`],
    enterprise: [done ? `Target-ul zilnic a fost livrat (${ctx.goalPct}%). Felicitări întregii echipe (tu).` : `Sip de ${ev.ml} mL înregistrat. Progres: ${ctx.goalPct}% din target. Keep the momentum.`],
  };
  return pick(lines[tier] || lines.free, rng);
}

/* =========================== Chat local (fără API) =========================== */
const has = (t, ...words) => words.some((w) => t.includes(w));
// fără diacritice, ca „cat am baut” să meargă la fel ca „cât am băut”
export const norm = (s) => s.toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '');

export function localReply(question, ctx, tier, rng = Math.random) {
  const q = norm(question);
  const d = decide(ctx);
  if (has(q, 'cat am baut', 'cat am bat', 'status', 'progres', 'cat mai am')) {
    return ctx.sensing || ctx.todayMl
      ? `Azi: ${ctx.todayMl} / ${ctx.goal} mL (${ctx.goalPct}%), în ${ctx.sips} înghițituri.${ctx.deficit > 0 ? ` Ești în urmă cu ${ctx.deficit} mL față de plan.` : ' Ești peste plan.'}`
      : 'Încă nimic azi. Sau sticla nu e conectată. Sau amândouă.';
  }
  if (has(q, 'bere', 'vin', 'cafea', 'suc', 'cola', 'energizant', 'palinca', 'tuica')) {
    return 'Senzorul meu e calibrat pentru apă. Restul îl raportez direct ficatului.';
  }
  if (has(q, 'glum', 'banc', 'amuza')) {
    return pick([
      'De ce nu se ceartă niciodată apa? Pentru că trece peste orice.',
      'Am vrut să spun o glumă despre apă, dar era prea fluidă.',
      'Un pahar de apă intră într-un bar. Barmanul: „H₂O?” Paharul: „Nu, doar trec.”',
    ], rng);
  }
  if (has(q, 'cine esti', 'ce esti', 'cine te', 'esti ai', 'esti om')) {
    return `Sunt HydroGPT™, un agent de hidratare cu ${TIERS[tier].council ? 'trei organe consultative' : 'o singură regulă'} și zero sete. Rulez pe un ESP32 și pe speranță.`;
  }
  if (has(q, 'abonament', 'upgrade', 'pret', 'plan', 'premium', 'pro', 'ultra')) {
    const next = TIER_ORDER[TIER_ORDER.indexOf(tier) + 1];
    return next
      ? `Ești pe ${TIERS[tier].name}. ${TIERS[next].name} (${TIERS[next].price}) îți aduce: ${TIERS[next].perks[0].toLowerCase()}. Rinichii tăi au votat deja „da”.`
      : 'Ești pe Enterprise. Nu mai există nimic peste. Doar marea.';
  }
  if (has(q, 'motiv', 'nu vreau', 'nu-mi vine', 'nu imi vine', 'lene')) {
    return {
      free: 'Motivație? Pe Free ai doar apă. Bea-o.',
      pro: 'Corpul tău e 60% apă. Restul de 40% e voință. ACUM BEA!',
      ultra: 'Gândiți-vă la apă ca la un vin care nu vă dă dureri de cap.',
      enterprise: 'Hidratarea ta are impact direct asupra stakeholderilor (tu, mâine dimineață).',
    }[tier];
  }
  if (has(q, 'cand', 'trebuie', 'sa beau', 'beau', 'acum', 'sete', 'cat sa')) {
    return speak(d, tier, rng);
  }
  return `${pick(['Interesant.', 'Am notat.', 'Am trimis întrebarea la rinichi.'], rng)} ${speak(d, tier, rng)}`;
}

/* =========================== Raport Enterprise =========================== */
export function kpiReport(ctx, todayEvents) {
  const drinks = todayEvents.filter((e) => e.type === 'drink');
  const refills = todayEvents.filter((e) => e.type === 'refill').length;
  const avg = drinks.length ? Math.round(ctx.todayMl / drinks.length) : 0;
  const first = drinks.length ? Math.min(...drinks.map((e) => e.ts)) : null;
  const hours = first ? Math.max((ctx.now - first) / 3600000, 0.25) : 0;
  const velocity = hours ? Math.round(ctx.todayMl / hours) : 0;
  const onTrack = ctx.deficit <= 0;
  return [
    { k: 'Volum livrat (YTD = azi)', v: `${ctx.todayMl} mL` },
    { k: 'Atingere target', v: `${ctx.goalPct}%` },
    { k: 'Sip count', v: `${drinks.length}` },
    { k: 'Dimensiune medie sip', v: `${avg} mL` },
    { k: 'Sip velocity', v: `${velocity} mL/h` },
    { k: 'Water churn (reumpleri)', v: `${refills}` },
    { k: 'Variație față de forecast', v: `${ctx.deficit > 0 ? '−' : '+'}${Math.abs(ctx.deficit)} mL` },
    { k: 'Status', v: onTrack ? 'On track' : ctx.deficit > 500 ? 'At risk' : 'Needs attention' },
    { k: 'Hydration ROI', v: `${Math.round(ctx.goalPct * 1.37)}% (metodologie confidențială)` },
  ];
}
