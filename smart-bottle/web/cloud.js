// Partea de cloud (Supabase): cont anonim, abonament, sincronizare, clasament și agentul AI.
// Totul e opțional: fără rețea, aplicația merge mai departe doar local.
import { SUPABASE_URL, SUPABASE_KEY } from './config.js';

const SDK = 'https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2/+esm';

export const cloud = {
  sb: null,
  user: null,
  profile: null,       // { nickname, tier, goal_ml }
  ready: false,

  async init() {
    try {
      const { createClient } = await import(SDK);
      this.sb = createClient(SUPABASE_URL, SUPABASE_KEY, {
        auth: { persistSession: true, autoRefreshToken: true },
      });
      let { data: { session } } = await this.sb.auth.getSession();
      if (!session) {
        const { data, error } = await this.sb.auth.signInAnonymously();
        if (error) throw error;
        session = data.session;
      }
      this.user = session.user;
      await this.loadProfile();
      this.ready = true;
    } catch (e) {
      console.warn('cloud indisponibil, continuăm local', e);
      this.ready = false;
    }
    return this.ready;
  },

  async loadProfile() {
    const { data, error } = await this.sb.from('profiles').select('nickname, tier, goal_ml').eq('id', this.user.id).single();
    if (error) throw error;
    this.profile = data;
    return data;
  },

  // Evenimentele au client_id stabil, deci retrimiterea nu dublează nimic.
  async pushEvents(events) {
    if (!this.ready || !events.length) return;
    const rows = events.map((e) => ({
      ts: new Date(e.ts).toISOString(), kind: e.type, ml: Math.round(e.ml),
      client_id: `${e.ts}-${e.type}`,
    }));
    const { error } = await this.sb.from('drink_events').upsert(rows, { onConflict: 'user_id,client_id', ignoreDuplicates: true });
    if (error) console.warn('sincronizare', error.message);
  },

  async checkout(tier) {
    const { data, error } = await this.sb.rpc('checkout', { new_tier: tier });
    if (error) throw error;
    this.profile.tier = tier;
    return data;   // factura
  },

  async invoices() {
    if (!this.ready) return [];
    const { data } = await this.sb.from('invoices').select('*').order('created_at', { ascending: false }).limit(5);
    return data ?? [];
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
