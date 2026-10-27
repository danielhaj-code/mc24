// Partea de cloud (Supabase): autentificare cu Google, lista de acces, abonament,
// sincronizare, clasament și agentul AI. Fără internet, aplicația merge mai departe doar local.
import { SUPABASE_URL, SUPABASE_KEY } from './config.js';

const SDK = 'https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2/+esm';
const SYNC_EVERY_MS = 60000;   // lotul de evenimente pleacă cel mult o dată pe minut
const SYNC_BATCH_MAX = 20;     // sau mai devreme, dacă s-au strâns atâtea

export const cloud = {
  sb: null,
  user: null,
  profile: null,       // { nickname, tier, goal_ml }
  admin: false,
  ready: false,        // autentificat, pe listă și cu profil încărcat

  // Încarcă SDK-ul. false = fără internet.
  async connect() {
    if (this.sb) return true;
    try {
      const { createClient } = await import(SDK);
      this.sb = createClient(SUPABASE_URL, SUPABASE_KEY, {
        auth: { persistSession: true, autoRefreshToken: true, detectSessionInUrl: true },
      });
      return true;
    } catch (e) {
      console.warn('Supabase indisponibil', e);
      return false;
    }
  },

  // Starea sesiunii: 'offline' | 'signed-out' | 'denied' | 'ready'
  async status() {
    if (!(await this.connect())) return 'offline';
    const { data: { session } } = await this.sb.auth.getSession();
    if (!session) return 'signed-out';
    this.user = session.user;
    const { data: allowed, error } = await this.sb.rpc('is_allowed');
    if (error) { console.warn('is_allowed', error.message); return 'offline'; }
    if (!allowed) return 'denied';
    await this.loadProfile();
    const { data: admin } = await this.sb.rpc('is_admin');
    this.admin = !!admin;
    this.ready = true;
    return 'ready';
  },

  // Păstrat pentru hydro.js: true dacă sesiunea e gata de folosit.
  async init() {
    if (this.ready) return true;
    return (await this.status()) === 'ready';
  },

  // Redirecționează spre Google; la întoarcere sesiunea vine în URL.
  // Adresele care nu sunt pe listă sunt respinse de hook-ul din Supabase la crearea contului.
  async signInWithGoogle() {
    const { error } = await this.sb.auth.signInWithOAuth({
      provider: 'google',
      options: { redirectTo: location.origin + location.pathname, queryParams: { prompt: 'select_account' } },
    });
    if (error) throw error;
  },

  async signOut() {
    this.ready = false; this.admin = false; this.profile = null; this.user = null;
    if (this.sb) await this.sb.auth.signOut();
  },

  async loadProfile() {
    const { data, error } = await this.sb.from('profiles').select('nickname, tier, goal_ml').eq('id', this.user.id).single();
    if (error) throw error;
    this.profile = data;
    return data;
  },

  // Evenimentele au client_id stabil, deci retrimiterea nu dublează nimic. Întoarce true la succes.
  async pushEvents(events) {
    if (!this.ready || !events.length) return true;
    const rows = events.map((e) => ({
      ts: new Date(e.ts).toISOString(), kind: e.type, ml: Math.round(e.ml),
      client_id: `${e.ts}-${e.type}`,
    }));
    const { error } = await this.sb.from('drink_events').upsert(rows, { onConflict: 'user_id,client_id', ignoreDuplicates: true });
    if (error) console.warn('sincronizare', error.message);
    return !error;
  },

  // Evenimentele noi nu pleacă la Supabase pe loc: se adună și se trimit într-un singur lot
  // la SYNC_EVERY_MS sau când sunt SYNC_BATCH_MAX (și la ascunderea paginii). Bluetooth-ul și
  // istoricul local rămân în timp real; doar cloud-ul primește mai rar.
  queue: [],
  flushTimer: null,
  onFlushed: null,

  queueEvent(ev, onFlushed) {
    this.queue.push(ev);
    if (onFlushed) this.onFlushed = onFlushed;
    if (this.queue.length >= SYNC_BATCH_MAX) this.flush();
    else if (!this.flushTimer) this.flushTimer = setTimeout(() => this.flush(), SYNC_EVERY_MS);
  },

  async flush() {
    clearTimeout(this.flushTimer);
    this.flushTimer = null;
    if (!this.queue.length) return;
    const batch = this.queue.splice(0);
    if (await this.pushEvents(batch)) this.onFlushed?.();
    else {                                   // eșec: le păstrăm pentru lotul următor
      this.queue.unshift(...batch);
      this.flushTimer = setTimeout(() => this.flush(), SYNC_EVERY_MS);
    }
  },

  async checkout(tier) {
    const { data, error } = await this.sb.rpc('checkout', { new_tier: tier });
    if (error) throw error;
    this.profile.tier = tier;
    return data;   // factura
  },

  async leaderboard() {
    if (!this.ready) return null;
    const { data, error } = await this.sb.rpc('leaderboard_today');
    if (error) { console.warn('clasament', error.message); return null; }
    return data;
  },

  async setNickname(nickname) {
    const { error } = await this.sb.from('profiles').update({ nickname }).eq('id', this.user.id);
    if (error) throw error;
    this.profile.nickname = nickname;
  },

  async setGoal(goal) {
    if (!this.ready) return;
    await this.sb.from('profiles').update({ goal_ml: goal }).eq('id', this.user.id);
  },

  async clearEvents() {
    if (!this.ready) return;
    await this.sb.from('drink_events').delete().eq('user_id', this.user.id);
  },

  // ---- administrare (funcțiile verifică singure că ești admin) ----
  async listAllowed() {
    const { data, error } = await this.sb.rpc('admin_list_allowed');
    if (error) throw error;
    return data;
  },
  async addAllowed(email, note) {
    const { error } = await this.sb.rpc('admin_add_email', { new_email: email, new_note: note || null });
    if (error) throw error;
  },
  async removeAllowed(email) {
    const { error } = await this.sb.rpc('admin_remove_email', { old_email: email });
    if (error) throw error;
  },

  // Agentul AI din edge function. Întoarce null dacă trebuie folosit agentul local.
  async askAgent(payload) {
    if (!this.ready) return null;
    const { data, error } = await this.sb.functions.invoke('hydrogpt', { body: payload });
    if (error) {
      let detail = null;
      try { detail = await error.context?.json?.(); } catch { /* fără corp */ }
      return { fallback: true, status: error.context?.status, ...detail };
    }
    return data;
  },
};
