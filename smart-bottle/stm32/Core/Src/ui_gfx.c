#include "ui_gfx.h"
#include <stddef.h>

/* Ținta curentă: ecranul sau, între gfx_begin/gfx_end, o zonă din SDRAM unde compunem
 * o bucată de ecran ca s-o copiem dintr-o dată (fără pâlpâire). */
static volatile uint32_t *screen, *fb, *scratch;
static int tw = GFX_W, th = GFX_H, ox, oy, offscreen;

void gfx_init(uint32_t framebuffer, uint32_t scratch_addr)
{
    screen = fb = (volatile uint32_t *)framebuffer;
    scratch = (volatile uint32_t *)scratch_addr;
}

static void clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > GFX_W) *w = GFX_W - *x;
    if (*y + *h > GFX_H) *h = GFX_H - *y;
}

int gfx_begin(int x, int y, int w, int h)
{
    if (offscreen) return 0;              /* deja într-o zonă: desenăm direct în ea */
    clip(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return 0;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) scratch[j * w + i] = screen[(y + j) * GFX_W + x + i];
    fb = scratch; ox = x; oy = y; tw = w; th = h; offscreen = 1;
    return 1;
}

void gfx_end(void)
{
    if (!offscreen) return;
    for (int j = 0; j < th; j++)
        for (int i = 0; i < tw; i++) screen[(oy + j) * GFX_W + ox + i] = scratch[j * tw + i];
    fb = screen; ox = oy = 0; tw = GFX_W; th = GFX_H; offscreen = 0;
}

/* amestec liniar între culoarea de sub pixel și culoarea dată, a = 0..255 */
static void blend(int x, int y, uint32_t c, unsigned a)
{
    x -= ox; y -= oy;
    if ((unsigned)x >= (unsigned)tw || (unsigned)y >= (unsigned)th || a == 0) return;
    volatile uint32_t *p = &fb[y * tw + x];
    if (a >= 255) { *p = c; return; }
    uint32_t d = *p, na = 255 - a;
    uint32_t r = (((c >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * na) / 255;
    uint32_t g = (((c >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * na) / 255;
    uint32_t b = ((c & 0xFF) * a + (d & 0xFF) * na) / 255;
    *p = 0xFF000000u | (r << 16) | (g << 8) | b;
}

void gfx_pixel(int x, int y, uint32_t color, unsigned alpha)
{
    blend(x, y, color, alpha);
}

void gfx_fill(int x, int y, int w, int h, uint32_t color)
{
    x -= ox; y -= oy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > tw) w = tw - x;
    if (y + h > th) h = th - y;
    for (int j = 0; j < h; j++) {
        volatile uint32_t *p = &fb[(y + j) * tw + x];
        for (int i = 0; i < w; i++) p[i] = color;
    }
}

/* Acoperirea unui pixel de un cerc de rază r centrat în (0,0), cu 4x4 eșantioane. */
static unsigned coverage(int px, int py, int r)
{
    /* coordonate în 1/8 pixel: eșantionul sx e la px + (sx + 0.5) / 4 */
    unsigned n = 0;
    int lim = 64 * r * r;
    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
            int dx = 8 * px + 2 * sx + 1, dy = 8 * py + 2 * sy + 1;
            if (dx * dx + dy * dy <= lim) n++;
        }
    return n * 255 / 16;
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t color)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    gfx_fill(x + r, y, w - 2 * r, h, color);
    gfx_fill(x, y + r, r, h - 2 * r, color);
    gfx_fill(x + w - r, y + r, r, h - 2 * r, color);
    /* colțuri: pixelul (i, j) din pătratul r x r, distanța față de centrul colțului */
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            unsigned a = coverage(r - 1 - i, r - 1 - j, r);
            blend(x + i, y + j, color, a);
            blend(x + w - 1 - i, y + j, color, a);
            blend(x + i, y + h - 1 - j, color, a);
            blend(x + w - 1 - i, y + h - 1 - j, color, a);
        }
}

void gfx_card(int x, int y, int w, int h, uint32_t color)
{
    gfx_round_rect(x + 1, y + 3, w, h, 16, C_SHADOW);
    gfx_round_rect(x, y, w, h, 16, color);
}

void gfx_circle(int cx, int cy, int r, uint32_t color)
{
    gfx_round_rect(cx - r, cy - r, 2 * r, 2 * r, r, color);
}

/* ---- text ---- */

static uint32_t utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t c = *s++;
    if (c >= 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        s += 2;
    } else if (c >= 0xC0 && (s[0] & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
        s += 1;
    }
    *ps = (const char *)s;
    return c;
}

static const ufont_glyph_t *glyph(const ufont_t *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* caracter lipsă: încearcă varianta fără diacritice, apoi '?' */
static const ufont_glyph_t *glyph_or_fallback(const ufont_t *f, uint32_t cp)
{
    const ufont_glyph_t *g = glyph(f, cp);
    if (g) return g;
    switch (cp) {
    case 0x103: case 0xE2: cp = 'a'; break;
    case 0x102: case 0xC2: cp = 'A'; break;
    case 0xEE: cp = 'i'; break;
    case 0xCE: cp = 'I'; break;
    case 0x219: cp = 's'; break;
    case 0x218: cp = 'S'; break;
    case 0x21B: cp = 't'; break;
    case 0x21A: cp = 'T'; break;
    default: cp = '?'; break;
    }
    return glyph(f, cp);
}

int gfx_text_width(const ufont_t *f, const char *s)
{
    int w = 0;
    while (*s) {
        const ufont_glyph_t *g = glyph_or_fallback(f, utf8_next(&s));
        if (g) w += g->adv;
    }
    return w;
}

int gfx_text(int x, int y, const ufont_t *f, uint32_t color, const char *s)
{
    int x0 = x;
    while (*s) {
        const ufont_glyph_t *g = glyph_or_fallback(f, utf8_next(&s));
        if (!g) continue;
        const uint8_t *bits = f->bits + g->off;
        int stride = (g->w + 1) / 2;
        for (int j = 0; j < g->h; j++)
            for (int i = 0; i < g->w; i++) {
                uint8_t v = bits[j * stride + i / 2];
                unsigned a = (i & 1) ? (v & 0x0F) : (v >> 4);
                if (a) blend(x + g->x + i, y + g->y + j, color, a * 17);
            }
        x += g->adv;
    }
    return x - x0;
}

void gfx_text_box(int x, int y, int w, const ufont_t *f, uint32_t color, uint32_t bg, int align, const char *s)
{
    int own = gfx_begin(x, y, w, f->height);
    gfx_fill(x, y, w, f->height, bg);
    int sw = gfx_text_width(f, s);
    int tx = align == 1 ? x + (w - sw) / 2 : align == 2 ? x + w - sw : x;
    gfx_text(tx, y, f, color, s);
    if (own) gfx_end();
}
