#!/usr/bin/env python3
"""Genera iconos (PNG, .ico, .icns) y bitmaps del instalador a partir de branding/logo-source.jpg."""
import os
from PIL import Image, ImageDraw

D = os.path.join(os.path.dirname(__file__), '..', 'branding')
src = Image.open(os.path.join(D, 'logo-source.jpg')).convert('RGBA')
W = src.size[0]
px = src.load()
xs = [x for x in range(W) if any(sum(px[x, y][:3]) < 600 for y in range(0, W, 4))]
ys = [y for y in range(W) if any(sum(px[x, y][:3]) < 600 for x in range(0, W, 4))]
cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
r = max(max(xs) - min(xs), max(ys) - min(ys)) / 2 + 2
S = 4
mask = Image.new('L', (W * S, W * S), 0)
ImageDraw.Draw(mask).ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=255)
src.putalpha(mask.resize((W, W), Image.LANCZOS))
icon = src.crop((int(cx - r), int(cy - r), int(cx + r), int(cy + r))).resize((1024, 1024), Image.LANCZOS)
icon.save(os.path.join(D, 'logo.png'))
for s in (16, 24, 32, 48, 64, 128, 256, 512):
    icon.resize((s, s), Image.LANCZOS).save(os.path.join(D, 'logo-%d.png' % s))
icon.save(os.path.join(D, 'icon.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
icon.save(os.path.join(D, 'icon.icns'))

BG_TOP, BG_BOT = (12, 14, 40), (24, 18, 70)


def gradient(w, h):
    im = Image.new('RGB', (w, h))
    dr = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        dr.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOT)))
    return im


wel = gradient(164, 314)
l = icon.resize((136, 136), Image.LANCZOS)
wel.paste(l, (14, 60), l)
dr = ImageDraw.Draw(wel)
for i, c in enumerate([(124, 77, 255), (90, 110, 255), (47, 128, 255)]):
    dr.line([(30, 230 + i * 8), (134, 230 + i * 8)], fill=c, width=2)
wel.save(os.path.join(D, 'nsis-welcome.bmp'))
hdr = Image.new('RGB', (150, 57), (255, 255, 255))
l = icon.resize((50, 50), Image.LANCZOS)
hdr.paste(l, (96, 3), l)
hdr.save(os.path.join(D, 'nsis-header.bmp'))
print('branding OK')
