"""Realistischer "woef-woef": de klank verandert van w → oe → f, met een
ruwe keel (jitter + ademruis) en meebewegende formanten. Geen opname.

    python3 tools/make_woef.py preview   → sounds/woef_*.wav
    python3 tools/make_woef.py header V  → src/sounds/bark.h
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 16000
HERE = os.path.dirname(os.path.abspath(__file__))


def lerp(a, b, t):
    return a + (b - a) * t


def env3(t, pts):
    """Stuksgewijs lineair: pts = [(t, waarde), ...] met t in 0..1."""
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        if t <= t1:
            return lerp(v0, v1, (t - t0) / max(t1 - t0, 1e-9))
    return pts[-1][1]


def woef(ms, f0, size, seed, vowel="oe"):
    """Eén "woef" (of "waf" met vowel="a"). f0 = basistoon (Hz), size: 1 = middel, <1 = kleiner (hoger), >1 = groter."""
    rnd = random.Random(seed)
    n = int(RATE * ms / 1000)
    if vowel == "a":
        # w (dicht) → a (wijd open, helder) → f (lucht)
        F1 = [(0, 350), (0.2, 780), (0.6, 720), (1.0, 450)]
        F2 = [(0, 700), (0.2, 1250), (0.6, 1150), (1.0, 900)]
    else:
        # formanten (Hz) per fase: w (dicht) → oe (open, rond) → f (lucht)
        F1 = [(0, 320), (0.25, 520), (0.6, 480), (1.0, 380)]
        F2 = [(0, 650), (0.25, 950), (0.6, 900), (1.0, 800)]
    F3 = [(0, 2200), (1.0, 2400)]
    pitch = [(0, f0 * 0.8), (0.18, f0 * 1.15), (0.6, f0), (1.0, f0 * 0.75)]
    voice = [(0, 0.0), (0.05, 1.0), (0.65, 0.9), (0.82, 0.0), (1.0, 0.0)]   # stem
    breath = [(0, 0.35), (0.1, 0.15), (0.7, 0.15), (0.85, 0.55), (1.0, 0.0)]  # lucht / "f"
    amp = [(0, 0.0), (0.04, 1.0), (0.5, 0.75), (0.85, 0.35), (1.0, 0.0)]
    # drie resonatoren met tijdvariabele coëfficiënten
    st = [[0.0, 0.0] for _ in range(3)]
    ph, out = 0.0, []
    for i in range(n):
        t = i / n
        f = env3(t, pitch) * (1 + 0.04 * (rnd.random() - 0.5))       # jitter = ruwe keel
        ph += f / RATE
        glot = (2 * (ph - math.floor(ph)) - 1) * env3(t, voice)        # zaagtand ≈ stembanden
        nz = (rnd.random() * 2 - 1) * env3(t, breath)
        src = glot + nz
        y = 0.0
        for k, (track, bw, g) in enumerate([(F1, 120, 1.0), (F2, 160, 0.7), (F3, 300, 0.35)]):
            fc = env3(t, track) / size
            r = math.exp(-math.pi * bw / RATE)
            a1, a2 = -2 * r * math.cos(2 * math.pi * fc / RATE), r * r
            v = (1 - r) * src - a1 * st[k][0] - a2 * st[k][1]
            st[k][1], st[k][0] = st[k][0], v
            y += g * v
        out.append(y * env3(t, amp))
    pk = max(abs(v) for v in out) or 1
    return [v / pk for v in out]


def woef_woef(f0, size, ms1, ms2, gap_ms, seed):
    return (woef(ms1, f0, size, seed) + [0.0] * int(RATE * gap_ms / 1000)
            + [0.9 * v for v in woef(ms2, f0 * 0.95, size, seed + 1)])


VARIANTS = {
    "A": ("middelgrote hond, woef-woef", lambda: woef_woef(380, 1.0, 190, 210, 90, 11)),
    "B": ("flinke hond, zwaarder woef-woef", lambda: woef_woef(300, 1.25, 220, 240, 100, 21)),
    "C": ("kleinere hond, kort en fel woef-woef", lambda: woef_woef(520, 0.8, 150, 160, 70, 31)),
    "D": ("klein hondje, één korte felle waf", lambda: woef(170, 560, 0.8, 41, "a")),
    "E": ("middelgrote hond, één waf", lambda: woef(200, 420, 1.0, 51, "a")),
}


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "header":
        v = sys.argv[2].upper()
        s = VARIANTS[v][1]()
        os.makedirs(os.path.join(HERE, "..", "src", "sounds"), exist_ok=True)
        with open(os.path.join(HERE, "..", "src", "sounds", "bark.h"), "w") as f:
            f.write(f"// Gegenereerd door tools/make_woef.py (variant {v}: {VARIANTS[v][0]}).\n")
            f.write("#pragma once\n#include <stdint.h>\n\n")
            f.write(f"constexpr int BARK_RATE = {RATE};\nconstexpr int BARK_N = {len(s)};\n")
            f.write("static const int16_t BARK[BARK_N] = {\n")
            vals = [str(int(32000 * x)) for x in s]
            for i in range(0, len(vals), 16):
                f.write("  " + ",".join(vals[i:i + 16]) + ",\n")
            f.write("};\n")
        print(f"src/sounds/bark.h: variant {v}, {len(s)} samples ({len(s) / RATE * 1000:.0f} ms)")
        return
    os.makedirs(os.path.join(HERE, "..", "sounds"), exist_ok=True)
    for v, (desc, fn) in VARIANTS.items():
        s = fn()
        out = []
        for gap in [1.2, 1.0, 0.9, 0.8, 0.7]:          # paar keer, steeds iets sneller
            out += s + [0.0] * max(0, int(RATE * gap) - len(s))
        path = os.path.join(HERE, "..", "sounds", f"woef_{v}.wav")
        with wave.open(path, "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
            w.writeframes(b"".join(struct.pack("<h", int(32000 * 0.75 * x)) for x in out))
        print(path, "-", desc, f"({len(s) / RATE * 1000:.0f} ms)")


main()
