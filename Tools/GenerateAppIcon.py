"""Builds the app icon's images from Resources/Icons/Bellows.svg.

Writes Resources/Icons/Bellows.png (512 px: window icon and editor logo) and Resources/Icons/Bellows.ico
(16 to 256 px: the Windows program icon). Both are checked in, so building the engine needs neither tool.

Needs rsvg-convert (librsvg; on Debian/Ubuntu `apt install librsvg2-bin`) and Pillow.

    python Tools/GenerateAppIcon.py
"""
import io
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent
ICONS = REPOSITORY / "Resources" / "Icons"
SOURCE = ICONS / "Bellows.svg"
# Each size is drawn from the SVG at that size (sharper than shrinking the 256 px image).
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
MASTER_SIZE = 512


def render(size: int) -> Image.Image:
    result = subprocess.run(["rsvg-convert", "--width", str(size), "--height", str(size), str(SOURCE)],
                            capture_output=True, check=True)
    return Image.open(io.BytesIO(result.stdout)).convert("RGBA")


def main() -> int:
    if shutil.which("rsvg-convert") is None:
        print("rsvg-convert was not found; install librsvg (e.g. apt install librsvg2-bin)", file=sys.stderr)
        return 1
    render(MASTER_SIZE).save(ICONS / "Bellows.png", optimize=True)
    images = {size: render(size) for size in ICO_SIZES}
    largest = images[ICO_SIZES[-1]]
    largest.save(ICONS / "Bellows.ico", sizes=[(size, size) for size in ICO_SIZES],
                 append_images=[images[size] for size in ICO_SIZES[:-1]])
    print(f"Wrote {ICONS / 'Bellows.png'} and {ICONS / 'Bellows.ico'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
