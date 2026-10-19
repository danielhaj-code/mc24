// HydroGPT™ — edge function: agentul de hidratare cu un LLM adevărat (Gemini) și unelte.
//
// Secrete (Supabase → Edge Functions → Secrets):
//   GEMINI_API_KEY   cheia din Google AI Studio (obligatoriu pentru AI real)
//   GEMINI_MODEL     opțional, implicit gemini-3.5-flash-lite
//
// Cerere (POST, cu JWT-ul utilizatorului):
//   { question, ctx, decision, history: [{role:'user'|'agent', text}] }
// Răspuns:
//   { reply, actions: [{type:'remind', minutes, message}], toolCalls: [...], model }
//   402 { fallback:true } pe Free/Pro (AI-ul adevărat e premium, evident)
//   503 { fallback:true } fără cheie sau dacă modelul nu răspunde → aplicația folosește agentul local
import { createClient } from 'npm:@supabase/supabase-js@2';

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
};
const json = (body: unknown, status = 200) =>
  new Response(JSON.stringify(body), { status, headers: { ...CORS, 'Content-Type': 'application/json' } });

const PERSONA: Record<string, string> = {
  ultra: 'Ești un somelier de apă extrem de rafinat. Vorbești cu „dumneavoastră”, descrii apa ca pe un vin de colecție (note, finaluri, recolte de robinet).',
  enterprise: 'Ești un consultant corporatist. Vorbești numai în jargon de business: KPI, OKR, sync, deliverable, stakeholder, action item, Q4. Tratezi fiecare înghițitură ca pe un proiect strategic.',
};

const MAX_STEPS = 4;
const MAX_QUESTION = 500;

const TOOLS = [{
  functionDeclarations: [
    {
      name: 'get_hydration_status',
      description: 'Starea curentă: mL în sticlă, cât a băut azi, obiectivul, deficitul față de plan, minute de la ultima înghițitură și votul Consiliului Organelor (Rinichiul, Vezica, Creierul).',
      parameters: { type: 'object', properties: {} },
    },
    {
      name: 'get_drink_history',
      description: 'Totalul băut pe fiecare zi, pentru ultimele N zile (din baza de date).',
      parameters: { type: 'object', properties: { days: { type: 'integer', description: 'între 1 și 30' } }, required: ['days'] },
    },
    {
      name: 'schedule_reminder',
      description: 'Programează un memento în aplicație peste un număr de minute.',
      parameters: {
        type: 'object',
        properties: {
          minutes: { type: 'integer', description: 'între 1 și 240' },
          message: { type: 'string', description: 'textul mementoului, scurt, în personajul tău' },
        },
        required: ['minutes', 'message'],
      },
    },
  ],
}];

Deno.serve(async (req) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: CORS });
  if (req.method !== 'POST') return json({ error: 'doar POST' }, 405);

  const auth = req.headers.get('Authorization') ?? '';
  const sb = createClient(Deno.env.get('SUPABASE_URL')!, Deno.env.get('SUPABASE_ANON_KEY')!, {
    global: { headers: { Authorization: auth } },
  });
  const { data: { user } } = await sb.auth.getUser(auth.replace(/^Bearer\s+/i, ''));
  if (!user) return json({ error: 'neautentificat' }, 401);
  const { data: allowed } = await sb.rpc('is_allowed');
  if (!allowed) return json({ error: 'fără acces' }, 403);

  // Abonamentul se citește din baza de date, nu din ce trimite clientul.
  const { data: profile } = await sb.from('profiles').select('tier, nickname, goal_ml').eq('id', user.id).single();
  const tier = profile?.tier ?? 'free';
  if (tier !== 'ultra' && tier !== 'enterprise') {
    return json({ fallback: true, error: 'upgrade', message: 'AI-ul adevărat e disponibil din Ultra. 402: Payment Required (literal).' }, 402);
  }

  const key = Deno.env.get('GEMINI_API_KEY');
  if (!key) return json({ fallback: true, error: 'GEMINI_API_KEY lipsește' }, 503);
  const model = Deno.env.get('GEMINI_MODEL') ?? 'gemini-3.5-flash-lite';

  let body: { question?: string; ctx?: Record<string, unknown>; decision?: unknown; history?: { role: string; text: string }[] };
  try { body = await req.json(); } catch { return json({ error: 'JSON invalid' }, 400); }
  const question = String(body.question ?? '').slice(0, MAX_QUESTION).trim();
  if (!question) return json({ error: 'întrebare goală' }, 400);

  const system = [
    'Ești HydroGPT™, agentul de hidratare al aplicației Smart Bottle (proiect de facultate, USV).',
    PERSONA[tier],
    `Utilizatorul se numește „${profile?.nickname ?? 'anonim'}”.`,
    'Răspunzi DOAR în română, maximum 3 propoziții, amuzant, dar cu cifre reale.',
    'Înainte să recomanzi ceva, apelează get_hydration_status. Nu inventa cifre.',
    'Dacă recomanzi să aștepte, poți programa un memento cu schedule_reminder. Nu spune că ai programat un memento dacă nu ai apelat unealta.',
    'bottleMl înseamnă câtă apă e acum în sticlă; capacity e capacitatea sticlei.',
    'Nu dai sfaturi medicale; pentru probleme de sănătate trimiți politicos la medic.',
  ].join(' ');

  const contents: Record<string, unknown>[] = [];
  for (const m of (body.history ?? []).slice(-8)) {
    contents.push({ role: m.role === 'user' ? 'user' : 'model', parts: [{ text: String(m.text).slice(0, MAX_QUESTION) }] });
  }
  contents.push({ role: 'user', parts: [{ text: question }] });

  const actions: { type: string; minutes: number; message: string }[] = [];
  const toolCalls: { name: string; args: unknown }[] = [];

  const runTool = async (name: string, args: Record<string, unknown>) => {
    toolCalls.push({ name, args });
    if (name === 'get_hydration_status') return { ...(body.ctx ?? {}), council: body.decision ?? null };
    if (name === 'get_drink_history') {
      const days = Math.min(30, Math.max(1, Number(args.days) || 7));
      const since = new Date(Date.now() - days * 86400000).toISOString();
      const { data } = await sb.from('drink_events').select('ts, ml').eq('kind', 'drink').gte('ts', since);
      const byDay: Record<string, number> = {};
      for (const e of data ?? []) {
        const d = new Date(e.ts).toLocaleDateString('sv-SE', { timeZone: 'Europe/Bucharest' });
        byDay[d] = (byDay[d] ?? 0) + e.ml;
      }
      return { days, goal_ml: profile?.goal_ml ?? 2000, totals: byDay };
    }
    if (name === 'schedule_reminder') {
      const minutes = Math.min(240, Math.max(1, Math.round(Number(args.minutes) || 30)));
      const message = String(args.message ?? 'Bea apă.').slice(0, 200);
      actions.push({ type: 'remind', minutes, message });
      return { ok: true, minutes };
    }
    return { error: `unealtă necunoscută: ${name}` };
  };

  try {
    for (let step = 0; step < MAX_STEPS; step++) {
      const res = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'x-goog-api-key': key },
        body: JSON.stringify({
          systemInstruction: { parts: [{ text: system }] },
          contents,
          tools: TOOLS,
          generationConfig: { temperature: 0.9, maxOutputTokens: 400 },
        }),
      });
      if (!res.ok) {
        const detail = (await res.text()).slice(0, 300);
        console.error('gemini', res.status, detail);
        // mesajul Google (fără cheie în el) ajută la depanare din aplicație
        let reason = '';
        try { reason = JSON.parse(detail)?.error?.message ?? ''; } catch { /* nu e JSON */ }
        return json({ fallback: true, error: `modelul a răspuns ${res.status}`, reason: reason.slice(0, 200) }, 503);
      }
      const out = await res.json();
      const content = out?.candidates?.[0]?.content;
      const parts: Record<string, any>[] = content?.parts ?? [];
      const calls = parts.filter((p) => p.functionCall);
      if (!calls.length) {
        const reply = parts.map((p) => p.text ?? '').join('').trim();
        return json({ reply: reply || '…(rinichii au rămas fără cuvinte)', actions, toolCalls, model });
      }
      // Răspunsul modelului se păstrează exact (include semnăturile de raționament).
      contents.push(content);
      const responses = [];
      for (const p of calls) {
        const result = await runTool(p.functionCall.name, p.functionCall.args ?? {});
        responses.push({ functionResponse: { name: p.functionCall.name, response: result } });
      }
      contents.push({ role: 'user', parts: responses });
    }
    return json({ reply: 'Am consultat prea multe organe. Bea un pahar și întreabă-mă din nou.', actions, toolCalls, model });
  } catch (e) {
    console.error(e);
    return json({ fallback: true, error: 'eroare la model' }, 503);
  }
});
