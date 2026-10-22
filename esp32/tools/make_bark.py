"""Synthetische hondenblaf voor het waarschuwingsgeluid (geen opname, geen rechten).

    python3 tools/make_bark.py preview   → sounds/bark_*.wav om te beluisteren
    python3 tools/make_bark.py header V  → src/sounds/bark.h met variant V (A/B/C)

Een blaf = korte, toonrijke stoot (zaagtand + ruis) met een toonhoogte die
snel omhoog en weer omlaag gaat, door twee "mond"-formanten gefilterd.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 16000
HERE = os.path.dirname(os.path.abspath(__file__))


def resonator(x, f, bw):
    """Tweede-orde bandfilter (formant)."""
    r = math.exp(-math.pi * bw / RATE)
    a1, a2 = -2 * r * math.cos(2 * math.pi * f / RATE), r * r
    g = 1 - r
    y1 = y2 = 0.0
    out = []
    for v in x:
        y = g * v - a1 * y1 - a2 * y2
        out.append(y)
        y2, y1 = y1, y
    return out


def bark(ms, f_lo, f_hi, formants, noise=0.25, seed=1):
    rnd = random.Random(seed)
    n = int(RATE * ms / 1000)
    src, ph = [], 0.0
    for i in range(n):
        t = i / n
        # toon: snel omhoog (eerste 30%), dan omlaag
        f = f_lo + (f_hi - f_lo) * (t / 0.3 if t < 0.3 else max(0.0, 1 - (t - 0.3) / 0.7) * 0.9 + 0.1)
        ph += f / RATE
        saw = 2 * (ph - math.floor(ph)) - 1
        src.append(saw + noise * (rnd.random() * 2 - 1))
    y = [0.0] * n
    for f, bw, g in formants:
        r = resonator(src, f, bw)
        y = [a + g * b for a, b in zip(y, r)]
    # omhullende: harde aanzet, snel uitsterven
    for i in range(n):
        t = i / RATE
        att = min(1.0, t / 0.008)
        y[i] *= att * math.exp(-t / (ms / 1000 * 0.35))
    peak = max(abs(v) for v in y) or 1
    return [v / peak for v in y]


VARIANTS = {
    # naam: (beschrijving, functie)
    "A": ("kleine hond - kort, hoog 'waf'",
          lambda: bark(160, 700, 1100, [(1300, 300, 1.0), (2600, 500, 0.5)], 0.2, 1)),
    "B": ("grote hond - laag, vol 'WOEF'",
          lambda: bark(260, 260, 420, [(700, 200, 1.0), (1500, 300, 0.6)], 0.3, 2)),
    "C": ("middel, dubbel 'woef-woef'",
          lambda: bark(150, 450, 750, [(1000, 250, 1.0), (2100, 400, 0.5)], 0.25, 3)
          + [0.0] * int(RATE * 0.06)
          + bark(150, 430, 700, [(1000, 250, 1.0), (2100, 400, 0.5)], 0.25, 4)),
}


def write_wav(path, samples, gaps):
    """Geigerteller-demo: blaffen steeds sneller achter elkaar."""
    out = []
    for gap in gaps:
        out += samples + [0.0] * max(0, int(RATE * gap) - len(samples))
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(32000 * 0.7 * s)) for s in out))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "header":
        v = sys.argv[2].upper()
        s = VARIANTS[v][1]()
        os.makedirs(os.path.join(HERE, "..", "src", "sounds"), exist_ok=True)
        with open(os.path.join(HERE, "..", "src", "sounds", "bark.h"), "w") as f:
            f.write(f"// Gegenereerd door tools/make_bark.py (variant {v}: {VARIANTS[v][0]}).\n")
            f.write("#pragma once\n#include <stdint.h>\n\n")
            f.write(f"constexpr int BARK_RATE = {RATE};\nconstexpr int BARK_N = {len(s)};\n")
            f.write("static const int16_t BARK[BARK_N] = {\n")
            vals = [str(int(32767 * x)) for x in s]
            for i in range(0, len(vals), 16):
                f.write("  " + ",".join(vals[i:i + 16]) + ",\n")
            f.write("};\n")
        print(f"src/sounds/bark.h: variant {v}, {len(s)} samples ({len(s) / RATE * 1000:.0f} ms)")
        return
    os.makedirs(os.path.join(HERE, "..", "sounds"), exist_ok=True)
    gaps = [0.9, 0.8, 0.7, 0.6, 0.5, 0.45, 0.4, 0.35, 0.32, 0.3, 0.3]
    for v, (desc, fn) in VARIANTS.items():
        path = os.path.join(HERE, "..", "sounds", f"bark_{v}.wav")
        write_wav(path, fn(), gaps)
        print(path, "-", desc)


main()
