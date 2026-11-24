// Varianta stm32-template/ (șablonul de laborator Template_GPIO) are o copie a modulelor din
// stm32/Core. Testul pică dacă cele două au ajuns să difere: rulează stm32-template/sync-core.sh.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const files = [
  ...['app', 'display', 'filter', 'fonts_ui', 'link', 'protocol', 'sensor', 'ui_gfx'].map((n) => `Src/${n}.c`),
  ...['app', 'app_config', 'display', 'filter', 'link', 'protocol', 'sensor', 'ui_gfx'].map((n) => `Inc/${n}.h`),
];
// git poate scrie CRLF sau LF în copia de lucru; comparăm conținutul, nu terminațiile de linie
const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8').replace(/\r\n/g, '\n');

for (const f of files) {
  assert.equal(read(`../stm32-template/${f}`), read(`../stm32/Core/${f}`), `${f} diferă: rulează stm32-template/sync-core.sh`);
}
console.log(`ok: ${files.length} fișiere identice între stm32/Core și stm32-template`);
