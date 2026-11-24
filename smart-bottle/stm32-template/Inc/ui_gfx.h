#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdint.h>

/* Desen direct în framebuffer-ul ARGB8888 al LCD-ului (240 x 320), cu text antialiasat
 * (fonturi 4 biți/pixel generate de tools/gen_fonts.py) și colțuri rotunjite.
 *
 * Rol în sistem: biblioteca minimă de desen folosită doar de display.c. LTDC-ul (pornit de BSP în
 * display_init) citește singur, continuu, framebuffer-ul din SDRAM și îl trimite la panou; noi doar
 * scriem pixeli în memorie, cu CPU-ul (fără DMA2D). Nu există dublu buffer pe tot ecranul: pentru
 * zonele care se schimbă des există gfx_begin/gfx_end (compunere într-o zonă separată din SDRAM).
 *
 * Sistem de coordonate: pixeli, originea (0, 0) stânga-sus, x spre dreapta (0..GFX_W-1), y în jos
 * (0..GFX_H-1), mereu în coordonatele ecranului — și între gfx_begin/gfx_end. Tot ce iese din
 * ecran (sau din zona activă) e tăiat; nu e nevoie de verificări la apelant.
 * Culorile sunt 0xAARRGGBB; canalul A al culorii e ignorat la amestec (se scrie mereu opac),
 * transparența se dă separat prin `alpha`. Nu e reentrant: doar din bucla principală. */

/* O glifă din font: bitmap 4 biți/pixel (2 pixeli pe octet, nibble-ul de sus = pixelul din stânga,
 * fiecare rând începe la octet întreg), valoarea 0..15 = acoperirea pixelului. */
typedef struct {
    uint16_t cp;       /* cod Unicode */
    uint16_t off;      /* poziția în bits[] */
    uint8_t  w, h;
    int8_t   x, y;     /* deplasare față de originea glifei (stânga-sus a rândului) */
    uint8_t  adv;      /* avans orizontal */
} ufont_glyph_t;

/* Un font: glifele sortate crescător după cp (căutare binară), toate bitmap-urile într-un tablou. */
typedef struct {
    uint8_t height, ascent;    /* înălțimea rândului și distanța de la sus la linia de bază, în px */
    uint16_t count;            /* numărul de glife din glyphs[] */
    const ufont_glyph_t *glyphs;
    const uint8_t *bits;
} ufont_t;

/* Fonturile generate în fonts_ui.c (nu se editează de mână; se regenerează cu stm32/tools/gen_fonts.py):
 * small/body/title = Roboto 13/16/18 px (ASCII + diacritice românești), mono = JetBrains Mono 14 px
 * (doar ASCII: diacriticele cad pe varianta fără diacritice), num = JetBrains Mono 36 px (doar cifre
 * și câteva semne, pentru valoarea mare). */
extern const ufont_t font_small, font_body, font_title, font_mono, font_num;

/* Paleta site-ului (style.css), întunecată pentru contrast: panoul TFT spală nuanțele pale. */
#define C_BG        0xFFDCE3EBu   /* fundalul ecranului */
#define C_CARD      0xFFFFFFFFu   /* cardurile albe */
#define C_SHADOW    0xFFB4C0CEu   /* umbra de sub carduri */
#define C_TEXT      0xFF0E151Eu
#define C_MUTED     0xFF3E4A59u   /* etichete, text secundar */
#define C_ACCENT    0xFF245A8Fu   /* stare bună / selecție (text), pe fundal C_ACCENT_BG */
#define C_ACCENT_BG 0xFFB3D3F0u
#define C_PINK      0xFF8E2F55u   /* eroare / lipsă, pe fundal C_PINK_BG */
#define C_PINK_BG   0xFFF2C9D9u
#define C_GREY      0xFFD3D9E0u   /* inactiv (butoane dezactivate, comutator oprit) */
#define C_OK        0xFF3F86CFu   /* lichid >= 50% */
#define C_MID       0xFFD99A2Bu   /* lichid >= 20% */
#define C_LOW       0xFFD0557Fu   /* lichid < 20% */
#define C_OUTLINE   0xFF8796A8u   /* conturul sticlei */
#define C_TRACK     0xFFE6EBF1u   /* fundalul barelor */
#define C_WARN      0xFF7A5414u   /* avertisment („se mișcă”, „nu răspunde”), pe fundal C_WARN_BG */
#define C_WARN_BG   0xFFF5DFAFu

/* Dimensiunea ecranului în pixeli (portret). */
#define GFX_W 240
#define GFX_H 320

/* framebuffer = adresa stratului LTDC (GFX_W*GFX_H*4 octeți); scratch = zonă liberă de aceeași
 * mărime maximă, folosită de gfx_begin. Nu desenează nimic. */
void gfx_init(uint32_t framebuffer, uint32_t scratch);
/* Desenele dintre gfx_begin și gfx_end se fac într-o zonă din SDRAM și apar pe ecran dintr-o dată.
 * gfx_begin întoarce 1 dacă a pornit zona (atunci gfx_end trebuie apelat).
 * Întoarce 0 dacă o zonă e deja activă (apelurile se pot imbrica: desenăm în zona exterioară, iar
 * desenele în afara ei se pierd!) sau dacă dreptunghiul e în afara ecranului. Tipar:
 *     int own = gfx_begin(x, y, w, h);  ...desen...  if (own) gfx_end(); */
int  gfx_begin(int x, int y, int w, int h);
void gfx_end(void);
void gfx_pixel(int x, int y, uint32_t color, unsigned alpha);   /* alpha 0..255 */
/* Dreptunghi plin, opac (fără amestec). */
void gfx_fill(int x, int y, int w, int h, uint32_t color);
/* Dreptunghi plin cu colțuri de rază r, margini antialiasate; r e limitat la jumătate din w și h. */
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t color);
/* Card cu umbră discretă, ca pe site. */
void gfx_card(int x, int y, int w, int h, uint32_t color);
/* Disc plin de rază r centrat în (cx, cy). */
void gfx_circle(int cx, int cy, int r, uint32_t color);

/* Text UTF-8; y = marginea de sus a rândului. Întoarce lățimea desenată.
 * Fără fundal (pixelii se amestecă peste ce e dedesubt) și fără trecere la rând nou. */
int gfx_text(int x, int y, const ufont_t *f, uint32_t color, const char *s);
/* Lățimea în pixeli a textului (suma avansurilor), fără să deseneze. */
int gfx_text_width(const ufont_t *f, const char *s);
/* Șterge caseta (x, y, w, f->height) cu bg, apoi scrie aliniat: 0 stânga, 1 centru, 2 dreapta. */
void gfx_text_box(int x, int y, int w, const ufont_t *f, uint32_t color, uint32_t bg, int align, const char *s);

#endif /* UI_GFX_H */
