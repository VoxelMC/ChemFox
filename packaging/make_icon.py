"""Draw the ChemFox icon: a benzene ring as a fox face on an orange tile.

Run with the build environment's Python (it has Pillow):
    ..\\.env\\python.exe packaging\\make_icon.py
Writes packaging/icon/chemfox.ico, chemfox.icns and chemfox.png.
"""
import math
import os

from PIL import Image, ImageDraw

S = 1024  # drawn large, then scaled down so every size stays smooth
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon")


def tile():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Orange rounded square with a gentle top-to-bottom gradient.
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    top, bottom = (251, 146, 60), (234, 88, 12)
    for y in range(S):
        t = y / (S - 1)
        gd.line([(0, y), (S, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=210, fill=255)
    img.paste(grad, (0, 0), mask)
    return img


def fox_ring(img):
    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    cx, cy, r = S / 2, S * 0.60, S * 0.25
    w = int(S * 0.045)
    # Hexagon with a point at the bottom (the chin) and one at the top: the face.
    pts = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in (-90, -30, 30, 90, 150, 210)]
    # Ears stand up off the two upper edges (top-left and top-right).
    for i, j in ((5, 0), (0, 1)):
        (x1, y1), (x2, y2) = pts[i], pts[j]
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        ln = math.hypot(mx - cx, my - cy)
        ox, oy = (mx - cx) / ln, (my - cy) / ln          # outward from the face
        tip = (mx + ox * r * 0.55, my + oy * r * 0.55 - r * 0.55)  # out and up
        # base slightly inset along the edge so the ear sits on it
        b1 = (x1 + (x2 - x1) * 0.08, y1 + (y2 - y1) * 0.08)
        b2 = (x2 + (x1 - x2) * 0.08, y2 + (y1 - y2) * 0.08)
        d.polygon([b1, tip, b2], fill=white)
        # inner ear in orange so the ears read as ears
        gx, gy = (b1[0] + tip[0] + b2[0]) / 3, (b1[1] + tip[1] + b2[1]) / 3
        inner = [(p[0] + (gx - p[0]) * 0.42, p[1] + (gy - p[1]) * 0.42) for p in (b1, tip, b2)]
        inner = [(x + ox * r * 0.05, y + oy * r * 0.05) for x, y in inner]
        d.polygon(inner, fill=(234, 88, 12, 255))
    # The ring itself, with Kekulé double bonds inside (alternate edges).
    d.line(pts + [pts[0]], fill=white, width=w, joint="curve")
    for p in pts:
        d.ellipse([p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2], fill=white)
    for k in (0, 2, 4):
        (x1, y1), (x2, y2) = pts[k], pts[(k + 1) % 6]
        # shorten and pull towards the centre
        f, s = 0.22, 0.20
        ax, ay = x1 + (x2 - x1) * s, y1 + (y2 - y1) * s
        bx, by = x2 + (x1 - x2) * s, y2 + (y1 - y2) * s
        ax, ay = ax + (cx - ax) * f, ay + (cy - ay) * f
        bx, by = bx + (cx - bx) * f, by + (cy - by) * f
        d.line([(ax, ay), (bx, by)], fill=white, width=int(w * 0.8))
    # Nose: a small dark dot below the centre.
    nr = S * 0.035
    d.ellipse([cx - nr, cy + r * 0.38 - nr, cx + nr, cy + r * 0.38 + nr], fill=(67, 20, 7, 255))
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    img = fox_ring(tile())
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, "chemfox.png"))
    img.save(os.path.join(OUT, "chemfox.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    img.save(os.path.join(OUT, "chemfox.icns"))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
