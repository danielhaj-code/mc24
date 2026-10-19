# Smart Bottle

Sticlă inteligentă: STM32F429I-DISC1 citește nivelul apei și îl afișează pe LCD, un ESP32 îl trimite prin BLE,
iar aplicația web (Windows și Android, prin Web Bluetooth) afișează nivelul, istoricul și obiectivul zilnic.

```
Senzor ──► STM32F429I-DISC1 ──UART5──► ESP32 ──BLE (Nordic UART)──► Aplicație web (Chrome / Edge)
           ADC/HX711, filtru,           punte                       mL, consum, grafice, memento
           LCD, calibrare în RAM        transparentă                IndexedDB (istoric local)
```

Protocolul comun e descris în [PROTOCOL.md](PROTOCOL.md), iar lista de componente în [BOM.md](BOM.md).

## Structură

| Folder | Conținut |
|---|---|
| `stm32/Core/Inc`, `stm32/Core/Src` | Modulele firmware (se adaugă într-un proiect STM32CubeIDE) |
| `esp32/SmartBottleBridge` | Sketch Arduino pentru puntea UART ↔ BLE |
| `web/` | Aplicația web (PWA), fără build: senzor, HydroGPT™, abonamente, clasament |
| `supabase/` | Schema bazei de date (migrare SQL) și funcția `hydrogpt` (agentul AI în cloud) |
| `tests/` | Teste pe PC pentru filtru, protocol, logica aplicației și agent |

## Cablaj

| De la | La | Observații |
|---|---|---|
| STM32 PC12 (UART5 TX) | ESP32 GPIO16 (RX2) | 3.3 V ↔ 3.3 V, fără divizor |
| STM32 PD2 (UART5 RX) | ESP32 GPIO17 (TX2) | |
| STM32 GND | ESP32 GND | masă comună, obligatoriu |
| STM32 PE4 | Senzor apă `+` | senzorul e alimentat doar în timpul citirii |
| STM32 PA5 (ADC1_IN5) | Senzor apă `S` | |
| STM32 GND | Senzor apă `−` | |
| *(varianta HX711)* STM32 PE2 / PE3 | HX711 DT / SCK | VCC 3.3 V; RATE la VCC pentru 80 SPS |

Pinii sunt aleși dintre cei nefolosiți de LCD, SDRAM și giroscop. **Verifică-i în UM1670 înainte de cablare.**
Dacă schimbi un pin, schimbă-l în `app_config.h` și în CubeMX.

## Firmware STM32 (STM32CubeIDE)

1. **Proiect nou** pentru placa *STM32F429I-DISC1*. La întrebarea „Initialize all peripherals with their default mode?” răspunde **Nu**, ca să nu se genereze cod care intră în conflict cu BSP-ul.
2. **Configurare în CubeMX:**
   - **RCC/Clock:** HSE 8 MHz, SYSCLK 168 sau 180 MHz. Întârzierile HX711 se adaptează singure prin `SystemCoreClock`.
   - **ADC1:** canal IN5 (PA5), rezoluție 12 biți, conversie simplă, fără DMA. Sampling time 84 cicluri sau mai mult (senzorul are impedanță mare).
   - **UART5:** asincron, 115200 8N1, cu **UART5 global interrupt** activat (NVIC).
   - **GPIO:** PE4 output push-pull, nivel inițial Low. Pentru HX711: PE3 output Low și PE2 input pull-up.
3. **Adaugă BSP-ul** din pachetul STM32CubeF4:
   - Fișiere: `Drivers/BSP/STM32F429I-Discovery/` (`stm32f429i_discovery.c`, `_lcd.c`, `_sdram.c`), `Drivers/BSP/Components/ili9341/` și `Utilities/Fonts/`.
   - Adaugă căile în *Include paths*.
   - În `stm32f4xx_hal_conf.h` activează `HAL_LTDC_MODULE_ENABLED`, `HAL_DMA2D_MODULE_ENABLED`, `HAL_SDRAM_MODULE_ENABLED`, `HAL_SPI_MODULE_ENABLED` și `HAL_I2C_MODULE_ENABLED`.
4. **Copiază** `stm32/Core/Inc/*` și `stm32/Core/Src/*` în proiect.
5. **În `main.c`:**
   ```c
   /* USER CODE BEGIN Includes */
   #include "app.h"
   /* USER CODE BEGIN 2 */
   app_init();
   /* în while (1), USER CODE BEGIN 3 */
   app_loop();
   ```
   Callback-urile `HAL_UART_RxCpltCallback` și `HAL_UART_ErrorCallback` sunt deja în `link.c`. Nu le defini și în altă parte.
6. **Varianta cu celulă de sarcină:** pune `#define SENSOR_USE_HX711 1` în `app_config.h`.

**LED-uri:** LD3 (verde) e aprins când telefonul e conectat, iar LD4 (roșu) clipește la eroare de senzor.

## ESP32

**Modul mock (implicit, `MOCK_STM32 1`)** — cât timp STM32 nu e legat, ESP32 generează singur
mesajele pe care le-ar trimite placa (`L,raw,pct,stable` o dată pe secundă, `A,C`/`E,C`) și răspunde la
`C,...` și `R`. Formatul e identic, deci aplicația nu știe diferența. ESP32 rămâne doar adaptor Bluetooth:
mock-ul e o înlocuire temporară a plăcii, nu citește niciun senzor.

| Comandă (Serial Monitor, 115200) | Efect |
|---|---|
| `d` sau butonul **BOOT** | o înghițitură (3 s de citiri instabile, apoi nivelul scade 6–14%) |
| `r` | reumplere |
| `l` | ridică / pune la loc sticla (aplicația afișează „sticla lipsește”) |
| `a` | pornește / oprește înghițiturile automate (la 20–40 s) |

**Cu STM32 legat:** pune `#define MOCK_STM32 0` în `SmartBottleBridge.ino` (sau `-DMOCK_STM32=0`).
Comunicația serială e deja implementată la ambele capete (UART5 pe STM32, `Serial2` pe ESP32, vezi Cablaj).

Deschide `esp32/SmartBottleBridge/SmartBottleBridge.ino` în Arduino IDE, alege placa **ESP32 Dev Module** și încarcă sketch-ul. Funcționează cu ambele versiuni de core ESP32 (2.x și 3.x).

**Test fără aplicație:** deschide nRF Connect pe telefon, conectează-te la `SmartBottle` și activează notificările pe caracteristica TX. Ar trebui să vezi `L,...` în fiecare secundă.

## Aplicația web

**Local (Windows):**
```
cd web
python -m http.server 8000
```
Deschide `http://localhost:8000` în Chrome sau Edge. Pe `localhost` Web Bluetooth funcționează și fără HTTPS.

**Android:** pagina trebuie servită prin HTTPS. Ai două variante:
- Urci folderul `web/` pe GitHub Pages, Cloudflare Pages sau Netlify (sunt doar fișiere statice).
- Folosești `adb reverse tcp:8000 tcp:8000` și deschizi `http://localhost:8000` în Chrome pe telefon.

Din meniul Chrome poți alege **Instalează aplicația** (PWA).

**Butonul Demo** simulează dispozitivul cu înghițituri și reumpleri, deci poți testa interfața fără hardware.

**Utilizare:**
1. Conectează-te la dispozitiv.
2. Pune recipientul gol, așteaptă starea „stabil” și apasă **Setează GOL**.
3. Umple recipientul, așteaptă din nou și apasă **Setează PLIN**.
4. Alege forma recipientului și capacitatea.

Calibrarea se salvează în browser și se retrimite automat la fiecare conectare.

**Limitări cunoscute:**
- Nu funcționează în Firefox sau pe iOS.
- Memento-ul de inactivitate apare doar cât timp pagina e deschisă (fără activitate în fundal).

## HydroGPT™ și abonamentele (partea amuzantă)

**Agentul.** Decizia „bea / așteaptă / reumple” o ia *Consiliul Organelor*: Rinichiul, Vezica și Creierul
votează pe baza datelor reale (cât ai băut azi față de o curbă ideală 8:00–22:00, minute de la ultima
înghițitură, cât ai băut în ultima jumătate de oră, ora din zi). Majoritatea câștigă. Logica e în
`web/agent.js` și e testată. Agentul trimite singur mementouri, comentează fiecare înghițitură și răspunde
în chat. Pe Ultra și Enterprise, întrebările merg la un LLM adevărat (Gemini) prin funcția
`supabase/functions/hydrogpt`, care are unelte: citește starea sticlei, istoricul din baza de date și
poate programa mementouri. Dacă AI-ul din cloud nu răspunde, răspunde agentul local.

| | Free | Pro | Ultra | Enterprise |
|---|---|---|---|---|
| Preț | 0 lei | 9,99 lei | 49,99 lei | Contactează vânzările |
| Afișaj | rotunjit la ±50 mL | ±10 mL | la mililitru | la mililitru |
| Chat | 3 întrebări / zi | nelimitat | nelimitat + AI în cloud | idem |
| Personalitate | pasiv-agresiv, cu reclame | antrenor | somelier de apă | consultant corporatist |
| Extra | reclame la apă | grafic 7 zile | votul organelor, temă aurie | raport KPI pentru board |
| Memento | la 60 min | la 20 min | la 10 min | la 5 min |

Plata e 100% fictivă: niciun câmp de card, `checkout()` din baza de date emite o factură cu reducere 100%.

## Cloud (Supabase)

Proiectul e deja configurat (`web/config.js`). Fără internet, aplicația merge mai departe doar local.

- **Conturi anonime:** fiecare browser primește un cont și o poreclă („Delfinul Deshidratat #0586”).
- **Tabele:** `profiles` (porecla, abonamentul), `drink_events` (istoricul, sincronizat fără duplicate), `invoices`.
- **RLS:** fiecare vede doar datele lui. Abonamentul se schimbă doar prin `checkout()`, nu prin update direct.
  Clasamentul (`leaderboard_today()`) expune doar porecla și totalul zilei.
- **Agentul AI:** funcția `hydrogpt` citește abonamentul din baza de date (pe Free/Pro răspunde `402 Payment Required`)
  și cheia Gemini din secretul `GEMINI_API_KEY`. Modelul se poate schimba din secretul `GEMINI_MODEL`
  (implicit `gemini-3.5-flash-lite`).

**De la zero, pe alt proiect:** rulează `supabase/migrations/*.sql` în SQL Editor, activează
*Authentication → Sign In / Providers → Allow anonymous sign-ins*, publică funcția
(`supabase functions deploy hydrogpt`), adaugă secretul `GEMINI_API_KEY` și pune URL-ul și cheia
publishable în `web/config.js`.

## Teste

```
cd tests
gcc -Wall -Wextra -I../stm32/Core/Inc test_host.c ../stm32/Core/Src/filter.c ../stm32/Core/Src/protocol.c -o t && ./t
node test_logic.mjs
node test_agent.mjs
```
