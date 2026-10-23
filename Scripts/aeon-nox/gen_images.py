# Artwork the fork adds to Aeon Nox, drawn with Pillow:
#   images/background.png, images/form_focus.png
#   collections/Home/medium_artwork/logo/<item>.png and icon/<item>.png
#   collections/Catalog/medium_artwork/icon/Catalog Sources.png
#   python gen_images.py <Aeon Nox folder>
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = sys.argv[1]
ACCENT = (0x75, 0xc8, 0xf7)
SS = 4  # supersampling

def save(im, rel):
    path = os.path.join(ROOT, *rel.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, optimize=True)
    print('wrote', rel, im.size, os.path.getsize(path))

# ---------------------------------------------------------------------------------------
def background():
    # Dark blue-black with a soft teal glow top right, like the theme's info panel
    w, h = 960, 540
    im = Image.new('RGB', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            d1 = math.hypot((x - 0.74 * w) / w, (y - 0.18 * h) / h * 0.75)
            d2 = math.hypot((x - 0.12 * w) / w, (y - 1.0 * h) / h * 0.9)
            g1 = max(0.0, 1 - d1 / 0.75) ** 2
            g2 = max(0.0, 1 - d2 / 0.6) ** 2 * 0.45
            g = g1 + g2
            # tiny ordered dither so the stretched gradient doesn't band
            n = ((x * 7 + y * 13) % 5 - 2) * 0.35
            r = 4 + 18 * g + n
            gg = 8 + 46 * g + n
            b = 12 + 64 * g + n
            px[x, y] = (int(r), int(gg), int(b))
    save(im, 'images/background.png')

def form_focus():
    # The focused row: accent glow fading to the right, with a solid edge on the left
    w, h = 512, 64
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    px = im.load()
    for x in range(w):
        a = 0.50 - 0.38 * (x / (w - 1))
        for y in range(h):
            edge = 1.0 if y in (0, h - 1) else 0.0
            alpha = a + 0.18 * edge
            px[x, y] = ACCENT + (int(255 * min(alpha, 1.0)),)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 5, h - 1], fill=ACCENT + (255,))
    save(im, 'images/form_focus.png')

# ---------------------------------------------------------------------------------------
# Icons, drawn as white on a mask at SS x the size, then tinted

def canvas(size):
    return Image.new('L', (size * SS, size * SS), 0)

def icon_catalog(size):
    m = canvas(size); d = ImageDraw.Draw(m); s = size * SS; w = int(s * 0.07)
    cx = s // 2
    # arrow
    d.rectangle([cx - w // 2, int(s * 0.12), cx + w // 2, int(s * 0.52)], fill=255)
    d.polygon([(cx - int(s * 0.2), int(s * 0.42)), (cx + int(s * 0.2), int(s * 0.42)), (cx, int(s * 0.66))], fill=255)
    # tray
    y0, y1, x0, x1 = int(s * 0.62), int(s * 0.86), int(s * 0.14), int(s * 0.86)
    d.line([(x0, y0), (x0, y1), (x1, y1), (x1, y0)], fill=255, width=w, joint='curve')
    for x, y in ((x0, y1), (x1, y1), (x0, y0), (x1, y0)):
        d.ellipse([x - w // 2, y - w // 2, x + w // 2, y + w // 2], fill=255)
    return m

def icon_settings(size):
    m = canvas(size); d = ImageDraw.Draw(m); s = size * SS
    cx = cy = s / 2
    teeth, r_out, r_in = 8, s * 0.42, s * 0.32
    pts = []
    for i in range(teeth * 4):
        a = 2 * math.pi * i / (teeth * 4) - math.pi / 2
        r = r_out if (i % 4) in (0, 1) else r_in
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=255)
    hole = s * 0.14
    d.ellipse([cx - hole, cy - hole, cx + hole, cy + hole], fill=0)
    return m

def icon_manage(size):
    m = canvas(size); d = ImageDraw.Draw(m); s = size * SS; w = int(s * 0.06)
    for i, k in enumerate((0.32, 0.66, 0.45)):
        y = int(s * (0.24 + 0.26 * i))
        d.rectangle([int(s * 0.12), y - w // 2, int(s * 0.88), y + w // 2], fill=255)
        x = int(s * k); r = int(s * 0.09)
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)
        r2 = int(s * 0.045)
        d.ellipse([x - r2, y - r2, x + r2, y + r2], fill=0)
    return m

def icon_browse(size):
    m = canvas(size); d = ImageDraw.Draw(m); s = size * SS
    gap, cols, rows = s * 0.05, 3, 2
    x0, y0, x1, y1 = s * 0.1, s * 0.2, s * 0.9, s * 0.8
    cw = (x1 - x0 - gap * (cols - 1)) / cols
    ch = (y1 - y0 - gap * (rows - 1)) / rows
    for r in range(rows):
        for c in range(cols):
            x = x0 + c * (cw + gap); y = y0 + r * (ch + gap)
            d.rounded_rectangle([x, y, x + cw, y + ch], radius=s * 0.03, fill=255)
    # a play mark on the middle tile of the top row
    x = x0 + 1 * (cw + gap) + cw / 2; y = y0 + ch / 2; t = ch * 0.22
    d.polygon([(x - t * 0.8, y - t), (x - t * 0.8, y + t), (x + t, y)], fill=0)
    return m

def icon_sources(size):
    m = canvas(size); d = ImageDraw.Draw(m); s = size * SS; w = int(s * 0.055)
    x0, x1 = s * 0.2, s * 0.8
    eh = s * 0.16
    top, bottom = s * 0.14, s * 0.78
    # a stack of disks (a database)
    d.rectangle([x0, top + eh / 2, x1, bottom + eh / 2], fill=255)
    d.ellipse([x0, bottom, x1, bottom + eh], fill=255)
    d.ellipse([x0, top, x1, top + eh], fill=255)
    d.ellipse([x0 + w, top + w * 0.7, x1 - w, top + eh - w * 0.7], fill=0)
    for k in (0.38, 0.62):
        y = top + (bottom - top) * k
        d.arc([x0 + w, y - eh / 2 + w, x1 - w, y + eh / 2], 0, 180, fill=0, width=w)
    return m

ICONS = {'Catalog': icon_catalog, 'Settings': icon_settings, 'Manage': icon_manage, 'Browse': icon_browse}

def tint(mask, size, color=ACCENT, glow=True):
    mask = mask.resize((size, size), Image.LANCZOS)
    im = Image.new('RGBA', (size, size), color + (0,))
    if glow:
        halo = mask.filter(ImageFilter.GaussianBlur(size * 0.035)).point(lambda v: int(v * 0.55))
        im.putalpha(halo)
    solid = Image.new('RGBA', (size, size), color + (255,))
    solid.putalpha(mask)
    return Image.alpha_composite(im, solid)

def icon_image(fn, size, pad=0.0):
    im = tint(fn(size), size)
    if pad:
        out = Image.new('RGBA', (int(size * (1 + pad)), int(size * (1 + pad))), (0, 0, 0, 0))
        out.alpha_composite(im, ((out.width - size) // 2, (out.height - size) // 2))
        return out
    return im

# ---------------------------------------------------------------------------------------
def logo(name, fn, font_path):
    # Icon and word, sized like the theme's other logos (shown at 90 px high, 400 wide at most)
    H = 200
    font = ImageFont.truetype(font_path, 190)
    word = name.upper()
    l, t, r, b = font.getbbox(word)
    ic = tint(fn(170), 170, glow=False)
    gap = 34
    W = 170 + gap + (r - l) + 8
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    im.alpha_composite(ic, (0, (H - 170) // 2))
    d = ImageDraw.Draw(im)
    text_h = b - t
    d.text((170 + gap - l, (H - text_h) // 2 - t), word, font=font, fill=(255, 255, 255, 255))
    return im

background()
form_focus()
font = os.path.join(ROOT, 'fonts', 'BebasNeue.ttf')
for name, fn in ICONS.items():
    save(logo(name, fn, font), 'collections/Home/medium_artwork/logo/%s.png' % name)
    save(icon_image(fn, 260), 'collections/Home/medium_artwork/icon/%s.png' % name)
save(icon_image(icon_sources, 260), 'collections/Catalog/medium_artwork/icon/Catalog Sources.png')
