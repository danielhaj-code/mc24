/* display.c — meniul de pe LCD-ul tactil al plăcii STM32F429I-Discovery.
 *
 * Cine îl folosește: app.c (bucla principală) — display_init() din app_init(), display_update() la
 * fiecare trecere prin app_loop(), display_poll_touch() la TOUCH_PERIOD_MS, display_next_page() la
 * butonul USER, display_notice() după calibrare / comutarea senzorului simulat.
 * Ce folosește: BSP-ul plăcii (LCD prin LTDC + SDRAM, touch STMPE811 prin I2C, driverul ILI9341
 * pentru rotire), ui_gfx pentru desen, HAL_GetTick() pentru ceas. Nimic din ISR.
 *
 * Model de redesenare (incremental, ca să nu pâlpâie și să nu consume CPU):
 *  - fiecare pagină are o parte „statică” (carduri, etichete), desenată doar la schimbarea paginii
 *    (force = page != drawn_page), și o parte „dinamică”, redesenată când ui_state_t s-a schimbat
 *    față de copia `shown` (state_changed) sau, pe pagina Info, și la fiecare secundă (ceasul);
 *  - pastila de legătură din antet se redesenează doar când se schimbă link_state();
 *  - sticla de pe Acasă se redesenează doar când se schimbă procentul (cache în bottle_pct);
 *  - drawn_page = -1 forțează redesenarea completă (după screensaver, după un mesaj de calibrare). */
#include "display.h"
#include "app_config.h"
#include "main.h"
#include "ui_gfx.h"
#include "stm32f429i_discovery_lcd.h"   /* BSP din STM32CubeF4: Drivers/BSP/STM32F429I-Discovery */
#include "stm32f429i_discovery_ts.h"
#include "ili9341.h"
#include <stdio.h>
#include <string.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

/* Ecran 240 x 320 (portret), același limbaj vizual ca site-ul: fundal deschis, carduri albe
 * rotunjite cu umbră, Roboto pentru text, JetBrains Mono pentru cifre. Meniul are 4 pagini,
 * alese din bara de jos. Tot ce se schimbă des e compus întâi în SDRAM (gfx_begin/gfx_end)
 * și copiat pe ecran dintr-o dată, ca să nu pâlpâie. */

/* Paginile meniului, în ordinea taburilor din bara de jos (și a butonului USER):
 *   Acasă   — sticla desenată + nivelul (mL sau %), starea citirii, valoarea brută;
 *   Istoric — graficul nivelului din ultimele 10 minute (date de la ESP32);
 *   Calib.  — citirea brută, butoanele „Setează gol/plin”, comutatorul „Senzor simulat”;
 *   Info    — versiune, timp de funcționare, starea legăturii ESP32 / aplicație, calibrare. */

enum { PAGE_HOME, PAGE_HIST, PAGE_CAL, PAGE_INFO, PAGE_COUNT };
static const char *const TAB_NAMES[PAGE_COUNT] = { "Acasă", "Istoric", "Calib.", "Info" };

/* Geometria comună (pixeli, coordonate de ecran 240 x 320):
 * bara de taburi = card la y 276..314 (TAB_Y, TAB_H); butoanele de calibrare la y 172..228 (BTN_Y,
 * BTN_H) — și desenul, și zonele tactile folosesc aceleași constante, ca să rămână aliniate. */
#define TAB_Y      276
#define TAB_H      38
#define BTN_Y      172
#define BTN_H      56
/* cât stă afișat un mesaj display_notice() (ms) */
#define NOTICE_MS  4000
#define MOCK_SW_X  176                    /* comutatorul „Senzor simulat” */
/* LCD_FRAME_BUFFER = 0xD0000000 (începutul SDRAM); ecranul ocupă 240*320*4 = 300 KiB, deci la +1 MiB
 * avem loc sigur pentru zona de compunere a ui_gfx (cel mult tot ecranul). */
#define SCRATCH    (LCD_FRAME_BUFFER + 0x100000)   /* zonă liberă în SDRAM, după framebuffer */

/* apa: gradient ca sticla de pe site, de sus (deschis) în jos (închis) */
#define WATER_TOP     0xFF7DB8EBu
#define WATER_BOTTOM  0xFF2C6FB8u
#define WATER_SURFACE 0xFFB9DCF8u

/* Starea afișajului (doar bucla principală):
 * page = pagina aleasă (touch / buton); drawn_page = pagina desenată acum pe ecran, -1 = nimic valid;
 * shown = copia ui_state_t de la ultima redesenare; shown_sec = secunda (HAL_GetTick()/1000) de atunci. */
static int page = PAGE_HOME, drawn_page = -1;
static ui_state_t shown;
static uint32_t shown_sec;
static int shown_link = -1;               /* starea afișată în pastila din antet */
static int bottle_pct = -1;               /* nivelul desenat în sticlă; -1 = de redesenat */
/* ts_ok = touch-ul a pornit; touch_was_down = starea de la eșantionul trecut (detectăm doar frontul
 * de apăsare, ca o atingere ținută să nu repete acțiunea).
 * notice = mesajul de pe pagina de calibrare: text, ok (culoare), until = HAL_GetTick() de expirare.
 * last_touch_ms = ultima atingere / apăsare de buton, pentru screensaver. */
static int ts_ok, touch_was_down;
static struct { char text[40]; int ok; uint32_t until; int active; } notice;
static uint32_t last_touch_ms;
static int saver;                         /* screensaver activ */
/* saver_drawn_ms = când a fost desenat ultimul cadru al screensaver-ului (se mută la SAVER_MOVE_MS) */
static uint32_t saver_drawn_ms;

/* ESP32-ul e viu dacă a trimis un heartbeat (H) în ultimele ESP_TIMEOUT_MS. esp_last_ms = 0 = niciodată.
 * Diferența fără semn rămâne corectă și la depășirea lui HAL_GetTick() (după ~49 de zile). */
static int esp_alive(const ui_state_t *st)
{
    return st->esp_last_ms && HAL_GetTick() - st->esp_last_ms < ESP_TIMEOUT_MS;
}

/* 0 = fără ESP32, 1 = fără client, 2 = client BLE dar aplicația nu răspunde, 3 = aplicație activă.
 * Starea st->app vine de la ESP32, deci fără heartbeat recent n-o mai credem (poate fi veche). */
static int link_state(const ui_state_t *st)
{
    if (!esp_alive(st)) return 0;
    return 1 + st->app;
}

/* Procentul afișat în textul „x% din y mL”: din mL-ul aplicației dacă îl avem, altfel din citirea
 * locală; limitat la 0..100 (ml poate depăși cap dacă aplicația are altă calibrare). */
static int shown_pct(const ui_state_t *st)
{
    int pct = st->has_ml && st->cap > 0 ? st->ml * 100 / st->cap : st->pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

/* Interpolare liniară între culorile a și b: num/den = 0 → a, 1 → b (pe canale R, G, B; rezultat opac).
 * num e limitat la 0..den; den <= 0 → a. Folosit pentru gradientul apei. */
static uint32_t mix(uint32_t a, uint32_t b, int num, int den)
{
    if (num < 0) num = 0;
    if (num > den) num = den;
    if (den <= 0) return a;
    uint32_t r = (((a >> 16) & 0xFF) * (den - num) + ((b >> 16) & 0xFF) * num) / den;
    uint32_t g = (((a >> 8) & 0xFF) * (den - num) + ((b >> 8) & 0xFF) * num) / den;
    uint32_t bl = ((a & 0xFF) * (den - num) + (b & 0xFF) * num) / den;
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

/* ---------------- elemente comune ---------------- */

/* Pastilă (etichetă rotunjită) cu text font_small: 22 px înălțime, 10 px spațiu de o parte și de alta.
 * Lățimea depinde de text; apelantul șterge zona dinainte dacă textul se poate scurta. */
static void pill(int x, int y, const char *text, uint32_t fg, uint32_t bg)
{
    int w = gfx_text_width(&font_small, text) + 20;
    gfx_round_rect(x, y, w, 22, 11, bg);
    gfx_text(x + 10, y + 2, &font_small, fg, text);
}

/* Antetul: pastila stării legăturii, aliniată la dreapta (marginea dreaptă la x = 228), lângă titlul
 * „Smart Bottle” (desenat o dată, la schimbarea paginii). Șterge zona fixă 138..230 x 13..35 ca să nu
 * rămână resturi de la un text mai lung. Memorează starea desenată în shown_link.
 *   0 Fără ESP32 (roz)  — nu vin heartbeat-uri;  1 Deconectat (gri) — ESP32 fără client BLE;
 *   2 App nu răsp. (galben) — client BLE, dar aplicația nu răspunde;  3 Conectat (albastru). */
static void draw_header(const ui_state_t *st)
{
    static const char *const text[] = { "Fără ESP32", "Deconectat", "App nu răsp.", "Conectat" };
    static const uint32_t fg[] = { C_PINK, C_MUTED, C_WARN, C_ACCENT };
    static const uint32_t bg[] = { C_PINK_BG, C_GREY, C_WARN_BG, C_ACCENT_BG };
    int s = link_state(st);
    int own = gfx_begin(138, 13, 92, 22);
    gfx_fill(138, 13, 92, 22, C_BG);
    int w = gfx_text_width(&font_small, text[s]) + 20;
    pill(228 - w, 13, text[s], fg[s], bg[s]);
    if (own) gfx_end();
    shown_link = s;
}

/* Bara de jos: 4 taburi de 54 px începând de la x = 12; tabul curent are fundal C_ACCENT_BG.
 * Desenată doar la schimbarea paginii (page se schimbă doar de aici sau din butonul USER). */
static void draw_tabs(void)
{
    gfx_card(8, TAB_Y, 224, TAB_H, C_CARD);
    for (int i = 0; i < PAGE_COUNT; i++) {
        int x = 12 + i * 54;
        if (i == page) gfx_round_rect(x + 2, TAB_Y + 5, 50, 28, 14, C_ACCENT_BG);
        /* centrul tabului e la x + 27 */
        int tw = gfx_text_width(&font_small, TAB_NAMES[i]);
        gfx_text(x + 27 - tw / 2, TAB_Y + 10, &font_small, i == page ? C_ACCENT : C_MUTED, TAB_NAMES[i]);
    }
}

/* starea citirii, ca pe site: „stabil” / „se mișcă” / eroare.
 * (x, y) = colțul pastilei; clear_w = lățimea zonei șterse înainte (cea mai lungă variantă de text);
 * stable_text = textul pentru citire stabilă (diferă între pagini: „stabil”, „simulat”, „poți calibra”). */
static void state_pill(int x, int y, int clear_w, const ui_state_t *st, const char *stable_text)
{
    int own = gfx_begin(x, y, clear_w, 22);
    gfx_fill(x, y, clear_w, 22, C_CARD);
    if (!st->sensor_ok)  pill(x, y, "eroare senzor", C_PINK, C_PINK_BG);
    else if (st->stable) pill(x, y, stable_text, C_ACCENT, C_ACCENT_BG);
    else                 pill(x, y, "se mișcă", C_WARN, C_WARN_BG);
    if (own) gfx_end();
}

/* ---------------- Acasă ---------------- */

/* Sticla: corp 58 x 160 la (36, 84), contur C_OUTLINE de 3 px, interior alb; apa are 5 px margine
 * față de contur, deci înălțimea utilă e BOT_H - 10 = 150 px (100%). Stânga cardului, coloana de
 * text e la dreapta (x ≈ 106..222). */
#define BOT_X  36
#define BOT_Y  84
#define BOT_W  58
#define BOT_H  160

/* Partea statică a paginii Acasă: cardul mare (12, 52, 216 x 212) și gâtul sticlei (inel C_OUTLINE cu
 * interior alb; partea lui de jos e acoperită apoi de corpul sticlei). Invalidează cache-ul bottle_pct,
 * ca sticla să fie redesenată peste cardul nou. */
static void home_static(void)
{
    gfx_card(12, 52, 216, 212, C_CARD);
    gfx_round_rect(BOT_X + 13, BOT_Y - 14, 32, 18, 6, C_OUTLINE);   /* gât */
    gfx_round_rect(BOT_X + 16, BOT_Y - 11, 26, 16, 4, C_CARD);
    bottle_pct = -1;
}

/* Apa: suprafață dreaptă, colțuri de jos rotunjite ca interiorul sticlei, gradient pe verticală.
 * x = stânga apei, bottom = y-ul de sub ultimul rând, w = lățimea, fill = înălțimea apei (px),
 * r = raza colțurilor de jos. Culoarea rândului depinde de poziția lui în toată înălțimea interioară
 * (span), nu în apă, ca gradientul să stea pe loc când nivelul se schimbă. Primele 2 rânduri de sus =
 * linia suprafeței, mai deschisă. */
static void draw_water(int x, int bottom, int w, int fill, int r)
{
    int top = bottom - fill, span = BOT_H - 10;
    for (int j = 0; j < fill; j++) {
        int y = top + j;
        uint32_t c = j < 2 ? WATER_SURFACE : mix(WATER_TOP, WATER_BOTTOM, y - (bottom - span), span);
        int d = bottom - 1 - y;                       /* rânduri de la fund */
        if (d >= r) { gfx_fill(x, y, w, 1, c); continue; }
        /* colț rotunjit: lățimea rândului din ecuația cercului + un pixel antialiasat la margine.
           Totul în unități de 1/2 pixel ca să rămânem pe întregi. */
        int dy = 2 * (r - d) - 1;                     /* distanța verticală față de centrul colțului */
        int in2 = 4 * r * r - dy * dy;                /* (2 * lățimea pe orizontală)^2 */
        int half = 0;
        while ((2 * half + 2) * (2 * half + 2) <= in2) half++;
        int in = r - half;
        gfx_fill(x + in, y, w - 2 * in, 1, c);
        if (in > 0) {
            int lo = 4 * half * half, hi = 4 * (half + 1) * (half + 1);
            unsigned a = (unsigned)(in2 - lo) * 255u / (unsigned)(hi - lo);
            gfx_pixel(x + in - 1, y, c, a);
            gfx_pixel(x + w - in, y, c, a);
        }
    }
}

/* Redesenează sticla doar dacă procentul s-a schimbat (bottle_pct = cache; -1 = invalid).
 * Eroare de senzor = sticlă goală. Compunere în zona sticlei (gfx_begin), ca apa să nu pâlpâie. */
static void home_bottle(const ui_state_t *st)
{
    /* sticla urmează citirea locală, la fiecare secundă; mL de la aplicație vin doar la citiri
       stabile cu sticla pe senzor, așa că desenul rămânea înghețat pe ultima valoare */
    int pct = st->sensor_ok ? st->pct : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct == bottle_pct) return;
    bottle_pct = pct;

    int own = gfx_begin(BOT_X, BOT_Y, BOT_W, BOT_H);
    gfx_fill(BOT_X, BOT_Y, BOT_W, BOT_H, C_CARD);
    gfx_round_rect(BOT_X, BOT_Y, BOT_W, BOT_H, 14, C_OUTLINE);
    gfx_round_rect(BOT_X + 3, BOT_Y + 3, BOT_W - 6, BOT_H - 6, 11, C_CARD);
    int inner = BOT_H - 10, fill = inner * pct / 100;
    if (fill > 0) draw_water(BOT_X + 5, BOT_Y + 5 + inner, BOT_W - 10, fill, 9);
    if (own) gfx_end();
}

/* Coloana din dreapta a paginii Acasă (x ≈ 106..222):
 *   y  84 — valoarea mare: mL de la aplicație sau % local; „–” la eroare de senzor;
 *   y 134 — unitatea; y 164 — „x% din cap mL” (doar cu date de la aplicație);
 *   y 192 — pastila stării citirii; y 226 — valoarea brută a convertorului (pentru depanare). */
static void home_dynamic(const ui_state_t *st)
{
    char buf[24];
    home_bottle(st);

    if (st->has_ml) snprintf(buf, sizeof buf, "%d", st->ml);
    else            snprintf(buf, sizeof buf, "%d", st->pct);
    gfx_text_box(106, 84, 116, &font_num, C_TEXT, C_CARD, 0, st->sensor_ok ? buf : "–");
    gfx_text_box(108, 134, 112, &font_body, C_MUTED, C_CARD, 0, st->has_ml ? "mL" : "% plin");

    if (st->has_ml) snprintf(buf, sizeof buf, "%d%% din %d mL", shown_pct(st), st->cap);
    else            snprintf(buf, sizeof buf, "fără date mL");
    gfx_text_box(108, 164, 116, &font_small, C_MUTED, C_CARD, 0, buf);

    state_pill(106, 192, 118, st, st->mock ? "simulat" : "stabil");

    snprintf(buf, sizeof buf, "brut %ld", (long)st->raw);
    gfx_text_box(108, 226, 116, &font_mono, C_MUTED, C_CARD, 0, buf);
}

/* ---------------- Istoric ---------------- */

/* graficul nivelului din ultimele 10 minute; punctele vin de la ESP32, câte unul la 10 s.
 * Cadrul graficului: x 44..224 (HIST_POINTS * HG_STEP = 180 px), y 92 (100%) .. 228 (0%) → 136 px. */
#define HG_X     44                       /* stânga graficului; în stânga, etichetele axei */
#define HG_TOP   92
#define HG_BOT   228
#define HG_STEP  3                        /* 60 de puncte × 3 px = 180 px */

/* Partea statică a Istoricului: doar cardul; restul se compune în hist_dynamic. */
static void hist_static(void)
{
    gfx_card(12, 52, 216, 212, C_CARD);
}

/* Redesenează tot cardul (titlu, ultima valoare, axa, stâlpii, etichetele timpului) într-o singură
 * zonă gfx_begin; apelat la orice schimbare din ui_state_t (de obicei un G nou, la 10 s).
 * Fără date (has_hist = 0) afișează doar un mesaj de așteptare. */
static void hist_dynamic(const ui_state_t *st)
{
    char buf[24];
    int own = gfx_begin(20, 58, 200, 200);
    gfx_fill(20, 58, 200, 200, C_CARD);
    gfx_text(28, 62, &font_small, C_MUTED, "Nivel · ultimele 10 min");

    if (!st->has_hist) {
        gfx_text_box(20, 150, 200, &font_small, C_MUTED, C_CARD, 1, "Aștept istoricul de la ESP32");
        if (own) gfx_end();
        return;
    }

    /* ultima valoare validă (cel mai nou punct != -1), afișată sus în dreapta */
    int last = -1;
    for (int i = HIST_POINTS - 1; i >= 0 && last < 0; i--) last = st->hist[i];
    snprintf(buf, sizeof buf, last < 0 ? "–" : "%d%%", last);
    gfx_text_box(140, 62, 76, &font_body, C_TEXT, C_CARD, 2, buf);

    /* axa: 100 / 50 / 0 %, linii punctate */
    static const int marks[3] = { 100, 50, 0 };
    for (int k = 0; k < 3; k++) {
        int y = HG_BOT - marks[k] * (HG_BOT - HG_TOP) / 100;
        snprintf(buf, sizeof buf, "%d", marks[k]);
        int tw = gfx_text_width(&font_small, buf);
        gfx_text(HG_X - 6 - tw, y - 7, &font_small, C_MUTED, buf);
        for (int x = HG_X; x < HG_X + HIST_POINTS * HG_STEP; x += 6) gfx_fill(x, y, 3, 1, C_TRACK);
    }

    /* un stâlp la 10 s; intervalele fără citiri rămân goale */
    for (int i = 0; i < HIST_POINTS; i++) {
        int v = st->hist[i];
        if (v < 0) continue;
        int h = v * (HG_BOT - HG_TOP) / 100;
        /* stâlp de cel puțin 1 px, ca 0% să se vadă diferit de „fără citiri”; ultimul (acum) în accent */
        if (h < 1) h = 1;
        gfx_fill(HG_X + i * HG_STEP, HG_BOT - h, HG_STEP - 1, h, i == HIST_POINTS - 1 ? C_ACCENT : C_OK);
    }

    gfx_text(HG_X, HG_BOT + 6, &font_small, C_MUTED, "-10 min");
    int tw = gfx_text_width(&font_small, "-5");
    gfx_text(HG_X + HIST_POINTS * HG_STEP / 2 - tw / 2, HG_BOT + 6, &font_small, C_MUTED, "-5");
    tw = gfx_text_width(&font_small, "acum");
    gfx_text(HG_X + HIST_POINTS * HG_STEP - tw, HG_BOT + 6, &font_small, C_MUTED, "acum");
    if (own) gfx_end();
}

/* ---------------- Calibrare ---------------- */

/* Butoanele de calibrare sunt active doar cu citire validă și stabilă (calibrarea pe o valoare care
 * se mișcă ar fi greșită). Aceeași condiție blochează și atingerea în display_poll_touch. */
static int cal_enabled(const ui_state_t *st) { return st->sensor_ok && st->stable; }

/* Buton de calibrare 104 x 56 la (x, BTN_Y), pe fundalul ecranului (nu în card). Zona ștearsă e
 * 106 x 60 ca să cuprindă și umbra cardului (+1 px dreapta, +3 px jos). Activ = card albastru-deschis,
 * inactiv = dreptunghi gri fără umbră. */
static void cal_button(int x, const char *text, int enabled)
{
    int own = gfx_begin(x, BTN_Y, 106, BTN_H + 4);
    gfx_fill(x, BTN_Y, 106, BTN_H + 4, C_BG);
    if (enabled) gfx_card(x, BTN_Y, 104, BTN_H, C_ACCENT_BG);
    else         gfx_round_rect(x, BTN_Y, 104, BTN_H, 16, C_GREY);
    int tw = gfx_text_width(&font_body, text);
    gfx_text(x + 52 - tw / 2, BTN_Y + 18, &font_body, enabled ? C_ACCENT : C_MUTED, text);
    if (own) gfx_end();
}

/* Partea statică a paginii de calibrare: cardul de sus (citire + stare, y 52..164) și cardul de jos
 * (comutatorul senzorului simulat, y 234..270); între ele stau butoanele (y 172..228). */
static void cal_static(void)
{
    gfx_card(12, 52, 216, 112, C_CARD);
    gfx_card(12, 234, 216, 36, C_CARD);
}

/* Partea dinamică: citirea brută (font mare), sub ea mesajul activ (notice) sau pastila de stare,
 * butoanele (active/inactive) și comutatorul „Senzor simulat” (pastila glisantă 40 x 20 la
 * x = MOCK_SW_X, butonul rotund în dreapta = pornit). Apelat și la expirarea mesajului. */
static void cal_dynamic(const ui_state_t *st)
{
    char buf[32];
    gfx_text_box(28, 62, 190, &font_small, C_MUTED, C_CARD, 0, "Citirea de acum");
    snprintf(buf, sizeof buf, "%ld", (long)st->raw);
    gfx_text_box(24, 78, 196, &font_num, C_TEXT, C_CARD, 0, st->sensor_ok ? buf : "–");

    if (notice.active) {
        int own = gfx_begin(24, 132, 196, 22);
        gfx_fill(24, 132, 196, 22, C_CARD);
        pill(24, 132, notice.text, notice.ok ? C_ACCENT : C_PINK, notice.ok ? C_ACCENT_BG : C_PINK_BG);
        if (own) gfx_end();
    } else {
        state_pill(24, 132, 196, st, "stabil · poți calibra");
    }

    cal_button(12, "Setează gol", cal_enabled(st));
    cal_button(124, "Setează plin", cal_enabled(st));

    /* comutator: senzor simulat (cât timp senzorul nu e montat) */
    int own = gfx_begin(20, 238, 200, 28);
    gfx_fill(20, 238, 200, 28, C_CARD);
    gfx_text(26, 243, &font_small, C_TEXT, "Senzor simulat");
    gfx_text_box(MOCK_SW_X - 54, 243, 48, &font_small, st->mock ? C_ACCENT : C_MUTED, C_CARD, 2,
                 st->mock ? "pornit" : "oprit");
    gfx_round_rect(MOCK_SW_X, 242, 40, 20, 10, st->mock ? C_ACCENT : C_GREY);
    gfx_circle(st->mock ? MOCK_SW_X + 30 : MOCK_SW_X + 10, 252, 8, C_CARD);
    if (own) gfx_end();
}

/* ---------------- Info ---------------- */

/* Tabel de 9 rânduri: etichete la x = 26, valori aliniate la dreapta în caseta x 104..214;
 * primul rând la y = 60, câte 22 px pe rând (ultimul la y = 236, în cardul 52..264). */
#define INFO_ROWS 9
#define INFO_Y    60
#define INFO_STEP 22

/* Cardul și etichetele (fixe); valorile le scrie info_dynamic. */
static void info_static(void)
{
    gfx_card(12, 52, 216, 212, C_CARD);
    static const char *const labels[INFO_ROWS] = {
        "Firmware", "Funcționare", "Senzor", "Legătură", "ESP32", "Aplicație", "Date aplicație", "Calibrare", "Obiectiv"
    };
    for (int i = 0; i < INFO_ROWS; i++) gfx_text(26, INFO_Y + i * INFO_STEP, &font_small, C_MUTED, labels[i]);
}

/* „acum N s” sub 2 minute, altfel „acum N min”, de la momentul since_ms (HAL_GetTick()). */
static void ago_text(char *out, size_t cap, uint32_t since_ms)
{
    uint32_t a = (HAL_GetTick() - since_ms) / 1000u;
    if (a < 120) snprintf(out, cap, "acum %lu s", (unsigned long)a);
    else         snprintf(out, cap, "acum %lu min", (unsigned long)(a / 60));
}

/* Valorile paginii Info, colorate după stare (albastru = bine, galben = atenție, roz = problemă).
 * Apelat și la fiecare secundă (ceasul „Funcționare” și textele „acum N s”). Toate valorile sunt
 * calculate întâi, apoi scrise într-o singură zonă gfx_begin. */
static void info_dynamic(const ui_state_t *st)
{
    char v[INFO_ROWS][32];
    uint32_t col[INFO_ROWS];
    for (int i = 0; i < INFO_ROWS; i++) col[i] = C_TEXT;

    uint32_t s = HAL_GetTick() / 1000u;
    snprintf(v[0], sizeof v[0], "%s", FW_VERSION);
    /* timpul de la pornirea plăcii, hh:mm:ss */
    snprintf(v[1], sizeof v[1], "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
             (unsigned long)(s % 60));
#if SENSOR_USE_HX711
    snprintf(v[2], sizeof v[2], "%s", st->mock ? "simulat (HX711)" : "HX711");
#else
    snprintf(v[2], sizeof v[2], "%s", st->mock ? "simulat" : "analogic PA5");
#endif
    if (st->mock) col[2] = C_WARN;
#if LINK_VCP
    snprintf(v[3], sizeof v[3], "USB (test)");
#else
    snprintf(v[3], sizeof v[3], "UART5 · ESP32");
#endif
    /* heartbeat ESP32 */
    if (!st->esp_last_ms) {
        snprintf(v[4], sizeof v[4], "fără semnal");
        col[4] = C_PINK;
    } else if (!esp_alive(st)) {
        char ago[20];
        ago_text(ago, sizeof ago, st->esp_last_ms);
        snprintf(v[4], sizeof v[4], "nu răsp., %s", ago);
        col[4] = C_PINK;
    } else {
        unsigned long up = st->esp_uptime;
        if (up < 3600) snprintf(v[4], sizeof v[4], "activ · %lu min", up / 60);
        else           snprintf(v[4], sizeof v[4], "activ · %lu h", up / 3600);
        if (st->esp_restarts) {
            size_t n = strlen(v[4]);
            snprintf(v[4] + n, sizeof v[4] - n, " · %d rep.", st->esp_restarts);
        }
        col[4] = C_ACCENT;
    }
    /* starea aplicației, ca în antet: fără ESP32 viu, starea venită de la el nu mai e de încredere */
    switch (esp_alive(st) ? st->app : 0) {
    case 2:  snprintf(v[5], sizeof v[5], "activă"); col[5] = C_ACCENT; break;
    case 1:  snprintf(v[5], sizeof v[5], "nu răspunde"); col[5] = C_WARN; break;
    default: snprintf(v[5], sizeof v[5], "neconectată"); break;
    }
    if (!st->last_app_ms) snprintf(v[6], sizeof v[6], "niciodată");
    else                  ago_text(v[6], sizeof v[6], st->last_app_ms);
    snprintf(v[7], sizeof v[7], "%ld / %ld", (long)st->cal_empty, (long)st->cal_full);
    if (st->has_today) snprintf(v[8], sizeof v[8], "%d mL", st->goal);
    else               snprintf(v[8], sizeof v[8], "–");

    int own = gfx_begin(104, INFO_Y, 112, INFO_ROWS * INFO_STEP);
    for (int i = 0; i < INFO_ROWS; i++)
        gfx_text_box(104, INFO_Y + i * INFO_STEP, 110, &font_small, col[i], C_CARD, 2, v[i]);
    if (own) gfx_end();
}

/* ---------------- Screensaver ---------------- */

/* fundalul închis al screensaver-ului (protejează panoul și nu orbește noaptea) și perioada de mutare */
#define SAVER_BG      0xFF0F1620u
#define SAVER_MOVE_MS 10000u

/* doar nivelul, pe fundal închis; poziția se mută la 10 s.
 * Desenează tot ecranul: valoarea mare (mL sau %) și sub ea unitatea + totalul de azi, centrate
 * orizontal. y se schimbă la fiecare SAVER_MOVE_MS: pasul * 67 mod 200 sare „aleator” prin 40..239,
 * ca pixelii să nu stea aprinși mereu în același loc. */
static void saver_draw(const ui_state_t *st)
{
    char big[16], small[32];
    if (!st->sensor_ok)  snprintf(big, sizeof big, "–");
    else if (st->has_ml) snprintf(big, sizeof big, "%d", st->ml);
    else                 snprintf(big, sizeof big, "%d", st->pct);
    if (st->has_today)   snprintf(small, sizeof small, "%s · azi %d/%d", st->has_ml ? "mL" : "%", st->total, st->goal);
    else                 snprintf(small, sizeof small, "%s", st->has_ml ? "mL" : "% plin");

    uint32_t step = HAL_GetTick() / SAVER_MOVE_MS;
    int y = 40 + (int)((step * 67u) % 200u);

    int own = gfx_begin(0, 0, GFX_W, GFX_H);
    gfx_fill(0, 0, GFX_W, GFX_H, SAVER_BG);
    int w = gfx_text_width(&font_num, big);
    gfx_text((GFX_W - w) / 2, y, &font_num, st->sensor_ok ? WATER_TOP : 0xFF8796A8u, big);
    w = gfx_text_width(&font_small, small);
    gfx_text((GFX_W - w) / 2, y + 50, &font_small, 0xFFAEB9C6u, small);
    if (own) gfx_end();
    saver_drawn_ms = HAL_GetTick();
}

/* ---------------- cadru ---------------- */

/* Pornește LCD-ul prin BSP (SDRAM, LTDC, ILI9341 pe SPI), opțional rotește imaginea cu 180°, setează
 * stratul de fundal pe framebuffer, pornește ui_gfx și touch-ul. Dacă touch-ul nu pornește, meniul
 * merge doar din butonul USER (ts_ok = 0). Apelat o dată din app_init(). */
void display_init(void)
{
    BSP_LCD_Init();
#if LCD_ROTATE_180
    /* Rotire din hardware: inversăm GS și SS în Display Function Control (0xB6).
       BSP scrie 0x0A,0xA7,0x27,0x04; aici diferă doar al doilea octet. */
    ili9341_WriteReg(LCD_DFC);
    ili9341_WriteData(0x0A);
    ili9341_WriteData(0xC7);
    ili9341_WriteData(0x27);
    ili9341_WriteData(0x04);
#endif
    /* stratul 0 al LTDC = tot ecranul, ARGB8888, din framebuffer-ul din SDRAM */
    BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_SelectLayer(LCD_BACKGROUND_LAYER);
    BSP_LCD_DisplayOn();
    gfx_init(LCD_FRAME_BUFFER, SCRATCH);
    gfx_fill(0, 0, GFX_W, GFX_H, C_BG);

    last_touch_ms = HAL_GetTick();
    ts_ok = BSP_TS_Init(GFX_W, GFX_H) == TS_OK;
    drawn_page = -1;
}

/* câmpurile care se schimbă la fiecare mesaj fără să schimbe ce se vede pe pagini fără ceas.
 * Compară stările ignorând câmpurile de timp (last_app_ms, esp_last_ms, esp_uptime): ele se schimbă
 * la fiecare mesaj/heartbeat (la 2 s) și ar redesena degeaba. Ce depinde de ele (pagina Info, pastila
 * din antet) se reîmprospătează separat: la fiecare secundă, respectiv la schimbarea link_state(). */
static int state_changed(const ui_state_t *a, const ui_state_t *b)
{
    ui_state_t x = *a, y = *b;
    x.last_app_ms = y.last_app_ms = 0;
    x.esp_last_ms = y.esp_last_ms = 0;
    x.esp_uptime = y.esp_uptime = 0;
    return memcmp(&x, &y, sizeof x) != 0;
}

/* Apelat la fiecare trecere prin app_loop(); durează aproape nimic dacă nu e nimic de redesenat.
 * Ordinea: screensaver (dacă e activ, nimic altceva) → la pagină nouă, partea statică compusă într-o
 * zonă pe tot ecranul (apare dintr-o dată, cu partea dinamică inclusă) → expirarea mesajului →
 * antetul dacă s-a schimbat legătura → partea dinamică a paginii, doar la nevoie.
 * Screensaver-ul pornește după SCREENSAVER_MS (app_config.h) fără atingere/buton; 0 îl dezactivează. */
void display_update(const ui_state_t *st)
{
    uint32_t now_sec = HAL_GetTick() / 1000u;

#if SCREENSAVER_MS
    if (!saver && HAL_GetTick() - last_touch_ms >= SCREENSAVER_MS) {
        saver = 1;
        saver_draw(st);
    }
    if (saver) {
        if (state_changed(st, &shown) || HAL_GetTick() - saver_drawn_ms >= SAVER_MOVE_MS)
            saver_draw(st);
        shown = *st;
        return;
    }
#endif
    int force = page != drawn_page;
    /* own = 1 dacă am deschis noi zona de compunere pe tot ecranul (închisă la sfârșit, după dinamic) */
    int own = 0;

    if (force) {
        own = gfx_begin(0, 0, GFX_W, GFX_H);   /* pagina nouă apare dintr-o dată */
        gfx_fill(0, 0, GFX_W, GFX_H, C_BG);
        gfx_text(16, 12, &font_title, C_TEXT, "Smart Bottle");
        switch (page) {
        case PAGE_HOME:  home_static();  break;
        case PAGE_HIST:  hist_static();  break;
        case PAGE_CAL:   cal_static();   break;
        default:         info_static();  break;
        }
        draw_tabs();
        draw_header(st);
    }

    /* mesajul a expirat: revenim la pastila de stare (comparație cu semn, corectă la depășirea tick-ului) */
    if (notice.active && (int32_t)(HAL_GetTick() - notice.until) >= 0) {
        notice.active = 0;
        if (page == PAGE_CAL && !force) cal_dynamic(st);
    }

    int changed = state_changed(st, &shown);
    int tick = now_sec != shown_sec;

    if (!force && link_state(st) != shown_link) draw_header(st);

    switch (page) {
    case PAGE_HOME:
        if (force || changed) home_dynamic(st);
        break;
    case PAGE_HIST:
        if (force || changed) hist_dynamic(st);
        break;
    case PAGE_CAL:
        if (force || changed) cal_dynamic(st);
        break;
    default:
        if (force || changed || tick) info_dynamic(st);
        break;
    }
    if (force) {
        if (own) gfx_end();
        drawn_page = page;
    }
    shown = *st;
    shown_sec = now_sec;
}

/* Copiază mesajul (tăiat la 39 de caractere) și îl ține NOTICE_MS. Forțează redesenarea completă,
 * oricare ar fi pagina (mesajul se vede doar pe Calibrare). Apelat de app.c. */
void display_notice(const char *msg, int ok)
{
    snprintf(notice.text, sizeof notice.text, "%s", msg);
    notice.ok = ok;
    notice.until = HAL_GetTick() + NOTICE_MS;
    notice.active = 1;
    drawn_page = -1;                      /* redesenează pagina la următorul update */
}

/* Butonul USER (antirebond în app.c): trezește din screensaver sau trece la pagina următoare, ciclic. */
void display_next_page(void)
{
    last_touch_ms = HAL_GetTick();
    if (saver) {                          /* ca la atingere: prima apăsare doar trezește ecranul */
        saver = 0;
        drawn_page = -1;
        return;
    }
    page = (page + 1) % PAGE_COUNT;
}

/* Citește touch-ul (STMPE811 prin I2C, durează puțin) și reacționează doar la frontul de apăsare.
 * Prima atingere în screensaver doar trezește ecranul. Zone:
 *   y >= 270 (bara de taburi, cu 6 px toleranță în sus) → schimbă pagina (redesenată la update);
 *   Calibrare, y 234..269 (cardul de jos, toată lățimea) → UI_EV_TOGGLE_MOCK;
 *   Calibrare, y 172..227, buton activ → UI_EV_SET_EMPTY (x 12..115) / UI_EV_SET_FULL (x 124..227).
 * Întoarce evenimentul pentru app_loop(), care face efectiv acțiunea. */
ui_event_t display_poll_touch(const ui_state_t *st)
{
    if (!ts_ok) return UI_EV_NONE;
    TS_StateTypeDef ts;
    BSP_TS_GetState(&ts);
    int down = ts.TouchDetected != 0;
    int edge = down && !touch_was_down;
    touch_was_down = down;
    if (!edge) return UI_EV_NONE;

    last_touch_ms = HAL_GetTick();
    if (saver) {                          /* prima atingere doar trezește ecranul */
        saver = 0;
        drawn_page = -1;
        return UI_EV_NONE;
    }

    int x = ts.X, y = ts.Y;
#if LCD_ROTATE_180
    x = GFX_W - 1 - x;                    /* panoul tactil nu se rotește odată cu imaginea */
    y = GFX_H - 1 - y;
#endif

    if (y >= TAB_Y - 6) {
        /* tabul sub deget; în afara barei (x < 12 sau x > 227) se ia tabul de la margine */
        int p = (x - 12) / 54;
        if (p < 0) p = 0;
        if (p >= PAGE_COUNT) p = PAGE_COUNT - 1;
        page = p;
        return UI_EV_NONE;
    }
    if (page == PAGE_CAL && y >= 234 && y < 272) return UI_EV_TOGGLE_MOCK;
    if (page == PAGE_CAL && y >= BTN_Y && y < BTN_Y + BTN_H && cal_enabled(st)) {
        if (x >= 12 && x < 116)  return UI_EV_SET_EMPTY;
        if (x >= 124 && x < 228) return UI_EV_SET_FULL;
    }
    return UI_EV_NONE;
}
