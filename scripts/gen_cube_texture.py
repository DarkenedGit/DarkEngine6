"""Generate a demo albedo PNG for texturing the Sandbox cube."""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SIZE = 512
CELL = 64

# Dark Engine palette
BG_A = (18, 22, 32)
BG_B = (28, 36, 52)
ACCENT = (64, 180, 255)
ACCENT_DIM = (32, 90, 140)
GRID = (50, 70, 100)
TEXT = (230, 240, 255)
SUB = (140, 170, 200)


def load_font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    candidates = [
        r"C:\Windows\Fonts\segoeuib.ttf" if bold else r"C:\Windows\Fonts\segoeui.ttf",
        r"C:\Windows\Fonts\arialbd.ttf" if bold else r"C:\Windows\Fonts\arial.ttf",
        r"C:\Windows\Fonts\consolab.ttf" if bold else r"C:\Windows\Fonts\consola.ttf",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main() -> None:
    img = Image.new("RGBA", (SIZE, SIZE), (*BG_A, 255))
    draw = ImageDraw.Draw(img)

    # Checkerboard base (good for UV verification)
    for y in range(0, SIZE, CELL):
        for x in range(0, SIZE, CELL):
            if ((x // CELL) + (y // CELL)) % 2 == 0:
                draw.rectangle([x, y, x + CELL - 1, y + CELL - 1], fill=(*BG_B, 255))

    # Fine grid
    for i in range(0, SIZE + 1, CELL // 2):
        color = (*GRID, 180) if i % CELL == 0 else (*GRID, 80)
        draw.line([(i, 0), (i, SIZE - 1)], fill=color, width=1)
        draw.line([(0, i), (SIZE - 1, i)], fill=color, width=1)

    # Frame
    margin = 8
    draw.rectangle(
        [margin, margin, SIZE - margin - 1, SIZE - margin - 1],
        outline=(*ACCENT, 220),
        width=3,
    )
    draw.rectangle(
        [margin + 6, margin + 6, SIZE - margin - 7, SIZE - margin - 7],
        outline=(*ACCENT_DIM, 160),
        width=1,
    )

    # Corner markers for orientation (R / G / B / Y)
    corners = [
        (margin + 14, margin + 14, (220, 70, 70)),
        (SIZE - margin - 42, margin + 14, (70, 200, 90)),
        (margin + 14, SIZE - margin - 42, (70, 120, 240)),
        (SIZE - margin - 42, SIZE - margin - 42, (240, 200, 60)),
    ]
    for cx, cy, col in corners:
        draw.ellipse(
            [cx, cy, cx + 28, cy + 28],
            fill=(*col, 255),
            outline=(255, 255, 255, 200),
            width=2,
        )

    # Center badge
    cx, cy = SIZE // 2, SIZE // 2 - 20
    r = 72
    diamond = [(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)]
    draw.polygon(diamond, fill=(12, 16, 26, 240), outline=(*ACCENT, 255))
    r2 = 58
    diamond2 = [(cx, cy - r2), (cx + r2, cy), (cx, cy + r2), (cx - r2, cy)]
    draw.polygon(diamond2, outline=(*ACCENT_DIM, 220))

    font_title = load_font(42, bold=True)
    font_sub = load_font(18)
    font_tiny = load_font(14)

    def center_text(text: str, y: int, font: ImageFont.ImageFont, fill: tuple) -> None:
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        x = (SIZE - tw) // 2
        draw.text((x + 2, y + 2), text, font=font, fill=(0, 0, 0, 160))
        draw.text((x, y), text, font=font, fill=fill)

    center_text("DARK", cy - 36, font_title, (*ACCENT, 255))
    center_text("ENGINE", cy + 2, font_title, (*TEXT, 255))
    center_text("albedo  ·  512²", cy + 88, font_sub, (*SUB, 255))

    # UV axis hints
    bbox = draw.textbbox((0, 0), "U →", font=font_tiny)
    tw = bbox[2] - bbox[0]
    draw.text(((SIZE - tw) // 2, margin + 16), "U →", font=font_tiny, fill=(*SUB, 220))
    bbox = draw.textbbox((0, 0), "V ↓", font=font_tiny)
    draw.text(
        (margin + 16, (SIZE - (bbox[3] - bbox[1])) // 2),
        "V ↓",
        font=font_tiny,
        fill=(*SUB, 220),
    )

    # Frame ticks
    for i in range(8):
        t = (i + 0.5) / 8
        points = [
            (int(margin + t * (SIZE - 2 * margin)), margin),
            (SIZE - margin - 1, int(margin + t * (SIZE - 2 * margin))),
            (int(margin + t * (SIZE - 2 * margin)), SIZE - margin - 1),
            (margin, int(margin + t * (SIZE - 2 * margin))),
        ]
        for x, y in points:
            draw.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(*ACCENT, 200))

    # Project content tree + next to Debug exe (Sandbox CWD is usually bin/Debug)
    root = Path(__file__).resolve().parents[1]
    out_dirs = [
        root / "content" / "textures",
        root / "build" / "bin" / "Debug" / "content" / "textures",
    ]
    for out_dir in out_dirs:
        out_dir.mkdir(parents=True, exist_ok=True)
        path = out_dir / "dark_engine_cube.png"
        img.save(path, format="PNG", optimize=True)
        print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
