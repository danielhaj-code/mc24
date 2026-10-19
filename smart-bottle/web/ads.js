// Reclamele planului Free. Toți „advertiserii” sunt inventați, iar ilustrațiile sunt desenate aici.
// Formate: banner nativ sus, card sponsorizat în listă și reclame video care nu se pot sări,
// declanșate de câteva acțiuni (demo, conectare, calibrare, export, „Elimină reclamele”).

/* =========================== Ilustrații =========================== */
// Fiecare reclamă are propria „identitate de brand”, ca reclamele adevărate. viewBox 400×300.
const ART = {
  spring: () => `
    <defs>
      <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#BFE3F7"/><stop offset="1" stop-color="#EAF6FB"/></linearGradient>
      <linearGradient id="mt1" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7FA6B8"/><stop offset="1" stop-color="#A9C6D2"/></linearGradient>
      <linearGradient id="mt2" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5E8A73"/><stop offset="1" stop-color="#86AE92"/></linearGradient>
      <linearGradient id="btl" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#D6EEFA"/><stop offset=".5" stop-color="#F4FBFF"/><stop offset="1" stop-color="#B9DDF1"/></linearGradient>
      <linearGradient id="cap" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#2F6E8F"/><stop offset="1" stop-color="#4C93B5"/></linearGradient>
    </defs>
    <rect width="400" height="300" fill="url(#sky)"/>
    <circle cx="320" cy="62" r="26" fill="#FFF6D8"/>
    <path d="M0 200 L70 110 L120 160 L180 80 L250 170 L300 120 L400 210 V300 H0Z" fill="url(#mt1)"/>
    <path d="M150 104 L180 80 L205 112 L192 106 L180 118 L168 106Z" fill="#fff" opacity=".85"/>
    <path d="M0 240 Q90 190 170 230 T400 220 V300 H0Z" fill="url(#mt2)"/>
    <path d="M60 300 Q120 250 150 262 Q200 280 230 300Z" fill="#9FD3EA" opacity=".8"/>
    <g transform="translate(230 70)">
      <ellipse cx="45" cy="210" rx="52" ry="9" fill="#2B4A3A" opacity=".25"/>
      <rect x="30" y="0" width="30" height="22" rx="4" fill="url(#cap)"/>
      <path d="M28 22 H62 L70 48 Q90 60 90 92 V196 Q90 210 76 210 H14 Q0 210 0 196 V92 Q0 60 20 48Z" fill="url(#btl)" stroke="#9CC9E0" stroke-width="2"/>
      <rect x="4" y="110" width="82" height="58" fill="#fff" opacity=".92"/>
      <path d="M12 150 L30 128 L42 142 L54 124 L78 150Z" fill="#5E8A73"/>
      <text x="45" y="164" text-anchor="middle" font-family="Georgia,serif" font-size="9" fill="#2F6E8F" font-weight="700">IZVORUL</text>
      <rect x="10" y="70" width="6" height="110" rx="3" fill="#fff" opacity=".7"/>
    </g>
    <g fill="#fff" opacity=".9"><circle cx="210" cy="230" r="4"/><circle cx="222" cy="214" r="2.5"/><circle cx="200" cy="208" r="3"/></g>`,

  glass: () => `
    <defs>
      <radialGradient id="bgG" cx=".5" cy=".4" r=".8"><stop offset="0" stop-color="#2A2F4F"/><stop offset="1" stop-color="#0F1226"/></radialGradient>
      <linearGradient id="gl" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#FFFFFF" stop-opacity=".25"/><stop offset=".5" stop-color="#FFFFFF" stop-opacity=".05"/><stop offset="1" stop-color="#FFFFFF" stop-opacity=".2"/></linearGradient>
      <linearGradient id="liq" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7FD3FF" stop-opacity=".75"/><stop offset="1" stop-color="#2E8BD6" stop-opacity=".9"/></linearGradient>
      <filter id="glow"><feGaussianBlur stdDeviation="6"/></filter>
    </defs>
    <rect width="400" height="300" fill="url(#bgG)"/>
    <ellipse cx="200" cy="262" rx="90" ry="14" fill="#6FE3FF" opacity=".35" filter="url(#glow)"/>
    <ellipse cx="200" cy="258" rx="62" ry="9" fill="none" stroke="#6FE3FF" stroke-width="3" filter="url(#glow)"/>
    <ellipse cx="200" cy="258" rx="62" ry="9" fill="none" stroke="#B6F2FF" stroke-width="1.5"/>
    <path d="M140 60 L152 252 Q153 262 163 262 H237 Q247 262 248 252 L260 60Z" fill="url(#gl)" stroke="#CFE9FF" stroke-opacity=".6" stroke-width="2"/>
    <path d="M146 130 L152 252 Q153 262 163 262 H237 Q247 262 248 252 L254 130Z" fill="url(#liq)"/>
    <g opacity=".95">
      <rect x="168" y="112" width="34" height="34" rx="6" fill="#E9FAFF" opacity=".75" transform="rotate(-12 185 129)"/>
      <rect x="204" y="120" width="30" height="30" rx="6" fill="#E9FAFF" opacity=".65" transform="rotate(14 219 135)"/>
    </g>
    <rect x="154" y="70" width="6" height="170" rx="3" fill="#fff" opacity=".35"/>
    <g fill="#B6F2FF" opacity=".8"><circle cx="190" cy="200" r="3"/><circle cx="214" cy="226" r="2"/><circle cx="226" cy="184" r="2.5"/></g>
    <g transform="translate(288 46)"><rect width="78" height="30" rx="15" fill="#FF5DA2"/><text x="39" y="20" text-anchor="middle" font-family="Arial,sans-serif" font-size="13" font-weight="800" fill="#fff">NOU</text></g>
    <text x="34" y="64" font-family="Arial,sans-serif" font-size="22" font-weight="800" fill="#fff" letter-spacing="1">PaharMax</text>
    <text x="34" y="88" font-family="Arial,sans-serif" font-size="13" fill="#9FB3D9">3000 Pro Ultra+</text>`,

  kidneys: () => `
    <defs>
      <linearGradient id="spa" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FCE7EF"/><stop offset="1" stop-color="#F7D3E0"/></linearGradient>
      <linearGradient id="tub" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#E7EDF3"/></linearGradient>
      <linearGradient id="kd" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#E58BA6"/><stop offset="1" stop-color="#C45C7E"/></linearGradient>
    </defs>
    <rect width="400" height="300" fill="url(#spa)"/>
    <g fill="#fff" opacity=".7"><circle cx="70" cy="60" r="30"/><circle cx="100" cy="54" r="22"/><circle cx="330" cy="40" r="18"/></g>
    <g stroke="#fff" stroke-width="4" fill="none" stroke-linecap="round" opacity=".9">
      <path d="M150 120 q-10 -16 0 -32 q10 -16 0 -32"/><path d="M200 112 q-10 -16 0 -32 q10 -16 0 -32"/><path d="M250 120 q-10 -16 0 -32 q10 -16 0 -32"/>
    </g>
    <g transform="translate(130 140)">
      <path d="M38 52 C8 52 0 22 18 8 C34 -4 52 8 50 24 C48 34 58 38 58 46 C58 52 50 54 38 52Z" fill="url(#kd)"/>
      <circle cx="26" cy="22" r="3" fill="#3A1A26"/><circle cx="40" cy="22" r="3" fill="#3A1A26"/>
      <path d="M26 32 q7 6 14 0" stroke="#3A1A26" stroke-width="2.5" fill="none" stroke-linecap="round"/>
    </g>
    <g transform="translate(270 140) scale(-1 1)">
      <path d="M38 52 C8 52 0 22 18 8 C34 -4 52 8 50 24 C48 34 58 38 58 46 C58 52 50 54 38 52Z" fill="url(#kd)"/>
      <circle cx="26" cy="22" r="3" fill="#3A1A26"/><circle cx="40" cy="22" r="3" fill="#3A1A26"/>
      <path d="M26 32 q7 6 14 0" stroke="#3A1A26" stroke-width="2.5" fill="none" stroke-linecap="round"/>
      <path d="M8 6 L4 -6 L16 2" fill="#F4E7C5"/>
    </g>
    <path d="M70 186 H330 V246 Q330 270 306 270 H94 Q70 270 70 246Z" fill="url(#tub)" stroke="#D9E1EA" stroke-width="2"/>
    <path d="M70 186 H330" stroke="#9FD3EA" stroke-width="10" stroke-linecap="round"/>
    <g fill="#fff"><circle cx="110" cy="182" r="7"/><circle cx="128" cy="178" r="5"/><circle cx="272" cy="180" r="6"/><circle cx="292" cy="183" r="8"/></g>
    <g transform="translate(300 214) rotate(-10)"><circle r="34" fill="#C45C7E"/><text y="-2" text-anchor="middle" font-family="Arial,sans-serif" font-size="18" font-weight="800" fill="#fff">-30%</text><text y="16" text-anchor="middle" font-family="Arial,sans-serif" font-size="9" fill="#FCE7EF">al doilea</text></g>`,

  course: () => `
    <defs>
      <linearGradient id="crs" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFF8E7"/><stop offset="1" stop-color="#F4E7C5"/></linearGradient>
    </defs>
    <rect width="400" height="300" fill="#1E3A5F"/>
    <g opacity=".15" stroke="#fff"><path d="M0 60 H400 M0 120 H400 M0 180 H400 M0 240 H400"/><path d="M80 0 V300 M160 0 V300 M240 0 V300 M320 0 V300"/></g>
    <g transform="translate(70 50) rotate(-4)">
      <rect width="260" height="180" rx="6" fill="url(#crs)"/>
      <rect x="10" y="10" width="240" height="160" rx="4" fill="none" stroke="#C8A44D" stroke-width="2"/>
      <text x="130" y="48" text-anchor="middle" font-family="Georgia,serif" font-size="13" fill="#7A5B16" letter-spacing="2">CERTIFICAT</text>
      <text x="130" y="76" text-anchor="middle" font-family="Georgia,serif" font-size="17" font-style="italic" fill="#3A2C0A">Băutor de Apă Autorizat</text>
      <path d="M60 104 H200" stroke="#C8A44D" stroke-width="1"/>
      <text x="130" y="122" text-anchor="middle" font-family="Georgia,serif" font-size="10" fill="#7A5B16">Nivel avansat: înghițitura cu ambele mâini</text>
      <path d="M150 150 q12 -14 24 0 t24 0" stroke="#1E3A5F" stroke-width="1.5" fill="none"/>
      <g transform="translate(50 146)"><circle r="20" fill="#C8A44D"/><circle r="14" fill="#E6C66E"/><path d="M-10 18 L-14 40 L0 32 L14 40 L10 18" fill="#B23A48"/></g>
    </g>
    <g transform="translate(300 230)"><rect width="84" height="40" rx="20" fill="#FFD166"/><text x="42" y="25" text-anchor="middle" font-family="Arial,sans-serif" font-size="13" font-weight="800" fill="#1E3A5F">30 ZILE</text></g>`,

  battery: () => `
    <defs>
      <linearGradient id="bt" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#1B1F24"/><stop offset=".5" stop-color="#3A414A"/><stop offset="1" stop-color="#1B1F24"/></linearGradient>
      <linearGradient id="chg" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#3DDC97"/><stop offset="1" stop-color="#B8F5D6"/></linearGradient>
    </defs>
    <rect width="400" height="300" fill="#F1F5F2"/>
    <circle cx="200" cy="150" r="120" fill="#DDF5E9"/>
    <g transform="translate(150 54)">
      <rect x="34" y="0" width="32" height="14" rx="4" fill="#3A414A"/>
      <rect x="0" y="12" width="100" height="190" rx="16" fill="url(#bt)"/>
      <rect x="10" y="62" width="80" height="130" rx="8" fill="url(#chg)"/>
      <path d="M56 78 L32 128 H50 L42 172 L70 116 H52Z" fill="#fff"/>
      <rect x="10" y="24" width="80" height="28" rx="6" fill="#0F1215"/>
      <text x="50" y="43" text-anchor="middle" font-family="Courier New,monospace" font-size="13" fill="#3DDC97">48:00h</text>
    </g>
    <g transform="translate(60 210)"><rect width="74" height="44" rx="6" fill="#1D5C3F"/><rect x="6" y="6" width="62" height="32" rx="3" fill="#2E7D57"/><g fill="#C9A227"><rect x="10" y="40" width="4" height="8"/><rect x="20" y="40" width="4" height="8"/><rect x="30" y="40" width="4" height="8"/><rect x="40" y="40" width="4" height="8"/><rect x="50" y="40" width="4" height="8"/><rect x="60" y="40" width="4" height="8"/></g><text x="37" y="27" text-anchor="middle" font-family="Arial,sans-serif" font-size="9" font-weight="700" fill="#DDF5E9">ESP32</text></g>
    <path d="M134 232 Q160 232 168 212" stroke="#3A414A" stroke-width="5" fill="none" stroke-linecap="round"/>`,

  ice: () => `
    <defs>
      <linearGradient id="slate" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3B4148"/><stop offset="1" stop-color="#22272C"/></linearGradient>
      <linearGradient id="cube" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFFFFF" stop-opacity=".95"/><stop offset="1" stop-color="#A8D8F0" stop-opacity=".7"/></linearGradient>
    </defs>
    <rect width="400" height="300" fill="#ECE6DC"/>
    <path d="M30 180 L370 160 L380 260 L20 276Z" fill="url(#slate)"/>
    <g>
      <path d="M120 150 L170 136 L196 166 L146 182Z" fill="#E9F7FF"/><path d="M120 150 L146 182 L146 226 L120 194Z" fill="#BFE3F5"/><path d="M146 182 L196 166 L196 210 L146 226Z" fill="url(#cube)"/>
      <path d="M210 158 L254 146 L278 172 L234 186Z" fill="#E9F7FF"/><path d="M210 158 L234 186 L234 226 L210 198Z" fill="#BFE3F5"/><path d="M234 186 L278 172 L278 212 L234 226Z" fill="url(#cube)"/>
      <path d="M176 112 L210 102 L228 124 L194 134Z" fill="#E9F7FF"/><path d="M176 112 L194 134 L194 164 L176 142Z" fill="#BFE3F5"/><path d="M194 134 L228 124 L228 154 L194 164Z" fill="url(#cube)"/>
    </g>
    <g fill="#fff" opacity=".9"><path d="M150 196 l3 -8 l3 8 l8 3 l-8 3 l-3 8 l-3 -8 l-8 -3Z"/><path d="M262 190 l2 -6 l2 6 l6 2 l-6 2 l-2 6 l-2 -6 l-6 -2Z"/></g>
    <path d="M300 90 q30 10 40 40" stroke="#5B6B4E" stroke-width="3" fill="none"/>
    <g fill="#7E9A64"><ellipse cx="318" cy="96" rx="12" ry="6" transform="rotate(25 318 96)"/><ellipse cx="334" cy="114" rx="12" ry="6" transform="rotate(55 334 114)"/></g>
    <text x="36" y="62" font-family="Georgia,serif" font-size="24" font-style="italic" fill="#3B4148">Gheață</text>
    <text x="36" y="88" font-family="Georgia,serif" font-size="14" letter-spacing="3" fill="#8A7A62">ARTIZANALĂ</text>`,

  flat: () => `
    <rect width="400" height="300" fill="#FFFFFF"/>
    <rect y="200" width="400" height="100" fill="#EEF1F4"/>
    <ellipse cx="200" cy="222" rx="120" ry="10" fill="#8EC5EA"/>
    <ellipse cx="180" cy="220" rx="40" ry="2" fill="#fff" opacity=".7"/>
    <g transform="translate(130 160)">
      <rect width="140" height="22" rx="4" fill="#F2C94C" stroke="#C99A1A" stroke-width="2"/>
      <rect x="56" y="5" width="28" height="12" rx="6" fill="#DFF3E4" stroke="#7FB58C"/>
      <circle cx="70" cy="11" r="4" fill="#7FB58C"/>
    </g>
    <text x="200" y="74" text-anchor="middle" font-family="Arial,sans-serif" font-size="30" font-weight="900" fill="#1B2A3A" letter-spacing="-1">ULTRA PLATĂ</text>
    <text x="200" y="102" text-anchor="middle" font-family="Arial,sans-serif" font-size="13" fill="#5C6B7A">0 bule. 0 valuri. 0 opinii.</text>
    <g transform="translate(318 236)"><rect width="66" height="26" rx="13" fill="#1B2A3A"/><text x="33" y="17" text-anchor="middle" font-family="Arial,sans-serif" font-size="11" font-weight="700" fill="#fff">±0,0 mm</text></g>`,
};

/* =========================== Catalogul de reclame =========================== */
export const AD_CATALOG = [
  { id: 'spring', advertiser: 'Izvorul din Carpați', headline: 'Apă de izvor, îmbuteliată direct din vârful muntelui.', body: 'Filtrată prin 400 de metri de rocă și o singură privire severă.', cta: 'Comandă bax', bg: '#EAF6FB', ink: '#1E4D63', price: '49,99 lei', deal: '-70%', ticker: 'LIVRARE GRATUITĂ*  ★  *DOAR PE VÂRFUL MUNTELUI  ★  BAXUL SE CARĂ PE JOS  ★  ' },
  { id: 'glass', advertiser: 'PaharMax', headline: 'Paharul care îți spune că ți-e sete. Ca sticla ta, dar mai scump.', body: 'LED-uri RGB, Bluetooth 6.0 și un abonament lunar obligatoriu.', cta: 'Precomandă', bg: '#151933', ink: '#FFFFFF', price: '1.299 lei', deal: 'NOU!!!', ticker: 'STOC LIMITAT  ★  ULTIMELE 3 BUCĂȚI DE 3 ANI  ★  ABONAMENT OBLIGATORIU  ★  ' },
  { id: 'kidneys', advertiser: 'Rinichi Fericiți SPA', headline: 'Un weekend de relaxare pentru rinichii tăi.', body: 'Jacuzzi, prosoape pufoase, filtrare la cerere. -30% la al doilea rinichi.', cta: 'Rezervă acum', bg: '#FCE7EF', ink: '#7A2E4A', price: '399 lei', deal: '-30%', ticker: 'AL DOILEA RINICHI LA JUMĂTATE DE PREȚ  ★  OFERTA NU SE APLICĂ RINICHILOR  ★  ' },
  { id: 'course', advertiser: 'H₂O Academy', headline: 'Învață să bei apă în doar 30 de zile.', body: 'Curs video cu 47 de module. Certificat recunoscut de tine.', cta: 'Înscrie-te', bg: '#1E3A5F', ink: '#FFFFFF', price: '19,99 lei', deal: '-95%', ticker: 'ULTIMELE LOCURI  ★  ÎNSCRIERI ÎNCHISE DE 2019  ★  CERTIFICAT ÎN PDF  ★  ' },
  { id: 'battery', advertiser: 'VoltCuib', headline: 'Sticla ta merită 48 de ore de Bluetooth.', body: 'Powerbank pentru ESP32, cu indicator care minte doar puțin.', cta: 'Vezi oferta', bg: '#F1F5F2', ink: '#1D5C3F', price: '89 lei', deal: '2+1', ticker: '48 DE ORE*  ★  *ÎN CONDIȚII DE LABORATOR LA 0 KELVIN  ★  CABLU VÂNDUT SEPARAT  ★  ' },
  { id: 'ice', advertiser: 'Gheață Artizanală', headline: 'Cuburi tăiate manual, maturate 12 luni.', body: 'Fiecare cub e unic. Se topesc la fel ca celelalte.', cta: 'Degustă', bg: '#ECE6DC', ink: '#3B4148', price: '120 lei/cub', deal: 'LUX', ticker: 'FIECARE CUB E UNIC  ★  SE TOPEȘTE ÎN 9 MINUTE  ★  NU SE RETURNEAZĂ  ★  ' },
  { id: 'flat', advertiser: 'Apa Ultra Plată', headline: 'Cea mai plată apă de pe piață. Verificată cu nivela.', body: 'Pentru cei care consideră apa plată prea agitată.', cta: 'Încearcă', bg: '#FFFFFF', ink: '#1B2A3A', price: '7,99 lei', deal: '-0%', ticker: '0 BULE  ★  0 VALURI  ★  0 MOTIVE SĂ CUMPERI  ★  CUMPĂRĂ ACUM  ★  ' },
];

export function adArt(id, uid = 'a') {
  // id-urile din <defs> se repetă între reclame: le prefixăm ca să nu se ciocnească în pagină
  const svg = ART[id]().replace(/id="([^"]+)"/g, `id="${uid}-$1"`).replace(/url\(#([^)]+)\)/g, `url(#${uid}-$1)`);
  return `<svg viewBox="0 0 400 300" preserveAspectRatio="xMidYMid slice" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">${svg}</svg>`;
}

/* =========================== Afișare =========================== */
const $ = (id) => document.getElementById(id);
let opts = { enabled: () => false, onRemoveAds: () => {}, toast: () => {} };
let uidCounter = 0;
const pickNext = (avoid) => {
  let ad;
  do { ad = AD_CATALOG[Math.floor(Math.random() * AD_CATALOG.length)]; } while (AD_CATALOG.length > 1 && ad.id === avoid);
  return ad;
};

// Momeli pentru butonul X fals din colțul reclamei
const FAKE_X = [
  'Ha! X-ul ăla era tot reclamă.',
  'Reclama nu poate fi închisă. Poate fi doar admirată.',
  'Ai apăsat pe reclamă. Advertiserul îți mulțumește.',
  'Încercare înregistrată. Vânzările au crescut cu 0,3%.',
];

function countdown(el) {
  // ofertă „care expiră” și se resetează la nesfârșit
  let left = 4 * 60 + 59 - Math.floor(Math.random() * 120);
  const tick = () => {
    left = left <= 0 ? 299 : left - 1;
    el.textContent = `Oferta expiră în ${String(Math.floor(left / 60)).padStart(2, '0')}:${String(left % 60).padStart(2, '0')}`;
  };
  tick();
  const t = setInterval(() => { if (!el.isConnected) clearInterval(t); else tick(); }, 1000);
}

function nativeAd(ad, layout) {
  const el = document.createElement('div');
  el.className = `junk-ad ${layout}`;
  el.style.setProperty('--ad-bg', ad.bg);
  el.style.setProperty('--ad-ink', ad.ink);
  el.innerHTML = `
    <button class="junk-x" type="button" aria-label="Închide reclama">×</button>
    <span class="junk-label">PUBLICITATE</span>
    <div class="junk-art">${adArt(ad.id, `n${++uidCounter}`)}<span class="junk-burst"><b></b></span></div>
    <div class="junk-copy">
      <div class="junk-adv"></div>
      <div class="junk-head"></div>
      <div class="junk-body"></div>
      <div class="junk-price"><s class="junk-old"></s><b class="junk-new"></b></div>
      <div class="junk-timer"></div>
      <div class="junk-row"><button class="junk-cta" type="button"></button><button class="junk-remove" type="button">Elimină reclamele</button></div>
    </div>
    <div class="junk-ticker" aria-hidden="true"><span></span></div>`;
  el.querySelector('.junk-adv').textContent = ad.advertiser;
  el.querySelector('.junk-head').textContent = ad.headline;
  el.querySelector('.junk-body').textContent = ad.body;
  el.querySelector('.junk-burst b').textContent = ad.deal;
  el.querySelector('.junk-new').textContent = ad.price;
  el.querySelector('.junk-old').textContent = '9.999 lei';
  el.querySelector('.junk-cta').textContent = `${ad.cta.toUpperCase()} ACUM!!!`;
  el.querySelector('.junk-ticker span').textContent = ad.ticker.repeat(4);
  countdown(el.querySelector('.junk-timer'));
  el.querySelector('.junk-cta').addEventListener('click', () => opts.toast(`${ad.advertiser} nu există. Nici oferta. Dar rinichii tăi au apreciat gestul.`));
  el.querySelector('.junk-x').addEventListener('click', () => {
    el.classList.remove('shake'); void el.offsetWidth; el.classList.add('shake');
    opts.toast(FAKE_X[Math.floor(Math.random() * FAKE_X.length)]);
  });
  el.querySelector('.junk-remove').addEventListener('click', () => opts.onRemoveAds());
  return el;
}

let current = { banner: null, feed: null };
function renderSlots() {
  const on = opts.enabled();
  $('adBanner').hidden = !on;
  $('adFeed').hidden = !on;
  if (!on) return;
  current.banner = pickNext(current.banner?.id);
  current.feed = pickNext(current.banner.id);
  $('adBanner').replaceChildren(nativeAd(current.banner, 'banner'));
  $('adFeed').replaceChildren(nativeAd(current.feed, 'feed'));
}

/* =========================== Reclame video care nu se pot sări =========================== */
const MIN_GAP_MS = 60000;            // cel mult o reclamă video pe minut (excepție: „Elimină reclamele”)
let lastVideo = 0;
let playing = null;

const REASONS = {
  demo: 'Pornim demo-ul imediat după acest scurt mesaj.',
  connect: 'Sticla s-a conectat. Datele tale sunt prezentate de:',
  calibrate: 'Calibrarea continuă după reclamă.',
  export: 'Exportul tău CSV e sponsorizat de:',
  removeAds: 'Un ultim mesaj înainte să scapi de reclame.',
};

// Rezolvă după ce reclama s-a terminat (sau imediat, dacă nu e cazul).
export function playUnskippable(reason, { seconds = 5, force = false } = {}) {
  if (!opts.enabled()) return Promise.resolve();
  if (playing) return playing;
  if (!force && Date.now() - lastVideo < MIN_GAP_MS) return Promise.resolve();
  lastVideo = Date.now();
  const ad = pickNext(current.banner?.id);
  const dlg = $('adPlayer');
  dlg.style.setProperty('--ad-bg', ad.bg);
  dlg.style.setProperty('--ad-ink', ad.ink);
  dlg.style.setProperty('--ad-dur', `${seconds}s`);
  $('adPlayerReason').textContent = REASONS[reason] ?? '';
  const stage = $('adPlayerStage');
  stage.replaceChildren(nativeAd(ad, 'video'));
  $('adPlayerAdv').textContent = ad.advertiser;
  const btn = $('adPlayerDone');
  dlg.classList.remove('playing');
  void dlg.offsetWidth;               // repornește animațiile
  dlg.classList.add('playing');
  dlg.showModal();

  playing = new Promise((resolve) => {
    let left = seconds;
    let penalties = 0;
    let ended = false;
    const count = $('adPlayerCount');
    const show = () => {
      count.textContent = ended ? 'Reclama s-a terminat' : `Reclama se termină în ${left}`;
      btn.textContent = ended ? 'Continuă' : `Sari peste reclamă (${left})`;
      btn.classList.toggle('locked', !ended);
    };
    show();
    const timer = setInterval(() => {
      left--;
      if (left <= 0) { clearInterval(timer); ended = true; btn.focus(); }
      show();
    }, 1000);
    const onClick = () => {
      if (!ended) {
        // butonul de „sari” e decorativ; încercările costă (de cel mult 3 ori)
        if (penalties < 3) { penalties++; left++; show(); opts.toast('Reclama nu poate fi sărită. +1 secundă pentru tentativă.'); }
        else opts.toast('Bine, bine. Nu mai adăugăm secunde.');
        btn.classList.remove('nope'); void btn.offsetWidth; btn.classList.add('nope');
        return;
      }
      btn.removeEventListener('click', onClick);
      dlg.close();
      playing = null;
      resolve();
    };
    btn.addEventListener('click', onClick);
  });
  return playing;
}

export function initAds(o) {
  opts = { ...opts, ...o };
  // Escape nu închide reclama (de aceea „nu se poate sări”)
  $('adPlayer').addEventListener('cancel', (e) => e.preventDefault());
  renderSlots();
  setInterval(() => { if (opts.enabled() && document.visibilityState === 'visible') renderSlots(); }, 20000);
}

export function refreshAds() { renderSlots(); }
