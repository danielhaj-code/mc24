// Ecranul de autentificare (Google), ieșirea din cont și panoul de administrare.
// Lista de acces e aplicată în Supabase (hook la crearea contului + RLS); aici e doar interfața.
import { cloud } from './cloud.js';
import { icon } from './hydro.js';

const $ = (id) => document.getElementById(id);
function setStep(step) {
  for (const el of document.querySelectorAll('[data-step]')) el.hidden = el.dataset.step !== step;
}

function setError(text) {
  $('authError').textContent = text || '';
  $('authError').hidden = !text;
}

// Mesajele Supabase / Google, traduse pentru utilizator.
function explain(err) {
  const m = (err?.message || err || '').toLowerCase();
  if (m.includes('nu are acces') || m.includes('403') || m.includes('hook') || m.includes('access_denied')) return 'Contul Google ales nu are acces încă. Cere-i administratorului să-ți adauge adresa.';
  return err?.message || String(err) || 'Ceva n-a mers. Încearcă din nou.';
}

// După întoarcerea de la Google, erorile vin în URL (?error=… sau #error=…).
function urlError() {
  const params = new URLSearchParams(location.search + '&' + location.hash.replace(/^#/, ''));
  const err = params.get('error_description') || params.get('error');
  if (err) history.replaceState(null, '', location.pathname);
  return err ? decodeURIComponent(err.replace(/\+/g, ' ')) : null;
}

async function finish(resolve) {
  const st = await cloud.status();
  if (st === 'ready') {
    document.body.dataset.auth = 'in';
    renderAccount();
    resolve('ready');
  } else if (st === 'denied') {
    $('authDeniedEmail').textContent = cloud.user?.email ?? '';
    setStep('denied');
  } else {
    setStep('offline');
  }
}

function renderAccount() {
  $('btnSignOut').hidden = !cloud.ready;
  $('btnSignOut').title = cloud.user?.email ? `Ieși din ${cloud.user.email}` : 'Ieși din cont';
  $('btnAdmin').hidden = !cloud.admin;
  // contul stă în fereastra de setări
  $('accountRow').hidden = !cloud.ready;
  $('accountEmail').textContent = cloud.user?.email ?? '';
}

/* =========================== Panoul de administrare =========================== */
const fmtDate = (ts) => new Date(ts).toLocaleDateString('ro-RO', { day: 'numeric', month: 'short' });

async function renderAdmin() {
  const list = $('adminList');
  list.replaceChildren(...[0, 1, 2].map(() => Object.assign(document.createElement('li'), { className: 'skeleton' })));
  let rows;
  try { rows = await cloud.listAllowed(); } catch (e) { $('adminError').textContent = explain(e); $('adminError').hidden = false; list.replaceChildren(); return; }
  $('adminCount').textContent = rows.length === 1 ? '1 adresă' : `${rows.length} adrese`;
  list.replaceChildren();
  for (const r of rows) {
    const li = document.createElement('li');
    const who = document.createElement('div');
    who.className = 'who';
    const mail = Object.assign(document.createElement('span'), { className: 'mail', textContent: r.email });
    const meta = Object.assign(document.createElement('span'), { className: 'meta muted' });
    meta.textContent = [
      r.note,
      r.registered ? (r.last_sign_in ? `ultima intrare ${fmtDate(r.last_sign_in)}` : 'cont creat') : 'încă n-a intrat',
    ].filter(Boolean).join(', ');
    who.append(mail, meta);
    const status = Object.assign(document.createElement('span'), {
      className: 'status', textContent: r.is_admin ? 'administrator' : r.registered ? 'activ' : 'invitat',
    });
    status.dataset.state = r.is_admin ? 'admin' : r.registered ? 'active' : 'invited';
    const rm = document.createElement('button');
    rm.className = 'ghost icon-btn small';
    rm.setAttribute('aria-label', `Scoate ${r.email} de pe listă`);
    rm.append(icon('trash'));
    rm.disabled = r.is_admin;
    rm.addEventListener('click', async () => {
      if (!confirm(`Scoți ${r.email} de pe listă? Nu va mai putea intra și nu-și va mai vedea datele.`)) return;
      try { await cloud.removeAllowed(r.email); renderAdmin(); } catch (e) { alert(explain(e)); }
    });
    li.append(who, status, rm);
    list.append(li);
  }
}

async function addEmail(e) {
  e.preventDefault();
  const input = $('adminEmail');
  const value = input.value.trim().toLowerCase();
  const err = $('adminError');
  err.hidden = true;
  if (!/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(value)) { err.textContent = 'Scrie o adresă de email validă.'; err.hidden = false; return; }
  const btn = $('adminAdd');
  btn.disabled = true;
  try {
    await cloud.addAllowed(value, $('adminNote').value.trim());
    input.value = ''; $('adminNote').value = '';
    await renderAdmin();
    input.focus();
  } catch (ex) {
    err.textContent = explain(ex); err.hidden = false;
  } finally {
    btn.disabled = false;
  }
}

/* =========================== Pornire =========================== */
// Se rezolvă când utilizatorul a intrat ('ready') sau a ales să continue fără cont ('offline').
export function authGate() {
  $('btnSignOut').addEventListener('click', async () => {
    if (!confirm('Ieși din cont pe acest dispozitiv?')) return;
    await cloud.signOut();
    location.reload();
  });
  $('btnAdmin').addEventListener('click', () => { $('adminError').hidden = true; renderAdmin(); $('adminDialog').showModal(); });
  $('adminForm').addEventListener('submit', addEmail);

  return new Promise(async (resolve) => {
    $('authGoogle').addEventListener('click', async () => {
      setError('');
      const btn = $('authGoogle');
      btn.disabled = true;
      try { await cloud.signInWithGoogle(); }       // pagina pleacă spre Google
      catch (e) { setError(explain(e)); btn.disabled = false; }
    });
    $('authDeniedOut').addEventListener('click', async () => { await cloud.signOut(); setError(''); setStep('signin'); });
    $('authOffline').addEventListener('click', () => { document.body.dataset.auth = 'offline'; renderAccount(); resolve('offline'); });
    $('authRetry').addEventListener('click', () => location.reload());

    const returned = urlError();          // eroare la întoarcerea de la Google (ex. adresă nepermisă)
    const st = await cloud.status();
    if (st === 'ready') { document.body.dataset.auth = 'in'; renderAccount(); resolve('ready'); return; }
    document.body.dataset.auth = 'out';
    if (st === 'denied') { $('authDeniedEmail').textContent = cloud.user?.email ?? ''; setStep('denied'); }
    else if (st === 'offline') setStep('offline');
    else setStep('signin');
    if (returned) setError(explain(returned));

    // La întoarcerea de la Google, Supabase preia sesiunea din URL și anunță aici.
    // (în callback nu se apelează direct Supabase, de aceea setTimeout)
    cloud.sb?.auth.onAuthStateChange((event) => {
      if (event === 'SIGNED_IN' && document.body.dataset.auth === 'out') setTimeout(() => finish(resolve), 0);
    });
  });
}
