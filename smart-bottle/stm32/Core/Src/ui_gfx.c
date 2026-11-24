/* ui_gfx.c — primitive de desen software pentru LCD-ul 240 x 320 (ARGB8888, 4 octeți/pixel).
 *
 * Folosit doar de display.c. Framebuffer-ul e la LCD_FRAME_BUFFER (0xD0000000, SDRAM externă prin
 * FMC); LTDC îl citește și îl afișează continuu, deci orice scriere apare pe ecran la următorul
 * cadru. Toate scrierile le face CPU-ul, pixel cu pixel (fără DMA2D, ca să rămână cod simplu și ușor
 * de urmărit; la 240 x 320 viteza e suficientă). Pointerii sunt `volatile` pentru că memoria e
 * citită în paralel de LTDC.
 *
 * Decizii:
 *  - antialiasing prin acoperire: fiecare pixel de margine primește alpha = cât din el e acoperit
 *    de formă, și se amestecă liniar cu ce e deja desenat (blend);
 *  - fără dublu buffer complet: gfx_begin/gfx_end compun o zonă într-un buffer „scratch” și o copiază
 *    la final, ca pe ecran să nu apară stările intermediare (ștergere + redesen = pâlpâire). */
#include "ui_gfx.h"
#include <stddef.h>

/* Ținta curentă: ecranul sau, între gfx_begin/gfx_end, o zonă din SDRAM unde compunem
 * o bucată de ecran ca s-o copiem dintr-o dată (fără pâlpâire).
 * screen = framebuffer-ul real; fb = unde scriem acum (screen sau scratch); scratch = zona de compunere.
 * tw, th = lățimea / înălțimea țintei curente (rândul are tw pixeli); ox, oy = colțul zonei pe ecran
 * (scăzute din coordonate ca apelanții să lucreze mereu în coordonate de ecran);
 * offscreen = 1 între gfx_begin și gfx_end. */
static volatile uint32_t *screen, *fb, *scratch;
static int tw = GFX_W, th = GFX_H, ox, oy, offscreen;

/* Ține minte adresele; apelat o dată din display_init(), după ce BSP a pornit SDRAM-ul și LTDC. */
void gfx_init(uint32_t framebuffer, uint32_t scratch_addr)
{
    screen = fb = (volatile uint32_t *)framebuffer;
    scratch = (volatile uint32_t *)scratch_addr;
}

/* Taie dreptunghiul la marginile ecranului (folosit doar de gfx_begin; w/h pot ieși <= 0). */
static void clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > GFX_W) *w = GFX_W - *x;
    if (*y + *h > GFX_H) *h = GFX_H - *y;
}

/* Pornește compunerea într-o zonă: copiază întâi ce e acum pe ecran în scratch (ca desenele parțiale
 * să se suprapună corect peste fundalul existent), apoi redirecționează desenul acolo.
 * Zona e stocată compact (rând de w pixeli). Cost: o citire + o scriere SDRAM pe pixel. */
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

/* Copiază zona compusă înapoi pe ecran și revine la desenul direct. Fără efect dacă nu e zonă activă. */
void gfx_end(void)
{
    if (!offscreen) return;
    for (int j = 0; j < th; j++)
        for (int i = 0; i < tw; i++) screen[(oy + j) * GFX_W + ox + i] = scratch[j * tw + i];
    fb = screen; ox = oy = 0; tw = GFX_W; th = GFX_H; offscreen = 0;
}

/* amestec liniar între culoarea de sub pixel și culoarea dată, a = 0..255.
 * Coordonatele sunt de ecran; le mutăm în coordonatele țintei și ignorăm ce iese (comparația unsigned
 * prinde și valorile negative). a >= 255 = scriere directă; altfel, pe fiecare canal:
 * rez = (c * a + d * (255 - a)) / 255, d = pixelul existent. Rezultatul e mereu opac (A = 0xFF). */
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

/* Pixel izolat cu transparență (folosit pentru marginile antialiasate ale apei din display.c). */
void gfx_pixel(int x, int y, uint32_t color, unsigned alpha)
{
    blend(x, y, color, alpha);
}

/* Dreptunghi plin, opac. Clipping-ul se face direct în coordonatele țintei (ecran sau zonă). */
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

/* Acoperirea unui pixel de un cerc de rază r centrat în (0,0), cu 4x4 eșantioane.
 * px, py = poziția pixelului față de centru (stânga-sus a pixelului, în pixeli întregi). Întoarce
 * 0..255 = fracțiunea din cele 16 eșantioane aflate în cerc. Totul în întregi (fără float). */
static unsigned coverage(int px, int py, int r)
{
    /* coordonate în 1/8 pixel: eșantionul sx e la px + (sx + 0.5) / 4 */
    unsigned n = 0;
    /* raza în aceleași unități de 1/8 px: (8r)^2; comparăm pătratele, fără radical */
    int lim = 64 * r * r;
    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
            int dx = 8 * px + 2 * sx + 1, dy = 8 * py + 2 * sy + 1;
            if (dx * dx + dy * dy <= lim) n++;
        }
    return n * 255 / 16;
}

/* Trei dreptunghiuri opace acoperă tot în afară de cele 4 colțuri r x r; colțurile se desenează pixel
 * cu pixel, cu acoperirea calculată pentru colțul stânga-sus și oglindită pentru celelalte trei. */
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
            /* (r-1-i, r-1-j) = distanța (în pixeli) a pixelului față de centrul colțului (x+r, y+r) */
            unsigned a = coverage(r - 1 - i, r - 1 - j, r);
            blend(x + i, y + j, color, a);
            blend(x + w - 1 - i, y + j, color, a);
            blend(x + i, y + h - 1 - j, color, a);
            blend(x + w - 1 - i, y + h - 1 - j, color, a);
        }
}

/* Umbra = același dreptunghi deplasat 1 px dreapta și 3 px jos, în C_SHADOW; raza 16 ca pe site. */
void gfx_card(int x, int y, int w, int h, uint32_t color)
{
    gfx_round_rect(x + 1, y + 3, w, h, 16, C_SHADOW);
    gfx_round_rect(x, y, w, h, 16, color);
}

/* Cerc plin = pătrat cu raza colțurilor egală cu jumătate din latură. */
void gfx_circle(int cx, int cy, int r, uint32_t color)
{
    gfx_round_rect(cx - r, cy - r, 2 * r, 2 * r, r, color);
}

/* ---- text ---- */

/* Decodează un caracter UTF-8 și avansează *ps. Acceptă secvențe de 2 octeți (U+0080..U+07FF:
 * ă â î ș ț și majusculele, „·” U+00B7) și de 3 octeți („–” U+2013); 4 octeți nu apar în texte.
 * Un octet invalid sau o secvență trunchiată se întoarce ca atare (un singur octet consumat) și cade
 * pe '?' la căutarea glifei; verificarea octeților de continuare oprește citirea la terminatorul 0. */
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

/* Căutare binară după cp în glifele sortate ale fontului; NULL dacă lipsește. */
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

/* Suma avansurilor; nu ține cont de kerning (fonturile nu au). */
int gfx_text_width(const ufont_t *f, const char *s)
{
    int w = 0;
    while (*s) {
        const ufont_glyph_t *g = glyph_or_fallback(f, utf8_next(&s));
        if (g) w += g->adv;
    }
    return w;
}

/* Desenează fiecare glifă la (x + g->x, y + g->y); valoarea de 4 biți (0..15) devine alpha prin
 * înmulțire cu 17 (15 * 17 = 255). Pixelii cu acoperire 0 sunt săriți (fundalul rămâne neatins). */
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
                /* 2 pixeli pe octet: pixelul par în nibble-ul de sus, cel impar în cel de jos */
                uint8_t v = bits[j * stride + i / 2];
                unsigned a = (i & 1) ? (v & 0x0F) : (v >> 4);
                if (a) blend(x + g->x + i, y + g->y + j, color, a * 17);
            }
        x += g->adv;
    }
    return x - x0;
}

/* Casetă de text care se redesenează fără pâlpâire: dacă nu suntem deja într-o zonă, o deschide pe
 * caseta textului, șterge cu bg, scrie, apoi copiază. Textul mai lat decât w nu e tăiat la w
 * (doar la marginea zonei / ecranului). */
void gfx_text_box(int x, int y, int w, const ufont_t *f, uint32_t color, uint32_t bg, int align, const char *s)
{
    int own = gfx_begin(x, y, w, f->height);
    gfx_fill(x, y, w, f->height, bg);
    int sw = gfx_text_width(f, s);
    int tx = align == 1 ? x + (w - sw) / 2 : align == 2 ? x + w - sw : x;
    gfx_text(tx, y, f, color, s);
    if (own) gfx_end();
}
