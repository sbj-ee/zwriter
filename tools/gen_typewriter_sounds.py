#!/usr/bin/env python3
"""Synthesize the bundled typewriter samples (pure stdlib, deterministic).

Models an old manual (Underwood / Royal era) typewriter. The key strike is
shaped after measurements of a well-liked reference recording: one very bright,
sharp type-bar impact, a bright decaying wash, a softer second cluster as the
mechanism settles, and a faint low thump at the end.

  key-N.wav   typed character: lever tick -> type-bar SLAP (bright, plus some
                steel/wood body) -> decaying wash -> escapement "ka-chick" +
                bar settling clacks -> faint desk/carriage thump
  space.wav   space bar / backspace: no type bar, so a duller, softer thup and
                the escapement ticks
  return.wav  carriage return: rack ratchet zip, end-stop slam, bell

Deliberately no sustained narrow-band ringing: that is what makes a synthetic
click sound like a tin can. Everything is broadband, low-Q and short-lived.

Usage:  python3 tools/gen_typewriter_sounds.py
"""
import math
import random
import struct
import wave
from pathlib import Path

SR = 44100
OUT = Path(__file__).resolve().parent.parent / "assets" / "sounds"
KEY_VARIANTS = 6


# --- tiny DSP toolbox -------------------------------------------------------

def buf(seconds):
    return [0.0] * int(SR * seconds)


def add(dst, src, at=0.0, gain=1.0):
    i0 = int(at * SR)
    for i, v in enumerate(src):
        j = i0 + i
        if 0 <= j < len(dst):
            dst[j] += v * gain


def env_exp(n, decay_s, attack_s=0.0003):
    a = max(1, int(attack_s * SR))
    return [math.exp(-(i / SR) / decay_s) * (i / a if i < a else 1.0) for i in range(n)]


def noise(rng, seconds, decay_s):
    n = int(seconds * SR)
    e = env_exp(n, decay_s)
    return [rng.uniform(-1, 1) * e[i] for i in range(n)]


def hump(rng, seconds, attack_s, decay_s):
    """Noise with a smooth rise and exponential fall (a soft 'bump' of wash)."""
    n = int(seconds * SR)
    a = max(1, int(attack_s * SR))
    return [rng.uniform(-1, 1) * math.exp(-(i / SR) / decay_s)
            * (math.sin(0.5 * math.pi * i / a) if i < a else 1.0) for i in range(n)]


def highpass(x, cutoff):
    rc = 1.0 / (2 * math.pi * cutoff)
    a = rc / (rc + 1.0 / SR)
    y, px, py = [], 0.0, 0.0
    for v in x:
        py = a * (py + v - px)
        px = v
        y.append(py)
    return y


def lowpass(x, cutoff):
    rc = 1.0 / (2 * math.pi * cutoff)
    a = (1.0 / SR) / (rc + 1.0 / SR)
    y, p = [], 0.0
    for v in x:
        p += a * (v - p)
        y.append(p)
    return y


def bandpass(x, f0, q):
    """RBJ constant-peak biquad band-pass. Low q = wide, dull, non-ringing."""
    w0 = 2 * math.pi * f0 / SR
    alpha = math.sin(w0) / (2 * q)
    a0 = 1 + alpha
    b0, b2 = alpha / a0, -alpha / a0
    a1, a2 = -2 * math.cos(w0) / a0, (1 - alpha) / a0
    y, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for v in x:
        o = b0 * v + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, v
        y2, y1 = y1, o
        y.append(o)
    return y


def thud(f_start, f_end, seconds, decay_s, tau_s):
    """Damped sine whose pitch falls from f_start to f_end: a solid 'thock'."""
    n = int(seconds * SR)
    e = env_exp(n, decay_s, 0.0006)
    out, ph = [], 0.0
    for i in range(n):
        f = f_end + (f_start - f_end) * math.exp(-(i / SR) / tau_s)
        ph += 2 * math.pi * f / SR
        out.append(math.sin(ph) * e[i])
    return out


def tick(rng, hp, dur, decay):
    """A crisp, short, high-passed noise click."""
    return highpass(noise(rng, dur, decay), hp)


# --- events -----------------------------------------------------------------

def escapement(out, rng, at, j, gain=1.0):
    """'ka-chick': the escapement dog stepping over two rack teeth. Steel on
    steel, so bright, but wide-band and very short (no ringing)."""
    e1 = at
    e2 = at + 0.011 + rng.uniform(-0.002, 0.002)
    add(out, tick(rng, 1800, 0.006, 0.0012), e1, 0.62 * gain)
    add(out, bandpass(noise(rng, 0.018, 0.004), 2600 * j, 1.1), e1, 0.55 * gain)
    add(out, tick(rng, 2200, 0.005, 0.0010), e2, 0.48 * gain)
    add(out, bandpass(noise(rng, 0.014, 0.003), 3100 * j, 1.1), e2, 0.40 * gain)


def key_strike(seed):
    rng = random.Random(seed)
    j = 1.0 + rng.uniform(-0.10, 0.10)          # per-variant timbre shift
    g = lambda lo=0.85, hi=1.15: rng.uniform(lo, hi)
    out = buf(0.28)

    # 1) Lever tick: quiet precursor before the bar lands.
    add(out, highpass(noise(rng, 0.008, 0.002), 2200), 0.004, 0.10 * g())

    # 2) Type-bar SLAP: one very bright, sharp impact (mostly 2.5-12 kHz) ...
    slap = 0.015 + rng.uniform(-0.0015, 0.0015)
    bright = lowpass(highpass(noise(rng, 0.045, 0.0075), 2400), 13000)
    add(out, bright, slap, 1.0)
    add(out, highpass(noise(rng, 0.004, 0.0009), 5000), slap, 0.55)   # metal edge
    #    ... with steel/wood body underneath, for the heavier old-machine sound.
    add(out, bandpass(noise(rng, 0.040, 0.008), 1700 * j, 0.8), slap, 0.15 * g())
    add(out, thud(230 * j, 150 * j, 0.05, 0.016, 0.010), slap, 0.05 * g())

    # 3) Bright wash: the machine and room ringing down (no narrow ringing).
    add(out, lowpass(highpass(noise(rng, 0.20, 0.038), 1400), 9000), slap + 0.004, 0.075)

    #    A slower, quieter second wash keeps the gaps between events filled the
    #    way a real recording's room/body ring does (no dead digital silence).
    add(out, lowpass(highpass(hump(rng, 0.19, 0.030, 0.075), 1600), 5500),
        slap + 0.040, 0.030 * g())

    # 4) Escapement ka-chick as the carriage steps: a touch of steel ratchet.
    escapement(out, rng, slap + rng.uniform(0.078, 0.092), j, gain=0.17 * g())

    # 5) Second cluster: bar and linkage settling. Soft, bright, spread out.
    add(out, lowpass(highpass(hump(rng, 0.10, 0.012, 0.030), 1800), 8000),
        slap + 0.075, 0.062 * g())
    for _ in range(rng.randint(3, 5)):
        add(out, highpass(noise(rng, 0.006, 0.0014), 2200),
            slap + rng.uniform(0.075, 0.150), rng.uniform(0.05, 0.11))

    # 6) Faint low thump at the end (desk / carriage settling).
    add(out, thud(115 * j, 78 * j, 0.06, 0.022, 0.02), slap + rng.uniform(0.19, 0.21),
        0.035 * g())
    return out


def space_bar():
    """Space / backspace: no type bar, so no sharp slap — a duller, softer thup
    with the escapement ticks and a little settling."""
    rng = random.Random(4242)
    out = buf(0.24)
    add(out, highpass(noise(rng, 0.008, 0.002), 2000), 0.004, 0.08)
    thup = 0.015
    add(out, lowpass(highpass(noise(rng, 0.040, 0.008), 1200), 6500), thup, 0.55)
    add(out, bandpass(noise(rng, 0.045, 0.010), 1100, 0.8), thup, 0.55)
    add(out, thud(200, 130, 0.06, 0.020, 0.012), thup, 0.10)
    add(out, lowpass(highpass(noise(rng, 0.14, 0.030), 1400), 8000), thup + 0.004, 0.05)
    escapement(out, rng, thup + 0.040, 1.0, gain=0.34)
    add(out, lowpass(highpass(hump(rng, 0.08, 0.010, 0.025), 1800), 9000), thup + 0.070, 0.05)
    add(out, thud(110, 76, 0.05, 0.020, 0.02), thup + 0.16, 0.08)
    return out


def carriage_return():
    """Rack ratchet zip (accelerating clicks), end-stop slam, then the bell."""
    rng = random.Random(77)
    out = buf(1.15)

    t, gap = 0.0, 0.026
    while t < 0.27:
        add(out, tick(rng, 2000, 0.005, 0.0011), t, 0.50)
        add(out, bandpass(noise(rng, 0.012, 0.0025), 2700, 1.0), t, 0.32)
        add(out, thud(150, 120, 0.03, 0.008, 0.01), t, 0.14)
        t += gap
        gap = max(0.010, gap * 0.93)
    add(out, bandpass(noise(rng, 0.28, 0.20), 700, 0.6), 0.0, 0.12)

    stop = 0.285
    add(out, lowpass(highpass(noise(rng, 0.09, 0.014), 1800), 12000), stop, 1.0)
    add(out, bandpass(noise(rng, 0.06, 0.012), 1500, 0.8), stop, 0.55)
    add(out, tick(rng, 4000, 0.008, 0.0016), stop, 0.6)
    add(out, thud(200, 110, 0.12, 0.030, 0.015), stop, 0.28)
    add(out, lowpass(highpass(noise(rng, 0.25, 0.06), 1400), 9000), stop + 0.005, 0.09)

    # Bell: two bright partials, long decay (it is meant to ring).
    for f, g, d in ((2350, 0.30, 0.50), (3520, 0.20, 0.34), (5980, 0.08, 0.18)):
        n = int(0.85 * SR)
        e = env_exp(n, d)
        add(out, [math.sin(2 * math.pi * f * i / SR) * e[i] for i in range(n)], 0.315, g)
    return out


# --- output -----------------------------------------------------------------

def normalize(x, peak=0.85):
    m = max(abs(v) for v in x) or 1.0
    return [v * peak / m for v in x]


def fade_out(x, seconds=0.012):
    n = min(len(x), int(seconds * SR))
    for i in range(n):
        x[len(x) - n + i] *= (1.0 - (i + 1) / n)
    return x


def trim(x, floor=0.002):
    end = len(x)
    while end > 1 and abs(x[end - 1]) < floor:
        end -= 1
    return x[:end]


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(
            struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i in range(1, KEY_VARIANTS + 1):
        write_wav(OUT / f"key-{i}.wav", normalize(fade_out(trim(key_strike(1000 + i)))))
    write_wav(OUT / "space.wav", normalize(fade_out(trim(space_bar())), 0.80))
    write_wav(OUT / "return.wav", normalize(fade_out(trim(carriage_return()), 0.03)))
    print(f"wrote samples to {OUT}")


if __name__ == "__main__":
    main()
