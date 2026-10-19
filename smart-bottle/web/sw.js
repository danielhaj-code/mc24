// Cache pentru funcționare offline (aplicația e statică; datele stau în IndexedDB).
const CACHE = 'smartbottle-v2';
const ASSETS = ['./', 'index.html', 'style.css', 'app.js', 'logic.js', 'agent.js', 'hydro.js', 'cloud.js', 'config.js', 'manifest.webmanifest', 'icon.svg'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(ASSETS)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

// Rețea întâi (ca să vezi imediat versiunile noi), cache dacă ești offline.
self.addEventListener('fetch', (e) => {
  if (e.request.method !== 'GET') return;
  // Supabase (API, auth, funcții) nu trece prin cache; SDK-ul de pe CDN da, ca să meargă offline.
  const url = new URL(e.request.url);
  if (url.origin !== self.location.origin && url.hostname !== 'cdn.jsdelivr.net') return;
  e.respondWith(
    fetch(e.request)
      .then((res) => {
        const copy = res.clone();
        caches.open(CACHE).then((c) => c.put(e.request, copy));
        return res;
      })
      .catch(() => caches.match(e.request)),
  );
});
