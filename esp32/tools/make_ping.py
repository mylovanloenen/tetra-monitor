"""Klassieke "ping" (zoals een melding op internet): heldere toon die meteen
inzet en uitklinkt, met een zachte boventoon. Zelf gesynthetiseerd.

    python3 tools/make_ping.py   → sounds/ping.wav + src/sounds/ping.h
"""
import math
import os
import struct
import wave

RATE = 16000
HERE = os.path.dirname(os.path.abspath(__file__))
MS = 220
F0 = 1760           # A6: helder, goed hoorbaar op een klein speakertje


def ping():
    n = int(RATE * MS / 1000)
    out = []
    for i in range(n):
        t = i / RATE
        att = min(1.0, t / 0.003)                      # 3 ms aanzet: geen klik
        dec = math.exp(-t / 0.055)                     # uitklinken als een belletje
        y = (math.sin(2 * math.pi * F0 * t)
             + 0.35 * math.sin(2 * math.pi * F0 * 2.0 * t) * math.exp(-t / 0.03)
             + 0.15 * math.sin(2 * math.pi * F0 * 2.76 * t) * math.exp(-t / 0.02))
        out.append(att * dec * y)
    tail = int(n * 0.85)                               # laatste stukje zacht naar 0
    for i in range(tail, n):
        out[i] *= (n - i) / (n - tail)
    pk = max(abs(v) for v in out)
    return [v / pk for v in out]


s = ping()
os.makedirs(os.path.join(HERE, "..", "src", "sounds"), exist_ok=True)
with open(os.path.join(HERE, "..", "src", "sounds", "ping.h"), "w") as f:
    f.write("// Gegenereerd door tools/make_ping.py.\n#pragma once\n#include <stdint.h>\n\n")
    f.write(f"constexpr int PING_RATE = {RATE};\nconstexpr int PING_N = {len(s)};\n")
    f.write("static const int16_t PING[PING_N] = {\n")
    vals = [str(int(32000 * x)) for x in s]
    for i in range(0, len(vals), 16):
        f.write("  " + ",".join(vals[i:i + 16]) + ",\n")
    f.write("};\n")
out = []
for gap in [0.8, 0.6, 0.45, 0.35, 0.3]:
    out += s + [0.0] * max(0, int(RATE * gap) - len(s))
with wave.open(os.path.join(HERE, "..", "sounds", "ping.wav"), "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", int(32000 * 0.75 * x)) for x in out))
print(f"ping: {len(s)} samples ({MS} ms)")
