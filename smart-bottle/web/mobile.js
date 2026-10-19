// Comportamentul pe telefon: bara de secțiuni de jos, butoanele de conectare lângă sticlă,
// ecran ținut aprins cât e conectată sticla, vibrații scurte și reverificarea legăturii la revenire.
const $ = (id) => document.getElementById(id);
const PHONE = window.matchMedia('(max-width: 640px)');
const REDUCED = window.matchMedia('(prefers-reduced-motion: reduce)');

/* =========================== Butoanele de conectare =========================== */
// Pe telefon, „Conectează” și „Demo” stau sub numărul de mL (zona degetului mare);
// pe ecrane mari rămân în bara de sus.
function placeActions() {
  const actions = [$('btnDemo'), $('btnConnect')];
  if (PHONE.matches) $('heroActions').append(...actions);
  else $('btnSettings').before(...actions);
}

/* =========================== Bara de secțiuni =========================== */
function setupTabbar() {
  const links = [...document.querySelectorAll('#tabbar a')];
  const sections = links.map((a) => $(a.dataset.tab));
  const setActive = (id) => links.forEach((a) => {
    if (a.dataset.tab === id) a.setAttribute('aria-current', 'true'); else a.removeAttribute('aria-current');
  });

  let lockUntil = 0;   // după un tap, derularea lină nu trebuie să mute selecția prin alte file
  for (const a of links) {
    a.addEventListener('click', (e) => {
      e.preventDefault();
      lockUntil = Date.now() + 1200;
      $(a.dataset.tab).scrollIntoView({ behavior: REDUCED.matches ? 'auto' : 'smooth', block: 'start' });
      setActive(a.dataset.tab);
      haptic(8);
    });
  }

  // Secțiunea activă = prima din banda de sus a ecranului; la capătul paginii, ultima.
  const seen = new Map();
  const update = () => {
    if (Date.now() < lockUntil) return;
    const atEnd = window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 8;
    if (atEnd) { setActive(sections.at(-1).id); return; }
    const first = sections.find((sec) => seen.get(sec.id));
    if (first) setActive(first.id);
  };
  const io = new IntersectionObserver((entries) => {
    for (const en of entries) seen.set(en.target.id, en.isIntersecting);
    update();
  }, { rootMargin: '-80px 0px -55% 0px' });
  sections.forEach((sec) => io.observe(sec));
  window.addEventListener('scroll', () => requestAnimationFrame(update), { passive: true });
}

/* =========================== Ecran aprins =========================== */
// Fără activitate în fundal, Bluetooth-ul se oprește când telefonul intră în repaus.
// Cât e conectată sticla, cerem ecranului să rămână aprins (Screen Wake Lock).
let wakeLock = null;
let wantAwake = false;
let enabled = () => true;

async function acquire() {
  if (!wantAwake || !enabled() || wakeLock || document.visibilityState !== 'visible') return;
  if (!('wakeLock' in navigator)) return;
  try {
    wakeLock = await navigator.wakeLock.request('screen');
    wakeLock.addEventListener('release', () => { wakeLock = null; });
  } catch { wakeLock = null; /* baterie descărcată sau refuzat: continuăm fără */ }
}

async function release() {
  try { await wakeLock?.release(); } catch { /* deja eliberat */ }
  wakeLock = null;
}

export function keepAwake(on) {
  wantAwake = on;
  if (on) acquire(); else release();
}
export function refreshWakeLock() { if (wantAwake && enabled()) acquire(); else release(); }
export const wakeLockSupported = () => 'wakeLock' in navigator;

/* =========================== Vibrații =========================== */
export function haptic(pattern = 12) {
  if (!PHONE.matches || !('vibrate' in navigator)) return;
  try { navigator.vibrate(pattern); } catch { /* nesuportat */ }
}

/* =========================== Pornire =========================== */
// onResume: apelat când aplicația revine în prim-plan (timer-ele au fost încetinite între timp).
export function initMobile({ onResume, wakeLockEnabled }) {
  if (wakeLockEnabled) enabled = wakeLockEnabled;
  placeActions();
  PHONE.addEventListener('change', placeActions);
  setupTabbar();
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState !== 'visible') return;
    acquire();              // lock-ul se eliberează automat când pagina trece în fundal
    onResume?.();
  });
}
