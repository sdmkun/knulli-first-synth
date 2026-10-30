#!/usr/bin/env python3
"""Synthesizes the bundled starter drum kit into samples/kits/BASIC/.

Standard library only, so it runs anywhere:  python scripts/make-basic-kit.py
The files are committed; rerun only to change the sounds.
"""
import math
import os
import random
import struct
import wave

RATE = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples", "kits", "BASIC")
rng = random.Random(1234)  # fixed seed: same noise every run


def env(t, decay):
    return math.exp(-t / decay)


def noise():
    return rng.uniform(-1.0, 1.0)


class OnePole:
    """Simple one-pole filters for shaping noise."""

    def __init__(self, hz):
        self.a = math.exp(-2.0 * math.pi * hz / RATE)
        self.y = 0.0

    def lp(self, x):
        self.y = x + (self.y - x) * self.a
        return self.y

    def hp(self, x):
        return x - self.lp(x)


def render(seconds, fn):
    return [fn(i / RATE) for i in range(int(seconds * RATE))]


def kick():
    phase = 0.0
    out = []
    for i in range(int(0.5 * RATE)):
        t = i / RATE
        hz = 45.0 + 110.0 * math.exp(-t / 0.035)
        phase += hz / RATE
        click = noise() * env(t, 0.002) * 0.3
        out.append(math.sin(2 * math.pi * phase) * env(t, 0.22) + click)
    return out


def snare():
    hp = OnePole(1500)
    return render(0.3, lambda t: 0.5 * math.sin(2 * math.pi * 185 * t) * env(t, 0.06)
                  + 0.7 * hp.hp(noise()) * env(t, 0.09))


def hat(decay):
    hp1, hp2 = OnePole(6000), OnePole(6000)
    return render(decay * 6, lambda t: hp2.hp(hp1.hp(noise())) * env(t, decay) * 1.4)


def clap():
    bp_hp, bp_lp = OnePole(900), OnePole(2500)

    def fn(t):
        bursts = sum(env(t - d, 0.007) for d in (0.0, 0.011, 0.022) if t >= d)
        tail = env(t - 0.022, 0.09) if t >= 0.022 else 0.0
        return bp_lp.lp(bp_hp.hp(noise())) * (bursts * 0.8 + tail) * 2.0

    return render(0.35, fn)


def tom():
    phase = [0.0]

    def fn(t):
        phase[0] += (95.0 + 60.0 * math.exp(-t / 0.05)) / RATE
        return math.sin(2 * math.pi * phase[0]) * env(t, 0.18)

    return render(0.6, fn)


def rim():
    return render(0.08, lambda t: (0.6 * math.sin(2 * math.pi * 1750 * t) + 0.5 * math.sin(2 * math.pi * 820 * t))
                  * env(t, 0.012))


def bell():
    lp = OnePole(4000)

    def sq(hz, t):
        return 1.0 if math.sin(2 * math.pi * hz * t) >= 0 else -1.0

    return render(0.5, lambda t: lp.lp(0.5 * sq(540, t) + 0.5 * sq(800, t)) * env(t, 0.12) * 0.6)


def write(name, data):
    peak = max(abs(x) for x in data) or 1.0
    gain = 0.9 / peak
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, x * gain)) * 32767)) for x in data))
    print("wrote", os.path.normpath(path))


def main():
    os.makedirs(OUT, exist_ok=True)
    sounds = [
        ("1-KICK", kick()),
        ("2-SNARE", snare()),
        ("3-HAT", hat(0.03)),
        ("4-OPENHAT", hat(0.18)),
        ("5-CLAP", clap()),
        ("6-TOM", tom()),
        ("7-RIM", rim()),
        ("8-BELL", bell()),
    ]
    for name, data in sounds:
        write(name, data)


if __name__ == "__main__":
    main()
