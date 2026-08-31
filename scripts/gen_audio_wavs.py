#!/usr/bin/env python3
"""Generate tiny PCM WAV one-shots for Sandbox / Sandbox2D / Editor."""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "content" / "audio"
SR = 44100


def write_mono(path: Path, samples: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = b"".join(
            struct.pack("<h", max(-32767, min(32767, int(s * 32767.0)))) for s in samples
        )
        w.writeframes(frames)


def env_fade(t: float, dur: float, fade: float) -> float:
    if t < fade:
        return t / fade
    if t > dur - fade:
        return max(0.0, (dur - t) / fade)
    return 1.0


def tone(freq: float, dur: float, amp: float = 0.4, fade: float = 0.01) -> list[float]:
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / SR
        out.append(math.sin(2 * math.pi * freq * t) * amp * env_fade(t, dur, fade))
    return out


def blip(freq: float, dur: float, amp: float = 0.5) -> list[float]:
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / SR
        env = math.exp(-6.0 * t / dur)
        out.append(math.sin(2 * math.pi * freq * t) * amp * env)
    return out


def chirp(f0: float, f1: float, dur: float, amp: float = 0.45) -> list[float]:
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / SR
        u = t / dur
        f = f0 + (f1 - f0) * u
        env = math.exp(-4.0 * u)
        out.append(math.sin(2 * math.pi * f * t) * amp * env)
    return out


def mix(*bufs: list[float]) -> list[float]:
    n = max(len(b) for b in bufs)
    out = [0.0] * n
    for b in bufs:
        for i, s in enumerate(b):
            out[i] += s
    peak = max(1e-6, max(abs(s) for s in out))
    if peak > 0.95:
        out = [s * 0.95 / peak for s in out]
    return out


def grunt(dur: float = 0.22, amp: float = 0.55) -> list[float]:
    n = int(SR * dur)
    out = []
    rng = random.Random(7)
    phase1 = 0.0
    phase2 = 0.0
    for i in range(n):
        t = i / SR
        u = t / dur
        f1 = 170.0 - 55.0 * u
        f2 = 290.0 - 80.0 * u
        phase1 += 2.0 * math.pi * f1 / SR
        phase2 += 2.0 * math.pi * f2 / SR
        env = math.exp(-6.5 * u)
        if u < 0.04:
            env *= u / 0.04
        noise = (rng.random() * 2.0 - 1.0) * 0.28
        s = (math.sin(phase1) * 0.62 + math.sin(phase2) * 0.22 + noise) * amp * env
        out.append(s)
    return out


def land(dur: float = 0.14, amp: float = 0.62) -> list[float]:
    n = int(SR * dur)
    out = []
    rng = random.Random(3)
    phase = 0.0
    for i in range(n):
        t = i / SR
        u = t / dur
        f = 92.0 - 40.0 * u
        phase += 2.0 * math.pi * f / SR
        env = math.exp(-14.0 * u)
        if u < 0.02:
            env *= u / 0.02
        noise = (rng.random() * 2.0 - 1.0) * 0.45 * math.exp(-18.0 * u)
        out.append((math.sin(phase) * 0.7 + noise) * amp * env)
    return out


def splash(dur: float = 0.32, amp: float = 0.58) -> list[float]:
    n = int(SR * dur)
    out = []
    rng = random.Random(11)
    p1 = 0.0
    p2 = 0.0
    for i in range(n):
        t = i / SR
        u = t / dur
        f1 = 240.0 - 90.0 * u
        f2 = 520.0 - 220.0 * u
        p1 += 2.0 * math.pi * f1 / SR
        p2 += 2.0 * math.pi * f2 / SR
        env = math.exp(-5.5 * u)
        if u < 0.03:
            env *= u / 0.03
        noise = (rng.random() * 2.0 - 1.0) * 0.7 * math.exp(-4.0 * u)
        s = (math.sin(p1) * 0.22 + math.sin(p2) * 0.12 + noise) * amp * env
        out.append(s)
    return out


def ambient(dur: float = 2.0, amp: float = 0.12) -> list[float]:
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / SR
        # Seamless loop: integer cycles at 110 and 165 Hz over 2.0s
        a = math.sin(2 * math.pi * 110.0 * t)
        b = math.sin(2 * math.pi * 165.0 * t + 0.4)
        out.append((a * 0.65 + b * 0.35) * amp)
    return out


def main() -> None:
    write_mono(ROOT / "ui_click.wav", blip(1400.0, 0.06, 0.35))
    write_mono(ROOT / "jump.wav", chirp(420.0, 780.0, 0.14, 0.5))
    write_mono(ROOT / "coin.wav", mix(blip(880.0, 0.16, 0.4), blip(1320.0, 0.12, 0.28)))
    write_mono(ROOT / "place.wav", blip(620.0, 0.09, 0.4))
    write_mono(ROOT / "delete.wav", chirp(480.0, 180.0, 0.12, 0.4))
    write_mono(ROOT / "whoosh.wav", chirp(180.0, 90.0, 0.22, 0.35))
    write_mono(ROOT / "grunt.wav", grunt())
    write_mono(ROOT / "land.wav", land())
    write_mono(ROOT / "splash.wav", splash())
    write_mono(ROOT / "ambient_loop.wav", ambient())
    print(f"wrote wavs to {ROOT}")


if __name__ == "__main__":
    main()
