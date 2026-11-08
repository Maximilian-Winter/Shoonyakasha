"""
The vegetation's textures: an atlas of foliage cards (twigs of leaves,
conifer branches, bush and shrub clusters, a tuft of grass) and a bark
tile, baked from CC0 leaf and bark scans from ambientCG, or from leaves,
needles and blades drawn here when those are not downloaded.

    python -m shoonyakasha.planet.vegetation_textures    # download the missing scans

The scans are kept in DIRECTORY: the SHOONYAKASHA_VEGETATION_TEXTURES
environment variable's directory, else shoonyakasha/vegetation_textures in
the user's cache (beside the ground's textures).

The fir's cards ("fir_a", "fir_b") are Poly Haven's fir twigs (scans.CARD_SCANS,
FIR_CARDS) seen from above, flattened along their thin axis, their stalk
at the card's base, drawn at twice the cell's size with their albedo's
alpha test and averaged down (`twig_card`), when they are prepared
(scan_export) and the atlas is not asked for drawn cards (scanned False);
else the conifer cards drawn here.

`atlas()` bakes the foliage atlas: CARDS cells of CELL x CELL texels in a
row-major grid ATLAS_COLUMNS wide, RGBA in sRGB, each card's opaque texels
scaled so their mean is CARD_MEAN (linear) in each channel, then turned
towards their grey (luminance) to keep CARD_CHROMA of their colour, which
the shader multiplies by the plant's colour; transparent texels take the
card's mean colour, so its mip levels do not darken at the edges. Each
leaf and sprig is drawn LEAF_SHADE_SPREAD lighter or darker than the
next (at most half that either way) and casts a soft shadow onto the
leaves already drawn under it: their colour darkened by LEAF_SHADOW
where its outline, blurred by LEAF_SHADOW_BLUR and moved LEAF_SHADOW_SHIFT
down and right (both of the card's size), covers them.
`card_rect(name)` gives a card's (u0, v0, u1, v1) in the atlas, v down.
`bark()` gives the bark tile, RGBA in sRGB. Both use the scans when all
are downloaded (`available()`), else what is drawn here; baking needs
Pillow. PlanetWorld.setup hands them to the pipeline's "vegetationAtlas"
and "vegetationBark" external images.

`rock()` gives the rocks' surface (ROCK_ASSET), ROCK_SIZE square: (albedo
RGBA in sRGB, its channels scaled to a mean of CARD_MEAN (linear) and
turned towards their grey to keep ROCK_CHROMA of their colour; normal map
RGBA, OpenGL's convention), from its scan when downloaded
(`rock_available()`), else a noise of the same mean and a flat normal
map. PlanetWorld.setup hands them to "rockAlbedo" and "rockNormal".
"""

import io
import math
import os
import sys
import urllib.request
import zipfile

import numpy as np

from .ground_textures import USER_AGENT


def _directory():
    chosen = os.environ.get("SHOONYAKASHA_VEGETATION_TEXTURES")
    if chosen:
        return chosen
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        cache = os.environ["LOCALAPPDATA"]
    else:
        cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(cache, "shoonyakasha", "vegetation_textures")


DIRECTORY = _directory()

# (element, ambientCG asset): broad leaves, conifer sprigs, small leafy sprigs, grass blades, bark.
ASSETS = [("leaf", "LeafSet024"), ("needles", "LeafSet019"), ("sprig", "LeafSet002"), ("blade", "Foliage005"),
          ("bark", "Bark001")]

CELL = 512
ATLAS_COLUMNS = 4
CARDS = ("broadleaf_a", "broadleaf_b", "conifer_a", "conifer_b", "bush", "acacia", "shrub", "grass", "fir_a", "fir_b")
FIR_CARDS = {"fir_a": ("fir_twig_main", "conifer_a"), "fir_b": ("fir_twig_tip", "conifer_b")}   # (twig, fallback)
CARD_MEAN = 0.5
CARD_CHROMA = 0.3       # of a card's colour (after its channels are scaled to CARD_MEAN) kept, its grey all
BARK_SIZE = 512
ROCK_ASSET = "Rock030"   # ambientCG
ROCK_SIZE = 1024
ROCK_CHROMA = 0.5


def paths(element):
    """(colour, opacity) JPEG paths of an element's scan; the bark has no opacity."""
    return (os.path.join(DIRECTORY, element + "_color.jpg"), os.path.join(DIRECTORY, element + "_opacity.jpg"))


def rock_paths():
    """(colour, normal) JPEG paths of the rocks' surface scan."""
    return (os.path.join(DIRECTORY, "rock_color.jpg"), os.path.join(DIRECTORY, "rock_normal.jpg"))


def rock_available():
    return all(os.path.exists(p) for p in rock_paths())


def _get(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def fetch():
    """Download the missing scans (1K JPEG) and write CREDITS.md."""
    os.makedirs(DIRECTORY, exist_ok=True)
    credits = ["Vegetation textures, all CC0 (https://creativecommons.org/publicdomain/zero/1.0/):", ""]
    for element, asset in ASSETS:
        colour_path, opacity_path = paths(element)
        site = "https://ambientcg.com/view?id=" + asset
        credits.append("- %s: %s (%s)" % (element, asset, site))
        wanted = [colour_path] + ([] if element == "bark" else [opacity_path])
        if all(os.path.exists(p) for p in wanted):
            print("have", element)
            continue
        print("fetching", element, "from", site, flush=True)
        archive = zipfile.ZipFile(io.BytesIO(_get("https://ambientcg.com/get?file=%s_1K-JPG.zip" % asset)))
        names = archive.namelist()
        for path, suffix in zip(wanted, ("_Color.jpg", "_Opacity.jpg")):
            with open(path, "wb") as f:
                f.write(archive.read(next(n for n in names if n.endswith(suffix))))
    site = "https://ambientcg.com/view?id=" + ROCK_ASSET
    credits.append("- rock: %s (%s)" % (ROCK_ASSET, site))
    if rock_available():
        print("have rock")
    else:
        print("fetching rock from", site, flush=True)
        archive = zipfile.ZipFile(io.BytesIO(_get("https://ambientcg.com/get?file=%s_1K-JPG.zip" % ROCK_ASSET)))
        names = archive.namelist()
        for path, suffix in zip(rock_paths(), ("_Color.jpg", "_NormalGL.jpg")):
            with open(path, "wb") as f:
                f.write(archive.read(next(n for n in names if n.endswith(suffix))))
    with open(os.path.join(DIRECTORY, "CREDITS.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(credits) + "\n")


def available():
    return all(os.path.exists(p) for element, _ in ASSETS
               for p in (paths(element)[:1] if element == "bark" else paths(element)))


# ── Elements ─────────────────────────────────────────────────────────────

def _srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def _linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1.0 / 2.4) - 0.055)


def _regions(mask, smallest):
    """Bounding boxes (y0, x0, y1, x1) of the 4-connected regions of `mask`
    with at least `smallest` cells, largest first."""
    seen = np.zeros(mask.shape, bool)
    height, width = mask.shape
    boxes = []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        stack = [(y, x)]
        seen[y, x] = True
        count, y0, x0, y1, x1 = 0, y, x, y, x
        while stack:
            cy, cx = stack.pop()
            count += 1
            y0, x0, y1, x1 = min(y0, cy), min(x0, cx), max(y1, cy), max(x1, cx)
            for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                if 0 <= ny < height and 0 <= nx < width and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        if count >= smallest:
            boxes.append((count, (y0, x0, y1 + 1, x1 + 1)))
    return [box for _, box in sorted(boxes, reverse=True)]


def _upright(element):
    """An RGBA element (PIL image) turned so its longest axis is vertical,
    cropped to its opaque texels."""
    alpha = np.asarray(element)[..., 3] > 127
    ys, xs = np.nonzero(alpha)
    if len(xs) > 2:
        xy = np.stack([xs - xs.mean(), ys - ys.mean()])
        values, vectors = np.linalg.eigh(np.cov(xy))
        axis = vectors[:, np.argmax(values)]
        angle = math.degrees(math.atan2(axis[1], axis[0]))
        from PIL import Image
        element = element.rotate(angle - 90.0, resample=Image.BICUBIC, expand=True)
    box = element.getchannel("A").point(lambda a: 255 if a > 127 else 0).getbbox()
    return element.crop(box) if box else element


def _scanned(element):
    """The scan's elements: each opaque region of its opacity, cut out and upright."""
    from PIL import Image
    colour_path, opacity_path = paths(element)
    colour = Image.open(colour_path).convert("RGB")
    opacity = Image.open(opacity_path).convert("L").resize(colour.size)
    rgba = colour.copy()
    rgba.putalpha(opacity)
    small = np.asarray(opacity.resize((128, 128))) > 127
    scale = colour.size[0] / 128.0, colour.size[1] / 128.0
    elements = []
    for y0, x0, y1, x1 in _regions(small, 12):
        box = (max(int((x0 - 1) * scale[0]), 0), max(int((y0 - 1) * scale[1]), 0),
               min(int((x1 + 1) * scale[0]), colour.size[0]), min(int((y1 + 1) * scale[1]), colour.size[1]))
        elements.append(_upright(rgba.crop(box)))
    return elements


def _drawn(element, rng):
    """Elements drawn here: leaves, conifer sprigs, small leafy sprigs or grass blades, upright, RGBA."""
    from PIL import Image, ImageDraw
    elements = []
    for k in range(4):
        if element in ("leaf", "sprig"):
            width, height = (90, 150) if element == "leaf" else (40, 64)
            y = np.linspace(0.0, 1.0, height)[:, None]
            x = np.linspace(-1.0, 1.0, width)[None, :]
            half = 0.92 * np.sin(np.pi * np.clip(y, 0.0, 1.0)) ** 0.75 * (1.0 - 0.25 * y)
            inside = np.clip((half - np.abs(x)) * width * 0.5, 0.0, 1.0)
            vein = np.exp(-(x / 0.04) ** 2) * 0.25
            shade = 0.75 + 0.25 * (1.0 - np.abs(x)) + vein + rng.uniform(-0.05, 0.05)
            rgb = np.stack([0.20 * shade, 0.45 * shade, 0.12 * shade], -1) * np.ones((height, width, 1))
            rgba = np.concatenate([rgb, inside[..., None] * np.ones((height, width, 1))], -1)
            elements.append(Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), "RGBA"))
        elif element == "needles":
            image = Image.new("RGBA", (90, 240), (0, 0, 0, 0))
            draw = ImageDraw.Draw(image)
            draw.line([(45, 239), (45, 5)], fill=(70, 50, 30, 255), width=3)
            for t in np.linspace(0.05, 0.95, 40):
                y = 239 - t * 230
                reach = 38 * (1.0 - 0.6 * t)
                for side in (-1, 1):
                    end = (45 + side * reach, y - reach * 0.6 + rng.uniform(-4, 4))
                    draw.line([(45, y), end], fill=(30, int(70 + rng.uniform(-10, 10)), 35, 255), width=2)
            elements.append(image)
        else:   # blade
            width, height = 16, 256
            y = np.linspace(0.0, 1.0, height)[:, None]          # 0 at the tip (top), 1 at the base
            x = np.linspace(-1.0, 1.0, width)[None, :]
            inside = np.clip((0.9 * y ** 0.7 - np.abs(x)) * width * 0.5, 0.0, 1.0)
            shade = 0.6 + 0.4 * (1.0 - y) + rng.uniform(-0.05, 0.05)
            rgb = np.stack([0.25 * shade, 0.45 * shade, 0.12 * shade], -1) * np.ones((height, width, 1))
            rgba = np.concatenate([rgb, inside[..., None] * np.ones((height, width, 1))], -1)
            elements.append(Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), "RGBA"))
    return elements


def elements(element, rng, scanned=None):
    """An element's images: from its scan when the scans are downloaded
    (`scanned` None) or when `scanned` is True, else drawn."""
    use_scan = available() if scanned is None else scanned
    return _scanned(element) if use_scan else _drawn(element, rng)


# ── Cards ────────────────────────────────────────────────────────────────

LEAF_SHADE_SPREAD = 0.4     # a leaf's or sprig's brightness: 1 plus up to half this either way
LEAF_SHADOW = 0.45          # of the colour under a leaf's shadow, taken away
LEAF_SHADOW_BLUR = 0.012    # of the card's size, the shadow's blur radius
LEAF_SHADOW_SHIFT = (0.008, 0.014)   # of the card's size, the shadow's offset (right, down)


def _place(card, element, base, length, angle, flip=False, shade=1.0, shadow=False):
    """`element` scaled to `length` texels long, its bottom at `base`, turned
    `angle` degrees from straight up (positive clockwise), its colour times
    `shade`, composited onto `card`; with `shadow`, its blurred outline
    first darkens what `card` already holds under it."""
    from PIL import Image, ImageFilter
    if flip:
        element = element.transpose(Image.FLIP_LEFT_RIGHT)
    scale = length / max(element.size[1], 1)
    size = (max(int(element.size[0] * scale), 1), max(int(length), 1))
    piece = element.resize(size, Image.BICUBIC)
    if shade != 1.0:
        rgba = np.asarray(piece).astype(np.float64)
        rgba[..., :3] = np.clip(rgba[..., :3] * shade, 0.0, 255.0)
        piece = Image.fromarray(rgba.astype(np.uint8), "RGBA")
    # Pad so the base sits at the centre, then turn about the centre.
    padded = Image.new("RGBA", (size[0] + 2 * size[1], 2 * size[1]), (0, 0, 0, 0))
    padded.paste(piece, (size[1], 0))
    turned = padded.rotate(-angle, resample=Image.BICUBIC)
    at = (int(base[0] - turned.size[0] / 2), int(base[1] - turned.size[1] / 2))
    if shadow:
        c = card.size[0]
        outline = turned.getchannel("A").filter(ImageFilter.GaussianBlur(LEAF_SHADOW_BLUR * c))
        cover = Image.new("L", card.size, 0)
        cover.paste(outline, (at[0] + int(LEAF_SHADOW_SHIFT[0] * c), at[1] + int(LEAF_SHADOW_SHIFT[1] * c)))
        under = np.asarray(card).astype(np.float64)
        under[..., :3] *= 1.0 - LEAF_SHADOW * np.asarray(cover).astype(np.float64)[..., None] / 255.0
        card.paste(Image.fromarray(np.clip(under + 0.5, 0, 255).astype(np.uint8), "RGBA"))
    card.alpha_composite(turned, at)


def _stem(card, points, width):
    """A stem through `points`, `width` texels wide on a card of 256 (as wide for its size on another)."""
    from PIL import ImageDraw
    ImageDraw.Draw(card).line(points, fill=(255, 255, 255, 255), width=max(1, round(width * card.size[0] / 256)),
                              joint="curve")


def twig_card(scan, size=CELL):
    """A prepared twig (scans.Scan) seen from above as a card: RGBA (PIL
    image) `size` square, x across, its z down the card (its stalk at the
    largest z, the card's base), its nearest opaque texel (the albedo's
    alpha at least 1/2) kept, drawn at twice the size and averaged down, a
    texel half covered counting as covered so the needles keep their
    width."""
    from PIL import Image
    big = 2 * size
    p = scan.positions.astype(np.float64)
    tex = np.asarray(Image.open(scan.textures[0]["albedo"]).convert("RGBA")).astype(np.float64) / 255.0
    th, tw = tex.shape[:2]
    lo, hi = p.min(axis=0), p.max(axis=0)
    span = max(hi[0] - lo[0], hi[2] - lo[2], 1e-6)
    x = (p[:, 0] - 0.5 * (lo[0] + hi[0])) / span * (big - 1) + 0.5 * (big - 1)
    y = (p[:, 2] - lo[2]) / span * (big - 1)
    image = np.zeros((big, big, 4))
    depth = np.full((big, big), -np.inf)
    for a, b, c in scan.indices.reshape(-1, 3):
        X, Y = x[[a, b, c]], y[[a, b, c]]
        x0, x1 = int(max(np.floor(X.min()), 0)), int(min(np.ceil(X.max()), big - 1))
        y0, y1 = int(max(np.floor(Y.min()), 0)), int(min(np.ceil(Y.max()), big - 1))
        d = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2])
        if x1 < x0 or y1 < y0 or abs(d) < 1e-12:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((Y[1] - Y[2]) * (gx - X[2]) + (X[2] - X[1]) * (gy - Y[2])) / d
        w1 = ((Y[2] - Y[0]) * (gx - X[2]) + (X[0] - X[2]) * (gy - Y[2])) / d
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        uv = scan.uvs[[a, b, c]].astype(np.float64)
        u = (w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]) % 1.0
        v = (w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]) % 1.0
        z = w0 * p[a, 1] + w1 * p[b, 1] + w2 * p[c, 1]
        texel = tex[(v * (th - 1)).astype(int), (u * (tw - 1)).astype(int)]
        region = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        nearer = inside & (texel[..., 3] >= 0.5) & (z > depth[region])
        depth[region][nearer] = z[nearer]
        image[region][nearer] = np.concatenate([texel[..., :3], np.ones_like(texel[..., 3:])], -1)[nearer]
    linear = _srgb_to_linear(image[..., :3])
    alpha = image[..., 3:]
    small = (linear * alpha).reshape(size, 2, size, 2, 3).sum(axis=(1, 3))
    cover = alpha.reshape(size, 2, size, 2, 1).sum(axis=(1, 3))
    colour = np.where(cover > 0, small / np.maximum(cover, 1e-9), 0.0)
    out = np.concatenate([_linear_to_srgb(colour), np.minimum(cover / 2.0, 1.0)], -1)
    return Image.fromarray((np.clip(out, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGBA")


def _card(name, parts, rng):
    """One card, CELL x CELL: (its leaves, its stems), RGBA (PIL images);
    the stems' colour is set later."""
    from PIL import Image
    if name in FIR_CARDS:
        from . import scans
        twig, fallback = FIR_CARDS[name]
        scan = scans.prepared(twig) if parts.get("scanned", True) is not False else None
        if scan is None:
            return _card(fallback, parts, rng)
        return twig_card(scan), Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    card = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    stems = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    c = CELL
    pick = lambda kind: parts[kind][int(rng.integers(len(parts[kind])))]
    leaf_shade = lambda: 1.0 + LEAF_SHADE_SPREAD * (rng.random() - 0.5)
    if name.startswith("broadleaf"):
        twigs = 5 if name.endswith("a") else 4
        for k in range(twigs):
            spread = (k / (twigs - 1) - 0.5) * (100.0 if name.endswith("a") else 80.0) + rng.uniform(-8, 8)
            a = math.radians(spread)
            reach = c * rng.uniform(0.55, 0.75)
            points = [(c * 0.5 + reach * t * math.sin(a) * (0.6 + 0.4 * t), c * 0.99 - reach * t * math.cos(a))
                      for t in np.linspace(0, 1, 8)]
            _stem(stems, points, 3)
            leaves = 6 if name.endswith("a") else 8
            for j in range(leaves):
                t = 0.3 + 0.7 * j / (leaves - 1)
                x, y = c * 0.5 + reach * t * math.sin(a) * (0.6 + 0.4 * t), c * 0.99 - reach * t * math.cos(a)
                side = 1 if j % 2 else -1
                _place(card, pick("leaf"), (x, y), c * rng.uniform(0.16, 0.24), spread + side * rng.uniform(30, 65),
                       rng.random() < 0.5, leaf_shade(), True)
    elif name.startswith("conifer"):
        _stem(stems, [(c * 0.5, c * 0.99), (c * 0.5, c * 0.04)], 5)
        count = 7 if name.endswith("a") else 9
        for k in range(count):
            t = k / (count - 1)
            y = c * (0.92 - 0.85 * t)
            for side in (-1, 1):
                _place(card, pick("needles"), (c * 0.5, y), c * (0.55 - 0.3 * t) * rng.uniform(0.85, 1.05),
                       side * rng.uniform(50, 75), side < 0)
        _place(card, pick("needles"), (c * 0.5, c * 0.25), c * 0.3, 0.0)
    elif name == "bush":
        for k in range(34):
            angle = rng.uniform(-150, 150)
            r = math.sqrt(rng.random()) * 0.2
            base = (c * (0.5 + r * math.sin(math.radians(angle))), c * (0.55 - r * math.cos(math.radians(angle))))
            _place(card, pick("sprig"), base, c * rng.uniform(0.18, 0.26), angle, rng.random() < 0.5,
                   leaf_shade(), True)
    elif name == "acacia":
        for k in range(5):
            x0 = c * (0.15 + 0.7 * k / 4)
            _stem(stems, [(c * 0.5, c * 0.99), (x0, c * 0.55)], 3)
        for k in range(60):
            x, y = c * rng.uniform(0.06, 0.94), c * (0.5 - 0.22 * rng.random() ** 2)
            _place(card, pick("sprig"), (x, y), c * rng.uniform(0.12, 0.2), rng.uniform(-110, 110), rng.random() < 0.5,
                   leaf_shade(), True)
    elif name == "shrub":
        for k in range(9):
            angle = rng.uniform(-60, 60)
            tip = (c * (0.5 + 0.42 * math.sin(math.radians(angle))), c * (0.98 - 0.85 * math.cos(math.radians(angle))))
            _stem(stems, [(c * 0.5, c * 0.99), tip], 3)
            for t in np.linspace(0.45, 1.0, 4):
                base = (c * 0.5 + (tip[0] - c * 0.5) * t, c * 0.99 + (tip[1] - c * 0.99) * t)
                _place(card, pick("sprig"), base, c * rng.uniform(0.08, 0.14), angle + rng.uniform(-60, 60), False,
                       leaf_shade(), True)
    else:   # grass
        for k in range(22):
            lean = rng.uniform(-28, 28)
            _place(card, pick("blade"), (c * (0.5 + rng.uniform(-0.12, 0.12)), c * 0.995), c * rng.uniform(0.6, 0.97),
                   lean, rng.random() < 0.5)
    return card, stems


STEM_SHADE = 0.35       # the stems' colour, of CARD_MEAN in each channel


def _normalised(card, stems):
    """A card as uint8 RGBA (sRGB): its leaves' opaque texels scaled to a
    mean of CARD_MEAN per channel (linear) and turned towards their grey to
    keep CARD_CHROMA of their colour, over its stems in STEM_SHADE of that,
    the transparent texels the mean."""
    rgba = np.asarray(card).astype(np.float64) / 255.0
    alpha = rgba[..., 3:]
    linear = _srgb_to_linear(rgba[..., :3])
    weight = alpha > 0.5
    mean = (linear * weight).sum(axis=(0, 1)) / max(weight.sum(), 1.0)
    linear = linear / np.maximum(mean, 1e-4) * CARD_MEAN
    grey = (linear @ np.array([0.2126, 0.7152, 0.0722]))[..., None]
    linear = grey + CARD_CHROMA * (linear - grey)
    stem_alpha = np.asarray(stems)[..., 3:].astype(np.float64) / 255.0
    stem = np.full_like(linear, CARD_MEAN * STEM_SHADE)
    total = alpha + stem_alpha * (1.0 - alpha)
    colour = (linear * alpha + stem * stem_alpha * (1.0 - alpha)) / np.maximum(total, 1e-6)
    colour = np.where(total > 1e-3, colour, CARD_MEAN)
    out = np.concatenate([_linear_to_srgb(colour), total], -1)
    return (np.clip(out, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def atlas(scanned=None, seed=17):
    """The foliage atlas (uint8 RGBA, sRGB, (rows * CELL, ATLAS_COLUMNS * CELL, 4))."""
    rng = np.random.default_rng(seed)
    parts = {kind: elements(kind, rng, scanned) for kind in ("leaf", "needles", "sprig", "blade")}
    parts["scanned"] = scanned
    rows = (len(CARDS) + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS
    image = np.zeros((rows * CELL, ATLAS_COLUMNS * CELL, 4), np.uint8)
    for index, name in enumerate(CARDS):
        y, x = divmod(index, ATLAS_COLUMNS)
        image[y * CELL:(y + 1) * CELL, x * CELL:(x + 1) * CELL] = _normalised(*_card(name, parts, rng))
    return image


def card_rect(name):
    """A card's (u0, v0, u1, v1) in the atlas, v down, inset half a texel."""
    index = CARDS.index(name)
    rows = (len(CARDS) + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS
    y, x = divmod(index, ATLAS_COLUMNS)
    width, height = ATLAS_COLUMNS * CELL, rows * CELL
    return ((x * CELL + 0.5) / width, (y * CELL + 0.5) / height, ((x + 1) * CELL - 0.5) / width,
            ((y + 1) * CELL - 0.5) / height)


def bark(scanned=None, seed=5):
    """The bark tile, BARK_SIZE square, uint8 RGBA (sRGB), from the scan or
    a noise of vertical streaks."""
    use_scan = available() if scanned is None else scanned
    if use_scan:
        from PIL import Image
        image = Image.open(paths("bark")[0]).convert("RGBA").resize((BARK_SIZE, BARK_SIZE), Image.BICUBIC)
        return np.ascontiguousarray(np.asarray(image, np.uint8))
    rng = np.random.default_rng(seed)
    x = np.arange(BARK_SIZE)
    streaks = sum(np.sin(2 * np.pi * (x * f / BARK_SIZE + rng.random())) / f for f in (3, 7, 13, 29))
    value = 0.5 + 0.18 * streaks[None, :] + rng.normal(0.0, 0.05, (BARK_SIZE, BARK_SIZE))
    rgb = np.stack([0.42 * value, 0.36 * value, 0.30 * value], -1)
    out = np.concatenate([_linear_to_srgb(np.clip(rgb, 0, 1)), np.ones((BARK_SIZE, BARK_SIZE, 1))], -1)
    return (out * 255 + 0.5).astype(np.uint8)


def rock(scanned=None, seed=9):
    """The rocks' surface: (albedo, normal map), each uint8 RGBA ROCK_SIZE square."""
    use_scan = rock_available() if scanned is None else scanned
    if use_scan:
        from PIL import Image
        colour_path, normal_path = rock_paths()
        rgb = np.asarray(Image.open(colour_path).convert("RGB").resize((ROCK_SIZE, ROCK_SIZE), Image.BICUBIC))
        normal = np.asarray(Image.open(normal_path).convert("RGBA").resize((ROCK_SIZE, ROCK_SIZE), Image.BICUBIC))
        linear = _srgb_to_linear(rgb.astype(np.float64) / 255.0)
    else:
        rng = np.random.default_rng(seed)
        grain = np.zeros((ROCK_SIZE, ROCK_SIZE))
        for f in (4, 16, 64):
            coarse = rng.normal(0.0, 1.0, (f, f))
            grain += np.kron(coarse, np.ones((ROCK_SIZE // f, ROCK_SIZE // f))) / f ** 0.5
        linear = np.repeat((0.5 + 0.12 * grain)[..., None], 3, axis=-1)
        normal = np.zeros((ROCK_SIZE, ROCK_SIZE, 4), np.uint8)
        normal[...] = (128, 128, 255, 255)
    linear = linear / np.maximum(linear.mean(axis=(0, 1)), 1e-4) * CARD_MEAN
    grey = (linear @ np.array([0.2126, 0.7152, 0.0722]))[..., None]
    linear = grey + ROCK_CHROMA * (linear - grey)
    albedo = np.concatenate([_linear_to_srgb(np.clip(linear, 0.0, 1.0)), np.ones((ROCK_SIZE, ROCK_SIZE, 1))], -1)
    return (albedo * 255.0 + 0.5).astype(np.uint8), np.ascontiguousarray(normal, np.uint8)


if __name__ == "__main__":
    try:
        fetch()
    except Exception as error:   # network or archive trouble: say what failed
        sys.exit("could not fetch the textures: %s" % error)
    print("textures in", DIRECTORY)
