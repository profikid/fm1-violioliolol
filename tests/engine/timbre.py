#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Timbre figures for the demo renders (tests/engine/render_demos.c).

  python3 tests/engine/timbre.py DIR [DIR2]

For a plucked instrument it looks at the middle note (the second of the phrase): the harmonics at the attack
(20-60 ms) and half a second later, relative to the fundamental; the spectral centroid as a multiple of f0 at both
times; and how long the 1st, 3rd, 6th and 10th harmonics take to fall 20 dB. A real plucked string starts rich
(harmonics up to the 15th to 25th within 40 dB) and loses its highs much faster than its lows; an electric piano is
a strong fundamental, a few partials, and a long even decay.

For a bowed instrument it takes the first held note: the harmonic slope, which for a bowed string at the bridge is
about -6 dB an octave (a sawtooth), with the body's formants on top.

With two directories it prints both, for a before and after."""
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 44100


def load(path):
    with wave.open(str(path)) as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0


def harmonics(x, f0, count=30):
    n = len(x)
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(x * win, n * 4))
    freqs = np.fft.rfftfreq(n * 4, 1.0 / RATE)
    out = []
    for k in range(1, count + 1):
        f = f0 * k
        if f > RATE / 2 - 200:
            out.append(1e-12)
            continue
        band = (freqs > f * 0.97) & (freqs < f * 1.03)
        out.append(spec[band].max() if band.any() else 1e-12)
    return np.array(out)


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-12))


def centroid(h):
    k = np.arange(1, len(h) + 1)
    p = h ** 2
    return float((k * p).sum() / p.sum())


def fall_time(x, f0, k, drop=20.0):
    """seconds for harmonic k to fall `drop` dB from its level at 30 ms (50 ms windows)"""
    hop = int(0.025 * RATE)
    size = int(max(0.05, 6.0 / f0) * RATE)
    ref = None
    for i, start in enumerate(range(int(0.03 * RATE), len(x) - size, hop)):
        lvl = db(harmonics(x[start:start + size], f0, k)[k - 1])
        if ref is None:
            ref = lvl
        elif lvl < ref - drop:
            return 0.03 + i * 0.025
    return float("nan")


def pluck(x, f0, start):
    seg = x[start:start + int(1.3 * RATE)]
    span = max(0.04, 6.0 / f0)                  # at least six periods: a bass note needs more than 40 ms
    a = harmonics(seg[int(0.02 * RATE):int((0.02 + span) * RATE)], f0)
    b = harmonics(seg[int(0.5 * RATE):int((0.5 + max(0.1, span)) * RATE)], f0)
    within = int((db(a) > db(a).max() - 40).sum())
    falls = [fall_time(seg, f0, k) for k in (1, 3, 6, 10)]
    return {
        "harmonics within 40 dB at attack": within,
        "centroid attack / 0.5 s (x f0)": f"{centroid(a):.1f} / {centroid(b):.1f}",
        "attack h2..h8 dB re h1": " ".join(f"{v:+.0f}" for v in (db(a[1:8]) - db(a[0]))),
        "-20 dB time h1 h3 h6 h10 (s)": " ".join("—" if np.isnan(t) else f"{t:.2f}" for t in falls),
    }


def bowed(x, f0):
    seg = x[int(0.8 * RATE):int(1.6 * RATE)]
    h = harmonics(seg, f0, 24)
    k = np.arange(1, 25)
    ok = h > h.max() * 1e-4
    slope = np.polyfit(np.log2(k[ok]), db(h[ok]), 1)[0]
    return {
        "harmonic slope (dB/oct)": f"{slope:+.1f}",
        "h2..h8 dB re h1": " ".join(f"{v:+.0f}" for v in (db(h[1:8]) - db(h[0]))),
        "centroid (x f0)": f"{centroid(h):.1f}",
    }


def note_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def describe(path):
    import re
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    name = path.stem.split("_", 1)[1].replace("_", " ").upper()
    x = load(path)
    # the phrase (render_demos.c): plucked root - 12, root, root + 12, 1.65 s apart, after 50 ms
    octave = OCTAVES.get(name, 0)
    root = 60 + 12 * octave
    if name in BOWED:
        return name, bowed(x, note_hz(root - 5))
    start = int((0.05 + 1.65) * RATE)
    return name, pluck(x, note_hz(root), start)


# key octaves of the factory patches (dsp/instrument.c), and which are bowed
OCTAVES = {"FINGER BASS": -2, "PICK BASS": -2, "SLAP BASS": -2, "FRETLESS": -2, "UPRIGHT": -2, "NYLON": -1,
           "STEEL": -1, "CLEAN ELEC": -1, "DRIVE ELEC": -1, "JAZZ": -1, "VIOLIN": 0, "VIOLA": 0, "CELLO": -1,
           "CONTRABASS": -2}
BOWED = {"VIOLIN", "VIOLA", "CELLO", "CONTRABASS"}


def main(dirs):
    files = [sorted(Path(d).glob("*.wav")) for d in dirs]
    for group in zip(*files):
        for i, path in enumerate(group):
            name, figures = describe(path)
            tag = "" if len(group) == 1 else (" before" if i == 0 else " after")
            print(f"{name}{tag}")
            for k, v in figures.items():
                print(f"    {k:36} {v}")
        print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(sys.argv[1:])
