"""Generează fonturile antialiasate pentru LCD (4 biți/pixel) din aceleași fonturi ca site-ul:
Roboto pentru text, JetBrains Mono pentru cifre. Ieșire: Core/Src/fonts_ui.c

    pip install pillow
    python tools/gen_fonts.py Roboto.ttf JetBrainsMono.ttf

TTF-urile variabile vin din github.com/google/fonts (ofl/roboto, ofl/jetbrainsmono), licență OFL.
Fișierul generat e în git, deci scriptul trebuie rulat doar când schimbi fonturile sau mărimile.
"""
import os
import sys

from PIL import ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'Core', 'Src', 'fonts_ui.c')

ASCII = ''.join(chr(c) for c in range(0x20, 0x7F))
RO = 'ăâîșțĂÂÎȘȚ·°–'
NUM = ' 0123456789%,.-:/mL'

# nume C, fișier (0 = Roboto, 1 = JetBrains Mono), px, greutate, caractere
FONTS = [
    ('font_small', 0, 13, 500, ASCII + RO),
    ('font_body',  0, 16, 600, ASCII + RO),
    ('font_title', 0, 18, 600, ASCII + RO),
    ('font_mono',  1, 14, 600, ASCII),
    ('font_num',   1, 36, 600, NUM),
]


def load(path, px, weight):
    f = ImageFont.truetype(path, px)
    axes = f.get_variation_axes()
    vals = []
    for a in axes:
        name = a['name'] if isinstance(a['name'], str) else a['name'].decode()
        if name.lower().startswith('weight'):
            vals.append(weight)
        else:
            vals.append(a['default'])
    if axes:
        f.set_variation_by_axes(vals)
    return f


def gen(name, f, chars):
    ascent, descent = f.getmetrics()
    chars = sorted(set(chars), key=ord)
    bits, glyphs = [], []
    for ch in chars:
        adv = int(round(f.getlength(ch)))
        l, t, r, b = f.getbbox(ch, anchor='la')
        w, h = max(0, r - l), max(0, b - t)
        off = len(bits)
        if w and h:
            mask = f.getmask(ch, mode='L')
            mw, mh = mask.size
            w, h = min(w, mw), min(h, mh)
            for y in range(h):
                row = [mask.getpixel((x, y)) >> 4 for x in range(w)]
                if len(row) % 2:
                    row.append(0)
                for i in range(0, len(row), 2):
                    bits.append((row[i] << 4) | row[i + 1])
        glyphs.append((ord(ch), off, w, h, l, t, adv))
    out = [f'/* {name}: {len(chars)} caractere, {len(bits)} octeți */']
    out.append(f'static const uint8_t {name}_bits[] = {{')
    for i in range(0, len(bits), 24):
        out.append('    ' + ','.join(f'0x{v:02X}' for v in bits[i:i + 24]) + ',')
    out.append('};')
    out.append(f'static const ufont_glyph_t {name}_glyphs[] = {{')
    for cp, off, w, h, l, t, adv in glyphs:
        out.append(f'    {{0x{cp:04X},{off},{w},{h},{l},{t},{adv}}},')
    out.append('};')
    out.append(f'const ufont_t {name} = {{ {ascent + descent}, {ascent}, {len(glyphs)}, '
               f'{name}_glyphs, {name}_bits }};')
    return '\n'.join(out), len(bits)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')   # consola Windows
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    paths = sys.argv[1:3]
    parts = ['/* GENERAT de tools/gen_fonts.py — nu edita manual.',
             ' * Roboto și JetBrains Mono, SIL Open Font License 1.1. */',
             '#include "ui_gfx.h"', '']
    total = 0
    for name, which, px, weight, chars in FONTS:
        src, n = gen(name, load(paths[which], px, weight), chars)
        parts.append(src)
        parts.append('')
        total += n
    with open(OUT, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(parts))
    print(f'{OUT}: {total} octeți de bitmap')


if __name__ == '__main__':
    main()
