"""Encode content/env/studio_gradient.hdr — 128x64 Radiance RGBE, no sun disc."""
from __future__ import annotations

import math
from pathlib import Path

WIDTH = 128
HEIGHT = 64
ZENITH = (0.12, 0.16, 0.22)
HORIZON = (0.35, 0.38, 0.42)


def lerp3(a: tuple[float, float, float], b: tuple[float, float, float], t: float) -> tuple[float, float, float]:
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def float_to_rgbe(r: float, g: float, b: float) -> bytes:
    v = max(r, g, b)
    if v < 1e-32:
        return bytes((0, 0, 0, 0))
    mantissa, exponent = math.frexp(v)
    scale = mantissa * 256.0 / v
    return bytes(
        (
            min(255, int(r * scale)),
            min(255, int(g * scale)),
            min(255, int(b * scale)),
            exponent + 128,
        )
    )


def sample(x: int, y: int) -> tuple[float, float, float]:
    # Image top = +Y zenith; equator at v=0.5. Lower hemisphere stays horizon (no disc).
    v = (y + 0.5) / HEIGHT
    t = v * 2.0
    if t > 1.0:
        t = 1.0
    return lerp3(ZENITH, HORIZON, t)


def main() -> None:
    repo = Path(__file__).resolve().parents[1]
    out = repo / "content" / "env" / "studio_gradient.hdr"
    out.parent.mkdir(parents=True, exist_ok=True)

    body = bytearray()
    peak = 0.0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = sample(x, y)
            peak = max(peak, r, g, b)
            body.extend(float_to_rgbe(r, g, b))

    if peak >= 2.0:
        raise SystemExit(f"studio_gradient peak {peak} must be < 2")

    header = (
        "#?RADIANCE\n"
        "# DarkEngine6 generated studio gradient (CC0). No sun disc.\n"
        "FORMAT=32-bit_rle_rgbe\n"
        "\n"
        f"-Y {HEIGHT} +X {WIDTH}\n"
    )
    out.write_bytes(header.encode("ascii") + body)
    print(f"wrote {out} ({out.stat().st_size} bytes, peak={peak:.4f})")


if __name__ == "__main__":
    main()
