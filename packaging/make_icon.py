"""The ChemFox logo: a fox head made from a benzene hexagon, with a small nose, on an orange tile.

Run with the build environment's Python (it has Pillow):
    ..\\.env\\python.exe packaging\\make_icon.py
Writes the app icons (packaging/icon: chemfox.ico, .icns, .png), the download page's icons
(docs/assets: icon.png, icon.svg) and the press kit (docs/press, plus chemfox-press-kit.zip).

Everything is drawn from one set of polygons (geometry()), so the PNGs, the icons and the SVGs
can't drift apart.
"""
import math
import os
import shutil
import zipfile

from PIL import Image, ImageDraw

S = 1024  # design units: the tile is S x S
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ICONS = os.path.join(HERE, "icon")
ASSETS = os.path.join(ROOT, "docs", "assets")
PRESS = os.path.join(ROOT, "docs", "press")

# Colours.
ORANGE = "#F06E1E"   # the brand accent: the tile, and the nose
WHITE = "#FFFFFF"
BLACK = "#000000"
DARK = "#1A1A1A"     # dark tile (matches the site's dark background)

# Tile fills, top and bottom. "flat" is the one in use; the others are kept to switch back to.
TILES = {
    "flat": (ORANGE, ORANGE),
    "subtle": ("#FB923C", "#EA580C"),   # orange-400 -> orange-600
    "strong": ("#FDBA74", "#C2410C"),   # orange-300 -> orange-700
}
TILE = "flat"

TILE_INSET, TILE_RADIUS = 40, 210  # rounded square inside the S x S canvas


def geometry():
    """The head outline (one polygon: hexagon plus ears) and the nose triangle, in design units."""
    # The head's vertical centre puts ear tips to chin in the middle of the tile.
    cx, cy, r = S / 2, S * 0.57, S * 0.25
    w = int(S * 0.048)
    R = r + (w / 2) / math.cos(math.radians(30))  # outer radius of the hexagon

    def vertex(a):
        return (cx + R * math.cos(math.radians(a)), cy + R * math.sin(math.radians(a)))

    top, chin = vertex(-90), vertex(90)
    right, left = vertex(-30), vertex(210)  # tops of the vertical sides
    ears = {}
    for side, corner in ((1, right), (-1, left)):
        # The ear's outer edge continues the vertical side straight up; its base sits on the top edge.
        tip = (corner[0], corner[1] - r * 1.12)
        base = (corner[0] + (top[0] - corner[0]) * 0.62, corner[1] + (top[1] - corner[1]) * 0.62)
        ears[side] = (tip, base)
    outline = [top, ears[1][1], ears[1][0], right, vertex(30), chin, vertex(150), left, ears[-1][0], ears[-1][1]]
    nw, nh = r * 0.34, r * 0.24
    ny = cy + r * 0.42
    nose = [(cx - nw / 2, ny - nh / 2), (cx + nw / 2, ny - nh / 2), (cx, ny + nh / 2)]
    return outline, nose


def mark_box(pad=0.06):
    """Bounding box of the fox (with padding as a fraction of its height): x, y, w, h."""
    outline, _ = geometry()
    xs, ys = [p[0] for p in outline], [p[1] for p in outline]
    h = max(ys) - min(ys)
    p = h * pad
    return min(xs) - p, min(ys) - p, max(xs) - min(xs) + 2 * p, h + 2 * p


def rgb(hexcolour, alpha=255):
    h = hexcolour.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


# --- raster -----------------------------------------------------------------------------------

def render(size, tile=None, fox=WHITE, nose=ORANGE, gradient=None, crop=None):
    """A PNG of the logo. tile: tile colour or None (transparent); nose: colour or None to cut it
    out. crop: a design-unit box (x, y, w, h) to frame instead of the whole tile."""
    ss = 4  # supersample for smooth edges
    box = crop or (0, 0, S, S)
    scale = size * ss / max(box[2], box[3])
    W, H = round(box[2] * scale), round(box[3] * scale)
    tx = lambda p: ((p[0] - box[0]) * scale, (p[1] - box[1]) * scale)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if tile or gradient:
        top, bottom = gradient or (tile, tile)
        fill = Image.new("RGBA", (W, H))
        fd = ImageDraw.Draw(fill)
        a, b = rgb(top), rgb(bottom)
        for y in range(H):
            t = y / max(H - 1, 1)
            fd.line([(0, y), (W, y)], fill=tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(4)))
        mask = Image.new("L", (W, H), 0)
        x0, y0 = tx((TILE_INSET, TILE_INSET))
        x1, y1 = tx((S - TILE_INSET, S - TILE_INSET))
        ImageDraw.Draw(mask).rounded_rectangle([x0, y0, x1, y1], radius=TILE_RADIUS * scale, fill=255)
        img.paste(fill, (0, 0), mask)
    outline, nosepts = geometry()
    # Draw the fox on its own layer so a cut-out nose shows whatever is behind it.
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ld.polygon([tx(p) for p in outline], fill=rgb(fox))
    ld.polygon([tx(p) for p in nosepts], fill=rgb(nose) if nose else (0, 0, 0, 0))
    img.alpha_composite(layer)
    w, h = round(W / ss), round(H / ss)
    return img.resize((w, h), Image.LANCZOS)


# --- vector -----------------------------------------------------------------------------------

def svg(tile=None, fox=WHITE, nose=ORANGE, gradient=None, crop=None, title="ChemFox"):
    box = crop or (0, 0, S, S)
    f = lambda v: f"{v:.1f}".rstrip("0").rstrip(".")
    pts = lambda poly: " ".join(f"{f(x)},{f(y)}" for x, y in poly)
    outline, nosepts = geometry()
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{f(box[0])} {f(box[1])} {f(box[2])} {f(box[3])}" '
           f'role="img" aria-label="{title}">', f"<title>{title}</title>"]
    if gradient:
        out.append(f'<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{gradient[0]}"/>'
                   f'<stop offset="1" stop-color="{gradient[1]}"/></linearGradient></defs>')
    if tile or gradient:
        side = S - 2 * TILE_INSET
        out.append(f'<rect x="{TILE_INSET}" y="{TILE_INSET}" width="{side}" height="{side}" rx="{TILE_RADIUS}" '
                   f'fill="{"url(#g)" if gradient else tile}"/>')
    if nose:
        out.append(f'<polygon points="{pts(outline)}" fill="{fox}"/>')
        out.append(f'<polygon points="{pts(nosepts)}" fill="{nose}"/>')
    else:
        # One path, nose cut out (even-odd), so it works on any background.
        d = "M" + " L".join(f"{f(x)},{f(y)}" for x, y in outline) + " Z M" + " L".join(f"{f(x)},{f(y)}" for x, y in nosepts) + " Z"
        out.append(f'<path d="{d}" fill="{fox}" fill-rule="evenodd"/>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


# --- outputs ----------------------------------------------------------------------------------

def app_icons():
    os.makedirs(ICONS, exist_ok=True)
    grad = TILES[TILE] if TILES[TILE][0] != TILES[TILE][1] else None
    big = render(1024, tile=TILES[TILE][0], nose=TILES[TILE][1] if not grad else ORANGE, gradient=grad)
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(ICONS, "chemfox.png"))
    big.save(os.path.join(ICONS, "chemfox.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(os.path.join(ICONS, "chemfox.icns"))
    os.makedirs(ASSETS, exist_ok=True)
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(ASSETS, "icon.png"))
    big.resize((180, 180), Image.LANCZOS).save(os.path.join(ASSETS, "apple-touch-icon.png"))
    with open(os.path.join(ASSETS, "icon.svg"), "w", encoding="utf-8") as fh:
        fh.write(svg(tile=ORANGE, gradient=grad))


# The press kit: (folder, name, kwargs). Icons are the full tile; marks are the fox alone.
VARIANTS = [
    ("icon", "chemfox-icon", dict(tile=ORANGE, fox=WHITE, nose=ORANGE)),
    ("icon", "chemfox-icon-dark", dict(tile=DARK, fox=WHITE, nose=ORANGE)),
    ("icon", "chemfox-icon-light", dict(tile=WHITE, fox=ORANGE, nose=WHITE)),
    ("icon", "chemfox-icon-black", dict(tile=BLACK, fox=WHITE, nose=BLACK)),
    ("mark", "chemfox-mark-orange", dict(fox=ORANGE, nose=None)),
    ("mark", "chemfox-mark-white", dict(fox=WHITE, nose=None)),
    ("mark", "chemfox-mark-black", dict(fox=BLACK, nose=None)),
    ("mark", "chemfox-mark-white-orange-nose", dict(fox=WHITE, nose=ORANGE)),
]

README = """# ChemFox press kit

The ChemFox logo is a fox head built from a benzene hexagon. Use these files as they are: please don't
stretch, recolour outside the palette, rotate, add effects or redraw the logo.

## Files

| Folder | What | Use it for |
|---|---|---|
| `icon/` | The app icon: the fox on a rounded tile | Store listings, articles, slides, social posts |
| `mark/` | The fox on its own, transparent background (the nose is cut out) | On photos or coloured backgrounds, small spaces |

Every logo comes as **SVG** (vector, any size) and **PNG** (1024, 512 and 256 px).

- `chemfox-icon`: the main icon. White fox on orange.
- `chemfox-icon-dark` / `-light` / `-black`: for dark pages, light pages, and one-colour use.
- `chemfox-mark-orange`: the fox alone on light backgrounds.
- `chemfox-mark-white`: the fox alone on dark or orange backgrounds.
- `chemfox-mark-black`: one-colour printing.
- `chemfox-mark-white-orange-nose`: white fox with a solid orange nose, for dark backgrounds where a
  cut-out nose would vanish.

## Colours

| | Hex | RGB |
|---|---|---|
| ChemFox orange (accent) | `#F06E1E` | 240, 110, 30 |
| White | `#FFFFFF` | 255, 255, 255 |
| Black | `#000000` | 0, 0, 0 |
| Dark (backgrounds) | `#1A1A1A` | 26, 26, 26 |

## Spacing and size

- Keep clear space around the logo of at least the width of the nose on every side (the mark files
  already include it).
- Don't show the icon smaller than 16 px or the mark smaller than 24 px tall.
- On orange backgrounds, use the white mark. Don't put the orange mark on orange.

## Name

Write it **ChemFox**: one word, capital C and F.
"""


def press_kit():
    if os.path.isdir(PRESS):
        shutil.rmtree(PRESS)
    crop = mark_box()
    for folder, name, kw in VARIANTS:
        d = os.path.join(PRESS, folder)
        os.makedirs(d, exist_ok=True)
        c = crop if folder == "mark" else None
        with open(os.path.join(d, name + ".svg"), "w", encoding="utf-8") as fh:
            fh.write(svg(crop=c, **kw))
        big = render(1024, crop=c, **kw)
        for px in (1024, 512, 256):
            im = big if px == 1024 else big.resize((round(big.width * px / 1024), round(big.height * px / 1024)), Image.LANCZOS)
            im.save(os.path.join(d, f"{name}-{px}.png"))
    with open(os.path.join(PRESS, "README.md"), "w", encoding="utf-8") as fh:
        fh.write(README)
    sheet(os.path.join(PRESS, "overview.png"))
    z = os.path.join(PRESS, "chemfox-press-kit.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for base, _, files in os.walk(PRESS):
            for fn in files:
                if fn.endswith(".zip"):
                    continue
                full = os.path.join(base, fn)
                zf.write(full, os.path.join("chemfox-press-kit", os.path.relpath(full, PRESS)))


def sheet(path):
    """One image showing every variant on suitable backgrounds, for a quick look."""
    cell, pad = 300, 30
    crop = mark_box()
    backs = {"chemfox-mark-white": DARK, "chemfox-mark-white-orange-nose": DARK}
    img = Image.new("RGBA", (pad + 4 * (cell + pad), pad + 2 * (cell + pad)), rgb("#F4F4F5"))
    for i, (folder, name, kw) in enumerate(VARIANTS):
        x, y = pad + (i % 4) * (cell + pad), pad + (i // 4) * (cell + pad)
        bg = backs.get(name)
        if bg:
            ImageDraw.Draw(img).rounded_rectangle([x, y, x + cell, y + cell], radius=24, fill=rgb(bg))
        im = render(cell - (60 if folder == "mark" else 0), crop=crop if folder == "mark" else None, **kw)
        img.alpha_composite(im, (x + (cell - im.width) // 2, y + (cell - im.height) // 2))
    img.save(path)


def main():
    app_icons()
    press_kit()
    print("wrote", ICONS, ASSETS, PRESS)


if __name__ == "__main__":
    main()
