"""Draws the ForgeScript icon and builds every image made from it.

Writes ForgeScript.svg (the source), then runs Tools/GenerateAppIcon.py for ForgeScript.png (512 px)
and ForgeScript.ico (16 to 256 px, forgescript.exe's icon), and writes the VS Code extension's icon.
All of them are checked in, so building needs none of this.

The icon follows the MageCap family (MageCap Forge's anvil, MageCap Bellows's bellows): the same tile,
a flat off-white tool with sharp corners, and one flat accent color inside a dark ring. ForgeScript's
tool is a smith's hammer, and its accent a speech bubble with written lines, for scripting in plain
English. The bubble's tail points at the hammer's head, so the two read as one idea.

Needs rsvg-convert (librsvg; on Debian/Ubuntu `apt install librsvg2-bin`) and Pillow.

    python ForgeScript/Icons/GenerateForgeScriptIcon.py
"""
import io
import math
import pathlib
import subprocess
import sys

from PIL import Image

ICONS = pathlib.Path(__file__).resolve().parent
REPOSITORY = ICONS.parent.parent
SOURCE = ICONS / "ForgeScript.svg"
EXTENSION_ICON = REPOSITORY / "ForgeScript" / "Editor" / "vscode-forgescript" / "icon.png"
# The Marketplace asks for at least 128 px; 256 stays sharp on high-DPI screens.
EXTENSION_ICON_SIZE = 256

# The family's colors.
WHITE = "#F0F2FA"
BLUE = "#43B2F5"
RING = "#1B1E34"
RING_WIDTH = 18  # Drawn as a stroke under the accent, so 9 px shows outside it.

# The tile, identical across the family.
TILE = ("""  <defs>
    <linearGradient id="tile" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#2A2E53"/>
      <stop offset="1" stop-color="#16182A"/>
    </linearGradient>
  </defs>
"""
        '  <rect x="22.25" y="22.25" width="467.5" height="467.5" rx="103.25" fill="url(#tile)" '
        'stroke="#4E5486" stroke-width="5.5"/>\n')

# The hammer, drawn along +y with the head's center at the origin, then turned and scaled into place.
# The head of a double-faced smith's hammer: a block around the eye, two narrowing necks, and a
# chamfered face at each end.
HEAD = [(-126, -28), (-110, -46), (-94, -46), (-76, -31), (-46, -31), (-46, -58), (46, -58), (46, -31),
        (76, -31), (94, -46), (110, -46), (126, -28), (126, 28), (110, 46), (94, 46), (76, 31), (46, 31),
        (46, 58), (-46, 58), (-46, 31), (-76, 31), (-94, 46), (-110, 46), (-126, 28)]
# A stout handle, and the metal ferrule at its end, cut from it by a thin gap.
HANDLE = [(-28, 30), (28, 30), (32, 224), (-32, 224)]
FERRULE = [(-36, 234), (36, 234), (36, 266), (30, 276), (-30, 276), (-36, 266)]
HAMMER_ANGLE = 45  # Degrees: the handle to the lower left, the head to the upper right.
HAMMER_CENTER = (327, 228)
HAMMER_SCALE = 0.77

# The speech bubble in the upper left, and the tail from its bottom edge toward the hammer's head.
BUBBLE_X, BUBBLE_Y, BUBBLE_WIDTH, BUBBLE_HEIGHT = 103, 81, 130, 95
BUBBLE_RADIUS = 34
TAIL_LENGTH = 26
LINE_WIDTH = 22
LINE_SPACING = 0.34  # Between the two lines' centers, as a share of the bubble's height.


def place(points: list[tuple[int, int]]) -> str:
    angle = math.radians(HAMMER_ANGLE)
    cos, sin = math.cos(angle), math.sin(angle)
    cx, cy = HAMMER_CENTER
    return " ".join(f"{cx + (x * cos - y * sin) * HAMMER_SCALE:.1f},{cy + (x * sin + y * cos) * HAMMER_SCALE:.1f}"
                    for x, y in points)


def bubble_shape() -> str:
    bottom = BUBBLE_Y + BUBBLE_HEIGHT
    tail = [(BUBBLE_X + BUBBLE_WIDTH * 0.37, bottom - 6), (BUBBLE_X + BUBBLE_WIDTH * 0.62, bottom - 6),
            (BUBBLE_X + BUBBLE_WIDTH * 0.74, bottom + TAIL_LENGTH)]
    tail_points = " ".join(f"{x:.1f},{y:.1f}" for x, y in tail)
    return (f'<rect x="{BUBBLE_X}" y="{BUBBLE_Y}" width="{BUBBLE_WIDTH}" height="{BUBBLE_HEIGHT}" '
            f'rx="{BUBBLE_RADIUS}"/><polygon points="{tail_points}"/>')


def svg() -> str:
    shape = bubble_shape()
    lines = []
    left = BUBBLE_X + BUBBLE_WIDTH * 0.22
    middle = BUBBLE_Y + BUBBLE_HEIGHT / 2
    # A long line and a shorter one: written words.
    for y, length in ((middle - BUBBLE_HEIGHT * LINE_SPACING / 2, BUBBLE_WIDTH * 0.56),
                      (middle + BUBBLE_HEIGHT * LINE_SPACING / 2, BUBBLE_WIDTH * 0.36)):
        lines.append(f'  <line x1="{left:.0f}" y1="{y:.0f}" x2="{left + length:.0f}" y2="{y:.0f}" '
                     f'stroke="{RING}" stroke-width="{LINE_WIDTH}" stroke-linecap="round"/>\n')
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">\n'
            + TILE
            + f'  <g fill="{RING}" stroke="{RING}" stroke-width="{RING_WIDTH}" stroke-linejoin="round">{shape}</g>\n'
            + f'  <g fill="{BLUE}" stroke="{BLUE}" stroke-width="2" stroke-linejoin="round">{shape}</g>\n'
            + "".join(lines)
            + "".join(f'  <polygon fill="{WHITE}" points="{place(part)}"/>\n' for part in (HANDLE, FERRULE, HEAD))
            + "</svg>\n")


def main() -> int:
    SOURCE.write_text(svg(), encoding="utf-8", newline="\n")
    print(f"Wrote {SOURCE}")
    built = subprocess.run([sys.executable, str(REPOSITORY / "Tools" / "GenerateAppIcon.py"), str(SOURCE)])
    if built.returncode != 0:
        return built.returncode
    rendered = subprocess.run(["rsvg-convert", "--width", str(EXTENSION_ICON_SIZE), "--height",
                               str(EXTENSION_ICON_SIZE), str(SOURCE)], capture_output=True, check=True)
    Image.open(io.BytesIO(rendered.stdout)).convert("RGBA").save(EXTENSION_ICON, optimize=True)
    print(f"Wrote {EXTENSION_ICON}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
