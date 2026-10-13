#!/usr/bin/env python3
"""Maakt het boot-logo voor het schermpje: hetzelfde beeld als de opstart-splash
van de app (priosense-logo op de achtergrond van het hoofdscherm), omgezet naar
het ruwe pixelformaat van de framebuffer. De service priosense-bootlogo zet dat
bestand bij het opstarten met `cat` op /dev/fb0 — zo zie je vanaf het moment
dat het scherm aangaat het logo, i.p.v. Pi-logo's en terminaltekst.

Het paneel is van zichzelf staand (320×480); de app draait de GUI in software
met `--rotate 90`. Dit script draait het logo met dezelfde Qt-transformatie,
zodat het boot-logo precies zo staat als de app.

    python3 make_bootlogo.py                     # rotatie 90 (zoals de service)
    python3 make_bootlogo.py --rotation 270      # als de app op --rotate 270 staat
"""
import argparse
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PyQt6.QtGui import QGuiApplication, QImage, QPainter, QTransform  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tetra_monitor as tm  # noqa: E402


def fb_info(fb):
    base = f"/sys/class/graphics/{os.path.basename(fb)}"
    with open(f"{base}/virtual_size") as f:
        w, h = (int(v) for v in f.read().strip().split(","))
    with open(f"{base}/bits_per_pixel") as f:
        bpp = int(f.read().strip())
    stride = w * bpp // 8
    try:
        with open(f"{base}/stride") as f:
            stride = int(f.read().strip())
    except OSError:
        pass
    return w, h, bpp, stride


def render(fb_w, fb_h, rotation):
    turned = rotation % 180 != 0
    lw, lh = (fb_h, fb_w) if turned else (fb_w, fb_h)   # liggend beeld zoals de app
    img = QImage(lw, lh, QImage.Format.Format_RGB32)
    p = QPainter(img)
    tm.paint_brand_splash(p, lw, lh)
    p.end()
    if rotation % 360:
        img = img.transformed(QTransform().rotate(rotation))
    if (img.width(), img.height()) != (fb_w, fb_h):
        raise SystemExit(f"beeld {img.width()}×{img.height()} past niet op framebuffer {fb_w}×{fb_h}")
    return img


def to_raw(img, bpp, stride):
    if bpp == 16:
        img = img.convertToFormat(QImage.Format.Format_RGB16)      # RGB565
    elif bpp == 32:
        img = img.convertToFormat(QImage.Format.Format_RGB32)      # XRGB8888
    else:
        raise SystemExit(f"framebuffer met {bpp} bits per pixel wordt niet ondersteund")
    row = img.width() * bpp // 8
    data = img.constBits()
    data.setsize(img.sizeInBytes())
    buf = bytes(data)
    out = bytearray()
    for y in range(img.height()):
        start = y * img.bytesPerLine()
        out += buf[start:start + row]
        out += b"\x00" * (stride - row)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--fb", default="/dev/fb0")
    ap.add_argument("--rotation", type=int, default=90,
                    help="zelfde getal als --rotate van tetra_monitor.py")
    ap.add_argument("--out", default=os.path.join(tm._script_dir(), "bootlogo.raw"))
    args = ap.parse_args()

    app = QGuiApplication(sys.argv)   # nodig voor lettertypen
    tm.load_brand_fonts()
    w, h, bpp, stride = fb_info(args.fb)
    raw = to_raw(render(w, h, args.rotation), bpp, stride)
    with open(args.out, "wb") as f:
        f.write(raw)
    print(f"boot-logo: {args.out} ({w}×{h}, {bpp} bpp, rotatie {args.rotation}°)")
    del app


if __name__ == "__main__":
    main()
