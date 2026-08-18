"""Convert ferret JPEG strips to tight transparent PNG sprite sheets.

Each source image is a white-background illustration. We:
  1. flood-fill near-white from the canvas edge (keeps interior muzzle/highlights)
  2. extract each pose as a connected component (handles overlapping columns)
  3. pack into a uniform cell, feet on the bottom, centered in X
"""
from __future__ import annotations

from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "content" / "textures"
DST = ROOT / "content" / "textures"
PREVIEW = ROOT / "scripts"

SHEETS = (
    ("Ferret_Idle.jpg", "Ferret_Idle.png", 8),
    ("Ferret_Run.jpg", "Ferret_Run.png", 6),
    ("Ferret_Jump.jpg", "Ferret_Jump.png", 7),
    ("Ferret_Standing.jpg", "Ferret_Standing.png", 6),
)

BG_MIN_LUMA = 248
BG_MAX_SAT = 14
PAD = 8
CELL_GAP = 1
MIN_AREA = 200


def is_background(r: int, g: int, b: int) -> bool:
    mn = min(r, g, b)
    mx = max(r, g, b)
    return mn >= BG_MIN_LUMA and (mx - mn) <= BG_MAX_SAT


def knockout_white(im: Image.Image) -> Image.Image:
    rgba = im.convert("RGBA")
    w, h = rgba.size
    px = rgba.load()
    bg = bytearray(w * h)

    def idx(x: int, y: int) -> int:
        return y * w + x

    q: deque[tuple[int, int]] = deque()
    for x in range(w):
        for y in (0, h - 1):
            r, g, b, _ = px[x, y]
            if is_background(r, g, b):
                bg[idx(x, y)] = 1
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if bg[idx(x, y)]:
                continue
            r, g, b, _ = px[x, y]
            if is_background(r, g, b):
                bg[idx(x, y)] = 1
                q.append((x, y))

    while q:
        x, y = q.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if nx < 0 or ny < 0 or nx >= w or ny >= h:
                continue
            i = idx(nx, ny)
            if bg[i]:
                continue
            r, g, b, _ = px[nx, ny]
            if is_background(r, g, b):
                bg[i] = 1
                q.append((nx, ny))

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    dst = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if bg[idx(x, y)]:
                continue
            sat = max(r, g, b) - min(r, g, b)
            luma = min(r, g, b)
            alpha = 255
            if luma >= 236 and sat <= 22:
                touch = False
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if 0 <= nx < w and 0 <= ny < h and bg[idx(nx, ny)]:
                        touch = True
                        break
                if touch:
                    t = (luma - 236) / 19.0
                    alpha = int(255 * (1.0 - t))
                    if alpha < 8:
                        continue
            dst[x, y] = (r, g, b, alpha)
    return out


def extract_components(im: Image.Image, expected: int) -> list[Image.Image]:
    w, h = im.size
    px = im.load()
    labels = [-1] * (w * h)
    comps: list[tuple[int, int, int, int, int]] = []  # area, minx, miny, maxx, maxy

    def idx(x: int, y: int) -> int:
        return y * w + x

    next_id = 0
    for y in range(h):
        for x in range(w):
            i = idx(x, y)
            if labels[i] != -1 or px[x, y][3] <= 16:
                continue
            q: deque[tuple[int, int]] = deque([(x, y)])
            labels[i] = next_id
            minx = maxx = x
            miny = maxy = y
            area = 0
            while q:
                cx, cy = q.popleft()
                area += 1
                if cx < minx:
                    minx = cx
                if cy < miny:
                    miny = cy
                if cx > maxx:
                    maxx = cx
                if cy > maxy:
                    maxy = cy
                for nx, ny in ((cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)):
                    if nx < 0 or ny < 0 or nx >= w or ny >= h:
                        continue
                    ni = idx(nx, ny)
                    if labels[ni] != -1 or px[nx, ny][3] <= 16:
                        continue
                    labels[ni] = next_id
                    q.append((nx, ny))
            if area >= MIN_AREA:
                comps.append((next_id, area, minx, miny, maxx, maxy))
            next_id += 1

    comps.sort(key=lambda t: (t[2] + t[4]) // 2)
    if len(comps) != expected:
        raise RuntimeError(f"expected {expected} poses, found {len(comps)}")

    frames: list[Image.Image] = []
    for cid, _area, minx, miny, maxx, maxy in comps:
        fw = maxx - minx + 1
        fh = maxy - miny + 1
        fr = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
        fp = fr.load()
        for y in range(miny, maxy + 1):
            for x in range(minx, maxx + 1):
                if labels[idx(x, y)] == cid:
                    fp[x - minx, y - miny] = px[x, y]
        frames.append(fr)
    return frames


def pack_strip(frames: list[Image.Image]) -> tuple[Image.Image, int, int]:
    cell_w = max(fr.size[0] for fr in frames) + PAD * 2
    cell_h = max(fr.size[1] for fr in frames) + PAD * 2
    sheet_w = len(frames) * cell_w + max(0, len(frames) - 1) * CELL_GAP
    sheet = Image.new("RGBA", (sheet_w, cell_h), (0, 0, 0, 0))

    for i, crop in enumerate(frames):
        cw, ch = crop.size
        x = i * (cell_w + CELL_GAP) + (cell_w - cw) // 2
        y = cell_h - PAD - ch
        sheet.paste(crop, (x, y), crop)
    return sheet, cell_w, cell_h


def checkerboard(size: tuple[int, int], cell: int = 16) -> Image.Image:
    w, h = size
    im = Image.new("RGBA", size, (210, 210, 214, 255))
    draw = ImageDraw.Draw(im)
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            if ((x // cell) + (y // cell)) & 1:
                draw.rectangle((x, y, x + cell - 1, y + cell - 1), fill=(236, 236, 240, 255))
    return im


def main() -> None:
    PREVIEW.mkdir(parents=True, exist_ok=True)
    print(f"{'png':<22} {'sheet':<12} {'cell':<12} frames")
    previews: list[Image.Image] = []
    for src_name, dst_name, count in SHEETS:
        knocked = knockout_white(Image.open(SRC / src_name))
        frames = extract_components(knocked, count)
        sheet, cell_w, cell_h = pack_strip(frames)
        sheet.save(DST / dst_name, "PNG")
        print(f"{dst_name:<22} {sheet.size[0]}x{sheet.size[1]:<7} {cell_w}x{cell_h:<7} {count}")

        prev = checkerboard(sheet.size)
        prev.alpha_composite(sheet)
        previews.append(prev)

    gap = 16
    pw = max(p.size[0] for p in previews)
    ph = sum(p.size[1] for p in previews) + gap * (len(previews) - 1)
    contact = Image.new("RGBA", (pw, ph), (32, 36, 44, 255))
    y = 0
    for p in previews:
        contact.paste(p, (0, y))
        y += p.size[1] + gap
    contact_path = PREVIEW / "_ferret_preview.png"
    contact.save(contact_path, "PNG")
    print(f"preview -> {contact_path}")


if __name__ == "__main__":
    main()
