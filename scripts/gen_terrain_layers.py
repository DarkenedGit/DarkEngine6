"""Generate modest dirt/grass/rock/snow terrain layers (albedo, tangent normal, ORM).

ORM is AO / roughness / metallic / height in A. Heights are distinct so height-blend
has contrast: dirt low, grass mid, rock high+noisy, snow high+flat.
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image

SIZE = 256


def _hash2(ix: int, iy: int, seed: int) -> float:
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return (n & 0xFFFFFF) / float(0xFFFFFF)


def _fade(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def value_noise(x: float, y: float, seed: int) -> float:
    x0 = math.floor(x)
    y0 = math.floor(y)
    fx = _fade(x - x0)
    fy = _fade(y - y0)
    x0i = int(x0)
    y0i = int(y0)
    v00 = _hash2(x0i, y0i, seed)
    v10 = _hash2(x0i + 1, y0i, seed)
    v01 = _hash2(x0i, y0i + 1, seed)
    v11 = _hash2(x0i + 1, y0i + 1, seed)
    return v00 + (v10 - v00) * fx + (v01 - v00) * fy + (v00 - v10 - v01 + v11) * fx * fy


def fbm(x: float, y: float, seed: int, octaves: int, lacunarity: float = 2.0, gain: float = 0.5) -> float:
    amp = 1.0
    freq = 1.0
    total = 0.0
    norm = 0.0
    for i in range(octaves):
        total += amp * value_noise(x * freq, y * freq, seed + i * 101)
        norm += amp
        amp *= gain
        freq *= lacunarity
    return total / norm if norm > 0.0 else 0.0


def ridged(x: float, y: float, seed: int, octaves: int) -> float:
    n = fbm(x, y, seed, octaves)
    r = 1.0 - abs(n * 2.0 - 1.0)
    return r * r


def clamp01(v: float) -> float:
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def u8(v: float) -> int:
    return int(clamp01(v) * 255.0 + 0.5)


def sample_wrap(grid: list[float], x: int, y: int) -> float:
    return grid[(y & (SIZE - 1)) * SIZE + (x & (SIZE - 1))]


def height_to_normal(height: list[float], strength: float) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    for y in range(SIZE):
        for x in range(SIZE):
            dx = sample_wrap(height, x + 1, y) - sample_wrap(height, x - 1, y)
            dy = sample_wrap(height, x, y + 1) - sample_wrap(height, x, y - 1)
            nx = -dx * strength
            ny = -dy * strength
            nz = 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            nx *= inv
            ny *= inv
            nz *= inv
            out.append((u8(nx * 0.5 + 0.5), u8(ny * 0.5 + 0.5), u8(nz * 0.5 + 0.5)))
    return out


def mix_rgb(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[float, float, float]:
    return (lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t))


def shade_rgb(rgb: tuple[float, float, float], shade: float) -> tuple[int, int, int]:
    return (u8(rgb[0] / 255.0 * shade), u8(rgb[1] / 255.0 * shade), u8(rgb[2] / 255.0 * shade))


def write_rgba(path: Path, pixels: list[tuple[int, int, int, int]]) -> None:
    img = Image.new("RGBA", (SIZE, SIZE))
    img.putdata(pixels)
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, format="PNG", optimize=True, compress_level=9)
    print(f"wrote {path} ({path.stat().st_size} bytes)")


def gen_layer(
    name: str,
    seed: int,
    albedo_lo: tuple[int, int, int],
    albedo_hi: tuple[int, int, int],
    height_mean: float,
    height_amp: float,
    rough_lo: float,
    rough_hi: float,
    metal: float,
    ao_lo: float,
    normal_strength: float,
    octaves: int,
    freq: float,
    ridged_mix: float,
) -> tuple[list[tuple[int, int, int, int]], list[tuple[int, int, int, int]], list[tuple[int, int, int, int]]]:
    height = [0.0] * (SIZE * SIZE)
    albedo: list[tuple[int, int, int, int]] = []
    orm: list[tuple[int, int, int, int]] = []

    for y in range(SIZE):
        for x in range(SIZE):
            u = x / float(SIZE) * freq
            v = y / float(SIZE) * freq
            n = fbm(u, v, seed, octaves)
            if ridged_mix > 0.0:
                n = lerp(n, ridged(u * 1.35, v * 1.35, seed + 17, octaves), ridged_mix)
            grain = fbm(u * 4.0, v * 4.0, seed + 91, 3)
            h = clamp01(height_mean + (n - 0.5) * 2.0 * height_amp)
            height[y * SIZE + x] = h

            tint = clamp01(n * 0.65 + grain * 0.35)
            rgb = mix_rgb(albedo_lo, albedo_hi, tint)
            cavity = lerp(0.72, 1.0, n)
            rgb = shade_rgb(rgb, cavity)
            albedo.append((rgb[0], rgb[1], rgb[2], 255))

            ao = lerp(ao_lo, 1.0, n)
            rough = lerp(rough_hi, rough_lo, n)
            orm.append((u8(ao), u8(rough), u8(metal), u8(h)))

    nrm = height_to_normal(height, normal_strength)
    normal = [(p[0], p[1], p[2], 255) for p in nrm]
    return albedo, normal, orm


LAYERS = {
    "dirt": dict(
        seed=1337,
        albedo_lo=(108, 74, 44),
        albedo_hi=(168, 128, 82),
        height_mean=0.30,
        height_amp=0.10,
        rough_lo=0.86,
        rough_hi=0.97,
        metal=0.0,
        ao_lo=0.78,
        normal_strength=4.5,
        octaves=5,
        freq=6.0,
        ridged_mix=0.15,
    ),
    "grass": dict(
        seed=2026,
        albedo_lo=(42, 88, 32),
        albedo_hi=(110, 150, 62),
        height_mean=0.50,
        height_amp=0.08,
        rough_lo=0.70,
        rough_hi=0.88,
        metal=0.0,
        ao_lo=0.82,
        normal_strength=3.2,
        octaves=5,
        freq=8.0,
        ridged_mix=0.0,
    ),
    "rock": dict(
        seed=9001,
        albedo_lo=(78, 74, 70),
        albedo_hi=(168, 162, 154),
        height_mean=0.82,
        height_amp=0.16,
        rough_lo=0.42,
        rough_hi=0.68,
        metal=0.0,
        ao_lo=0.55,
        normal_strength=8.0,
        octaves=6,
        freq=5.0,
        ridged_mix=0.65,
    ),
    "snow": dict(
        seed=4242,
        albedo_lo=(200, 214, 228),
        albedo_hi=(248, 252, 255),
        height_mean=0.88,
        height_amp=0.04,
        rough_lo=0.28,
        rough_hi=0.46,
        metal=0.0,
        ao_lo=0.90,
        normal_strength=1.6,
        octaves=4,
        freq=4.0,
        ridged_mix=0.0,
    ),
}


def main() -> None:
    root = Path(__file__).resolve().parents[1] / "content" / "terrain"
    for name, kwargs in LAYERS.items():
        albedo, normal, orm = gen_layer(name, **kwargs)
        write_rgba(root / name / "albedo.png", albedo)
        write_rgba(root / name / "normal.png", normal)
        write_rgba(root / name / "orm.png", orm)


if __name__ == "__main__":
    main()
