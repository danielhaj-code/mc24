#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdint.h>

/* Desen direct în framebuffer-ul ARGB8888 al LCD-ului (240 x 320), cu text antialiasat
 * (fonturi 4 biți/pixel generate de tools/gen_fonts.py) și colțuri rotunjite. */

typedef struct {
    uint16_t cp;       /* cod Unicode */
    uint16_t off;      /* poziția în bits[] */
    uint8_t  w, h;
    int8_t   x, y;     /* deplasare față de originea glifei (stânga-sus a rândului) */
    uint8_t  adv;      /* avans orizontal */
} ufont_glyph_t;

typedef struct {
    uint8_t height, ascent;
    uint16_t count;
    const ufont_glyph_t *glyphs;
    const uint8_t *bits;
} ufont_t;

extern const ufont_t font_small, font_body, font_title, font_mono, font_num;

/* Paleta site-ului (style.css), întunecată pentru contrast: panoul TFT spală nuanțele pale. */
#define C_BG        0xFFDCE3EBu
#define C_CARD      0xFFFFFFFFu
#define C_SHADOW    0xFFB4C0CEu
#define C_TEXT      0xFF0E151Eu
#define C_MUTED     0xFF3E4A59u
#define C_ACCENT    0xFF245A8Fu
#define C_ACCENT_BG 0xFFB3D3F0u
#define C_PINK      0xFF8E2F55u
#define C_PINK_BG   0xFFF2C9D9u
#define C_GREY      0xFFD3D9E0u
#define C_OK        0xFF3F86CFu   /* lichid >= 50% */
#define C_MID       0xFFD99A2Bu   /* lichid >= 20% */
#define C_LOW       0xFFD0557Fu   /* lichid < 20% */
#define C_OUTLINE   0xFF8796A8u   /* conturul sticlei */
#define C_TRACK     0xFFE6EBF1u   /* fundalul barelor */
#define C_WARN      0xFF7A5414u
#define C_WARN_BG   0xFFF5DFAFu

#define GFX_W 240
#define GFX_H 320

void gfx_init(uint32_t framebuffer, uint32_t scratch);
/* Desenele dintre gfx_begin și gfx_end se fac într-o zonă din SDRAM și apar pe ecran dintr-o dată.
 * gfx_begin întoarce 1 dacă a pornit zona (atunci gfx_end trebuie apelat). */
int  gfx_begin(int x, int y, int w, int h);
void gfx_end(void);
void gfx_pixel(int x, int y, uint32_t color, unsigned alpha);   /* alpha 0..255 */
void gfx_fill(int x, int y, int w, int h, uint32_t color);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t color);
/* Card cu umbră discretă, ca pe site. */
void gfx_card(int x, int y, int w, int h, uint32_t color);
void gfx_circle(int cx, int cy, int r, uint32_t color);

/* Text UTF-8; y = marginea de sus a rândului. Întoarce lățimea desenată. */
int gfx_text(int x, int y, const ufont_t *f, uint32_t color, const char *s);
int gfx_text_width(const ufont_t *f, const char *s);
/* Șterge caseta (x, y, w, f->height) cu bg, apoi scrie aliniat: 0 stânga, 1 centru, 2 dreapta. */
void gfx_text_box(int x, int y, int w, const ufont_t *f, uint32_t color, uint32_t bg, int align, const char *s);

#endif /* UI_GFX_H */
