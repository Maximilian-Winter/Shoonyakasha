#!/usr/bin/env python3
"""Build the Japanese and Mongolian fonts the canvas UI ships for vertical text.

Full CJK fonts are many megabytes, so the engine ships a subset of Noto Sans
JP (github.com/notofonts/noto-cjk, Sans/SubsetOTF/JP/NotoSansJP-Regular.otf):
kana, CJK punctuation, fullwidth forms, ASCII and the kanji in KANJI below,
which the tests and examples use. Its outlines are converted from CFF cubic
curves to TrueType quadratic ones. Noto Sans Mongolian
(github.com/notofonts/notofonts.github.io, fonts/NotoSansMongolian/unhinted)
is cut to the Mongolian blocks and ASCII. Both keep every OpenType feature,
among them the vertical forms (vert, vrt2) and vertical metrics (vmtx). Both
are under the SIL Open Font License, which allows subsets; the licences ship
beside them. It also writes a CFF sample of three glyphs for the tests of
fonts with cubic outlines.

    python tools/build_font_subsets.py <NotoSansJP-Regular.otf> <NotoSansMongolian-Regular.ttf>

Writes assets/fonts/NotoSansJP-Subset.ttf, assets/fonts/NotoSansMongolian-Regular.ttf
and tests/data/NotoSansJP-cff-sample.otf. Needs fontTools.
"""

import pathlib
import sys

from fontTools import subset
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable

ROOT = pathlib.Path(__file__).resolve().parent.parent

KANA_AND_PUNCTUATION = [(0x0020, 0x007E), (0x3000, 0x30FF), (0x31F0, 0x31FF), (0xFF00, 0xFFEF)]
KANJI = ("日本語縦書横漢字文章東京山川木人中大小上下左右年月時分今何私名前学生先国"
         "書読話見行来出入手口目耳心水火金土雨天気電車道町村花鳥風空海")
MONGOLIAN = [(0x0020, 0x007E), (0x1800, 0x18AF), (0x11660, 0x1167F), (0x200C, 0x200D), (0x202F, 0x202F)]
CFF_SAMPLE = "日あ漢"

# The largest distance, in font units, a quadratic curve may stray from the
# cubic it replaces.
MAX_ERROR = 1.0


def subset_font(source, unicodes, text=""):
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    font = TTFont(source)
    subsetter = subset.Subsetter(options)
    codepoints = [c for low, high in unicodes for c in range(low, high + 1)]
    subsetter.populate(unicodes=codepoints, text=text)
    subsetter.subset(font)
    return font


def to_quadratic(font):
    """Replaces the CFF outlines with TrueType ones of quadratic curves."""
    order = font.getGlyphOrder()
    glyph_set = font.getGlyphSet()
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(glyph_set)
        glyph_set[name].draw(Cu2QuPen(pen, MAX_ERROR, reverse_direction=True))
        glyphs[name] = pen.glyph()

    font["loca"] = newTable("loca")
    font["glyf"] = glyf = newTable("glyf")
    glyf.glyphOrder = order
    glyf.glyphs = glyphs
    del font["CFF "]
    if "VORG" in font:
        del font["VORG"]   # TrueType fonts take vertical origins from vmtx and the glyph boxes
    font["maxp"] = maxp = newTable("maxp")
    maxp.tableVersion = 0x00010000
    for field in ("maxZones", "maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs",
                  "maxStackElements", "maxSizeOfInstructions", "maxComponentElements"):
        setattr(maxp, field, 0)
    maxp.maxZones = 1
    post = font["post"]
    post.formatType = 2.0
    post.extraNames = []
    post.mapping = {}
    post.glyphOrder = order
    font["head"].glyphDataFormat = 0
    font.sfntVersion = "\000\001\000\000"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    japanese, mongolian = sys.argv[1], sys.argv[2]

    jp = subset_font(japanese, KANA_AND_PUNCTUATION, KANJI)
    to_quadratic(jp)
    jp.save(ROOT / "assets" / "fonts" / "NotoSansJP-Subset.ttf")

    subset_font(mongolian, MONGOLIAN).save(ROOT / "assets" / "fonts" / "NotoSansMongolian-Regular.ttf")

    subset_font(japanese, [], CFF_SAMPLE).save(ROOT / "tests" / "data" / "NotoSansJP-cff-sample.otf")

    for path in ("assets/fonts/NotoSansJP-Subset.ttf", "assets/fonts/NotoSansMongolian-Regular.ttf",
                 "tests/data/NotoSansJP-cff-sample.otf"):
        print("%s: %.1f KB" % (path, (ROOT / path).stat().st_size / 1024))


if __name__ == "__main__":
    main()
