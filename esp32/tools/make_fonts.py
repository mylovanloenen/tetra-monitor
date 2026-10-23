"""Zet de merklettertypen (../fonts/*.ttf) om naar VLW-fonts voor LovyanGFX
(anti-aliased) en schrijft ze als C-arrays naar src/fonts/brand_fonts.h.

    python3 tools/make_fonts.py        (nodig: pip install pillow)
"""
import os
import struct
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
TTF = os.path.join(HERE, "..", "..", "fonts")
OUT = os.path.join(HERE, "..", "src", "fonts", "brand_fonts.h")
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

# Scherpte: de randen van kleine letters zijn half-transparante pixels; op het
# 3,5"-scherm (165 ppi) ogen die wazig. Met een gamma < 1 worden die randpixels
# dekkender: lettervorm blijft gelijk, maar de tekst oogt strakker.
MONO_MAX_PX = 0           # t/m deze grootte: zonder anti-aliasing, op het pixelraster
                          # (FreeType-hinting voor zwart-wit) → haarscherp
EDGE_GAMMA_SMALL = 0.55   # (alleen als MONO_MAX_PX = 0)
EDGE_GAMMA_LARGE = 0.80   # grote koppen op het opstartscherm

# naam, bestand, pixelgrootte, tekens, letterafstand (zoals op de Pi)
FONTS = [
    ("FONT_BUTTON",   "Barlow-Bold.ttf",            17, ASCII, 1.0),
    ("FONT_STATUS",   "Barlow-SemiBold.ttf",        14, ASCII, 1.0),
    ("FONT_WORDMARK", "Archivo-Brand.ttf",          20, " priosense", 0.98),
    ("FONT_HERO",     "Archivo-Brand.ttf",          46, " Hear whatyoucn'", 0.98),
    ("FONT_ACCENT",   "InstrumentSerif-Italic.ttf", 56, " yet.", 1.0),
]


def vlw(path, px, chars, spacing):
    font = ImageFont.truetype(path, px)
    ascent, descent = font.getmetrics()
    metrics, bitmaps = [], []
    for ch in sorted(set(chars)):
        adv = round(font.getlength(ch) * spacing)
        l, t, r, b = font.getbbox(ch, anchor="ls")
        w, h = max(0, r - l), max(0, b - t)
        if ch == " " or w == 0 or h == 0:
            w = h = 0
            l = t = 0
        img = Image.new("L", (max(w, 1), max(h, 1)), 0)
        if w and h:
            d = ImageDraw.Draw(img)
            if px <= MONO_MAX_PX:
                d.fontmode = "1"      # zwart-wit met hinting: strakke, pixel-uitgelijnde lijnen
            d.text((-l, -t), ch, font=font, fill=255, anchor="ls")
        metrics.append(struct.pack(">7i", ord(ch), h, w, adv, -t, l, 0))
        if w and h:
            g = EDGE_GAMMA_SMALL if px <= 20 else EDGE_GAMMA_LARGE
            lut = [round(255 * (v / 255) ** g) for v in range(256)]
            img = img.point(lut)
        bitmaps.append(img.tobytes() if w and h else b"")
    head = struct.pack(">6i", len(metrics), 11, px, 0, ascent, descent)
    return head + b"".join(metrics) + b"".join(bitmaps)


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("// Gegenereerd door tools/make_fonts.py — niet met de hand aanpassen.\n")
        f.write("// Lettertypen: Barlow, Archivo, Instrument Serif (SIL Open Font License, zie ../fonts/).\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        for name, file, px, chars, spacing in FONTS:
            data = vlw(os.path.join(TTF, file), px, chars, spacing)
            f.write(f"// {file} {px}px\nstatic const uint8_t {name}[{len(data)}] = {{\n")
            for i in range(0, len(data), 24):
                f.write("  " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
            f.write("};\n\n")
            print(f"{name:14s} {file:28s} {px:3d}px  {len(data):6d} bytes")


main()
