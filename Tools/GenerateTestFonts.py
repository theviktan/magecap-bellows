"""Writes the small fonts the font tests read (BellowsTests/Data/).

CFFTest.otf is an OpenType font with CFF outlines (cubic curves), the kind most .otf files are; the
fonts Bellows ships are all TrueType. It has a square with a square hole (A), a round shape drawn with
curves (B), a space, and GPOS kerning that pulls A and B together.

The output is checked in, so running the tests needs no fontTools. Run from the repository root after
changing this script:

    pip install fonttools
    python Tools/GenerateTestFonts.py
"""

from pathlib import Path

from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen

UNITS_PER_EM = 1000
OUTPUT = Path(__file__).resolve().parent.parent / "BellowsTests" / "Data" / "CFFTest.otf"


def draw_square_with_hole(pen: T2CharStringPen) -> None:
    # The outside counter-clockwise, the hole clockwise.
    pen.moveTo((100, 0))
    pen.lineTo((600, 0))
    pen.lineTo((600, 700))
    pen.lineTo((100, 700))
    pen.closePath()
    pen.moveTo((250, 150))
    pen.lineTo((250, 550))
    pen.lineTo((450, 550))
    pen.lineTo((450, 150))
    pen.closePath()


def draw_round(pen: T2CharStringPen) -> None:
    pen.moveTo((350, 0))
    pen.curveTo((550, 0), (650, 150), (650, 350))
    pen.curveTo((650, 550), (550, 700), (350, 700))
    pen.curveTo((150, 700), (50, 550), (50, 350))
    pen.curveTo((50, 150), (150, 0), (350, 0))
    pen.closePath()


def charstring(width: int, draw=None):
    pen = T2CharStringPen(width, None)
    if draw:
        draw(pen)
    return pen.getCharString()


def main() -> None:
    glyphs = [".notdef", "space", "A", "B"]
    builder = FontBuilder(UNITS_PER_EM, isTTF=False)
    builder.setupGlyphOrder(glyphs)
    builder.setupCharacterMap({0x20: "space", 0x41: "A", 0x42: "B"})
    builder.setupCFF(
        "BellowsCFFTest",
        {"FullName": "Bellows CFF Test"},
        {
            ".notdef": charstring(500, draw_square_with_hole),
            "space": charstring(250),
            "A": charstring(700, draw_square_with_hole),
            "B": charstring(700, draw_round),
        },
        {},
    )
    builder.setupHorizontalMetrics({".notdef": (500, 100), "space": (250, 0), "A": (700, 100), "B": (700, 50)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({"familyName": "Bellows CFF Test", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    addOpenTypeFeaturesFromString(builder.font, "feature kern { pos A B -50; } kern;")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    builder.save(OUTPUT)
    print(f"Wrote {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
