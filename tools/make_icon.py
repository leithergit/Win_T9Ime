"""Draw the T9Ime icon: res/T9Ime.ico (16-256 px) and res/T9Ime.png (256 px preview).

    py -3 tools/make_icon.py          (needs Pillow)

A blue rounded tile with a 3x3 keypad (the nine-key grid of the input method)
and a bold white "T9" in front. Small sizes (< 48 px) show only "T9" on the
tile so it stays legible in the taskbar and the language list.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'res'
FONT = 'C:/Windows/Fonts/seguibl.ttf'  # Segoe UI Black
TOP, BOTTOM = (47, 134, 240), (21, 86, 184)  # gradient, the panel's accent blue
SS = 4  # supersampling


def tile(size: int, grid: bool) -> Image.Image:
    n = size * SS
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    # Vertical gradient clipped to a rounded square.
    grad = Image.new('RGBA', (n, n))
    px = grad.load()
    for y in range(n):
        t = y / (n - 1)
        c = tuple(round(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)) + (255,)
        for x in range(n):
            px[x, y] = c
    mask = Image.new('L', (n, n), 0)
    margin = round(n * 0.03)
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, n - 1 - margin, n - 1 - margin], radius=round(n * 0.2),
                                           fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)

    if grid:
        # 3x3 keypad: light keys blended over the tile (drawn on an overlay -
        # drawing translucent shapes directly would punch holes in the tile).
        keys = Image.new('RGBA', (n, n), (0, 0, 0, 0))
        kd = ImageDraw.Draw(keys)
        inner = n * 0.16
        span = n - 2 * inner
        gap = span * 0.06
        key = (span - 2 * gap) / 3
        for r in range(3):
            for c in range(3):
                x0 = inner + c * (key + gap)
                y0 = inner + r * (key + gap)
                alpha = 70 if (r, c) == (0, 1) else 46
                kd.rounded_rectangle([x0, y0, x0 + key, y0 + key], radius=key * 0.22, fill=(255, 255, 255, alpha))
        img = Image.alpha_composite(img, keys)
        d = ImageDraw.Draw(img)

    # "T9": bold white with a soft shadow.
    font = ImageFont.truetype(FONT, round(n * (0.5 if grid else 0.58)))
    text = 'T9'
    box = d.textbbox((0, 0), text, font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    x = (n - w) / 2 - box[0]
    y = (n - h) / 2 - box[1] + (n * 0.01)
    shadow = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((x, y + n * 0.025), text, font=font, fill=(8, 40, 100, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(n * 0.02))
    img = Image.alpha_composite(img, shadow)
    ImageDraw.Draw(img).text((x, y), text, font=font, fill=(255, 255, 255, 255))
    return img.resize((size, size), Image.LANCZOS)


def main():
    OUT.mkdir(exist_ok=True)
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    images = [tile(s, grid=s >= 48) for s in sizes]
    images[-1].save(OUT / 'T9Ime.png')
    # Pillow writes one image per size from the largest; give it each size explicitly.
    images[-1].save(OUT / 'T9Ime.ico', format='ICO', sizes=[(s, s) for s in sizes], append_images=images[:-1])
    print('wrote', OUT / 'T9Ime.ico', 'and', OUT / 'T9Ime.png')


if __name__ == '__main__':
    main()
