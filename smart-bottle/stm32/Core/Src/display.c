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

enum { PAGE_HOME, PAGE_TODAY, PAGE_CAL, PAGE_INFO, PAGE_COUNT };
static const char *const TAB_NAMES[PAGE_COUNT] = { "Acasă", "Azi", "Calib.", "Info" };

#define TAB_Y      276
#define TAB_H      38
#define BTN_Y      172
#define BTN_H      56
#define NOTICE_MS  4000
#define MOCK_SW_X  176                    /* comutatorul „Senzor simulat” */
#define SCRATCH    (LCD_FRAME_BUFFER + 0x100000)   /* zonă liberă în SDRAM, după framebuffer */

/* apa: gradient ca sticla de pe site, de sus (deschis) în jos (închis) */
#define WATER_TOP     0xFF7DB8EBu
#define WATER_BOTTOM  0xFF2C6FB8u
#define WATER_SURFACE 0xFFB9DCF8u

static int page = PAGE_HOME, drawn_page = -1;
static ui_state_t shown;
static uint32_t shown_sec;
static int shown_link = -1;               /* starea afișată în pastila din antet */
static int bottle_pct = -1;               /* nivelul desenat în sticlă; -1 = de redesenat */
static int ts_ok, touch_was_down;
static struct { char text[40]; int ok; uint32_t until; int active; } notice;
static uint32_t last_touch_ms;
static int saver;                         /* screensaver activ */
static uint32_t saver_drawn_ms;

static int esp_alive(const ui_state_t *st)
{
    return st->esp_last_ms && HAL_GetTick() - st->esp_last_ms < ESP_TIMEOUT_MS;
}

/* 0 = fără ESP32, 1 = fără client, 2 = client BLE dar aplicația nu răspunde, 3 = aplicație activă */
static int link_state(const ui_state_t *st)
{
    if (!esp_alive(st)) return 0;
    return 1 + st->app;
}

static int shown_pct(const ui_state_t *st)
{
    int pct = st->has_ml && st->cap > 0 ? st->ml * 100 / st->cap : st->pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

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

static void pill(int x, int y, const char *text, uint32_t fg, uint32_t bg)
{
    int w = gfx_text_width(&font_small, text) + 20;
    gfx_round_rect(x, y, w, 22, 11, bg);
    gfx_text(x + 10, y + 2, &font_small, fg, text);
}

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

static void draw_tabs(void)
{
    gfx_card(8, TAB_Y, 224, TAB_H, C_CARD);
    for (int i = 0; i < PAGE_COUNT; i++) {
        int x = 12 + i * 54;
        if (i == page) gfx_round_rect(x + 2, TAB_Y + 5, 50, 28, 14, C_ACCENT_BG);
        int tw = gfx_text_width(&font_small, TAB_NAMES[i]);
        gfx_text(x + 27 - tw / 2, TAB_Y + 10, &font_small, i == page ? C_ACCENT : C_MUTED, TAB_NAMES[i]);
    }
}

/* starea citirii, ca pe site: „stabil” / „se mișcă” / eroare */
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

#define BOT_X  36
#define BOT_Y  84
#define BOT_W  58
#define BOT_H  160

static void home_static(void)
{
    gfx_card(12, 52, 216, 212, C_CARD);
    gfx_round_rect(BOT_X + 13, BOT_Y - 14, 32, 18, 6, C_OUTLINE);   /* gât */
    gfx_round_rect(BOT_X + 16, BOT_Y - 11, 26, 16, 4, C_CARD);
    bottle_pct = -1;
}

/* Apa: suprafață dreaptă, colțuri de jos rotunjite ca interiorul sticlei, gradient pe verticală. */
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

static void home_bottle(const ui_state_t *st)
{
    int pct = st->sensor_ok ? shown_pct(st) : 0;
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

/* ---------------- Azi ---------------- */

static int since_now(const ui_state_t *st)
{
    if (st->since_min < 0) return st->since_min;
    return st->since_min + (int)((HAL_GetTick() - st->since_at_ms) / 60000u);
}

static void today_static(void)
{
    gfx_card(12, 52, 216, 108, C_CARD);
    gfx_card(12, 168, 216, 96, C_CARD);
}

static void today_line(const ui_state_t *st)
{
    char buf[64], ago[32];
    int m = since_now(st);
    if (m == -2)     snprintf(ago, sizeof ago, "–");
    else if (m < 0)  snprintf(ago, sizeof ago, "niciuna azi");
    else if (m < 1)  snprintf(ago, sizeof ago, "acum");
    else if (m < 60) snprintf(ago, sizeof ago, "acum %d min", m);
    else             snprintf(ago, sizeof ago, "acum %d h %d min", m / 60, m % 60);
    snprintf(buf, sizeof buf, "%d înghițituri · %s", st->sips, ago);
    gfx_text_box(26, 176, 194, &font_small, C_MUTED, C_CARD, 0, buf);
}

static void today_dynamic(const ui_state_t *st)
{
    char buf[24];
    int own;

    /* cardul de sus */
    own = gfx_begin(20, 58, 200, 96);
    gfx_fill(20, 58, 200, 96, C_CARD);
    if (!st->has_today) {
        gfx_text_box(20, 92, 200, &font_body, C_MUTED, C_CARD, 1, "Fără date de la aplicație");
    } else {
        gfx_text(28, 62, &font_small, C_MUTED, "Azi");
        snprintf(buf, sizeof buf, "%d", st->total);
        int w = gfx_text(24, 76, &font_num, C_TEXT, buf);
        snprintf(buf, sizeof buf, "din %d mL", st->goal);
        gfx_text(24 + w + 6, 98, &font_body, C_MUTED, buf);
        gfx_round_rect(28, 134, 184, 10, 5, C_TRACK);
        int p = st->goal > 0 ? st->total * 184 / st->goal : 0;
        if (p > 184) p = 184;
        if (p > 0) gfx_round_rect(28, 134, p < 10 ? 10 : p, 10, 5, st->total >= st->goal ? C_ACCENT : C_OK);
    }
    if (own) gfx_end();

    /* cardul de jos: rândul cu înghițiturile + ultimele 7 zile */
    own = gfx_begin(20, 174, 200, 88);
    gfx_fill(20, 174, 200, 88, C_CARD);
    if (!st->has_today) {
        gfx_text_box(20, 206, 200, &font_small, C_MUTED, C_CARD, 1, "Conectează telefonul");
    } else {
        today_line(st);
        const int top = 198, bottom = 240, bw = 16, gap = 12, x0 = 28;
        if (!st->has_week) {
            gfx_text_box(20, 210, 200, &font_small, C_MUTED, C_CARD, 1, "Istoricul vine de la aplicație");
        } else {
            int scale = st->goal;
            for (int i = 0; i < 7; i++) if (st->week[i] > scale) scale = st->week[i];
            if (scale <= 0) scale = 1;
            for (int i = 0; i < 7; i++) {
                int h = st->week[i] * (bottom - top) / scale;
                int x = x0 + i * (bw + gap);
                gfx_round_rect(x, top, bw, bottom - top, 4, C_TRACK);
                if (h > 0) gfx_round_rect(x, bottom - h, bw, h, h < 8 ? h / 2 : 4,
                                          st->week[i] >= st->goal ? C_ACCENT : C_OK);
            }
            int gy = bottom - st->goal * (bottom - top) / scale;
            for (int x = x0 - 4; x < x0 + 7 * (bw + gap) - gap + 4; x += 6) gfx_fill(x, gy, 3, 1, C_TEXT);
            gfx_text(x0, 243, &font_small, C_MUTED, "7 zile");
            int tw = gfx_text_width(&font_small, "azi");
            gfx_text(x0 + 6 * (bw + gap) + bw - tw, 243, &font_small, C_MUTED, "azi");
        }
    }
    if (own) gfx_end();
}

/* ---------------- Calibrare ---------------- */

static int cal_enabled(const ui_state_t *st) { return st->sensor_ok && st->stable; }

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

static void cal_static(void)
{
    gfx_card(12, 52, 216, 112, C_CARD);
    gfx_card(12, 234, 216, 36, C_CARD);
}

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

#define INFO_ROWS 9
#define INFO_Y    60
#define INFO_STEP 22

static void info_static(void)
{
    gfx_card(12, 52, 216, 212, C_CARD);
    static const char *const labels[INFO_ROWS] = {
        "Firmware", "Funcționare", "Senzor", "Legătură", "ESP32", "Aplicație", "Date aplicație", "Calibrare", "Obiectiv"
    };
    for (int i = 0; i < INFO_ROWS; i++) gfx_text(26, INFO_Y + i * INFO_STEP, &font_small, C_MUTED, labels[i]);
}

static void ago_text(char *out, size_t cap, uint32_t since_ms)
{
    uint32_t a = (HAL_GetTick() - since_ms) / 1000u;
    if (a < 120) snprintf(out, cap, "acum %lu s", (unsigned long)a);
    else         snprintf(out, cap, "acum %lu min", (unsigned long)(a / 60));
}

static void info_dynamic(const ui_state_t *st)
{
    char v[INFO_ROWS][32];
    uint32_t col[INFO_ROWS];
    for (int i = 0; i < INFO_ROWS; i++) col[i] = C_TEXT;

    uint32_t s = HAL_GetTick() / 1000u;
    snprintf(v[0], sizeof v[0], "%s", FW_VERSION);
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

#define SAVER_BG      0xFF0F1620u
#define SAVER_MOVE_MS 10000u

/* doar nivelul, pe fundal închis; poziția se mută la 10 s */
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
    BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_SelectLayer(LCD_BACKGROUND_LAYER);
    BSP_LCD_DisplayOn();
    gfx_init(LCD_FRAME_BUFFER, SCRATCH);
    gfx_fill(0, 0, GFX_W, GFX_H, C_BG);

    last_touch_ms = HAL_GetTick();
    ts_ok = BSP_TS_Init(GFX_W, GFX_H) == TS_OK;
    drawn_page = -1;
}

/* câmpurile care se schimbă la fiecare mesaj fără să schimbe ce se vede pe pagini fără ceas */
static int state_changed(const ui_state_t *a, const ui_state_t *b)
{
    ui_state_t x = *a, y = *b;
    x.last_app_ms = y.last_app_ms = 0;
    x.esp_last_ms = y.esp_last_ms = 0;
    x.esp_uptime = y.esp_uptime = 0;
    return memcmp(&x, &y, sizeof x) != 0;
}

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
    int own = 0;

    if (force) {
        own = gfx_begin(0, 0, GFX_W, GFX_H);   /* pagina nouă apare dintr-o dată */
        gfx_fill(0, 0, GFX_W, GFX_H, C_BG);
        gfx_text(16, 12, &font_title, C_TEXT, "Smart Bottle");
        switch (page) {
        case PAGE_HOME:  home_static();  break;
        case PAGE_TODAY: today_static(); break;
        case PAGE_CAL:   cal_static();   break;
        default:         info_static();  break;
        }
        draw_tabs();
        draw_header(st);
    }

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
    case PAGE_TODAY:
        if (force || changed) today_dynamic(st);
        else if (tick && st->has_today && now_sec % 30 == 0) today_line(st);
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

void display_notice(const char *msg, int ok)
{
    snprintf(notice.text, sizeof notice.text, "%s", msg);
    notice.ok = ok;
    notice.until = HAL_GetTick() + NOTICE_MS;
    notice.active = 1;
    drawn_page = -1;                      /* redesenează pagina la următorul update */
}

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
