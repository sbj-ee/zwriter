#!/usr/bin/env python3
"""Synthesize the bundled typewriter samples (pure stdlib, deterministic).

Writes assets/sounds/key-1.wav .. key-4.wav (type-bar strike variants) and
return.wav (carriage slide + bell + stop). Usage:

    python3 tools/gen_typewriter_sounds.py
"""
import math
import random
import struct
import wave
from pathlib import Path

SR = 44100
OUT = Path(__file__).resolve().parent.parent / "assets" / "sounds"


def buf(seconds):
    return [0.0] * int(SR * seconds)


def add(dst, src, at=0.0, gain=1.0):
    i0 = int(at * SR)
    for i, v in enumerate(src):
        j = i0 + i
        if j >= len(dst):
            break
        dst[j] += v * gain


def env_exp(n, decay_s, attack_s=0.0004):
    a = max(1, int(attack_s * SR))
    out = []
    for i in range(n):
        t = i / SR
        e = math.exp(-t / decay_s)
        if i < a:
            e *= i / a
        out.append(e)
    return out


def noise(rng, seconds, decay_s):
    n = int(seconds * SR)
    e = env_exp(n, decay_s)
    return [rng.uniform(-1, 1) * e[i] for i in range(n)]


def highpass(x, cutoff):
    rc = 1.0 / (2 * math.pi * cutoff)
    a = rc / (rc + 1.0 / SR)
    y, prev_x, prev_y = [], 0.0, 0.0
    for v in x:
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        y.append(prev_y)
    return y


def lowpass(x, cutoff):
    rc = 1.0 / (2 * math.pi * cutoff)
    a = (1.0 / SR) / (rc + 1.0 / SR)
    y, prev = [], 0.0
    for v in x:
        prev += a * (v - prev)
        y.append(prev)
    return y


def bandpass(x, lo, hi):
    return lowpass(highpass(x, lo), hi)


def partial(freq, seconds, decay_s, phase=0.0):
    n = int(seconds * SR)
    e = env_exp(n, decay_s)
    return [math.sin(2 * math.pi * freq * i / SR + phase) * e[i] for i in range(n)]


def key_strike(seed):
    """Type-bar strike: sharp tick, metallic clack, wooden body thump, and a
    softer second hit as the bar bottoms out on the platen."""
    rng = random.Random(seed)
    jit = 1.0 + rng.uniform(-0.08, 0.08)
    out = buf(0.16)

    # 1) Contact tick: very short high-passed noise.
    add(out, highpass(noise(rng, 0.012, 0.0025), 2500), 0.0, 1.0)
    # 2) Metallic clack: band-passed noise + inharmonic ringing partials.
    add(out, bandpass(noise(rng, 0.05, 0.010), 1400, 5200), 0.001, 0.9)
    for f, g, d in ((1850, 0.30, 0.018), (2930, 0.22, 0.014), (4470, 0.12, 0.010)):
        add(out, partial(f * jit, 0.06, d, rng.uniform(0, 6.28)), 0.001, g)
    # 3) Body thump: damped low sines (frame / basket resonance).
    for f, g, d in ((170, 0.55, 0.030), (310, 0.35, 0.022)):
        add(out, partial(f * jit, 0.10, d), 0.002, g)
    # 4) Bottom-out: quieter, slightly later knock.
    gap = 0.026 + rng.uniform(-0.004, 0.004)
    add(out, lowpass(noise(rng, 0.04, 0.008), 3200), gap, 0.45)
    add(out, partial(220 * jit, 0.06, 0.016), gap, 0.30)
    return out


def carriage_return():
    """Carriage slide (accelerating ratchet ticks), bell, and end-stop thunk."""
    rng = random.Random(77)
    out = buf(1.15)

    # Ratchet ticks: escapement rattle that speeds up.
    t, gap = 0.0, 0.032
    while t < 0.26:
        tick = highpass(noise(rng, 0.008, 0.002), 1800)
        add(out, tick, t, 0.45)
        add(out, partial(140, 0.03, 0.010), t, 0.15)
        t += gap
        gap = max(0.012, gap * 0.92)
    # Sliding rasp underneath.
    rasp = bandpass(noise(rng, 0.28, 0.20), 600, 3000)
    add(out, rasp, 0.0, 0.10)

    # End stop: heavy thunk when the carriage hits the margin.
    stop_at = 0.28
    add(out, lowpass(noise(rng, 0.06, 0.012), 1800), stop_at, 0.7)
    for f, g, d in ((95, 0.9, 0.05), (190, 0.5, 0.035)):
        add(out, partial(f, 0.2, d), stop_at, g)
    add(out, highpass(noise(rng, 0.01, 0.003), 2200), stop_at, 0.6)

    # Bell: two bright inharmonic partials with a long decay.
    bell_at = 0.31
    for f, g, d in ((2350, 0.34, 0.55), (3520, 0.22, 0.38), (5980, 0.10, 0.20)):
        add(out, partial(f, 0.85, d), bell_at, g)
    return out


def normalize(x, peak=0.85):
    m = max(abs(v) for v in x) or 1.0
    k = peak / m
    return [v * k for v in x]


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(
            struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples))


def trim(x, floor=0.002):
    end = len(x)
    while end > 1 and abs(x[end - 1]) < floor:
        end -= 1
    return x[:end]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i in range(1, 5):
        write_wav(OUT / f"key-{i}.wav", normalize(trim(key_strike(1000 + i))))
    write_wav(OUT / "return.wav", normalize(trim(carriage_return())))
    print(f"wrote samples to {OUT}")


if __name__ == "__main__":
    main()
